/*******************************************************************/
/* * * Projekt 2 - synchronizace procesů (Santa claus problem) * * */
/* * *                                                       * * * */
/* * *                  Jméno: Jiří Mládek                   * * * */
/* * *                                                       * * * */
/* * *                   Login: xmlade01                     * * * */
/* * *                                                       * * * */
/* * *                    Předmět: IOS                       * * * */
/* * *                                                       * * * */
/* * *                  Datum: 02.05.2021                    * * * */
/*******************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <semaphore.h>
#include <time.h>

//definice konstant
#define ERR_ARGC_NUM 1
#define ERR_NOT_NUM 2
#define ERR_WRONG_NUM 3
#define WRONG_CHILD 4
#define WRONG_SEMAF 5
#define FOPEN_FAIL 6

//makro pro alokaci sdílené paměti
#define ALOC_SHARED(pointer) {(pointer) = mmap(NULL, sizeof(*(pointer)), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);}
//makro pro odstranění sdílené paměti
#define DEL_SHARED(pointer) {munmap((pointer), sizeof(*(pointer)));} 

//struktura pro uložení argumentů programu
typedef struct Parameter {
    int elf_num;            //počet skřítků [NE]
    int reindeer_num;       //počet sobů [NR]
    int max_elf_time;       //maximální doba v ms, kdy skřítek pracuje samostatně [TE]
    int max_reindeer_time;  //maximální doba v ms, po které se sob vrací z dovolené [TR]
}param_t;

//struktura pro semafory a sdílené proměnné
typedef struct Shared {
    int *A_counter; //pořadové číslo
    int *elf_id; //číslo skřítka
    int *elf_wait_num; //počet čekajících skřítků ve frontě
    int *rd_id; //číslo soba
    int *rd_home_num; //počet sobů, co se vrátily domů
    int *holidays;
    sem_t *santa_work; //semafor, který slouží pro probuzení santy
    sem_t *sem_elf; //semafor, který slouží jako fronta pro skřítky
    sem_t *sem_reindeer; //semafor, který umožňuje sobům pokračovat
    sem_t *sem_write; //semafor, který hlídá zápis do sdílené paměti
    sem_t *santa_action; //semafor, který povolí santovi pokračovat
    FILE *output_file; //soubor pro výstup
}shared_t;

//funkce pro tisk chybových hlášení
void print_error(int err_num)
{
    switch (err_num)
    {
        case 1:
            fprintf(stderr, "%s", "Error: nespravny pocet argumentu!\n");
            break;
        case 2:
            fprintf(stderr, "%s", "Error: nespravny format argumentu!\n");
            break;
        case 3:
            fprintf(stderr, "%s", "Error: nespravny format cisla v argumentech!\n");
            break; 
        case 4:
            fprintf(stderr, "%s", "Error: Vytvoreni potomka nebylo uspesne!\n");
            break; 
        case 5:
            fprintf(stderr, "%s", "Error: Vytvoreni semaforu nebylo uspesne!\n");
            break; 
        case 6:
            fprintf(stderr, "%s", "Error: Nepodarilo se otevrit soubor!\n");

        default:
            break;
    }
}

//funkce pro kontrolu formátu argumentů programu
int check_arguments(int argc, char *argv[], param_t *Arguments)
{
    char *rest; //pro funkci strtol
    if(argc == 5)
    {
        for (int i = 1; i < argc; i++) //zkontroluji všechny argumenty
        {
            switch (i)
            {
                case 1:
                    Arguments->elf_num = strtol(argv[i], &rest, 10);
                    if (rest[0] != '\0')
                        return ERR_NOT_NUM;
                    break;
                case 2:
                    Arguments->reindeer_num = strtol(argv[i], &rest, 10);
                    if (rest[0] != '\0')
                        return ERR_NOT_NUM;
                    break;
                case 3:
                    Arguments->max_elf_time = strtol(argv[i], &rest, 10);
                    if (rest[0] != '\0')
                        return ERR_NOT_NUM;
                    break;
                case 4:
                    Arguments->max_reindeer_time = strtol(argv[i], &rest, 10);
                    if (rest[0] != '\0')
                        return ERR_NOT_NUM;
                    break;
                default:
                    break;
            }
        }
        if((Arguments->elf_num <= 0) || (Arguments->reindeer_num <= 0) || 
           (Arguments->elf_num >= 1000) || (Arguments->reindeer_num >= 20) ||
           (Arguments->max_elf_time < 0) || (Arguments->max_reindeer_time < 0) ||
           (Arguments->max_elf_time > 1000) || (Arguments->max_reindeer_time > 1000))
        {
               return ERR_WRONG_NUM; //nesprávné číslo
        }
    }
    else
    {
       return ERR_ARGC_NUM; //nesprávný počet argumentů 
    }
    return 0;
}

//funkce pro otevření souboru
int open_file(shared_t *Shared)
{
    Shared->output_file = fopen("proj2.out", "w");
    if (Shared->output_file == NULL)
    {
        print_error(FOPEN_FAIL);
        return 1;
    }
    setbuf(Shared->output_file, 0);
    return 0;
}

//funkce pro vytvoření sdílené paměti
void shared_memory(shared_t *Shared)
{
    ALOC_SHARED(Shared->A_counter);
    ALOC_SHARED(Shared->elf_id);
    ALOC_SHARED(Shared->elf_wait_num);
    ALOC_SHARED(Shared->rd_id);
    ALOC_SHARED(Shared->rd_home_num);
    ALOC_SHARED(Shared->sem_elf);
    ALOC_SHARED(Shared->sem_reindeer);
    ALOC_SHARED(Shared->santa_work);
    ALOC_SHARED(Shared->sem_write);
    ALOC_SHARED(Shared->santa_action);
    ALOC_SHARED(Shared->holidays);
    ALOC_SHARED(Shared->output_file);
}

//funkce pro odstranění sdílené paměti
void del_shared_memory(shared_t *Shared)
{
    DEL_SHARED(Shared->A_counter);
    DEL_SHARED(Shared->elf_id);
    DEL_SHARED(Shared->elf_wait_num);
    DEL_SHARED(Shared->rd_id);
    DEL_SHARED(Shared->rd_home_num);
    DEL_SHARED(Shared->sem_elf);
    DEL_SHARED(Shared->sem_reindeer);
    DEL_SHARED(Shared->santa_work);
    DEL_SHARED(Shared->sem_write);
    DEL_SHARED(Shared->santa_action);
    DEL_SHARED(Shared->holidays);
    DEL_SHARED(Shared->output_file);
}

//funkce pro vytvoření semaforů
int create_semaf(shared_t *Shared)
{
    Shared->santa_work = sem_open("/xmlade01.ios.proj2.santa_work", O_CREAT | O_EXCL, 0666, 0);
    Shared->sem_elf = sem_open("/xmlade01.ios.proj2.elf", O_CREAT | O_EXCL, 0666, 0);
    Shared->sem_reindeer = sem_open("/xmlade01.ios.proj2.reindeer", O_CREAT | O_EXCL, 0666, 0);
    Shared->sem_write = sem_open("/xmlade01.ios.proj2.write", O_CREAT | O_EXCL, 0666, 1);
    Shared->santa_action = sem_open("/xmlade01.ios.proj2.santa_action", O_CREAT | O_EXCL, 0666, 0);
    if((Shared->santa_work == SEM_FAILED) || (Shared->sem_elf == SEM_FAILED) || (Shared->sem_reindeer == SEM_FAILED) || (Shared->sem_write == SEM_FAILED) ||  (Shared->santa_action == SEM_FAILED))
        return WRONG_SEMAF;
    return 0;
}

//funkce pro odstranění semaforů
void destroy_semaf(shared_t *Shared)
{
    sem_close(Shared->santa_work);
    sem_close(Shared->sem_elf);
    sem_close(Shared->sem_reindeer);
    sem_close(Shared->sem_write);
    sem_close(Shared->santa_action);
    sem_unlink("xmlade01.ios.proj2.santa_work");
    sem_unlink("xmlade01.ios.proj2.elf");
    sem_unlink("xmlade01.ios.proj2.reindeer");
    sem_unlink("xmlade01.ios.proj2.write");
    sem_unlink("xmlade01.ios.proj2.santa_action");
}

//funkce, kterou provádí santa
void santa_process(shared_t *Shared, param_t *Arguments)
{
    int all_reindeers_hitched = 0; //na počátku nejsou sobové zapřaženi
    *Shared->holidays = 0; //skřítci nemusí ještě na dovolenou

    sem_wait(Shared->sem_write);
    fprintf(Shared->output_file, "%d: Santa: going to sleep\n", ++(*Shared->A_counter));
    sem_post(Shared->sem_write);

    while(!all_reindeers_hitched) //dokud nejsou všichni sobové zapřažení
    {
        sem_wait(Shared->santa_work); //uspání santy

        if(*(Shared->rd_home_num) == Arguments->reindeer_num)
        {
            sem_wait(Shared->sem_write);
            fprintf(Shared->output_file, "%d: Santa: closing workshop\n", ++(*Shared->A_counter));
            *(Shared->holidays) = 1; //v této chvíli mohou skřítci vypsat ""taking holidays"
            sem_post(Shared->sem_write);

            for (int i = 0; i < *(Shared->elf_wait_num) + 3; i++)
            {
                sem_post(Shared->sem_elf); //odemknu semafor pro elfy, kterým se nedostane pomoc od Santy a budou mít volno
            }
            for (int i = 0; i < Arguments->reindeer_num; i++)
            {
                sem_post(Shared->sem_reindeer); //díky tomuto budou sobi moct být zapřaženi
            }
            all_reindeers_hitched = 1; //v této chvíli jsou zapřaženi všichni sobové   
        }
        else if (*(Shared->elf_wait_num) >= 0)
        {
            sem_wait(Shared->sem_write);
            fprintf(Shared->output_file, "%d: Santa: helping elves\n", ++(*Shared->A_counter));
            sem_post(Shared->sem_write);

            for (int i = 0; i < 3; i++)
            {
                sem_post(Shared->sem_elf);
            }
            for (int i = 0; i < 3; i++)
            {
                sem_wait(Shared->santa_action); //vyřeším, aby se going to sleep vypsalo až ve chvíli, kdy elfové vypíší get help
            }
            sem_wait(Shared->sem_write);
            fprintf(Shared->output_file, "%d: Santa: going to sleep\n", ++(*Shared->A_counter));
            sem_post(Shared->sem_write);
        }
    }
    //až skončí cyklus tak to znamená, že jsou všichni sobové zapřaženi
    for (int i = 0; i < Arguments->reindeer_num; i++)
    {
        sem_wait(Shared->santa_action);//aby vypsal christmas started až po zapřažení všech sobů
    }
    sem_wait(Shared->sem_write);
    fprintf(Shared->output_file, "%d: Santa: Christmas started\n", ++(*Shared->A_counter));
    sem_post(Shared->sem_write);
}

//funkce, kterou budou vykonávat elfové
void elf_process(shared_t *Shared, param_t *Arguments)
{
    int uniq_elf_id; //lokální proměnná, skřítkovo ID

    sem_wait(Shared->sem_write);
    ++(*Shared->elf_id); //při každém elfím procesu zvýším elfovo id
    uniq_elf_id = *Shared->elf_id;
    fprintf(Shared->output_file, "%d: Elf %d: started\n", ++(*Shared->A_counter), uniq_elf_id);
    sem_post(Shared->sem_write);

    while (1)
    {
        //simulace práce skřítka
        if(Arguments->max_elf_time != 0)
        {
            usleep((rand() % (Arguments->max_elf_time)) * 1000); 
        }
        sem_wait(Shared->sem_write);
        fprintf(Shared->output_file, "%d: Elf %d: need help\n", ++(*Shared->A_counter), uniq_elf_id);
        ++(*Shared->elf_wait_num); //zvýším počet čekajících skřítků
        sem_post(Shared->sem_write);

        if(*(Shared->elf_wait_num) == 3)
        {
            sem_post(Shared->santa_work); //pokud budou 3 skřítkové potřebovat pomoc, tak vzbudí santu
        }

        if (*(Shared->holidays) != 1)
        {
            sem_wait(Shared->sem_elf); //zde bude elf čekat na to, aby mohl pokračovat
        }

        if (*(Shared->holidays) == 1)
        {
            sem_wait(Shared->sem_write);
            fprintf(Shared->output_file, "%d: Elf %d: taking holidays\n", ++(*Shared->A_counter), uniq_elf_id);
            sem_post(Shared->sem_write);
            break; 
        }
        else
        {
            sem_wait(Shared->sem_write);
            *(Shared->elf_wait_num) -= 1;
            fprintf(Shared->output_file, "%d: Elf %d: get help\n", ++(*Shared->A_counter), uniq_elf_id);
            sem_post(Shared->sem_write);
            
            sem_post(Shared->santa_action); //až napíšou všichni skřítci get help, tak může jít santa spát
        }
    }
}

void reindeer_process(shared_t *Shared, param_t *Arguments)
{
    int uniq_rd_id; //lokální proměnná pro sobovo ID
    
    sem_wait(Shared->sem_write);
    ++(*Shared->rd_id);
    uniq_rd_id = *Shared->rd_id;
    fprintf(Shared->output_file, "%d: RD %d: rstarted\n", ++(*Shared->A_counter), uniq_rd_id);
    sem_post(Shared->sem_write);

    //simulace soba, který je na dovolené
    if(Arguments->max_reindeer_time != 0)
    {
        usleep((rand() % (Arguments->max_reindeer_time)) * 1000);
    }

    sem_wait(Shared->sem_write);
    fprintf(Shared->output_file, "%d: RD %d: return home\n", ++(*Shared->A_counter), uniq_rd_id);
    ++(*Shared->rd_home_num); //zvýším počet sobů, co se vrátili domů
    sem_post(Shared->sem_write);

    if(*(Shared->rd_home_num) == Arguments->reindeer_num)
    {
        sem_post(Shared->santa_work); //pokud budou doma všichni sobové, tak vzbudím santu
    }

    sem_wait(Shared->sem_reindeer); //zde bude sob čekat, až se probudí santa a zapřáhne ho
    sem_wait(Shared->sem_write);
    fprintf(Shared->output_file, "%d: RD %d: get hitched\n", ++(*Shared->A_counter), uniq_rd_id);
    sem_post(Shared->sem_write);
    sem_post(Shared->santa_action); //až budou všichni sobové zapřaženi, tak začaly Vánoce
}


int main(int argc, char *argv[])
{
    //vytvoření proměnných
    param_t Arguments;
    shared_t Shared;
    int err_num = 0;
    pid_t santa_pid;
    pid_t elf_pid;
    pid_t reindeer_pid;

    //inicializace proměnných ze struktury kvůli sdílené paměti
    Shared.A_counter = NULL;
    Shared.elf_id = NULL;
    Shared.rd_id = NULL;
    Shared.elf_wait_num = NULL;
    Shared.elf_wait_num = NULL;
    Shared.holidays = NULL;
    Shared.output_file = NULL;

    //kontrola argumentů
    err_num = check_arguments(argc, argv, &Arguments);
    if(err_num != 0){
        print_error(err_num);
        return 1;
    }
    //vytvoření sdílené paměti
    shared_memory(&Shared);

    //načtení souboru
    err_num = open_file(&Shared);
    if(err_num != 0)
        return 1;
    
    //počáteční nastavení hodnot
    *Shared.A_counter = 0;
    *Shared.elf_id = 0;
    *Shared.elf_wait_num = 0;
    *Shared.rd_id = 0;
    *Shared.rd_home_num = 0;
    
    //vytvoření semaforů
    err_num = create_semaf(&Shared);
    if(err_num != 0){
        print_error(err_num);
        return 1;
    }
    
    //vytvoření santova procesu
    santa_pid = fork();
    if(santa_pid == -1)
    {
        print_error(WRONG_CHILD);
        return 1;
    }
    else if (santa_pid == 0)
    {
        santa_process(&Shared, &Arguments); //pokud je dítě, jedná se o proces santy
        exit(0);
    }

    //vytvoření procesů pro skřítky
    for (int i = 0; i < Arguments.elf_num; i++)
    {
        elf_pid = fork();
        if(elf_pid == -1)
        {
            print_error(WRONG_CHILD);
            return 1;
        }
        else if (elf_pid == 0)
        {
            elf_process(&Shared, &Arguments); //pokud je dítě, jedná se o skřítkův proces
            exit(0);
        }
    }

    //vytvoření procesů pro soby
    for (int i = 0; i < Arguments.reindeer_num; i++)
    {
        reindeer_pid = fork();
        if(reindeer_pid == -1)
        {
            print_error(WRONG_CHILD);
            return 1;
        }
        else if (reindeer_pid == 0)
        {
            reindeer_process(&Shared, &Arguments); //pokud je dítě, jedná se o sobův proces
            exit(0);
        }
    }

    //ošetřím to, aby se mi hlavní proces ukončil až tehdy, pokud se ukončí všechny ostatní
    for (int i = 0; i < Arguments.elf_num + Arguments.reindeer_num + 1; i++)
        wait(NULL);

    fclose(Shared.output_file); //uzavření souboru
    destroy_semaf(&Shared); //odstranění semaforů
    del_shared_memory(&Shared); //odstranění sdílené paměti

    return 0;
}

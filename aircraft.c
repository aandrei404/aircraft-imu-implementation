#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <semaphore.h>
#include <unistd.h>

FILE *acc_ptr; // accelerometru
FILE *ang_ptr; // giroscop

double integral_const = 1.66; // folosit pentru a calcula vitezele, este h/3 din formula

double sum_x, sum_y, sum_z = 0.0; // suma din formula pentru integrala
double acc_x, acc_y, acc_z = 0.0; // acceleratiile noastre, initial = 0
double theta, phi, psi = 0.0;     // unghiurile noastre, initial = 0
double vel_x, vel_y, vel_z = 0.0; // vitezele noastre
int parity_x, parity_y, parity_z = 0;
int op_num = 8;

/**
 * Formula de calcul a vitezelor, rezultat in urma aplicarii matricei de rotatie asupra
 * vitezelor calculate prin integrare:
 *
 * Rvel_x = cos(phi) * cos(theta) * vel_x + (sin(psi) * sin(phi) * cos(theta) - cos(psi) * sin(theta)) * vel_y + (cos(psi) * sin(phi) * cos(theta) + sin(psi) * sin(theta)) * vel_z;
 * Rvel_y = cos(phi) * sin(theta) * vel_x + (sin(psi) * sin(phi) * sin(theta) + cos(psi) * cos(theta)) * vel_y + (cos(psi) * sin(phi) * sin(theta) - sin(psi) * cos(theta)) * vel_z;
 * Rvel_z = -sin(phi) * vel_x + sin(psi) * cos(phi) * vel_y + cos(psi) * cos(phi) * vel_z;
 *
 */
double Rvel_x, Rvel_y, Rvel_z = 0.0;
double Rvel_rez = 0.0;

// timerul pentru accelerometru
timer_t tacc_id;
struct itimerspec acc_timer;

// timerul pentru giroscop
timer_t tang_id;
struct itimerspec ang_timer;

// timer pentru afisare
timer_t tprint_id;
struct itimerspec print_timer;

// signals
#define SIGACC 420 // id semnal citire acceleratii
#define SIGANG 609 // id semnal citire unghiuri
#define SIGPRN 999 // id semnal afisare

// sigevent pentru acc, giroscop si afisare valori
struct sigevent sig_read_acc;
struct sigevent sig_read_ang;
struct sigevent sig_print_vel;

// semafor pentru sincronizare threaduri
pthread_mutex_t semSync = PTHREAD_MUTEX_INITIALIZER;

// threads
pthread_t thread_acc_x, thread_acc_y, thread_acc_z; // calc acceleratiile
pthread_t thread_vel_x, thread_vel_y, thread_vel_z; // calc vitezele

pthread_t thread_fin_acc;  // finalizare acceleratii
pthread_t thread_print;    // afisare
pthread_t thread_read_acc; // citim acceleratiile
pthread_t thread_read_ang; // citim unghiurile

//--------------- thread logic---------------
// thread pt citire acceleratii
void *read_acc(void *ptr)
{
    pthread_mutex_lock(&semSync);
    fscanf((FILE *)ptr, "%lf,%lf,%lf\n", &acc_x, &acc_y, &acc_z);
    pthread_mutex_unlock(&semSync);
    return NULL;
}

// thread pt citire unghiuri + ultima masuratoare de acceleratii
void *read_ang(void *ptr)
{
    pthread_mutex_lock(&semSync);
    fscanf((FILE *)ptr, "%lf,%lf,%lf\n", &theta, &phi, &psi);
    pthread_mutex_unlock(&semSync);
    return NULL;
}

// thread pt afisare viteze
void *print_vel_all()
{
    pthread_mutex_lock(&semSync);
    Rvel_x = cos(phi) * cos(theta) * vel_x + (sin(psi) * sin(phi) * cos(theta) - cos(psi) * sin(theta)) * vel_y + (cos(psi) * sin(phi) * cos(theta) + sin(psi) * sin(theta)) * vel_z;
    Rvel_y = cos(phi) * sin(theta) * vel_x + (sin(psi) * sin(phi) * sin(theta) + cos(psi) * cos(theta)) * vel_y + (cos(psi) * sin(phi) * sin(theta) - sin(psi) * cos(theta)) * vel_z;
    Rvel_z = -sin(phi) * vel_x + sin(psi) * cos(phi) * vel_y + cos(psi) * cos(phi) * vel_z;
    Rvel_rez = sqrt(Rvel_x * Rvel_x + Rvel_y * Rvel_y + Rvel_z * Rvel_z);
    printf("Vel_x = %.3lf - Vel_y = %.3lf - Vel_z = %.3lf\nVel_modul =  %.3lf\n---------------\n", Rvel_x, Rvel_y, Rvel_z, Rvel_rez);
    pthread_mutex_unlock(&semSync);
    return NULL;
}

// trhreaduri pt calculul integralelor
void *calc_acc_x()
{
    pthread_mutex_lock(&semSync);
    if (parity_x == 0)
    {
        sum_x = sum_x + 4 * acc_x;
        parity_x = 1;
    }
    else
    {
        sum_x = sum_x + 2 * acc_x;
        parity_x = 0;
    }

    return NULL;
}

void *calc_acc_y()
{
    if (parity_y == 0)
    {
        sum_y = sum_y + 4 * acc_y;
        parity_y = 1;
    }
    else
    {
        sum_y = sum_y + 2 * acc_y;
        parity_y = 0;
    }

    return NULL;
}

void *calc_acc_z()
{
    if (parity_z == 0)
    {
        sum_z = sum_z + 4 * acc_z;
        parity_z = 1;
    }
    else
    {
        sum_z = sum_z + 2 * acc_z;
        parity_z = 0;
    }

    pthread_mutex_unlock(&semSync);
    return NULL;
}

// threaduri pentru calculul vitezelor
void *calc_vel_x()
{
    pthread_mutex_lock(&semSync);
    sum_x = sum_x + acc_x;
    vel_x = integral_const * sum_x;
    sum_x = acc_x;

    return NULL;
}

void *calc_vel_y()
{
    sum_y = sum_y + acc_y;
    vel_y = integral_const * sum_y;
    sum_y = acc_y;

    return NULL;
}

void *calc_vel_z()
{
    sum_z = sum_z + acc_z;
    vel_z = integral_const * sum_z;
    sum_z = acc_z;
    
    pthread_mutex_unlock(&semSync);

    return NULL;
}

// logica handler-ului de semnale
void handler_semnale(union sigval semnal)
{
    int semnal_no = semnal.sival_int; // obtinem id-ul semnalului

    switch (semnal_no)
    {
    default: // do nothing
        break;
    case SIGACC: // citire accelerometru + calcul suma pentru integrala
    {
        if (op_num != 1)
        {
            pthread_create(&thread_read_acc, NULL, read_acc, (void *)acc_ptr);
            // pthread_join(thread_read_acc, NULL);

            pthread_create(&thread_acc_x, NULL, calc_acc_x, NULL);
            // pthread_join(thread_acc_x, NULL);

            pthread_create(&thread_acc_y, NULL, calc_acc_y, NULL);
            // pthread_join(thread_acc_y, NULL);

            pthread_create(&thread_acc_z, NULL, calc_acc_z, NULL);
            // pthread_join(thread_acc_z, NULL);

            op_num = op_num - 1;
        }

        break;
    }
    case SIGANG: // citire giroscop + calcul viteze
    {

        pthread_create(&thread_read_acc, NULL, read_acc, (void *)acc_ptr);
        // pthread_join(thread_read_acc, NULL);
        pthread_create(&thread_read_ang, NULL, read_ang, (void *)ang_ptr);
        // pthread_join(thread_read_ang, NULL);

        pthread_create(&thread_vel_x, NULL, calc_vel_x, NULL);
        // pthread_join(thread_vel_x, NULL);

        pthread_create(&thread_vel_y, NULL, calc_vel_y, NULL);
        // pthread_join(thread_vel_y, NULL);

        pthread_create(&thread_vel_z, NULL, calc_vel_z, NULL);
        // pthread_join(thread_vel_z, NULL);

        parity_x = 0;
        parity_y = 0;
        parity_z = 0;
        op_num = 8;

        break;
    }
    case SIGPRN: // afisam vitezele
    {
        pthread_create(&thread_print, NULL, print_vel_all, NULL);
        break;
    }
    }
}

int main()
{

    // sigevent logic pt timere
    memset(&sig_read_acc, 0, sizeof(struct sigevent));  // acelerometru
    memset(&sig_read_ang, 0, sizeof(struct sigevent));  // giroscop
    memset(&sig_print_vel, 0, sizeof(struct sigevent)); // afisare

    // sigevent pt accelerometru
    sig_read_acc.sigev_notify = SIGEV_THREAD;
    sig_read_acc.sigev_notify_function = &handler_semnale;
    sig_read_acc.sigev_value.sival_int = SIGACC;

    // sigevent pt giroscop
    sig_read_ang.sigev_notify = SIGEV_THREAD;
    sig_read_ang.sigev_notify_function = &handler_semnale;
    sig_read_ang.sigev_value.sival_int = SIGANG;

    // sigevent pt citire
    sig_print_vel.sigev_notify = SIGEV_THREAD;
    sig_print_vel.sigev_notify_function = &handler_semnale;
    sig_print_vel.sigev_value.sival_int = SIGPRN;

    // timer logic
    memset(&ang_timer, 0, sizeof(struct itimerspec));   // giroscop
    memset(&acc_timer, 0, sizeof(struct itimerspec));   // accelerometru
    memset(&print_timer, 0, sizeof(struct itimerspec)); // afisare

    // cream timerele
    timer_create(CLOCK_REALTIME, &sig_read_acc, &tacc_id);    // accelerometru
    timer_create(CLOCK_REALTIME, &sig_read_ang, &tang_id);    // giroscop
    timer_create(CLOCK_REALTIME, &sig_print_vel, &tprint_id); // afisare

    // acc_timer la fiecare 5ms
    acc_timer.it_interval.tv_sec = 0;
    acc_timer.it_interval.tv_nsec = 5000000;
    acc_timer.it_value.tv_sec = 0;
    acc_timer.it_value.tv_nsec = 5000000;

    // ang_timer la fiecare 40ms
    ang_timer.it_interval.tv_sec = 0;
    ang_timer.it_interval.tv_nsec = 40000000;
    ang_timer.it_value.tv_sec = 0;
    ang_timer.it_value.tv_nsec = 40000000;

    // print timer la fiecare 1s
    print_timer.it_interval.tv_nsec = 0;
    print_timer.it_interval.tv_sec = 1;
    print_timer.it_value.tv_nsec = 0;
    print_timer.it_value.tv_sec = 1;

    // in cazul in care "senzorii" nostri esential lipsesc, nu putem porni programul
    if ((acc_ptr = fopen("./sensors/acc_data", "r")) == NULL || (ang_ptr = fopen("./sensors/ang_data", "r")) == NULL)
    {
        perror("Nu avem accelerometru sau giroscop!\n");
        exit(1);
    }
    else
    {

        // da-i nicule da-i tare
        printf("Lift off!...\n");
        // tureaza motorul tatiiii
        printf("-------------BRRRRRRRRRRRRRR---------------\n");

        // initializam timerele
        timer_settime(tacc_id, 0, &acc_timer, NULL);
        timer_settime(tang_id, 0, &ang_timer, NULL);
        timer_settime(tprint_id, 0, &print_timer, NULL);

        while (1) // main loop
        {
            sleep(80); // rulam doar pt 80 secunde momentan

            // stergem timerele si inchidem "senzorii" la final
            timer_delete(tacc_id);
            timer_delete(tang_id);
            timer_delete(tprint_id);
            fclose(acc_ptr);
            fclose(ang_ptr);
            printf("Avion cu motor!\n");
            exit(0);
        }
    }

    return 0;
}
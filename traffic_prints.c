#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#include "uthread.h"
#include "uthread_mutex_cond.h"

#ifdef VERBOSE
#define VERBOSE_PRINT(S, ...) printf (S, ##__VA_ARGS__)
#else
#define VERBOSE_PRINT(S, ...) ((void) 0) // do nothing
#endif

#define MAX_OCCUPANCY  3
#define NUM_ITERATIONS 100  //TODO 100
#define NUM_CARS       20  //TODO: 20

// These times determine the number of times yield is called when in
// the street, or when waiting before crossing again.
#define CROSSING_TIME             NUM_CARS
#define WAIT_TIME_BETWEEN_CROSSES NUM_CARS

/**
 * You might find these declarations useful.
 */
enum Direction {EAST = 0, WEST = 1};
const static enum Direction directions [] = {EAST, WEST};
const static enum Direction oppositeEnd [] = {WEST, EAST};

struct Street {
  uthread_mutex_t mx;
  uthread_cond_t cond;
  int numCars;
  enum Direction direction;
} Street;

void initializeStreet(void) {
  struct Street* s = malloc(sizeof(struct Street));
  Street = *s;
  Street.mx = uthread_mutex_create();
  Street.cond = uthread_cond_create(Street.mx);
  Street.numCars = 0;
  Street.direction = EAST; //default direction

}

#define WAITING_HISTOGRAM_SIZE (NUM_ITERATIONS * NUM_CARS)
int             entryTicker;                                          // incremented with each entry
int             waitingHistogram [WAITING_HISTOGRAM_SIZE];
int             waitingHistogramOverflow;
uthread_mutex_t waitingHistogramLock;
int             occupancyHistogram [2] [MAX_OCCUPANCY + 1];

int threadcount = 0;

void enterStreet (enum Direction g, int threadID) {
  uthread_mutex_lock(Street.mx);
  while(Street.numCars >= 3 || ((g != Street.direction) && Street.numCars != 0)){
    uthread_cond_wait(Street.cond);
  }
  uthread_mutex_lock(waitingHistogramLock);
  entryTicker++;
  uthread_mutex_unlock(waitingHistogramLock);

  if(Street.numCars == 0){
    Street.direction = g;
  }
  Street.numCars++;
  
  uthread_mutex_lock(waitingHistogramLock);
  occupancyHistogram[Street.direction][Street.numCars]++;
  uthread_mutex_unlock(waitingHistogramLock);
  
  assert(g == Street.direction);
  assert(Street.numCars <= 3);

  uthread_mutex_unlock(Street.mx);

}

void leaveStreet(int threadID, enum Direction g) {
  
  uthread_mutex_lock(Street.mx);
  Street.numCars--;
  uthread_cond_broadcast(Street.cond);
  uthread_mutex_unlock(Street.mx);
}

void recordWaitingTime (int waitingTime) {
  uthread_mutex_lock (waitingHistogramLock);
  if (waitingTime < WAITING_HISTOGRAM_SIZE)
    waitingHistogram [waitingTime] ++;
  else
    waitingHistogramOverflow ++;
  uthread_mutex_unlock (waitingHistogramLock);
}


void* car(void* direction){
  enum Direction* d = direction;

  uthread_mutex_lock (waitingHistogramLock);
  int threadID = threadcount++;
  uthread_mutex_unlock (waitingHistogramLock);

  for(int i = 0; i < NUM_ITERATIONS; i++){

    uthread_mutex_lock (waitingHistogramLock);
    int startET = entryTicker;
    uthread_mutex_unlock (waitingHistogramLock);

    enterStreet(*d, threadID);
    
    recordWaitingTime(entryTicker - startET);
    for(int i = 0; i < CROSSING_TIME; i++){
      uthread_yield();
    }
    leaveStreet(threadID, *d);
    for(int i = 0; i < WAIT_TIME_BETWEEN_CROSSES; i++){
      uthread_yield();
    }

  }
  return NULL;
}

int main (int argc, char** argv) {

  uthread_init(8);

  waitingHistogramLock = uthread_mutex_create();
  entryTicker = 0;

  initializeStreet();
  uthread_t pt [NUM_CARS];

  srand(time(NULL));
  for (int i = 0; i < NUM_CARS; i++){
    
    int j = rand() % 2;
    pt[i] = uthread_create(car, (void*) &directions[j]);
  }

  for(int i = 0; i < NUM_CARS; i++){
    uthread_join(pt[i], NULL);
  }
  fflush(stdin);

  printf ("Times with 1 car  going east: %d\n", occupancyHistogram [EAST] [1]);
  printf ("Times with 2 cars going east: %d\n", occupancyHistogram [EAST] [2]);
  printf ("Times with 3 cars going east: %d\n", occupancyHistogram [EAST] [3]);
  printf ("Times with 1 car  going west: %d\n", occupancyHistogram [WEST] [1]);
  printf ("Times with 2 cars going west: %d\n", occupancyHistogram [WEST] [2]);
  printf ("Times with 3 cars going west: %d\n", occupancyHistogram [WEST] [3]);
  
  printf ("Waiting Histogram\n");
  
  for (int i=0; i < WAITING_HISTOGRAM_SIZE; i++)
    if (waitingHistogram [i])
      printf ("  Cars waited for           %4d car%s to enter: %4d time(s)\n",
	      i, i==1 ? " " : "s", waitingHistogram [i]);
  if (waitingHistogramOverflow)
    printf ("  Cars waited for more than %4d cars to enter: %4d time(s)\n",
	    WAITING_HISTOGRAM_SIZE, waitingHistogramOverflow);
  //printf("%d\n", entryTicker);
}

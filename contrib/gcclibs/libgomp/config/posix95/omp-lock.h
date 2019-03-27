/* This header is used during the build process to find the size and 
   alignment of the public OpenMP locks, so that we can export data
   structures without polluting the namespace.

   In this POSIX95 implementation, we map the two locks to the
   same PTHREADS primitive.  */

#include <pthread.h>

typedef pthread_mutex_t omp_lock_t;

typedef struct
{
  pthread_mutex_t lock;
  pthread_t owner;
  int count;
} omp_nest_lock_t;

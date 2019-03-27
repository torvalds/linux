/* This header is used during the build process to find the size and 
   alignment of the public OpenMP locks, so that we can export data
   structures without polluting the namespace.

   When using the Linux futex primitive, non-recursive locks require
   only one int.  Recursive locks require we identify the owning thread
   and so require two ints.  */

typedef int omp_lock_t;
typedef struct { int owner, count; } omp_nest_lock_t;

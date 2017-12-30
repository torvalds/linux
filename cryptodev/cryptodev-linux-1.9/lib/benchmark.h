#include <sys/time.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>

typedef void (*sighandler_t)(int);

struct benchmark_st
{
  struct timeval start;
  sighandler_t old_handler;
};

extern int benchmark_must_finish;

int start_benchmark(struct benchmark_st * st);
int stop_benchmark(struct benchmark_st * st, unsigned long * elapsed);


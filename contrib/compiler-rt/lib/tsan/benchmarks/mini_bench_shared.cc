// Mini-benchmark for tsan: shared memory reads.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int len;
int *a;
const int kNumIter = 1000;

__attribute__((noinline))
void Run(int idx) {
  for (int i = 0, n = len; i < n; i++)
    if (a[i] != i) abort();
}

void *Thread(void *arg) {
  long idx = (long)arg;
  printf("Thread %ld started\n", idx);
  for (int i = 0; i < kNumIter; i++)
    Run(idx);
  printf("Thread %ld done\n", idx);
  return 0;
}

int main(int argc, char **argv) {
  int n_threads = 0;
  if (argc != 3) {
    n_threads = 4;
    len = 1000000;
  } else {
    n_threads = atoi(argv[1]);
    assert(n_threads > 0 && n_threads <= 32);
    len = atoi(argv[2]);
  }
  printf("%s: n_threads=%d len=%d iter=%d\n",
         __FILE__, n_threads, len, kNumIter);
  a = new int[len];
  for (int i = 0, n = len; i < n; i++)
    a[i] = i;
  pthread_t *t = new pthread_t[n_threads];
  for (int i = 0; i < n_threads; i++) {
    pthread_create(&t[i], 0, Thread, (void*)i);
  }
  for (int i = 0; i < n_threads; i++) {
    pthread_join(t[i], 0);
  }
  delete [] t;
  delete [] a;
  return 0;
}

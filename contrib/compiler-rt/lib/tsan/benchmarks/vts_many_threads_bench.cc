// Mini-benchmark for tsan VTS worst case performance
// Idea:
// 1) Spawn M + N threads (M >> N)
//    We'll call the 'M' threads as 'garbage threads'.
// 2) Make sure all threads have created thus no TIDs were reused
// 3) Join the garbage threads
// 4) Do many sync operations on the remaining N threads
//
// It turns out that due to O(M+N) VTS complexity the (4) is much slower with
// when N is large.
//
// Some numbers:
// a) clang++ native O1 with n_iterations=200kk takes
//      5s regardless of M
//    clang++ tsanv2 O1 with n_iterations=20kk takes
//      23.5s with M=200
//      11.5s with M=1
//    i.e. tsanv2 is ~23x to ~47x slower than native, depends on M.
// b) g++ native O1 with n_iterations=200kk takes
//      5.5s regardless of M
//    g++ tsanv1 O1 with n_iterations=2kk takes
//      39.5s with M=200
//      20.5s with M=1
//    i.e. tsanv1 is ~370x to ~720x slower than native, depends on M.

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

class __attribute__((aligned(64))) Mutex {
 public:
  Mutex()  { pthread_mutex_init(&m_, NULL); }
  ~Mutex() { pthread_mutex_destroy(&m_); }
  void Lock() { pthread_mutex_lock(&m_); }
  void Unlock() { pthread_mutex_unlock(&m_); }

 private:
  pthread_mutex_t m_;
};

const int kNumMutexes = 1024;
Mutex mutexes[kNumMutexes];

int n_threads, n_iterations;

pthread_barrier_t all_threads_ready, main_threads_ready;

void* GarbageThread(void *unused) {
  pthread_barrier_wait(&all_threads_ready);
  return 0;
}

void *Thread(void *arg) {
  long idx = (long)arg;
  pthread_barrier_wait(&all_threads_ready);

  // Wait for the main thread to join the garbage threads.
  pthread_barrier_wait(&main_threads_ready);

  printf("Thread %ld go!\n", idx);
  int offset = idx * kNumMutexes / n_threads;
  for (int i = 0; i < n_iterations; i++) {
    mutexes[(offset + i) % kNumMutexes].Lock();
    mutexes[(offset + i) % kNumMutexes].Unlock();
  }
  printf("Thread %ld done\n", idx);
  return 0;
}

int main(int argc, char **argv) {
  int n_garbage_threads;
  if (argc == 1) {
    n_threads = 2;
    n_garbage_threads = 200;
    n_iterations = 20000000;
  } else if (argc == 4) {
    n_threads = atoi(argv[1]);
    assert(n_threads > 0 && n_threads <= 32);
    n_garbage_threads = atoi(argv[2]);
    assert(n_garbage_threads > 0 && n_garbage_threads <= 16000);
    n_iterations = atoi(argv[3]);
  } else {
    printf("Usage: %s n_threads n_garbage_threads n_iterations\n", argv[0]);
    return 1;
  }
  printf("%s: n_threads=%d n_garbage_threads=%d n_iterations=%d\n",
         __FILE__, n_threads, n_garbage_threads, n_iterations);

  pthread_barrier_init(&all_threads_ready, NULL, n_garbage_threads + n_threads + 1);
  pthread_barrier_init(&main_threads_ready, NULL, n_threads + 1);

  pthread_t *t = new pthread_t[n_threads];
  {
    pthread_t *g_t = new pthread_t[n_garbage_threads];
    for (int i = 0; i < n_garbage_threads; i++) {
      int status = pthread_create(&g_t[i], 0, GarbageThread, NULL);
      assert(status == 0);
    }
    for (int i = 0; i < n_threads; i++) {
      int status = pthread_create(&t[i], 0, Thread, (void*)i);
      assert(status == 0);
    }
    pthread_barrier_wait(&all_threads_ready);
    printf("All threads started! Killing the garbage threads.\n");
    for (int i = 0; i < n_garbage_threads; i++) {
      pthread_join(g_t[i], 0);
    }
    delete [] g_t;
  }
  printf("Resuming the main threads.\n");
  pthread_barrier_wait(&main_threads_ready);


  for (int i = 0; i < n_threads; i++) {
    pthread_join(t[i], 0);
  }
  delete [] t;
  return 0;
}

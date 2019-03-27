//===-- scudo_tsd_exclusive.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Scudo exclusive TSD implementation.
///
//===----------------------------------------------------------------------===//

#include "scudo_tsd.h"

#if SCUDO_TSD_EXCLUSIVE

namespace __scudo {

static pthread_once_t GlobalInitialized = PTHREAD_ONCE_INIT;
static pthread_key_t PThreadKey;

__attribute__((tls_model("initial-exec")))
THREADLOCAL ThreadState ScudoThreadState = ThreadNotInitialized;
__attribute__((tls_model("initial-exec")))
THREADLOCAL ScudoTSD TSD;

// Fallback TSD for when the thread isn't initialized yet or is torn down. It
// can be shared between multiple threads and as such must be locked.
ScudoTSD FallbackTSD;

static void teardownThread(void *Ptr) {
  uptr I = reinterpret_cast<uptr>(Ptr);
  // The glibc POSIX thread-local-storage deallocation routine calls user
  // provided destructors in a loop of PTHREAD_DESTRUCTOR_ITERATIONS.
  // We want to be called last since other destructors might call free and the
  // like, so we wait until PTHREAD_DESTRUCTOR_ITERATIONS before draining the
  // quarantine and swallowing the cache.
  if (I > 1) {
    // If pthread_setspecific fails, we will go ahead with the teardown.
    if (LIKELY(pthread_setspecific(PThreadKey,
                                   reinterpret_cast<void *>(I - 1)) == 0))
      return;
  }
  TSD.commitBack();
  ScudoThreadState = ThreadTornDown;
}


static void initOnce() {
  CHECK_EQ(pthread_key_create(&PThreadKey, teardownThread), 0);
  initScudo();
  FallbackTSD.init();
}

void initThread(bool MinimalInit) {
  CHECK_EQ(pthread_once(&GlobalInitialized, initOnce), 0);
  if (UNLIKELY(MinimalInit))
    return;
  CHECK_EQ(pthread_setspecific(PThreadKey, reinterpret_cast<void *>(
      GetPthreadDestructorIterations())), 0);
  TSD.init();
  ScudoThreadState = ThreadInitialized;
}

}  // namespace __scudo

#endif  // SCUDO_TSD_EXCLUSIVE

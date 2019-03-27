//===-- dd_interceptors.cc ------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "dd_rtl.h"
#include "interception/interception.h"
#include "sanitizer_common/sanitizer_procmaps.h"
#include <pthread.h>
#include <stdlib.h>

using namespace __dsan;

__attribute__((tls_model("initial-exec")))
static __thread Thread *thr;
__attribute__((tls_model("initial-exec")))
static __thread volatile int initing;
static bool inited;
static uptr g_data_start;
static uptr g_data_end;

static bool InitThread() {
  if (initing)
    return false;
  if (thr != 0)
    return true;
  initing = true;
  if (!inited) {
    inited = true;
    Initialize();
  }
  thr = (Thread*)InternalAlloc(sizeof(*thr));
  internal_memset(thr, 0, sizeof(*thr));
  ThreadInit(thr);
  initing = false;
  return true;
}

INTERCEPTOR(int, pthread_mutex_destroy, pthread_mutex_t *m) {
  InitThread();
  MutexDestroy(thr, (uptr)m);
  return REAL(pthread_mutex_destroy)(m);
}

INTERCEPTOR(int, pthread_mutex_lock, pthread_mutex_t *m) {
  InitThread();
  MutexBeforeLock(thr, (uptr)m, true);
  int res = REAL(pthread_mutex_lock)(m);
  MutexAfterLock(thr, (uptr)m, true, false);
  return res;
}

INTERCEPTOR(int, pthread_mutex_trylock, pthread_mutex_t *m) {
  InitThread();
  int res = REAL(pthread_mutex_trylock)(m);
  if (res == 0)
    MutexAfterLock(thr, (uptr)m, true, true);
  return res;
}

INTERCEPTOR(int, pthread_mutex_unlock, pthread_mutex_t *m) {
  InitThread();
  MutexBeforeUnlock(thr, (uptr)m, true);
  return REAL(pthread_mutex_unlock)(m);
}

INTERCEPTOR(int, pthread_spin_destroy, pthread_spinlock_t *m) {
  InitThread();
  int res = REAL(pthread_spin_destroy)(m);
  MutexDestroy(thr, (uptr)m);
  return res;
}

INTERCEPTOR(int, pthread_spin_lock, pthread_spinlock_t *m) {
  InitThread();
  MutexBeforeLock(thr, (uptr)m, true);
  int res = REAL(pthread_spin_lock)(m);
  MutexAfterLock(thr, (uptr)m, true, false);
  return res;
}

INTERCEPTOR(int, pthread_spin_trylock, pthread_spinlock_t *m) {
  InitThread();
  int res = REAL(pthread_spin_trylock)(m);
  if (res == 0)
    MutexAfterLock(thr, (uptr)m, true, true);
  return res;
}

INTERCEPTOR(int, pthread_spin_unlock, pthread_spinlock_t *m) {
  InitThread();
  MutexBeforeUnlock(thr, (uptr)m, true);
  return REAL(pthread_spin_unlock)(m);
}

INTERCEPTOR(int, pthread_rwlock_destroy, pthread_rwlock_t *m) {
  InitThread();
  MutexDestroy(thr, (uptr)m);
  return REAL(pthread_rwlock_destroy)(m);
}

INTERCEPTOR(int, pthread_rwlock_rdlock, pthread_rwlock_t *m) {
  InitThread();
  MutexBeforeLock(thr, (uptr)m, false);
  int res = REAL(pthread_rwlock_rdlock)(m);
  MutexAfterLock(thr, (uptr)m, false, false);
  return res;
}

INTERCEPTOR(int, pthread_rwlock_tryrdlock, pthread_rwlock_t *m) {
  InitThread();
  int res = REAL(pthread_rwlock_tryrdlock)(m);
  if (res == 0)
    MutexAfterLock(thr, (uptr)m, false, true);
  return res;
}

INTERCEPTOR(int, pthread_rwlock_timedrdlock, pthread_rwlock_t *m,
    const timespec *abstime) {
  InitThread();
  int res = REAL(pthread_rwlock_timedrdlock)(m, abstime);
  if (res == 0)
    MutexAfterLock(thr, (uptr)m, false, true);
  return res;
}

INTERCEPTOR(int, pthread_rwlock_wrlock, pthread_rwlock_t *m) {
  InitThread();
  MutexBeforeLock(thr, (uptr)m, true);
  int res = REAL(pthread_rwlock_wrlock)(m);
  MutexAfterLock(thr, (uptr)m, true, false);
  return res;
}

INTERCEPTOR(int, pthread_rwlock_trywrlock, pthread_rwlock_t *m) {
  InitThread();
  int res = REAL(pthread_rwlock_trywrlock)(m);
  if (res == 0)
    MutexAfterLock(thr, (uptr)m, true, true);
  return res;
}

INTERCEPTOR(int, pthread_rwlock_timedwrlock, pthread_rwlock_t *m,
    const timespec *abstime) {
  InitThread();
  int res = REAL(pthread_rwlock_timedwrlock)(m, abstime);
  if (res == 0)
    MutexAfterLock(thr, (uptr)m, true, true);
  return res;
}

INTERCEPTOR(int, pthread_rwlock_unlock, pthread_rwlock_t *m) {
  InitThread();
  MutexBeforeUnlock(thr, (uptr)m, true);  // note: not necessary write unlock
  return REAL(pthread_rwlock_unlock)(m);
}

static pthread_cond_t *init_cond(pthread_cond_t *c, bool force = false) {
  atomic_uintptr_t *p = (atomic_uintptr_t*)c;
  uptr cond = atomic_load(p, memory_order_acquire);
  if (!force && cond != 0)
    return (pthread_cond_t*)cond;
  void *newcond = malloc(sizeof(pthread_cond_t));
  internal_memset(newcond, 0, sizeof(pthread_cond_t));
  if (atomic_compare_exchange_strong(p, &cond, (uptr)newcond,
      memory_order_acq_rel))
    return (pthread_cond_t*)newcond;
  free(newcond);
  return (pthread_cond_t*)cond;
}

INTERCEPTOR(int, pthread_cond_init, pthread_cond_t *c,
    const pthread_condattr_t *a) {
  InitThread();
  pthread_cond_t *cond = init_cond(c, true);
  return REAL(pthread_cond_init)(cond, a);
}

INTERCEPTOR(int, pthread_cond_wait, pthread_cond_t *c, pthread_mutex_t *m) {
  InitThread();
  pthread_cond_t *cond = init_cond(c);
  MutexBeforeUnlock(thr, (uptr)m, true);
  MutexBeforeLock(thr, (uptr)m, true);
  int res = REAL(pthread_cond_wait)(cond, m);
  MutexAfterLock(thr, (uptr)m, true, false);
  return res;
}

INTERCEPTOR(int, pthread_cond_timedwait, pthread_cond_t *c, pthread_mutex_t *m,
    const timespec *abstime) {
  InitThread();
  pthread_cond_t *cond = init_cond(c);
  MutexBeforeUnlock(thr, (uptr)m, true);
  MutexBeforeLock(thr, (uptr)m, true);
  int res = REAL(pthread_cond_timedwait)(cond, m, abstime);
  MutexAfterLock(thr, (uptr)m, true, false);
  return res;
}

INTERCEPTOR(int, pthread_cond_signal, pthread_cond_t *c) {
  InitThread();
  pthread_cond_t *cond = init_cond(c);
  return REAL(pthread_cond_signal)(cond);
}

INTERCEPTOR(int, pthread_cond_broadcast, pthread_cond_t *c) {
  InitThread();
  pthread_cond_t *cond = init_cond(c);
  return REAL(pthread_cond_broadcast)(cond);
}

INTERCEPTOR(int, pthread_cond_destroy, pthread_cond_t *c) {
  InitThread();
  pthread_cond_t *cond = init_cond(c);
  int res = REAL(pthread_cond_destroy)(cond);
  free(cond);
  atomic_store((atomic_uintptr_t*)c, 0, memory_order_relaxed);
  return res;
}

// for symbolizer
INTERCEPTOR(char*, realpath, const char *path, char *resolved_path) {
  InitThread();
  return REAL(realpath)(path, resolved_path);
}

INTERCEPTOR(SSIZE_T, read, int fd, void *ptr, SIZE_T count) {
  InitThread();
  return REAL(read)(fd, ptr, count);
}

INTERCEPTOR(SSIZE_T, pread, int fd, void *ptr, SIZE_T count, OFF_T offset) {
  InitThread();
  return REAL(pread)(fd, ptr, count, offset);
}

extern "C" {
void __dsan_before_mutex_lock(uptr m, int writelock) {
  if (!InitThread())
    return;
  MutexBeforeLock(thr, m, writelock);
}

void __dsan_after_mutex_lock(uptr m, int writelock, int trylock) {
  if (!InitThread())
    return;
  MutexAfterLock(thr, m, writelock, trylock);
}

void __dsan_before_mutex_unlock(uptr m, int writelock) {
  if (!InitThread())
    return;
  MutexBeforeUnlock(thr, m, writelock);
}

void __dsan_mutex_destroy(uptr m) {
  if (!InitThread())
    return;
  // if (m >= g_data_start && m < g_data_end)
  //   return;
  MutexDestroy(thr, m);
}
}  // extern "C"

namespace __dsan {

static void InitDataSeg() {
  MemoryMappingLayout proc_maps(true);
  char name[128];
  MemoryMappedSegment segment(name, ARRAY_SIZE(name));
  bool prev_is_data = false;
  while (proc_maps.Next(&segment)) {
    bool is_data = segment.offset != 0 && segment.filename[0] != 0;
    // BSS may get merged with [heap] in /proc/self/maps. This is not very
    // reliable.
    bool is_bss = segment.offset == 0 &&
                  (segment.filename[0] == 0 ||
                   internal_strcmp(segment.filename, "[heap]") == 0) &&
                  prev_is_data;
    if (g_data_start == 0 && is_data) g_data_start = segment.start;
    if (is_bss) g_data_end = segment.end;
    prev_is_data = is_data;
  }
  VPrintf(1, "guessed data_start=%p data_end=%p\n",  g_data_start, g_data_end);
  CHECK_LT(g_data_start, g_data_end);
  CHECK_GE((uptr)&g_data_start, g_data_start);
  CHECK_LT((uptr)&g_data_start, g_data_end);
}

void InitializeInterceptors() {
  INTERCEPT_FUNCTION(pthread_mutex_destroy);
  INTERCEPT_FUNCTION(pthread_mutex_lock);
  INTERCEPT_FUNCTION(pthread_mutex_trylock);
  INTERCEPT_FUNCTION(pthread_mutex_unlock);

  INTERCEPT_FUNCTION(pthread_spin_destroy);
  INTERCEPT_FUNCTION(pthread_spin_lock);
  INTERCEPT_FUNCTION(pthread_spin_trylock);
  INTERCEPT_FUNCTION(pthread_spin_unlock);

  INTERCEPT_FUNCTION(pthread_rwlock_destroy);
  INTERCEPT_FUNCTION(pthread_rwlock_rdlock);
  INTERCEPT_FUNCTION(pthread_rwlock_tryrdlock);
  INTERCEPT_FUNCTION(pthread_rwlock_timedrdlock);
  INTERCEPT_FUNCTION(pthread_rwlock_wrlock);
  INTERCEPT_FUNCTION(pthread_rwlock_trywrlock);
  INTERCEPT_FUNCTION(pthread_rwlock_timedwrlock);
  INTERCEPT_FUNCTION(pthread_rwlock_unlock);

  INTERCEPT_FUNCTION_VER(pthread_cond_init, "GLIBC_2.3.2");
  INTERCEPT_FUNCTION_VER(pthread_cond_signal, "GLIBC_2.3.2");
  INTERCEPT_FUNCTION_VER(pthread_cond_broadcast, "GLIBC_2.3.2");
  INTERCEPT_FUNCTION_VER(pthread_cond_wait, "GLIBC_2.3.2");
  INTERCEPT_FUNCTION_VER(pthread_cond_timedwait, "GLIBC_2.3.2");
  INTERCEPT_FUNCTION_VER(pthread_cond_destroy, "GLIBC_2.3.2");

  // for symbolizer
  INTERCEPT_FUNCTION(realpath);
  INTERCEPT_FUNCTION(read);
  INTERCEPT_FUNCTION(pread);

  InitDataSeg();
}

}  // namespace __dsan

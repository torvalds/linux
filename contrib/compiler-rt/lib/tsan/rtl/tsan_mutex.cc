//===-- tsan_mutex.cc -----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_libc.h"
#include "tsan_mutex.h"
#include "tsan_platform.h"
#include "tsan_rtl.h"

namespace __tsan {

// Simple reader-writer spin-mutex. Optimized for not-so-contended case.
// Readers have preference, can possibly starvate writers.

// The table fixes what mutexes can be locked under what mutexes.
// E.g. if the row for MutexTypeThreads contains MutexTypeReport,
// then Report mutex can be locked while under Threads mutex.
// The leaf mutexes can be locked under any other mutexes.
// Recursive locking is not supported.
#if SANITIZER_DEBUG && !SANITIZER_GO
const MutexType MutexTypeLeaf = (MutexType)-1;
static MutexType CanLockTab[MutexTypeCount][MutexTypeCount] = {
  /*0  MutexTypeInvalid*/     {},
  /*1  MutexTypeTrace*/       {MutexTypeLeaf},
  /*2  MutexTypeThreads*/     {MutexTypeReport},
  /*3  MutexTypeReport*/      {MutexTypeSyncVar,
                               MutexTypeMBlock, MutexTypeJavaMBlock},
  /*4  MutexTypeSyncVar*/     {MutexTypeDDetector},
  /*5  MutexTypeSyncTab*/     {},  // unused
  /*6  MutexTypeSlab*/        {MutexTypeLeaf},
  /*7  MutexTypeAnnotations*/ {},
  /*8  MutexTypeAtExit*/      {MutexTypeSyncVar},
  /*9  MutexTypeMBlock*/      {MutexTypeSyncVar},
  /*10 MutexTypeJavaMBlock*/  {MutexTypeSyncVar},
  /*11 MutexTypeDDetector*/   {},
  /*12 MutexTypeFired*/       {MutexTypeLeaf},
  /*13 MutexTypeRacy*/        {MutexTypeLeaf},
  /*14 MutexTypeGlobalProc*/  {},
};

static bool CanLockAdj[MutexTypeCount][MutexTypeCount];
#endif

void InitializeMutex() {
#if SANITIZER_DEBUG && !SANITIZER_GO
  // Build the "can lock" adjacency matrix.
  // If [i][j]==true, then one can lock mutex j while under mutex i.
  const int N = MutexTypeCount;
  int cnt[N] = {};
  bool leaf[N] = {};
  for (int i = 1; i < N; i++) {
    for (int j = 0; j < N; j++) {
      MutexType z = CanLockTab[i][j];
      if (z == MutexTypeInvalid)
        continue;
      if (z == MutexTypeLeaf) {
        CHECK(!leaf[i]);
        leaf[i] = true;
        continue;
      }
      CHECK(!CanLockAdj[i][(int)z]);
      CanLockAdj[i][(int)z] = true;
      cnt[i]++;
    }
  }
  for (int i = 0; i < N; i++) {
    CHECK(!leaf[i] || cnt[i] == 0);
  }
  // Add leaf mutexes.
  for (int i = 0; i < N; i++) {
    if (!leaf[i])
      continue;
    for (int j = 0; j < N; j++) {
      if (i == j || leaf[j] || j == MutexTypeInvalid)
        continue;
      CHECK(!CanLockAdj[j][i]);
      CanLockAdj[j][i] = true;
    }
  }
  // Build the transitive closure.
  bool CanLockAdj2[MutexTypeCount][MutexTypeCount];
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      CanLockAdj2[i][j] = CanLockAdj[i][j];
    }
  }
  for (int k = 0; k < N; k++) {
    for (int i = 0; i < N; i++) {
      for (int j = 0; j < N; j++) {
        if (CanLockAdj2[i][k] && CanLockAdj2[k][j]) {
          CanLockAdj2[i][j] = true;
        }
      }
    }
  }
#if 0
  Printf("Can lock graph:\n");
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      Printf("%d ", CanLockAdj[i][j]);
    }
    Printf("\n");
  }
  Printf("Can lock graph closure:\n");
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      Printf("%d ", CanLockAdj2[i][j]);
    }
    Printf("\n");
  }
#endif
  // Verify that the graph is acyclic.
  for (int i = 0; i < N; i++) {
    if (CanLockAdj2[i][i]) {
      Printf("Mutex %d participates in a cycle\n", i);
      Die();
    }
  }
#endif
}

InternalDeadlockDetector::InternalDeadlockDetector() {
  // Rely on zero initialization because some mutexes can be locked before ctor.
}

#if SANITIZER_DEBUG && !SANITIZER_GO
void InternalDeadlockDetector::Lock(MutexType t) {
  // Printf("LOCK %d @%zu\n", t, seq_ + 1);
  CHECK_GT(t, MutexTypeInvalid);
  CHECK_LT(t, MutexTypeCount);
  u64 max_seq = 0;
  u64 max_idx = MutexTypeInvalid;
  for (int i = 0; i != MutexTypeCount; i++) {
    if (locked_[i] == 0)
      continue;
    CHECK_NE(locked_[i], max_seq);
    if (max_seq < locked_[i]) {
      max_seq = locked_[i];
      max_idx = i;
    }
  }
  locked_[t] = ++seq_;
  if (max_idx == MutexTypeInvalid)
    return;
  // Printf("  last %d @%zu\n", max_idx, max_seq);
  if (!CanLockAdj[max_idx][t]) {
    Printf("ThreadSanitizer: internal deadlock detected\n");
    Printf("ThreadSanitizer: can't lock %d while under %zu\n",
               t, (uptr)max_idx);
    CHECK(0);
  }
}

void InternalDeadlockDetector::Unlock(MutexType t) {
  // Printf("UNLO %d @%zu #%zu\n", t, seq_, locked_[t]);
  CHECK(locked_[t]);
  locked_[t] = 0;
}

void InternalDeadlockDetector::CheckNoLocks() {
  for (int i = 0; i != MutexTypeCount; i++) {
    CHECK_EQ(locked_[i], 0);
  }
}
#endif

void CheckNoLocks(ThreadState *thr) {
#if SANITIZER_DEBUG && !SANITIZER_GO
  thr->internal_deadlock_detector.CheckNoLocks();
#endif
}

const uptr kUnlocked = 0;
const uptr kWriteLock = 1;
const uptr kReadLock = 2;

class Backoff {
 public:
  Backoff()
    : iter_() {
  }

  bool Do() {
    if (iter_++ < kActiveSpinIters)
      proc_yield(kActiveSpinCnt);
    else
      internal_sched_yield();
    return true;
  }

  u64 Contention() const {
    u64 active = iter_ % kActiveSpinIters;
    u64 passive = iter_ - active;
    return active + 10 * passive;
  }

 private:
  int iter_;
  static const int kActiveSpinIters = 10;
  static const int kActiveSpinCnt = 20;
};

Mutex::Mutex(MutexType type, StatType stat_type) {
  CHECK_GT(type, MutexTypeInvalid);
  CHECK_LT(type, MutexTypeCount);
#if SANITIZER_DEBUG
  type_ = type;
#endif
#if TSAN_COLLECT_STATS
  stat_type_ = stat_type;
#endif
  atomic_store(&state_, kUnlocked, memory_order_relaxed);
}

Mutex::~Mutex() {
  CHECK_EQ(atomic_load(&state_, memory_order_relaxed), kUnlocked);
}

void Mutex::Lock() {
#if SANITIZER_DEBUG && !SANITIZER_GO
  cur_thread()->internal_deadlock_detector.Lock(type_);
#endif
  uptr cmp = kUnlocked;
  if (atomic_compare_exchange_strong(&state_, &cmp, kWriteLock,
                                     memory_order_acquire))
    return;
  for (Backoff backoff; backoff.Do();) {
    if (atomic_load(&state_, memory_order_relaxed) == kUnlocked) {
      cmp = kUnlocked;
      if (atomic_compare_exchange_weak(&state_, &cmp, kWriteLock,
                                       memory_order_acquire)) {
#if TSAN_COLLECT_STATS && !SANITIZER_GO
        StatInc(cur_thread(), stat_type_, backoff.Contention());
#endif
        return;
      }
    }
  }
}

void Mutex::Unlock() {
  uptr prev = atomic_fetch_sub(&state_, kWriteLock, memory_order_release);
  (void)prev;
  DCHECK_NE(prev & kWriteLock, 0);
#if SANITIZER_DEBUG && !SANITIZER_GO
  cur_thread()->internal_deadlock_detector.Unlock(type_);
#endif
}

void Mutex::ReadLock() {
#if SANITIZER_DEBUG && !SANITIZER_GO
  cur_thread()->internal_deadlock_detector.Lock(type_);
#endif
  uptr prev = atomic_fetch_add(&state_, kReadLock, memory_order_acquire);
  if ((prev & kWriteLock) == 0)
    return;
  for (Backoff backoff; backoff.Do();) {
    prev = atomic_load(&state_, memory_order_acquire);
    if ((prev & kWriteLock) == 0) {
#if TSAN_COLLECT_STATS && !SANITIZER_GO
      StatInc(cur_thread(), stat_type_, backoff.Contention());
#endif
      return;
    }
  }
}

void Mutex::ReadUnlock() {
  uptr prev = atomic_fetch_sub(&state_, kReadLock, memory_order_release);
  (void)prev;
  DCHECK_EQ(prev & kWriteLock, 0);
  DCHECK_GT(prev & ~kWriteLock, 0);
#if SANITIZER_DEBUG && !SANITIZER_GO
  cur_thread()->internal_deadlock_detector.Unlock(type_);
#endif
}

void Mutex::CheckLocked() {
  CHECK_NE(atomic_load(&state_, memory_order_relaxed), 0);
}

}  // namespace __tsan

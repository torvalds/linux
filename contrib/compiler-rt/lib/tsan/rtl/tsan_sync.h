//===-- tsan_sync.h ---------------------------------------------*- C++ -*-===//
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
#ifndef TSAN_SYNC_H
#define TSAN_SYNC_H

#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_deadlock_detector_interface.h"
#include "tsan_defs.h"
#include "tsan_clock.h"
#include "tsan_mutex.h"
#include "tsan_dense_alloc.h"

namespace __tsan {

// These need to match __tsan_mutex_* flags defined in tsan_interface.h.
// See documentation there as well.
enum MutexFlags {
  MutexFlagLinkerInit          = 1 << 0, // __tsan_mutex_linker_init
  MutexFlagWriteReentrant      = 1 << 1, // __tsan_mutex_write_reentrant
  MutexFlagReadReentrant       = 1 << 2, // __tsan_mutex_read_reentrant
  MutexFlagReadLock            = 1 << 3, // __tsan_mutex_read_lock
  MutexFlagTryLock             = 1 << 4, // __tsan_mutex_try_lock
  MutexFlagTryLockFailed       = 1 << 5, // __tsan_mutex_try_lock_failed
  MutexFlagRecursiveLock       = 1 << 6, // __tsan_mutex_recursive_lock
  MutexFlagRecursiveUnlock     = 1 << 7, // __tsan_mutex_recursive_unlock
  MutexFlagNotStatic           = 1 << 8, // __tsan_mutex_not_static

  // The following flags are runtime private.
  // Mutex API misuse was detected, so don't report any more.
  MutexFlagBroken              = 1 << 30,
  // We did not intercept pre lock event, so handle it on post lock.
  MutexFlagDoPreLockOnPostLock = 1 << 29,
  // Must list all mutex creation flags.
  MutexCreationFlagMask        = MutexFlagLinkerInit |
                                 MutexFlagWriteReentrant |
                                 MutexFlagReadReentrant |
                                 MutexFlagNotStatic,
};

struct SyncVar {
  SyncVar();

  static const int kInvalidTid = -1;

  uptr addr;  // overwritten by DenseSlabAlloc freelist
  Mutex mtx;
  u64 uid;  // Globally unique id.
  u32 creation_stack_id;
  int owner_tid;  // Set only by exclusive owners.
  u64 last_lock;
  int recursion;
  atomic_uint32_t flags;
  u32 next;  // in MetaMap
  DDMutex dd;
  SyncClock read_clock;  // Used for rw mutexes only.
  // The clock is placed last, so that it is situated on a different cache line
  // with the mtx. This reduces contention for hot sync objects.
  SyncClock clock;

  void Init(ThreadState *thr, uptr pc, uptr addr, u64 uid);
  void Reset(Processor *proc);

  u64 GetId() const {
    // 48 lsb is addr, then 14 bits is low part of uid, then 2 zero bits.
    return GetLsb((u64)addr | (uid << 48), 60);
  }
  bool CheckId(u64 uid) const {
    CHECK_EQ(uid, GetLsb(uid, 14));
    return GetLsb(this->uid, 14) == uid;
  }
  static uptr SplitId(u64 id, u64 *uid) {
    *uid = id >> 48;
    return (uptr)GetLsb(id, 48);
  }

  bool IsFlagSet(u32 f) const {
    return atomic_load_relaxed(&flags) & f;
  }

  void SetFlags(u32 f) {
    atomic_store_relaxed(&flags, atomic_load_relaxed(&flags) | f);
  }

  void UpdateFlags(u32 flagz) {
    // Filter out operation flags.
    if (!(flagz & MutexCreationFlagMask))
      return;
    u32 current = atomic_load_relaxed(&flags);
    if (current & MutexCreationFlagMask)
      return;
    // Note: this can be called from MutexPostReadLock which holds only read
    // lock on the SyncVar.
    atomic_store_relaxed(&flags, current | (flagz & MutexCreationFlagMask));
  }
};

/* MetaMap allows to map arbitrary user pointers onto various descriptors.
   Currently it maps pointers to heap block descriptors and sync var descs.
   It uses 1/2 direct shadow, see tsan_platform.h.
*/
class MetaMap {
 public:
  MetaMap();

  void AllocBlock(ThreadState *thr, uptr pc, uptr p, uptr sz);
  uptr FreeBlock(Processor *proc, uptr p);
  bool FreeRange(Processor *proc, uptr p, uptr sz);
  void ResetRange(Processor *proc, uptr p, uptr sz);
  MBlock* GetBlock(uptr p);

  SyncVar* GetOrCreateAndLock(ThreadState *thr, uptr pc,
                              uptr addr, bool write_lock);
  SyncVar* GetIfExistsAndLock(uptr addr, bool write_lock);

  void MoveMemory(uptr src, uptr dst, uptr sz);

  void OnProcIdle(Processor *proc);

 private:
  static const u32 kFlagMask  = 3u << 30;
  static const u32 kFlagBlock = 1u << 30;
  static const u32 kFlagSync  = 2u << 30;
  typedef DenseSlabAlloc<MBlock, 1<<16, 1<<12> BlockAlloc;
  typedef DenseSlabAlloc<SyncVar, 1<<16, 1<<10> SyncAlloc;
  BlockAlloc block_alloc_;
  SyncAlloc sync_alloc_;
  atomic_uint64_t uid_gen_;

  SyncVar* GetAndLock(ThreadState *thr, uptr pc, uptr addr, bool write_lock,
                      bool create);
};

}  // namespace __tsan

#endif  // TSAN_SYNC_H

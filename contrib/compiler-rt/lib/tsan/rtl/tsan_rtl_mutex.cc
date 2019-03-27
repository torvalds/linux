//===-- tsan_rtl_mutex.cc -------------------------------------------------===//
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

#include <sanitizer_common/sanitizer_deadlock_detector_interface.h>
#include <sanitizer_common/sanitizer_stackdepot.h>

#include "tsan_rtl.h"
#include "tsan_flags.h"
#include "tsan_sync.h"
#include "tsan_report.h"
#include "tsan_symbolize.h"
#include "tsan_platform.h"

namespace __tsan {

void ReportDeadlock(ThreadState *thr, uptr pc, DDReport *r);

struct Callback : DDCallback {
  ThreadState *thr;
  uptr pc;

  Callback(ThreadState *thr, uptr pc)
      : thr(thr)
      , pc(pc) {
    DDCallback::pt = thr->proc()->dd_pt;
    DDCallback::lt = thr->dd_lt;
  }

  u32 Unwind() override { return CurrentStackId(thr, pc); }
  int UniqueTid() override { return thr->unique_id; }
};

void DDMutexInit(ThreadState *thr, uptr pc, SyncVar *s) {
  Callback cb(thr, pc);
  ctx->dd->MutexInit(&cb, &s->dd);
  s->dd.ctx = s->GetId();
}

static void ReportMutexMisuse(ThreadState *thr, uptr pc, ReportType typ,
    uptr addr, u64 mid) {
  // In Go, these misuses are either impossible, or detected by std lib,
  // or false positives (e.g. unlock in a different thread).
  if (SANITIZER_GO)
    return;
  ThreadRegistryLock l(ctx->thread_registry);
  ScopedReport rep(typ);
  rep.AddMutex(mid);
  VarSizeStackTrace trace;
  ObtainCurrentStack(thr, pc, &trace);
  rep.AddStack(trace, true);
  rep.AddLocation(addr, 1);
  OutputReport(thr, rep);
}

void MutexCreate(ThreadState *thr, uptr pc, uptr addr, u32 flagz) {
  DPrintf("#%d: MutexCreate %zx flagz=0x%x\n", thr->tid, addr, flagz);
  StatInc(thr, StatMutexCreate);
  if (!(flagz & MutexFlagLinkerInit) && IsAppMem(addr)) {
    CHECK(!thr->is_freeing);
    thr->is_freeing = true;
    MemoryWrite(thr, pc, addr, kSizeLog1);
    thr->is_freeing = false;
  }
  SyncVar *s = ctx->metamap.GetOrCreateAndLock(thr, pc, addr, true);
  s->SetFlags(flagz & MutexCreationFlagMask);
  if (!SANITIZER_GO && s->creation_stack_id == 0)
    s->creation_stack_id = CurrentStackId(thr, pc);
  s->mtx.Unlock();
}

void MutexDestroy(ThreadState *thr, uptr pc, uptr addr, u32 flagz) {
  DPrintf("#%d: MutexDestroy %zx\n", thr->tid, addr);
  StatInc(thr, StatMutexDestroy);
  SyncVar *s = ctx->metamap.GetIfExistsAndLock(addr, true);
  if (s == 0)
    return;
  if ((flagz & MutexFlagLinkerInit)
      || s->IsFlagSet(MutexFlagLinkerInit)
      || ((flagz & MutexFlagNotStatic) && !s->IsFlagSet(MutexFlagNotStatic))) {
    // Destroy is no-op for linker-initialized mutexes.
    s->mtx.Unlock();
    return;
  }
  if (common_flags()->detect_deadlocks) {
    Callback cb(thr, pc);
    ctx->dd->MutexDestroy(&cb, &s->dd);
    ctx->dd->MutexInit(&cb, &s->dd);
  }
  bool unlock_locked = false;
  if (flags()->report_destroy_locked
      && s->owner_tid != SyncVar::kInvalidTid
      && !s->IsFlagSet(MutexFlagBroken)) {
    s->SetFlags(MutexFlagBroken);
    unlock_locked = true;
  }
  u64 mid = s->GetId();
  u64 last_lock = s->last_lock;
  if (!unlock_locked)
    s->Reset(thr->proc());  // must not reset it before the report is printed
  s->mtx.Unlock();
  if (unlock_locked) {
    ThreadRegistryLock l(ctx->thread_registry);
    ScopedReport rep(ReportTypeMutexDestroyLocked);
    rep.AddMutex(mid);
    VarSizeStackTrace trace;
    ObtainCurrentStack(thr, pc, &trace);
    rep.AddStack(trace, true);
    FastState last(last_lock);
    RestoreStack(last.tid(), last.epoch(), &trace, 0);
    rep.AddStack(trace, true);
    rep.AddLocation(addr, 1);
    OutputReport(thr, rep);

    SyncVar *s = ctx->metamap.GetIfExistsAndLock(addr, true);
    if (s != 0) {
      s->Reset(thr->proc());
      s->mtx.Unlock();
    }
  }
  thr->mset.Remove(mid);
  // Imitate a memory write to catch unlock-destroy races.
  // Do this outside of sync mutex, because it can report a race which locks
  // sync mutexes.
  if (IsAppMem(addr)) {
    CHECK(!thr->is_freeing);
    thr->is_freeing = true;
    MemoryWrite(thr, pc, addr, kSizeLog1);
    thr->is_freeing = false;
  }
  // s will be destroyed and freed in MetaMap::FreeBlock.
}

void MutexPreLock(ThreadState *thr, uptr pc, uptr addr, u32 flagz) {
  DPrintf("#%d: MutexPreLock %zx flagz=0x%x\n", thr->tid, addr, flagz);
  if (!(flagz & MutexFlagTryLock) && common_flags()->detect_deadlocks) {
    SyncVar *s = ctx->metamap.GetOrCreateAndLock(thr, pc, addr, false);
    s->UpdateFlags(flagz);
    if (s->owner_tid != thr->tid) {
      Callback cb(thr, pc);
      ctx->dd->MutexBeforeLock(&cb, &s->dd, true);
      s->mtx.ReadUnlock();
      ReportDeadlock(thr, pc, ctx->dd->GetReport(&cb));
    } else {
      s->mtx.ReadUnlock();
    }
  }
}

void MutexPostLock(ThreadState *thr, uptr pc, uptr addr, u32 flagz, int rec) {
  DPrintf("#%d: MutexPostLock %zx flag=0x%x rec=%d\n",
      thr->tid, addr, flagz, rec);
  if (flagz & MutexFlagRecursiveLock)
    CHECK_GT(rec, 0);
  else
    rec = 1;
  if (IsAppMem(addr))
    MemoryReadAtomic(thr, pc, addr, kSizeLog1);
  SyncVar *s = ctx->metamap.GetOrCreateAndLock(thr, pc, addr, true);
  s->UpdateFlags(flagz);
  thr->fast_state.IncrementEpoch();
  TraceAddEvent(thr, thr->fast_state, EventTypeLock, s->GetId());
  bool report_double_lock = false;
  if (s->owner_tid == SyncVar::kInvalidTid) {
    CHECK_EQ(s->recursion, 0);
    s->owner_tid = thr->tid;
    s->last_lock = thr->fast_state.raw();
  } else if (s->owner_tid == thr->tid) {
    CHECK_GT(s->recursion, 0);
  } else if (flags()->report_mutex_bugs && !s->IsFlagSet(MutexFlagBroken)) {
    s->SetFlags(MutexFlagBroken);
    report_double_lock = true;
  }
  const bool first = s->recursion == 0;
  s->recursion += rec;
  if (first) {
    StatInc(thr, StatMutexLock);
    AcquireImpl(thr, pc, &s->clock);
    AcquireImpl(thr, pc, &s->read_clock);
  } else if (!s->IsFlagSet(MutexFlagWriteReentrant)) {
    StatInc(thr, StatMutexRecLock);
  }
  thr->mset.Add(s->GetId(), true, thr->fast_state.epoch());
  bool pre_lock = false;
  if (first && common_flags()->detect_deadlocks) {
    pre_lock = (flagz & MutexFlagDoPreLockOnPostLock) &&
        !(flagz & MutexFlagTryLock);
    Callback cb(thr, pc);
    if (pre_lock)
      ctx->dd->MutexBeforeLock(&cb, &s->dd, true);
    ctx->dd->MutexAfterLock(&cb, &s->dd, true, flagz & MutexFlagTryLock);
  }
  u64 mid = s->GetId();
  s->mtx.Unlock();
  // Can't touch s after this point.
  s = 0;
  if (report_double_lock)
    ReportMutexMisuse(thr, pc, ReportTypeMutexDoubleLock, addr, mid);
  if (first && pre_lock && common_flags()->detect_deadlocks) {
    Callback cb(thr, pc);
    ReportDeadlock(thr, pc, ctx->dd->GetReport(&cb));
  }
}

int MutexUnlock(ThreadState *thr, uptr pc, uptr addr, u32 flagz) {
  DPrintf("#%d: MutexUnlock %zx flagz=0x%x\n", thr->tid, addr, flagz);
  if (IsAppMem(addr))
    MemoryReadAtomic(thr, pc, addr, kSizeLog1);
  SyncVar *s = ctx->metamap.GetOrCreateAndLock(thr, pc, addr, true);
  thr->fast_state.IncrementEpoch();
  TraceAddEvent(thr, thr->fast_state, EventTypeUnlock, s->GetId());
  int rec = 0;
  bool report_bad_unlock = false;
  if (!SANITIZER_GO && (s->recursion == 0 || s->owner_tid != thr->tid)) {
    if (flags()->report_mutex_bugs && !s->IsFlagSet(MutexFlagBroken)) {
      s->SetFlags(MutexFlagBroken);
      report_bad_unlock = true;
    }
  } else {
    rec = (flagz & MutexFlagRecursiveUnlock) ? s->recursion : 1;
    s->recursion -= rec;
    if (s->recursion == 0) {
      StatInc(thr, StatMutexUnlock);
      s->owner_tid = SyncVar::kInvalidTid;
      ReleaseStoreImpl(thr, pc, &s->clock);
    } else {
      StatInc(thr, StatMutexRecUnlock);
    }
  }
  thr->mset.Del(s->GetId(), true);
  if (common_flags()->detect_deadlocks && s->recursion == 0 &&
      !report_bad_unlock) {
    Callback cb(thr, pc);
    ctx->dd->MutexBeforeUnlock(&cb, &s->dd, true);
  }
  u64 mid = s->GetId();
  s->mtx.Unlock();
  // Can't touch s after this point.
  if (report_bad_unlock)
    ReportMutexMisuse(thr, pc, ReportTypeMutexBadUnlock, addr, mid);
  if (common_flags()->detect_deadlocks && !report_bad_unlock) {
    Callback cb(thr, pc);
    ReportDeadlock(thr, pc, ctx->dd->GetReport(&cb));
  }
  return rec;
}

void MutexPreReadLock(ThreadState *thr, uptr pc, uptr addr, u32 flagz) {
  DPrintf("#%d: MutexPreReadLock %zx flagz=0x%x\n", thr->tid, addr, flagz);
  if (!(flagz & MutexFlagTryLock) && common_flags()->detect_deadlocks) {
    SyncVar *s = ctx->metamap.GetOrCreateAndLock(thr, pc, addr, false);
    s->UpdateFlags(flagz);
    Callback cb(thr, pc);
    ctx->dd->MutexBeforeLock(&cb, &s->dd, false);
    s->mtx.ReadUnlock();
    ReportDeadlock(thr, pc, ctx->dd->GetReport(&cb));
  }
}

void MutexPostReadLock(ThreadState *thr, uptr pc, uptr addr, u32 flagz) {
  DPrintf("#%d: MutexPostReadLock %zx flagz=0x%x\n", thr->tid, addr, flagz);
  StatInc(thr, StatMutexReadLock);
  if (IsAppMem(addr))
    MemoryReadAtomic(thr, pc, addr, kSizeLog1);
  SyncVar *s = ctx->metamap.GetOrCreateAndLock(thr, pc, addr, false);
  s->UpdateFlags(flagz);
  thr->fast_state.IncrementEpoch();
  TraceAddEvent(thr, thr->fast_state, EventTypeRLock, s->GetId());
  bool report_bad_lock = false;
  if (s->owner_tid != SyncVar::kInvalidTid) {
    if (flags()->report_mutex_bugs && !s->IsFlagSet(MutexFlagBroken)) {
      s->SetFlags(MutexFlagBroken);
      report_bad_lock = true;
    }
  }
  AcquireImpl(thr, pc, &s->clock);
  s->last_lock = thr->fast_state.raw();
  thr->mset.Add(s->GetId(), false, thr->fast_state.epoch());
  bool pre_lock = false;
  if (common_flags()->detect_deadlocks) {
    pre_lock = (flagz & MutexFlagDoPreLockOnPostLock) &&
        !(flagz & MutexFlagTryLock);
    Callback cb(thr, pc);
    if (pre_lock)
      ctx->dd->MutexBeforeLock(&cb, &s->dd, false);
    ctx->dd->MutexAfterLock(&cb, &s->dd, false, flagz & MutexFlagTryLock);
  }
  u64 mid = s->GetId();
  s->mtx.ReadUnlock();
  // Can't touch s after this point.
  s = 0;
  if (report_bad_lock)
    ReportMutexMisuse(thr, pc, ReportTypeMutexBadReadLock, addr, mid);
  if (pre_lock  && common_flags()->detect_deadlocks) {
    Callback cb(thr, pc);
    ReportDeadlock(thr, pc, ctx->dd->GetReport(&cb));
  }
}

void MutexReadUnlock(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: MutexReadUnlock %zx\n", thr->tid, addr);
  StatInc(thr, StatMutexReadUnlock);
  if (IsAppMem(addr))
    MemoryReadAtomic(thr, pc, addr, kSizeLog1);
  SyncVar *s = ctx->metamap.GetOrCreateAndLock(thr, pc, addr, true);
  thr->fast_state.IncrementEpoch();
  TraceAddEvent(thr, thr->fast_state, EventTypeRUnlock, s->GetId());
  bool report_bad_unlock = false;
  if (s->owner_tid != SyncVar::kInvalidTid) {
    if (flags()->report_mutex_bugs && !s->IsFlagSet(MutexFlagBroken)) {
      s->SetFlags(MutexFlagBroken);
      report_bad_unlock = true;
    }
  }
  ReleaseImpl(thr, pc, &s->read_clock);
  if (common_flags()->detect_deadlocks && s->recursion == 0) {
    Callback cb(thr, pc);
    ctx->dd->MutexBeforeUnlock(&cb, &s->dd, false);
  }
  u64 mid = s->GetId();
  s->mtx.Unlock();
  // Can't touch s after this point.
  thr->mset.Del(mid, false);
  if (report_bad_unlock)
    ReportMutexMisuse(thr, pc, ReportTypeMutexBadReadUnlock, addr, mid);
  if (common_flags()->detect_deadlocks) {
    Callback cb(thr, pc);
    ReportDeadlock(thr, pc, ctx->dd->GetReport(&cb));
  }
}

void MutexReadOrWriteUnlock(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: MutexReadOrWriteUnlock %zx\n", thr->tid, addr);
  if (IsAppMem(addr))
    MemoryReadAtomic(thr, pc, addr, kSizeLog1);
  SyncVar *s = ctx->metamap.GetOrCreateAndLock(thr, pc, addr, true);
  bool write = true;
  bool report_bad_unlock = false;
  if (s->owner_tid == SyncVar::kInvalidTid) {
    // Seems to be read unlock.
    write = false;
    StatInc(thr, StatMutexReadUnlock);
    thr->fast_state.IncrementEpoch();
    TraceAddEvent(thr, thr->fast_state, EventTypeRUnlock, s->GetId());
    ReleaseImpl(thr, pc, &s->read_clock);
  } else if (s->owner_tid == thr->tid) {
    // Seems to be write unlock.
    thr->fast_state.IncrementEpoch();
    TraceAddEvent(thr, thr->fast_state, EventTypeUnlock, s->GetId());
    CHECK_GT(s->recursion, 0);
    s->recursion--;
    if (s->recursion == 0) {
      StatInc(thr, StatMutexUnlock);
      s->owner_tid = SyncVar::kInvalidTid;
      ReleaseStoreImpl(thr, pc, &s->clock);
    } else {
      StatInc(thr, StatMutexRecUnlock);
    }
  } else if (!s->IsFlagSet(MutexFlagBroken)) {
    s->SetFlags(MutexFlagBroken);
    report_bad_unlock = true;
  }
  thr->mset.Del(s->GetId(), write);
  if (common_flags()->detect_deadlocks && s->recursion == 0) {
    Callback cb(thr, pc);
    ctx->dd->MutexBeforeUnlock(&cb, &s->dd, write);
  }
  u64 mid = s->GetId();
  s->mtx.Unlock();
  // Can't touch s after this point.
  if (report_bad_unlock)
    ReportMutexMisuse(thr, pc, ReportTypeMutexBadUnlock, addr, mid);
  if (common_flags()->detect_deadlocks) {
    Callback cb(thr, pc);
    ReportDeadlock(thr, pc, ctx->dd->GetReport(&cb));
  }
}

void MutexRepair(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: MutexRepair %zx\n", thr->tid, addr);
  SyncVar *s = ctx->metamap.GetOrCreateAndLock(thr, pc, addr, true);
  s->owner_tid = SyncVar::kInvalidTid;
  s->recursion = 0;
  s->mtx.Unlock();
}

void MutexInvalidAccess(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: MutexInvalidAccess %zx\n", thr->tid, addr);
  SyncVar *s = ctx->metamap.GetOrCreateAndLock(thr, pc, addr, true);
  u64 mid = s->GetId();
  s->mtx.Unlock();
  ReportMutexMisuse(thr, pc, ReportTypeMutexInvalidAccess, addr, mid);
}

void Acquire(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: Acquire %zx\n", thr->tid, addr);
  if (thr->ignore_sync)
    return;
  SyncVar *s = ctx->metamap.GetIfExistsAndLock(addr, false);
  if (!s)
    return;
  AcquireImpl(thr, pc, &s->clock);
  s->mtx.ReadUnlock();
}

static void UpdateClockCallback(ThreadContextBase *tctx_base, void *arg) {
  ThreadState *thr = reinterpret_cast<ThreadState*>(arg);
  ThreadContext *tctx = static_cast<ThreadContext*>(tctx_base);
  u64 epoch = tctx->epoch1;
  if (tctx->status == ThreadStatusRunning)
    epoch = tctx->thr->fast_state.epoch();
  thr->clock.set(&thr->proc()->clock_cache, tctx->tid, epoch);
}

void AcquireGlobal(ThreadState *thr, uptr pc) {
  DPrintf("#%d: AcquireGlobal\n", thr->tid);
  if (thr->ignore_sync)
    return;
  ThreadRegistryLock l(ctx->thread_registry);
  ctx->thread_registry->RunCallbackForEachThreadLocked(
      UpdateClockCallback, thr);
}

void Release(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: Release %zx\n", thr->tid, addr);
  if (thr->ignore_sync)
    return;
  SyncVar *s = ctx->metamap.GetOrCreateAndLock(thr, pc, addr, true);
  thr->fast_state.IncrementEpoch();
  // Can't increment epoch w/o writing to the trace as well.
  TraceAddEvent(thr, thr->fast_state, EventTypeMop, 0);
  ReleaseImpl(thr, pc, &s->clock);
  s->mtx.Unlock();
}

void ReleaseStore(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: ReleaseStore %zx\n", thr->tid, addr);
  if (thr->ignore_sync)
    return;
  SyncVar *s = ctx->metamap.GetOrCreateAndLock(thr, pc, addr, true);
  thr->fast_state.IncrementEpoch();
  // Can't increment epoch w/o writing to the trace as well.
  TraceAddEvent(thr, thr->fast_state, EventTypeMop, 0);
  ReleaseStoreImpl(thr, pc, &s->clock);
  s->mtx.Unlock();
}

#if !SANITIZER_GO
static void UpdateSleepClockCallback(ThreadContextBase *tctx_base, void *arg) {
  ThreadState *thr = reinterpret_cast<ThreadState*>(arg);
  ThreadContext *tctx = static_cast<ThreadContext*>(tctx_base);
  u64 epoch = tctx->epoch1;
  if (tctx->status == ThreadStatusRunning)
    epoch = tctx->thr->fast_state.epoch();
  thr->last_sleep_clock.set(&thr->proc()->clock_cache, tctx->tid, epoch);
}

void AfterSleep(ThreadState *thr, uptr pc) {
  DPrintf("#%d: AfterSleep %zx\n", thr->tid);
  if (thr->ignore_sync)
    return;
  thr->last_sleep_stack_id = CurrentStackId(thr, pc);
  ThreadRegistryLock l(ctx->thread_registry);
  ctx->thread_registry->RunCallbackForEachThreadLocked(
      UpdateSleepClockCallback, thr);
}
#endif

void AcquireImpl(ThreadState *thr, uptr pc, SyncClock *c) {
  if (thr->ignore_sync)
    return;
  thr->clock.set(thr->fast_state.epoch());
  thr->clock.acquire(&thr->proc()->clock_cache, c);
  StatInc(thr, StatSyncAcquire);
}

void ReleaseImpl(ThreadState *thr, uptr pc, SyncClock *c) {
  if (thr->ignore_sync)
    return;
  thr->clock.set(thr->fast_state.epoch());
  thr->fast_synch_epoch = thr->fast_state.epoch();
  thr->clock.release(&thr->proc()->clock_cache, c);
  StatInc(thr, StatSyncRelease);
}

void ReleaseStoreImpl(ThreadState *thr, uptr pc, SyncClock *c) {
  if (thr->ignore_sync)
    return;
  thr->clock.set(thr->fast_state.epoch());
  thr->fast_synch_epoch = thr->fast_state.epoch();
  thr->clock.ReleaseStore(&thr->proc()->clock_cache, c);
  StatInc(thr, StatSyncRelease);
}

void AcquireReleaseImpl(ThreadState *thr, uptr pc, SyncClock *c) {
  if (thr->ignore_sync)
    return;
  thr->clock.set(thr->fast_state.epoch());
  thr->fast_synch_epoch = thr->fast_state.epoch();
  thr->clock.acq_rel(&thr->proc()->clock_cache, c);
  StatInc(thr, StatSyncAcquire);
  StatInc(thr, StatSyncRelease);
}

void ReportDeadlock(ThreadState *thr, uptr pc, DDReport *r) {
  if (r == 0)
    return;
  ThreadRegistryLock l(ctx->thread_registry);
  ScopedReport rep(ReportTypeDeadlock);
  for (int i = 0; i < r->n; i++) {
    rep.AddMutex(r->loop[i].mtx_ctx0);
    rep.AddUniqueTid((int)r->loop[i].thr_ctx);
    rep.AddThread((int)r->loop[i].thr_ctx);
  }
  uptr dummy_pc = 0x42;
  for (int i = 0; i < r->n; i++) {
    for (int j = 0; j < (flags()->second_deadlock_stack ? 2 : 1); j++) {
      u32 stk = r->loop[i].stk[j];
      if (stk && stk != 0xffffffff) {
        rep.AddStack(StackDepotGet(stk), true);
      } else {
        // Sometimes we fail to extract the stack trace (FIXME: investigate),
        // but we should still produce some stack trace in the report.
        rep.AddStack(StackTrace(&dummy_pc, 1), true);
      }
    }
  }
  OutputReport(thr, rep);
}

}  // namespace __tsan

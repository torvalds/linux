//===-- tsan_rtl_thread.cc ------------------------------------------------===//
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

#include "sanitizer_common/sanitizer_placement_new.h"
#include "tsan_rtl.h"
#include "tsan_mman.h"
#include "tsan_platform.h"
#include "tsan_report.h"
#include "tsan_sync.h"

namespace __tsan {

// ThreadContext implementation.

ThreadContext::ThreadContext(int tid)
  : ThreadContextBase(tid)
  , thr()
  , sync()
  , epoch0()
  , epoch1() {
}

#if !SANITIZER_GO
ThreadContext::~ThreadContext() {
}
#endif

void ThreadContext::OnDead() {
  CHECK_EQ(sync.size(), 0);
}

void ThreadContext::OnJoined(void *arg) {
  ThreadState *caller_thr = static_cast<ThreadState *>(arg);
  AcquireImpl(caller_thr, 0, &sync);
  sync.Reset(&caller_thr->proc()->clock_cache);
}

struct OnCreatedArgs {
  ThreadState *thr;
  uptr pc;
};

void ThreadContext::OnCreated(void *arg) {
  thr = 0;
  if (tid == 0)
    return;
  OnCreatedArgs *args = static_cast<OnCreatedArgs *>(arg);
  if (!args->thr)  // GCD workers don't have a parent thread.
    return;
  args->thr->fast_state.IncrementEpoch();
  // Can't increment epoch w/o writing to the trace as well.
  TraceAddEvent(args->thr, args->thr->fast_state, EventTypeMop, 0);
  ReleaseImpl(args->thr, 0, &sync);
  creation_stack_id = CurrentStackId(args->thr, args->pc);
  if (reuse_count == 0)
    StatInc(args->thr, StatThreadMaxTid);
}

void ThreadContext::OnReset() {
  CHECK_EQ(sync.size(), 0);
  uptr trace_p = GetThreadTrace(tid);
  ReleaseMemoryPagesToOS(trace_p, trace_p + TraceSize() * sizeof(Event));
  //!!! ReleaseMemoryToOS(GetThreadTraceHeader(tid), sizeof(Trace));
}

void ThreadContext::OnDetached(void *arg) {
  ThreadState *thr1 = static_cast<ThreadState*>(arg);
  sync.Reset(&thr1->proc()->clock_cache);
}

struct OnStartedArgs {
  ThreadState *thr;
  uptr stk_addr;
  uptr stk_size;
  uptr tls_addr;
  uptr tls_size;
};

void ThreadContext::OnStarted(void *arg) {
  OnStartedArgs *args = static_cast<OnStartedArgs*>(arg);
  thr = args->thr;
  // RoundUp so that one trace part does not contain events
  // from different threads.
  epoch0 = RoundUp(epoch1 + 1, kTracePartSize);
  epoch1 = (u64)-1;
  new(thr) ThreadState(ctx, tid, unique_id, epoch0, reuse_count,
      args->stk_addr, args->stk_size, args->tls_addr, args->tls_size);
#if !SANITIZER_GO
  thr->shadow_stack = &ThreadTrace(thr->tid)->shadow_stack[0];
  thr->shadow_stack_pos = thr->shadow_stack;
  thr->shadow_stack_end = thr->shadow_stack + kShadowStackSize;
#else
  // Setup dynamic shadow stack.
  const int kInitStackSize = 8;
  thr->shadow_stack = (uptr*)internal_alloc(MBlockShadowStack,
      kInitStackSize * sizeof(uptr));
  thr->shadow_stack_pos = thr->shadow_stack;
  thr->shadow_stack_end = thr->shadow_stack + kInitStackSize;
#endif
  if (common_flags()->detect_deadlocks)
    thr->dd_lt = ctx->dd->CreateLogicalThread(unique_id);
  thr->fast_state.SetHistorySize(flags()->history_size);
  // Commit switch to the new part of the trace.
  // TraceAddEvent will reset stack0/mset0 in the new part for us.
  TraceAddEvent(thr, thr->fast_state, EventTypeMop, 0);

  thr->fast_synch_epoch = epoch0;
  AcquireImpl(thr, 0, &sync);
  StatInc(thr, StatSyncAcquire);
  sync.Reset(&thr->proc()->clock_cache);
  thr->is_inited = true;
  DPrintf("#%d: ThreadStart epoch=%zu stk_addr=%zx stk_size=%zx "
          "tls_addr=%zx tls_size=%zx\n",
          tid, (uptr)epoch0, args->stk_addr, args->stk_size,
          args->tls_addr, args->tls_size);
}

void ThreadContext::OnFinished() {
#if SANITIZER_GO
  internal_free(thr->shadow_stack);
  thr->shadow_stack = nullptr;
  thr->shadow_stack_pos = nullptr;
  thr->shadow_stack_end = nullptr;
#endif
  if (!detached) {
    thr->fast_state.IncrementEpoch();
    // Can't increment epoch w/o writing to the trace as well.
    TraceAddEvent(thr, thr->fast_state, EventTypeMop, 0);
    ReleaseImpl(thr, 0, &sync);
  }
  epoch1 = thr->fast_state.epoch();

  if (common_flags()->detect_deadlocks)
    ctx->dd->DestroyLogicalThread(thr->dd_lt);
  thr->clock.ResetCached(&thr->proc()->clock_cache);
#if !SANITIZER_GO
  thr->last_sleep_clock.ResetCached(&thr->proc()->clock_cache);
#endif
  thr->~ThreadState();
#if TSAN_COLLECT_STATS
  StatAggregate(ctx->stat, thr->stat);
#endif
  thr = 0;
}

#if !SANITIZER_GO
struct ThreadLeak {
  ThreadContext *tctx;
  int count;
};

static void MaybeReportThreadLeak(ThreadContextBase *tctx_base, void *arg) {
  Vector<ThreadLeak> &leaks = *(Vector<ThreadLeak>*)arg;
  ThreadContext *tctx = static_cast<ThreadContext*>(tctx_base);
  if (tctx->detached || tctx->status != ThreadStatusFinished)
    return;
  for (uptr i = 0; i < leaks.Size(); i++) {
    if (leaks[i].tctx->creation_stack_id == tctx->creation_stack_id) {
      leaks[i].count++;
      return;
    }
  }
  ThreadLeak leak = {tctx, 1};
  leaks.PushBack(leak);
}
#endif

#if !SANITIZER_GO
static void ReportIgnoresEnabled(ThreadContext *tctx, IgnoreSet *set) {
  if (tctx->tid == 0) {
    Printf("ThreadSanitizer: main thread finished with ignores enabled\n");
  } else {
    Printf("ThreadSanitizer: thread T%d %s finished with ignores enabled,"
      " created at:\n", tctx->tid, tctx->name);
    PrintStack(SymbolizeStackId(tctx->creation_stack_id));
  }
  Printf("  One of the following ignores was not ended"
      " (in order of probability)\n");
  for (uptr i = 0; i < set->Size(); i++) {
    Printf("  Ignore was enabled at:\n");
    PrintStack(SymbolizeStackId(set->At(i)));
  }
  Die();
}

static void ThreadCheckIgnore(ThreadState *thr) {
  if (ctx->after_multithreaded_fork)
    return;
  if (thr->ignore_reads_and_writes)
    ReportIgnoresEnabled(thr->tctx, &thr->mop_ignore_set);
  if (thr->ignore_sync)
    ReportIgnoresEnabled(thr->tctx, &thr->sync_ignore_set);
}
#else
static void ThreadCheckIgnore(ThreadState *thr) {}
#endif

void ThreadFinalize(ThreadState *thr) {
  ThreadCheckIgnore(thr);
#if !SANITIZER_GO
  if (!flags()->report_thread_leaks)
    return;
  ThreadRegistryLock l(ctx->thread_registry);
  Vector<ThreadLeak> leaks;
  ctx->thread_registry->RunCallbackForEachThreadLocked(
      MaybeReportThreadLeak, &leaks);
  for (uptr i = 0; i < leaks.Size(); i++) {
    ScopedReport rep(ReportTypeThreadLeak);
    rep.AddThread(leaks[i].tctx, true);
    rep.SetCount(leaks[i].count);
    OutputReport(thr, rep);
  }
#endif
}

int ThreadCount(ThreadState *thr) {
  uptr result;
  ctx->thread_registry->GetNumberOfThreads(0, 0, &result);
  return (int)result;
}

int ThreadCreate(ThreadState *thr, uptr pc, uptr uid, bool detached) {
  StatInc(thr, StatThreadCreate);
  OnCreatedArgs args = { thr, pc };
  u32 parent_tid = thr ? thr->tid : kInvalidTid;  // No parent for GCD workers.
  int tid =
      ctx->thread_registry->CreateThread(uid, detached, parent_tid, &args);
  DPrintf("#%d: ThreadCreate tid=%d uid=%zu\n", parent_tid, tid, uid);
  StatSet(thr, StatThreadMaxAlive, ctx->thread_registry->GetMaxAliveThreads());
  return tid;
}

void ThreadStart(ThreadState *thr, int tid, tid_t os_id, bool workerthread) {
  uptr stk_addr = 0;
  uptr stk_size = 0;
  uptr tls_addr = 0;
  uptr tls_size = 0;
#if !SANITIZER_GO
  GetThreadStackAndTls(tid == 0, &stk_addr, &stk_size, &tls_addr, &tls_size);

  if (tid) {
    if (stk_addr && stk_size)
      MemoryRangeImitateWrite(thr, /*pc=*/ 1, stk_addr, stk_size);

    if (tls_addr && tls_size) ImitateTlsWrite(thr, tls_addr, tls_size);
  }
#endif

  ThreadRegistry *tr = ctx->thread_registry;
  OnStartedArgs args = { thr, stk_addr, stk_size, tls_addr, tls_size };
  tr->StartThread(tid, os_id, workerthread, &args);

  tr->Lock();
  thr->tctx = (ThreadContext*)tr->GetThreadLocked(tid);
  tr->Unlock();

#if !SANITIZER_GO
  if (ctx->after_multithreaded_fork) {
    thr->ignore_interceptors++;
    ThreadIgnoreBegin(thr, 0);
    ThreadIgnoreSyncBegin(thr, 0);
  }
#endif
}

void ThreadFinish(ThreadState *thr) {
  ThreadCheckIgnore(thr);
  StatInc(thr, StatThreadFinish);
  if (thr->stk_addr && thr->stk_size)
    DontNeedShadowFor(thr->stk_addr, thr->stk_size);
  if (thr->tls_addr && thr->tls_size)
    DontNeedShadowFor(thr->tls_addr, thr->tls_size);
  thr->is_dead = true;
  ctx->thread_registry->FinishThread(thr->tid);
}

static bool FindThreadByUid(ThreadContextBase *tctx, void *arg) {
  uptr uid = (uptr)arg;
  if (tctx->user_id == uid && tctx->status != ThreadStatusInvalid) {
    tctx->user_id = 0;
    return true;
  }
  return false;
}

int ThreadTid(ThreadState *thr, uptr pc, uptr uid) {
  int res = ctx->thread_registry->FindThread(FindThreadByUid, (void*)uid);
  DPrintf("#%d: ThreadTid uid=%zu tid=%d\n", thr->tid, uid, res);
  return res;
}

void ThreadJoin(ThreadState *thr, uptr pc, int tid) {
  CHECK_GT(tid, 0);
  CHECK_LT(tid, kMaxTid);
  DPrintf("#%d: ThreadJoin tid=%d\n", thr->tid, tid);
  ctx->thread_registry->JoinThread(tid, thr);
}

void ThreadDetach(ThreadState *thr, uptr pc, int tid) {
  CHECK_GT(tid, 0);
  CHECK_LT(tid, kMaxTid);
  ctx->thread_registry->DetachThread(tid, thr);
}

void ThreadNotJoined(ThreadState *thr, uptr pc, int tid, uptr uid) {
  CHECK_GT(tid, 0);
  CHECK_LT(tid, kMaxTid);
  ctx->thread_registry->SetThreadUserId(tid, uid);
}

void ThreadSetName(ThreadState *thr, const char *name) {
  ctx->thread_registry->SetThreadName(thr->tid, name);
}

void MemoryAccessRange(ThreadState *thr, uptr pc, uptr addr,
                       uptr size, bool is_write) {
  if (size == 0)
    return;

  u64 *shadow_mem = (u64*)MemToShadow(addr);
  DPrintf2("#%d: MemoryAccessRange: @%p %p size=%d is_write=%d\n",
      thr->tid, (void*)pc, (void*)addr,
      (int)size, is_write);

#if SANITIZER_DEBUG
  if (!IsAppMem(addr)) {
    Printf("Access to non app mem %zx\n", addr);
    DCHECK(IsAppMem(addr));
  }
  if (!IsAppMem(addr + size - 1)) {
    Printf("Access to non app mem %zx\n", addr + size - 1);
    DCHECK(IsAppMem(addr + size - 1));
  }
  if (!IsShadowMem((uptr)shadow_mem)) {
    Printf("Bad shadow addr %p (%zx)\n", shadow_mem, addr);
    DCHECK(IsShadowMem((uptr)shadow_mem));
  }
  if (!IsShadowMem((uptr)(shadow_mem + size * kShadowCnt / 8 - 1))) {
    Printf("Bad shadow addr %p (%zx)\n",
               shadow_mem + size * kShadowCnt / 8 - 1, addr + size - 1);
    DCHECK(IsShadowMem((uptr)(shadow_mem + size * kShadowCnt / 8 - 1)));
  }
#endif

  StatInc(thr, StatMopRange);

  if (*shadow_mem == kShadowRodata) {
    DCHECK(!is_write);
    // Access to .rodata section, no races here.
    // Measurements show that it can be 10-20% of all memory accesses.
    StatInc(thr, StatMopRangeRodata);
    return;
  }

  FastState fast_state = thr->fast_state;
  if (fast_state.GetIgnoreBit())
    return;

  fast_state.IncrementEpoch();
  thr->fast_state = fast_state;
  TraceAddEvent(thr, fast_state, EventTypeMop, pc);

  bool unaligned = (addr % kShadowCell) != 0;

  // Handle unaligned beginning, if any.
  for (; addr % kShadowCell && size; addr++, size--) {
    int const kAccessSizeLog = 0;
    Shadow cur(fast_state);
    cur.SetWrite(is_write);
    cur.SetAddr0AndSizeLog(addr & (kShadowCell - 1), kAccessSizeLog);
    MemoryAccessImpl(thr, addr, kAccessSizeLog, is_write, false,
        shadow_mem, cur);
  }
  if (unaligned)
    shadow_mem += kShadowCnt;
  // Handle middle part, if any.
  for (; size >= kShadowCell; addr += kShadowCell, size -= kShadowCell) {
    int const kAccessSizeLog = 3;
    Shadow cur(fast_state);
    cur.SetWrite(is_write);
    cur.SetAddr0AndSizeLog(0, kAccessSizeLog);
    MemoryAccessImpl(thr, addr, kAccessSizeLog, is_write, false,
        shadow_mem, cur);
    shadow_mem += kShadowCnt;
  }
  // Handle ending, if any.
  for (; size; addr++, size--) {
    int const kAccessSizeLog = 0;
    Shadow cur(fast_state);
    cur.SetWrite(is_write);
    cur.SetAddr0AndSizeLog(addr & (kShadowCell - 1), kAccessSizeLog);
    MemoryAccessImpl(thr, addr, kAccessSizeLog, is_write, false,
        shadow_mem, cur);
  }
}

}  // namespace __tsan

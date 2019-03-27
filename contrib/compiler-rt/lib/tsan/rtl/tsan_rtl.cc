//===-- tsan_rtl.cc -------------------------------------------------------===//
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
// Main file (entry points) for the TSan run-time.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_file.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_symbolizer.h"
#include "tsan_defs.h"
#include "tsan_platform.h"
#include "tsan_rtl.h"
#include "tsan_mman.h"
#include "tsan_suppressions.h"
#include "tsan_symbolize.h"
#include "ubsan/ubsan_init.h"

#ifdef __SSE3__
// <emmintrin.h> transitively includes <stdlib.h>,
// and it's prohibited to include std headers into tsan runtime.
// So we do this dirty trick.
#define _MM_MALLOC_H_INCLUDED
#define __MM_MALLOC_H
#include <emmintrin.h>
typedef __m128i m128;
#endif

volatile int __tsan_resumed = 0;

extern "C" void __tsan_resume() {
  __tsan_resumed = 1;
}

namespace __tsan {

#if !SANITIZER_GO && !SANITIZER_MAC
__attribute__((tls_model("initial-exec")))
THREADLOCAL char cur_thread_placeholder[sizeof(ThreadState)] ALIGNED(64);
#endif
static char ctx_placeholder[sizeof(Context)] ALIGNED(64);
Context *ctx;

// Can be overriden by a front-end.
#ifdef TSAN_EXTERNAL_HOOKS
bool OnFinalize(bool failed);
void OnInitialize();
#else
SANITIZER_WEAK_CXX_DEFAULT_IMPL
bool OnFinalize(bool failed) {
  return failed;
}
SANITIZER_WEAK_CXX_DEFAULT_IMPL
void OnInitialize() {}
#endif

static char thread_registry_placeholder[sizeof(ThreadRegistry)];

static ThreadContextBase *CreateThreadContext(u32 tid) {
  // Map thread trace when context is created.
  char name[50];
  internal_snprintf(name, sizeof(name), "trace %u", tid);
  MapThreadTrace(GetThreadTrace(tid), TraceSize() * sizeof(Event), name);
  const uptr hdr = GetThreadTraceHeader(tid);
  internal_snprintf(name, sizeof(name), "trace header %u", tid);
  MapThreadTrace(hdr, sizeof(Trace), name);
  new((void*)hdr) Trace();
  // We are going to use only a small part of the trace with the default
  // value of history_size. However, the constructor writes to the whole trace.
  // Unmap the unused part.
  uptr hdr_end = hdr + sizeof(Trace);
  hdr_end -= sizeof(TraceHeader) * (kTraceParts - TraceParts());
  hdr_end = RoundUp(hdr_end, GetPageSizeCached());
  if (hdr_end < hdr + sizeof(Trace))
    UnmapOrDie((void*)hdr_end, hdr + sizeof(Trace) - hdr_end);
  void *mem = internal_alloc(MBlockThreadContex, sizeof(ThreadContext));
  return new(mem) ThreadContext(tid);
}

#if !SANITIZER_GO
static const u32 kThreadQuarantineSize = 16;
#else
static const u32 kThreadQuarantineSize = 64;
#endif

Context::Context()
  : initialized()
  , report_mtx(MutexTypeReport, StatMtxReport)
  , nreported()
  , nmissed_expected()
  , thread_registry(new(thread_registry_placeholder) ThreadRegistry(
      CreateThreadContext, kMaxTid, kThreadQuarantineSize, kMaxTidReuse))
  , racy_mtx(MutexTypeRacy, StatMtxRacy)
  , racy_stacks()
  , racy_addresses()
  , fired_suppressions_mtx(MutexTypeFired, StatMtxFired)
  , clock_alloc("clock allocator") {
  fired_suppressions.reserve(8);
}

// The objects are allocated in TLS, so one may rely on zero-initialization.
ThreadState::ThreadState(Context *ctx, int tid, int unique_id, u64 epoch,
                         unsigned reuse_count,
                         uptr stk_addr, uptr stk_size,
                         uptr tls_addr, uptr tls_size)
  : fast_state(tid, epoch)
  // Do not touch these, rely on zero initialization,
  // they may be accessed before the ctor.
  // , ignore_reads_and_writes()
  // , ignore_interceptors()
  , clock(tid, reuse_count)
#if !SANITIZER_GO
  , jmp_bufs()
#endif
  , tid(tid)
  , unique_id(unique_id)
  , stk_addr(stk_addr)
  , stk_size(stk_size)
  , tls_addr(tls_addr)
  , tls_size(tls_size)
#if !SANITIZER_GO
  , last_sleep_clock(tid)
#endif
{
}

#if !SANITIZER_GO
static void MemoryProfiler(Context *ctx, fd_t fd, int i) {
  uptr n_threads;
  uptr n_running_threads;
  ctx->thread_registry->GetNumberOfThreads(&n_threads, &n_running_threads);
  InternalMmapVector<char> buf(4096);
  WriteMemoryProfile(buf.data(), buf.size(), n_threads, n_running_threads);
  WriteToFile(fd, buf.data(), internal_strlen(buf.data()));
}

static void BackgroundThread(void *arg) {
  // This is a non-initialized non-user thread, nothing to see here.
  // We don't use ScopedIgnoreInterceptors, because we want ignores to be
  // enabled even when the thread function exits (e.g. during pthread thread
  // shutdown code).
  cur_thread()->ignore_interceptors++;
  const u64 kMs2Ns = 1000 * 1000;

  fd_t mprof_fd = kInvalidFd;
  if (flags()->profile_memory && flags()->profile_memory[0]) {
    if (internal_strcmp(flags()->profile_memory, "stdout") == 0) {
      mprof_fd = 1;
    } else if (internal_strcmp(flags()->profile_memory, "stderr") == 0) {
      mprof_fd = 2;
    } else {
      InternalScopedString filename(kMaxPathLength);
      filename.append("%s.%d", flags()->profile_memory, (int)internal_getpid());
      fd_t fd = OpenFile(filename.data(), WrOnly);
      if (fd == kInvalidFd) {
        Printf("ThreadSanitizer: failed to open memory profile file '%s'\n",
            &filename[0]);
      } else {
        mprof_fd = fd;
      }
    }
  }

  u64 last_flush = NanoTime();
  uptr last_rss = 0;
  for (int i = 0;
      atomic_load(&ctx->stop_background_thread, memory_order_relaxed) == 0;
      i++) {
    SleepForMillis(100);
    u64 now = NanoTime();

    // Flush memory if requested.
    if (flags()->flush_memory_ms > 0) {
      if (last_flush + flags()->flush_memory_ms * kMs2Ns < now) {
        VPrintf(1, "ThreadSanitizer: periodic memory flush\n");
        FlushShadowMemory();
        last_flush = NanoTime();
      }
    }
    // GetRSS can be expensive on huge programs, so don't do it every 100ms.
    if (flags()->memory_limit_mb > 0) {
      uptr rss = GetRSS();
      uptr limit = uptr(flags()->memory_limit_mb) << 20;
      VPrintf(1, "ThreadSanitizer: memory flush check"
                 " RSS=%llu LAST=%llu LIMIT=%llu\n",
              (u64)rss >> 20, (u64)last_rss >> 20, (u64)limit >> 20);
      if (2 * rss > limit + last_rss) {
        VPrintf(1, "ThreadSanitizer: flushing memory due to RSS\n");
        FlushShadowMemory();
        rss = GetRSS();
        VPrintf(1, "ThreadSanitizer: memory flushed RSS=%llu\n", (u64)rss>>20);
      }
      last_rss = rss;
    }

    // Write memory profile if requested.
    if (mprof_fd != kInvalidFd)
      MemoryProfiler(ctx, mprof_fd, i);

    // Flush symbolizer cache if requested.
    if (flags()->flush_symbolizer_ms > 0) {
      u64 last = atomic_load(&ctx->last_symbolize_time_ns,
                             memory_order_relaxed);
      if (last != 0 && last + flags()->flush_symbolizer_ms * kMs2Ns < now) {
        Lock l(&ctx->report_mtx);
        ScopedErrorReportLock l2;
        SymbolizeFlush();
        atomic_store(&ctx->last_symbolize_time_ns, 0, memory_order_relaxed);
      }
    }
  }
}

static void StartBackgroundThread() {
  ctx->background_thread = internal_start_thread(&BackgroundThread, 0);
}

#ifndef __mips__
static void StopBackgroundThread() {
  atomic_store(&ctx->stop_background_thread, 1, memory_order_relaxed);
  internal_join_thread(ctx->background_thread);
  ctx->background_thread = 0;
}
#endif
#endif

void DontNeedShadowFor(uptr addr, uptr size) {
  ReleaseMemoryPagesToOS(MemToShadow(addr), MemToShadow(addr + size));
}

void MapShadow(uptr addr, uptr size) {
  // Global data is not 64K aligned, but there are no adjacent mappings,
  // so we can get away with unaligned mapping.
  // CHECK_EQ(addr, addr & ~((64 << 10) - 1));  // windows wants 64K alignment
  const uptr kPageSize = GetPageSizeCached();
  uptr shadow_begin = RoundDownTo((uptr)MemToShadow(addr), kPageSize);
  uptr shadow_end = RoundUpTo((uptr)MemToShadow(addr + size), kPageSize);
  if (!MmapFixedNoReserve(shadow_begin, shadow_end - shadow_begin, "shadow"))
    Die();

  // Meta shadow is 2:1, so tread carefully.
  static bool data_mapped = false;
  static uptr mapped_meta_end = 0;
  uptr meta_begin = (uptr)MemToMeta(addr);
  uptr meta_end = (uptr)MemToMeta(addr + size);
  meta_begin = RoundDownTo(meta_begin, 64 << 10);
  meta_end = RoundUpTo(meta_end, 64 << 10);
  if (!data_mapped) {
    // First call maps data+bss.
    data_mapped = true;
    if (!MmapFixedNoReserve(meta_begin, meta_end - meta_begin, "meta shadow"))
      Die();
  } else {
    // Mapping continous heap.
    // Windows wants 64K alignment.
    meta_begin = RoundDownTo(meta_begin, 64 << 10);
    meta_end = RoundUpTo(meta_end, 64 << 10);
    if (meta_end <= mapped_meta_end)
      return;
    if (meta_begin < mapped_meta_end)
      meta_begin = mapped_meta_end;
    if (!MmapFixedNoReserve(meta_begin, meta_end - meta_begin, "meta shadow"))
      Die();
    mapped_meta_end = meta_end;
  }
  VPrintf(2, "mapped meta shadow for (%p-%p) at (%p-%p)\n",
      addr, addr+size, meta_begin, meta_end);
}

void MapThreadTrace(uptr addr, uptr size, const char *name) {
  DPrintf("#0: Mapping trace at %p-%p(0x%zx)\n", addr, addr + size, size);
  CHECK_GE(addr, TraceMemBeg());
  CHECK_LE(addr + size, TraceMemEnd());
  CHECK_EQ(addr, addr & ~((64 << 10) - 1));  // windows wants 64K alignment
  if (!MmapFixedNoReserve(addr, size, name)) {
    Printf("FATAL: ThreadSanitizer can not mmap thread trace (%p/%p)\n",
        addr, size);
    Die();
  }
}

static void CheckShadowMapping() {
  uptr beg, end;
  for (int i = 0; GetUserRegion(i, &beg, &end); i++) {
    // Skip cases for empty regions (heap definition for architectures that
    // do not use 64-bit allocator).
    if (beg == end)
      continue;
    VPrintf(3, "checking shadow region %p-%p\n", beg, end);
    uptr prev = 0;
    for (uptr p0 = beg; p0 <= end; p0 += (end - beg) / 4) {
      for (int x = -(int)kShadowCell; x <= (int)kShadowCell; x += kShadowCell) {
        const uptr p = RoundDown(p0 + x, kShadowCell);
        if (p < beg || p >= end)
          continue;
        const uptr s = MemToShadow(p);
        const uptr m = (uptr)MemToMeta(p);
        VPrintf(3, "  checking pointer %p: shadow=%p meta=%p\n", p, s, m);
        CHECK(IsAppMem(p));
        CHECK(IsShadowMem(s));
        CHECK_EQ(p, ShadowToMem(s));
        CHECK(IsMetaMem(m));
        if (prev) {
          // Ensure that shadow and meta mappings are linear within a single
          // user range. Lots of code that processes memory ranges assumes it.
          const uptr prev_s = MemToShadow(prev);
          const uptr prev_m = (uptr)MemToMeta(prev);
          CHECK_EQ(s - prev_s, (p - prev) * kShadowMultiplier);
          CHECK_EQ((m - prev_m) / kMetaShadowSize,
                   (p - prev) / kMetaShadowCell);
        }
        prev = p;
      }
    }
  }
}

#if !SANITIZER_GO
static void OnStackUnwind(const SignalContext &sig, const void *,
                          BufferedStackTrace *stack) {
  uptr top = 0;
  uptr bottom = 0;
  bool fast = common_flags()->fast_unwind_on_fatal;
  if (fast) GetThreadStackTopAndBottom(false, &top, &bottom);
  stack->Unwind(kStackTraceMax, sig.pc, sig.bp, sig.context, top, bottom, fast);
}

static void TsanOnDeadlySignal(int signo, void *siginfo, void *context) {
  HandleDeadlySignal(siginfo, context, GetTid(), &OnStackUnwind, nullptr);
}
#endif

void Initialize(ThreadState *thr) {
  // Thread safe because done before all threads exist.
  static bool is_initialized = false;
  if (is_initialized)
    return;
  is_initialized = true;
  // We are not ready to handle interceptors yet.
  ScopedIgnoreInterceptors ignore;
  SanitizerToolName = "ThreadSanitizer";
  // Install tool-specific callbacks in sanitizer_common.
  SetCheckFailedCallback(TsanCheckFailed);

  ctx = new(ctx_placeholder) Context;
  const char *options = GetEnv(SANITIZER_GO ? "GORACE" : "TSAN_OPTIONS");
  CacheBinaryName();
  CheckASLR();
  InitializeFlags(&ctx->flags, options);
  AvoidCVE_2016_2143();
  __sanitizer::InitializePlatformEarly();
  __tsan::InitializePlatformEarly();

#if !SANITIZER_GO
  // Re-exec ourselves if we need to set additional env or command line args.
  MaybeReexec();

  InitializeAllocator();
  ReplaceSystemMalloc();
#endif
  if (common_flags()->detect_deadlocks)
    ctx->dd = DDetector::Create(flags());
  Processor *proc = ProcCreate();
  ProcWire(proc, thr);
  InitializeInterceptors();
  CheckShadowMapping();
  InitializePlatform();
  InitializeMutex();
  InitializeDynamicAnnotations();
#if !SANITIZER_GO
  InitializeShadowMemory();
  InitializeAllocatorLate();
  InstallDeadlySignalHandlers(TsanOnDeadlySignal);
#endif
  // Setup correct file descriptor for error reports.
  __sanitizer_set_report_path(common_flags()->log_path);
  InitializeSuppressions();
#if !SANITIZER_GO
  InitializeLibIgnore();
  Symbolizer::GetOrInit()->AddHooks(EnterSymbolizer, ExitSymbolizer);
#endif

  VPrintf(1, "***** Running under ThreadSanitizer v2 (pid %d) *****\n",
          (int)internal_getpid());

  // Initialize thread 0.
  int tid = ThreadCreate(thr, 0, 0, true);
  CHECK_EQ(tid, 0);
  ThreadStart(thr, tid, GetTid(), /*workerthread*/ false);
#if TSAN_CONTAINS_UBSAN
  __ubsan::InitAsPlugin();
#endif
  ctx->initialized = true;

#if !SANITIZER_GO
  Symbolizer::LateInitialize();
#endif

  if (flags()->stop_on_start) {
    Printf("ThreadSanitizer is suspended at startup (pid %d)."
           " Call __tsan_resume().\n",
           (int)internal_getpid());
    while (__tsan_resumed == 0) {}
  }

  OnInitialize();
}

void MaybeSpawnBackgroundThread() {
  // On MIPS, TSan initialization is run before
  // __pthread_initialize_minimal_internal() is finished, so we can not spawn
  // new threads.
#if !SANITIZER_GO && !defined(__mips__)
  static atomic_uint32_t bg_thread = {};
  if (atomic_load(&bg_thread, memory_order_relaxed) == 0 &&
      atomic_exchange(&bg_thread, 1, memory_order_relaxed) == 0) {
    StartBackgroundThread();
    SetSandboxingCallback(StopBackgroundThread);
  }
#endif
}


int Finalize(ThreadState *thr) {
  bool failed = false;

  if (common_flags()->print_module_map == 1) PrintModuleMap();

  if (flags()->atexit_sleep_ms > 0 && ThreadCount(thr) > 1)
    SleepForMillis(flags()->atexit_sleep_ms);

  // Wait for pending reports.
  ctx->report_mtx.Lock();
  { ScopedErrorReportLock l; }
  ctx->report_mtx.Unlock();

#if !SANITIZER_GO
  if (Verbosity()) AllocatorPrintStats();
#endif

  ThreadFinalize(thr);

  if (ctx->nreported) {
    failed = true;
#if !SANITIZER_GO
    Printf("ThreadSanitizer: reported %d warnings\n", ctx->nreported);
#else
    Printf("Found %d data race(s)\n", ctx->nreported);
#endif
  }

  if (ctx->nmissed_expected) {
    failed = true;
    Printf("ThreadSanitizer: missed %d expected races\n",
        ctx->nmissed_expected);
  }

  if (common_flags()->print_suppressions)
    PrintMatchedSuppressions();
#if !SANITIZER_GO
  if (flags()->print_benign)
    PrintMatchedBenignRaces();
#endif

  failed = OnFinalize(failed);

#if TSAN_COLLECT_STATS
  StatAggregate(ctx->stat, thr->stat);
  StatOutput(ctx->stat);
#endif

  return failed ? common_flags()->exitcode : 0;
}

#if !SANITIZER_GO
void ForkBefore(ThreadState *thr, uptr pc) {
  ctx->thread_registry->Lock();
  ctx->report_mtx.Lock();
}

void ForkParentAfter(ThreadState *thr, uptr pc) {
  ctx->report_mtx.Unlock();
  ctx->thread_registry->Unlock();
}

void ForkChildAfter(ThreadState *thr, uptr pc) {
  ctx->report_mtx.Unlock();
  ctx->thread_registry->Unlock();

  uptr nthread = 0;
  ctx->thread_registry->GetNumberOfThreads(0, 0, &nthread /* alive threads */);
  VPrintf(1, "ThreadSanitizer: forked new process with pid %d,"
      " parent had %d threads\n", (int)internal_getpid(), (int)nthread);
  if (nthread == 1) {
    StartBackgroundThread();
  } else {
    // We've just forked a multi-threaded process. We cannot reasonably function
    // after that (some mutexes may be locked before fork). So just enable
    // ignores for everything in the hope that we will exec soon.
    ctx->after_multithreaded_fork = true;
    thr->ignore_interceptors++;
    ThreadIgnoreBegin(thr, pc);
    ThreadIgnoreSyncBegin(thr, pc);
  }
}
#endif

#if SANITIZER_GO
NOINLINE
void GrowShadowStack(ThreadState *thr) {
  const int sz = thr->shadow_stack_end - thr->shadow_stack;
  const int newsz = 2 * sz;
  uptr *newstack = (uptr*)internal_alloc(MBlockShadowStack,
      newsz * sizeof(uptr));
  internal_memcpy(newstack, thr->shadow_stack, sz * sizeof(uptr));
  internal_free(thr->shadow_stack);
  thr->shadow_stack = newstack;
  thr->shadow_stack_pos = newstack + sz;
  thr->shadow_stack_end = newstack + newsz;
}
#endif

u32 CurrentStackId(ThreadState *thr, uptr pc) {
  if (!thr->is_inited)  // May happen during bootstrap.
    return 0;
  if (pc != 0) {
#if !SANITIZER_GO
    DCHECK_LT(thr->shadow_stack_pos, thr->shadow_stack_end);
#else
    if (thr->shadow_stack_pos == thr->shadow_stack_end)
      GrowShadowStack(thr);
#endif
    thr->shadow_stack_pos[0] = pc;
    thr->shadow_stack_pos++;
  }
  u32 id = StackDepotPut(
      StackTrace(thr->shadow_stack, thr->shadow_stack_pos - thr->shadow_stack));
  if (pc != 0)
    thr->shadow_stack_pos--;
  return id;
}

void TraceSwitch(ThreadState *thr) {
#if !SANITIZER_GO
  if (ctx->after_multithreaded_fork)
    return;
#endif
  thr->nomalloc++;
  Trace *thr_trace = ThreadTrace(thr->tid);
  Lock l(&thr_trace->mtx);
  unsigned trace = (thr->fast_state.epoch() / kTracePartSize) % TraceParts();
  TraceHeader *hdr = &thr_trace->headers[trace];
  hdr->epoch0 = thr->fast_state.epoch();
  ObtainCurrentStack(thr, 0, &hdr->stack0);
  hdr->mset0 = thr->mset;
  thr->nomalloc--;
}

Trace *ThreadTrace(int tid) {
  return (Trace*)GetThreadTraceHeader(tid);
}

uptr TraceTopPC(ThreadState *thr) {
  Event *events = (Event*)GetThreadTrace(thr->tid);
  uptr pc = events[thr->fast_state.GetTracePos()];
  return pc;
}

uptr TraceSize() {
  return (uptr)(1ull << (kTracePartSizeBits + flags()->history_size + 1));
}

uptr TraceParts() {
  return TraceSize() / kTracePartSize;
}

#if !SANITIZER_GO
extern "C" void __tsan_trace_switch() {
  TraceSwitch(cur_thread());
}

extern "C" void __tsan_report_race() {
  ReportRace(cur_thread());
}
#endif

ALWAYS_INLINE
Shadow LoadShadow(u64 *p) {
  u64 raw = atomic_load((atomic_uint64_t*)p, memory_order_relaxed);
  return Shadow(raw);
}

ALWAYS_INLINE
void StoreShadow(u64 *sp, u64 s) {
  atomic_store((atomic_uint64_t*)sp, s, memory_order_relaxed);
}

ALWAYS_INLINE
void StoreIfNotYetStored(u64 *sp, u64 *s) {
  StoreShadow(sp, *s);
  *s = 0;
}

ALWAYS_INLINE
void HandleRace(ThreadState *thr, u64 *shadow_mem,
                              Shadow cur, Shadow old) {
  thr->racy_state[0] = cur.raw();
  thr->racy_state[1] = old.raw();
  thr->racy_shadow_addr = shadow_mem;
#if !SANITIZER_GO
  HACKY_CALL(__tsan_report_race);
#else
  ReportRace(thr);
#endif
}

static inline bool HappensBefore(Shadow old, ThreadState *thr) {
  return thr->clock.get(old.TidWithIgnore()) >= old.epoch();
}

ALWAYS_INLINE
void MemoryAccessImpl1(ThreadState *thr, uptr addr,
    int kAccessSizeLog, bool kAccessIsWrite, bool kIsAtomic,
    u64 *shadow_mem, Shadow cur) {
  StatInc(thr, StatMop);
  StatInc(thr, kAccessIsWrite ? StatMopWrite : StatMopRead);
  StatInc(thr, (StatType)(StatMop1 + kAccessSizeLog));

  // This potentially can live in an MMX/SSE scratch register.
  // The required intrinsics are:
  // __m128i _mm_move_epi64(__m128i*);
  // _mm_storel_epi64(u64*, __m128i);
  u64 store_word = cur.raw();

  // scan all the shadow values and dispatch to 4 categories:
  // same, replace, candidate and race (see comments below).
  // we consider only 3 cases regarding access sizes:
  // equal, intersect and not intersect. initially I considered
  // larger and smaller as well, it allowed to replace some
  // 'candidates' with 'same' or 'replace', but I think
  // it's just not worth it (performance- and complexity-wise).

  Shadow old(0);

  // It release mode we manually unroll the loop,
  // because empirically gcc generates better code this way.
  // However, we can't afford unrolling in debug mode, because the function
  // consumes almost 4K of stack. Gtest gives only 4K of stack to death test
  // threads, which is not enough for the unrolled loop.
#if SANITIZER_DEBUG
  for (int idx = 0; idx < 4; idx++) {
#include "tsan_update_shadow_word_inl.h"
  }
#else
  int idx = 0;
#include "tsan_update_shadow_word_inl.h"
  idx = 1;
#include "tsan_update_shadow_word_inl.h"
  idx = 2;
#include "tsan_update_shadow_word_inl.h"
  idx = 3;
#include "tsan_update_shadow_word_inl.h"
#endif

  // we did not find any races and had already stored
  // the current access info, so we are done
  if (LIKELY(store_word == 0))
    return;
  // choose a random candidate slot and replace it
  StoreShadow(shadow_mem + (cur.epoch() % kShadowCnt), store_word);
  StatInc(thr, StatShadowReplace);
  return;
 RACE:
  HandleRace(thr, shadow_mem, cur, old);
  return;
}

void UnalignedMemoryAccess(ThreadState *thr, uptr pc, uptr addr,
    int size, bool kAccessIsWrite, bool kIsAtomic) {
  while (size) {
    int size1 = 1;
    int kAccessSizeLog = kSizeLog1;
    if (size >= 8 && (addr & ~7) == ((addr + 7) & ~7)) {
      size1 = 8;
      kAccessSizeLog = kSizeLog8;
    } else if (size >= 4 && (addr & ~7) == ((addr + 3) & ~7)) {
      size1 = 4;
      kAccessSizeLog = kSizeLog4;
    } else if (size >= 2 && (addr & ~7) == ((addr + 1) & ~7)) {
      size1 = 2;
      kAccessSizeLog = kSizeLog2;
    }
    MemoryAccess(thr, pc, addr, kAccessSizeLog, kAccessIsWrite, kIsAtomic);
    addr += size1;
    size -= size1;
  }
}

ALWAYS_INLINE
bool ContainsSameAccessSlow(u64 *s, u64 a, u64 sync_epoch, bool is_write) {
  Shadow cur(a);
  for (uptr i = 0; i < kShadowCnt; i++) {
    Shadow old(LoadShadow(&s[i]));
    if (Shadow::Addr0AndSizeAreEqual(cur, old) &&
        old.TidWithIgnore() == cur.TidWithIgnore() &&
        old.epoch() > sync_epoch &&
        old.IsAtomic() == cur.IsAtomic() &&
        old.IsRead() <= cur.IsRead())
      return true;
  }
  return false;
}

#if defined(__SSE3__)
#define SHUF(v0, v1, i0, i1, i2, i3) _mm_castps_si128(_mm_shuffle_ps( \
    _mm_castsi128_ps(v0), _mm_castsi128_ps(v1), \
    (i0)*1 + (i1)*4 + (i2)*16 + (i3)*64))
ALWAYS_INLINE
bool ContainsSameAccessFast(u64 *s, u64 a, u64 sync_epoch, bool is_write) {
  // This is an optimized version of ContainsSameAccessSlow.
  // load current access into access[0:63]
  const m128 access     = _mm_cvtsi64_si128(a);
  // duplicate high part of access in addr0:
  // addr0[0:31]        = access[32:63]
  // addr0[32:63]       = access[32:63]
  // addr0[64:95]       = access[32:63]
  // addr0[96:127]      = access[32:63]
  const m128 addr0      = SHUF(access, access, 1, 1, 1, 1);
  // load 4 shadow slots
  const m128 shadow0    = _mm_load_si128((__m128i*)s);
  const m128 shadow1    = _mm_load_si128((__m128i*)s + 1);
  // load high parts of 4 shadow slots into addr_vect:
  // addr_vect[0:31]    = shadow0[32:63]
  // addr_vect[32:63]   = shadow0[96:127]
  // addr_vect[64:95]   = shadow1[32:63]
  // addr_vect[96:127]  = shadow1[96:127]
  m128 addr_vect        = SHUF(shadow0, shadow1, 1, 3, 1, 3);
  if (!is_write) {
    // set IsRead bit in addr_vect
    const m128 rw_mask1 = _mm_cvtsi64_si128(1<<15);
    const m128 rw_mask  = SHUF(rw_mask1, rw_mask1, 0, 0, 0, 0);
    addr_vect           = _mm_or_si128(addr_vect, rw_mask);
  }
  // addr0 == addr_vect?
  const m128 addr_res   = _mm_cmpeq_epi32(addr0, addr_vect);
  // epoch1[0:63]       = sync_epoch
  const m128 epoch1     = _mm_cvtsi64_si128(sync_epoch);
  // epoch[0:31]        = sync_epoch[0:31]
  // epoch[32:63]       = sync_epoch[0:31]
  // epoch[64:95]       = sync_epoch[0:31]
  // epoch[96:127]      = sync_epoch[0:31]
  const m128 epoch      = SHUF(epoch1, epoch1, 0, 0, 0, 0);
  // load low parts of shadow cell epochs into epoch_vect:
  // epoch_vect[0:31]   = shadow0[0:31]
  // epoch_vect[32:63]  = shadow0[64:95]
  // epoch_vect[64:95]  = shadow1[0:31]
  // epoch_vect[96:127] = shadow1[64:95]
  const m128 epoch_vect = SHUF(shadow0, shadow1, 0, 2, 0, 2);
  // epoch_vect >= sync_epoch?
  const m128 epoch_res  = _mm_cmpgt_epi32(epoch_vect, epoch);
  // addr_res & epoch_res
  const m128 res        = _mm_and_si128(addr_res, epoch_res);
  // mask[0] = res[7]
  // mask[1] = res[15]
  // ...
  // mask[15] = res[127]
  const int mask        = _mm_movemask_epi8(res);
  return mask != 0;
}
#endif

ALWAYS_INLINE
bool ContainsSameAccess(u64 *s, u64 a, u64 sync_epoch, bool is_write) {
#if defined(__SSE3__)
  bool res = ContainsSameAccessFast(s, a, sync_epoch, is_write);
  // NOTE: this check can fail if the shadow is concurrently mutated
  // by other threads. But it still can be useful if you modify
  // ContainsSameAccessFast and want to ensure that it's not completely broken.
  // DCHECK_EQ(res, ContainsSameAccessSlow(s, a, sync_epoch, is_write));
  return res;
#else
  return ContainsSameAccessSlow(s, a, sync_epoch, is_write);
#endif
}

ALWAYS_INLINE USED
void MemoryAccess(ThreadState *thr, uptr pc, uptr addr,
    int kAccessSizeLog, bool kAccessIsWrite, bool kIsAtomic) {
  u64 *shadow_mem = (u64*)MemToShadow(addr);
  DPrintf2("#%d: MemoryAccess: @%p %p size=%d"
      " is_write=%d shadow_mem=%p {%zx, %zx, %zx, %zx}\n",
      (int)thr->fast_state.tid(), (void*)pc, (void*)addr,
      (int)(1 << kAccessSizeLog), kAccessIsWrite, shadow_mem,
      (uptr)shadow_mem[0], (uptr)shadow_mem[1],
      (uptr)shadow_mem[2], (uptr)shadow_mem[3]);
#if SANITIZER_DEBUG
  if (!IsAppMem(addr)) {
    Printf("Access to non app mem %zx\n", addr);
    DCHECK(IsAppMem(addr));
  }
  if (!IsShadowMem((uptr)shadow_mem)) {
    Printf("Bad shadow addr %p (%zx)\n", shadow_mem, addr);
    DCHECK(IsShadowMem((uptr)shadow_mem));
  }
#endif

  if (!SANITIZER_GO && *shadow_mem == kShadowRodata) {
    // Access to .rodata section, no races here.
    // Measurements show that it can be 10-20% of all memory accesses.
    StatInc(thr, StatMop);
    StatInc(thr, kAccessIsWrite ? StatMopWrite : StatMopRead);
    StatInc(thr, (StatType)(StatMop1 + kAccessSizeLog));
    StatInc(thr, StatMopRodata);
    return;
  }

  FastState fast_state = thr->fast_state;
  if (fast_state.GetIgnoreBit()) {
    StatInc(thr, StatMop);
    StatInc(thr, kAccessIsWrite ? StatMopWrite : StatMopRead);
    StatInc(thr, (StatType)(StatMop1 + kAccessSizeLog));
    StatInc(thr, StatMopIgnored);
    return;
  }

  Shadow cur(fast_state);
  cur.SetAddr0AndSizeLog(addr & 7, kAccessSizeLog);
  cur.SetWrite(kAccessIsWrite);
  cur.SetAtomic(kIsAtomic);

  if (LIKELY(ContainsSameAccess(shadow_mem, cur.raw(),
      thr->fast_synch_epoch, kAccessIsWrite))) {
    StatInc(thr, StatMop);
    StatInc(thr, kAccessIsWrite ? StatMopWrite : StatMopRead);
    StatInc(thr, (StatType)(StatMop1 + kAccessSizeLog));
    StatInc(thr, StatMopSame);
    return;
  }

  if (kCollectHistory) {
    fast_state.IncrementEpoch();
    thr->fast_state = fast_state;
    TraceAddEvent(thr, fast_state, EventTypeMop, pc);
    cur.IncrementEpoch();
  }

  MemoryAccessImpl1(thr, addr, kAccessSizeLog, kAccessIsWrite, kIsAtomic,
      shadow_mem, cur);
}

// Called by MemoryAccessRange in tsan_rtl_thread.cc
ALWAYS_INLINE USED
void MemoryAccessImpl(ThreadState *thr, uptr addr,
    int kAccessSizeLog, bool kAccessIsWrite, bool kIsAtomic,
    u64 *shadow_mem, Shadow cur) {
  if (LIKELY(ContainsSameAccess(shadow_mem, cur.raw(),
      thr->fast_synch_epoch, kAccessIsWrite))) {
    StatInc(thr, StatMop);
    StatInc(thr, kAccessIsWrite ? StatMopWrite : StatMopRead);
    StatInc(thr, (StatType)(StatMop1 + kAccessSizeLog));
    StatInc(thr, StatMopSame);
    return;
  }

  MemoryAccessImpl1(thr, addr, kAccessSizeLog, kAccessIsWrite, kIsAtomic,
      shadow_mem, cur);
}

static void MemoryRangeSet(ThreadState *thr, uptr pc, uptr addr, uptr size,
                           u64 val) {
  (void)thr;
  (void)pc;
  if (size == 0)
    return;
  // FIXME: fix me.
  uptr offset = addr % kShadowCell;
  if (offset) {
    offset = kShadowCell - offset;
    if (size <= offset)
      return;
    addr += offset;
    size -= offset;
  }
  DCHECK_EQ(addr % 8, 0);
  // If a user passes some insane arguments (memset(0)),
  // let it just crash as usual.
  if (!IsAppMem(addr) || !IsAppMem(addr + size - 1))
    return;
  // Don't want to touch lots of shadow memory.
  // If a program maps 10MB stack, there is no need reset the whole range.
  size = (size + (kShadowCell - 1)) & ~(kShadowCell - 1);
  // UnmapOrDie/MmapFixedNoReserve does not work on Windows.
  if (SANITIZER_WINDOWS || size < common_flags()->clear_shadow_mmap_threshold) {
    u64 *p = (u64*)MemToShadow(addr);
    CHECK(IsShadowMem((uptr)p));
    CHECK(IsShadowMem((uptr)(p + size * kShadowCnt / kShadowCell - 1)));
    // FIXME: may overwrite a part outside the region
    for (uptr i = 0; i < size / kShadowCell * kShadowCnt;) {
      p[i++] = val;
      for (uptr j = 1; j < kShadowCnt; j++)
        p[i++] = 0;
    }
  } else {
    // The region is big, reset only beginning and end.
    const uptr kPageSize = GetPageSizeCached();
    u64 *begin = (u64*)MemToShadow(addr);
    u64 *end = begin + size / kShadowCell * kShadowCnt;
    u64 *p = begin;
    // Set at least first kPageSize/2 to page boundary.
    while ((p < begin + kPageSize / kShadowSize / 2) || ((uptr)p % kPageSize)) {
      *p++ = val;
      for (uptr j = 1; j < kShadowCnt; j++)
        *p++ = 0;
    }
    // Reset middle part.
    u64 *p1 = p;
    p = RoundDown(end, kPageSize);
    UnmapOrDie((void*)p1, (uptr)p - (uptr)p1);
    if (!MmapFixedNoReserve((uptr)p1, (uptr)p - (uptr)p1))
      Die();
    // Set the ending.
    while (p < end) {
      *p++ = val;
      for (uptr j = 1; j < kShadowCnt; j++)
        *p++ = 0;
    }
  }
}

void MemoryResetRange(ThreadState *thr, uptr pc, uptr addr, uptr size) {
  MemoryRangeSet(thr, pc, addr, size, 0);
}

void MemoryRangeFreed(ThreadState *thr, uptr pc, uptr addr, uptr size) {
  // Processing more than 1k (4k of shadow) is expensive,
  // can cause excessive memory consumption (user does not necessary touch
  // the whole range) and most likely unnecessary.
  if (size > 1024)
    size = 1024;
  CHECK_EQ(thr->is_freeing, false);
  thr->is_freeing = true;
  MemoryAccessRange(thr, pc, addr, size, true);
  thr->is_freeing = false;
  if (kCollectHistory) {
    thr->fast_state.IncrementEpoch();
    TraceAddEvent(thr, thr->fast_state, EventTypeMop, pc);
  }
  Shadow s(thr->fast_state);
  s.ClearIgnoreBit();
  s.MarkAsFreed();
  s.SetWrite(true);
  s.SetAddr0AndSizeLog(0, 3);
  MemoryRangeSet(thr, pc, addr, size, s.raw());
}

void MemoryRangeImitateWrite(ThreadState *thr, uptr pc, uptr addr, uptr size) {
  if (kCollectHistory) {
    thr->fast_state.IncrementEpoch();
    TraceAddEvent(thr, thr->fast_state, EventTypeMop, pc);
  }
  Shadow s(thr->fast_state);
  s.ClearIgnoreBit();
  s.SetWrite(true);
  s.SetAddr0AndSizeLog(0, 3);
  MemoryRangeSet(thr, pc, addr, size, s.raw());
}

ALWAYS_INLINE USED
void FuncEntry(ThreadState *thr, uptr pc) {
  StatInc(thr, StatFuncEnter);
  DPrintf2("#%d: FuncEntry %p\n", (int)thr->fast_state.tid(), (void*)pc);
  if (kCollectHistory) {
    thr->fast_state.IncrementEpoch();
    TraceAddEvent(thr, thr->fast_state, EventTypeFuncEnter, pc);
  }

  // Shadow stack maintenance can be replaced with
  // stack unwinding during trace switch (which presumably must be faster).
  DCHECK_GE(thr->shadow_stack_pos, thr->shadow_stack);
#if !SANITIZER_GO
  DCHECK_LT(thr->shadow_stack_pos, thr->shadow_stack_end);
#else
  if (thr->shadow_stack_pos == thr->shadow_stack_end)
    GrowShadowStack(thr);
#endif
  thr->shadow_stack_pos[0] = pc;
  thr->shadow_stack_pos++;
}

ALWAYS_INLINE USED
void FuncExit(ThreadState *thr) {
  StatInc(thr, StatFuncExit);
  DPrintf2("#%d: FuncExit\n", (int)thr->fast_state.tid());
  if (kCollectHistory) {
    thr->fast_state.IncrementEpoch();
    TraceAddEvent(thr, thr->fast_state, EventTypeFuncExit, 0);
  }

  DCHECK_GT(thr->shadow_stack_pos, thr->shadow_stack);
#if !SANITIZER_GO
  DCHECK_LT(thr->shadow_stack_pos, thr->shadow_stack_end);
#endif
  thr->shadow_stack_pos--;
}

void ThreadIgnoreBegin(ThreadState *thr, uptr pc, bool save_stack) {
  DPrintf("#%d: ThreadIgnoreBegin\n", thr->tid);
  thr->ignore_reads_and_writes++;
  CHECK_GT(thr->ignore_reads_and_writes, 0);
  thr->fast_state.SetIgnoreBit();
#if !SANITIZER_GO
  if (save_stack && !ctx->after_multithreaded_fork)
    thr->mop_ignore_set.Add(CurrentStackId(thr, pc));
#endif
}

void ThreadIgnoreEnd(ThreadState *thr, uptr pc) {
  DPrintf("#%d: ThreadIgnoreEnd\n", thr->tid);
  CHECK_GT(thr->ignore_reads_and_writes, 0);
  thr->ignore_reads_and_writes--;
  if (thr->ignore_reads_and_writes == 0) {
    thr->fast_state.ClearIgnoreBit();
#if !SANITIZER_GO
    thr->mop_ignore_set.Reset();
#endif
  }
}

#if !SANITIZER_GO
extern "C" SANITIZER_INTERFACE_ATTRIBUTE
uptr __tsan_testonly_shadow_stack_current_size() {
  ThreadState *thr = cur_thread();
  return thr->shadow_stack_pos - thr->shadow_stack;
}
#endif

void ThreadIgnoreSyncBegin(ThreadState *thr, uptr pc, bool save_stack) {
  DPrintf("#%d: ThreadIgnoreSyncBegin\n", thr->tid);
  thr->ignore_sync++;
  CHECK_GT(thr->ignore_sync, 0);
#if !SANITIZER_GO
  if (save_stack && !ctx->after_multithreaded_fork)
    thr->sync_ignore_set.Add(CurrentStackId(thr, pc));
#endif
}

void ThreadIgnoreSyncEnd(ThreadState *thr, uptr pc) {
  DPrintf("#%d: ThreadIgnoreSyncEnd\n", thr->tid);
  CHECK_GT(thr->ignore_sync, 0);
  thr->ignore_sync--;
#if !SANITIZER_GO
  if (thr->ignore_sync == 0)
    thr->sync_ignore_set.Reset();
#endif
}

bool MD5Hash::operator==(const MD5Hash &other) const {
  return hash[0] == other.hash[0] && hash[1] == other.hash[1];
}

#if SANITIZER_DEBUG
void build_consistency_debug() {}
#else
void build_consistency_release() {}
#endif

#if TSAN_COLLECT_STATS
void build_consistency_stats() {}
#else
void build_consistency_nostats() {}
#endif

}  // namespace __tsan

#if !SANITIZER_GO
// Must be included in this file to make sure everything is inlined.
#include "tsan_interface_inl.h"
#endif

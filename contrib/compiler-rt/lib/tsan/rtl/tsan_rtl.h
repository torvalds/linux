//===-- tsan_rtl.h ----------------------------------------------*- C++ -*-===//
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
// Main internal TSan header file.
//
// Ground rules:
//   - C++ run-time should not be used (static CTORs, RTTI, exceptions, static
//     function-scope locals)
//   - All functions/classes/etc reside in namespace __tsan, except for those
//     declared in tsan_interface.h.
//   - Platform-specific files should be used instead of ifdefs (*).
//   - No system headers included in header files (*).
//   - Platform specific headres included only into platform-specific files (*).
//
//  (*) Except when inlining is critical for performance.
//===----------------------------------------------------------------------===//

#ifndef TSAN_RTL_H
#define TSAN_RTL_H

#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_asm.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_deadlock_detector_interface.h"
#include "sanitizer_common/sanitizer_libignore.h"
#include "sanitizer_common/sanitizer_suppressions.h"
#include "sanitizer_common/sanitizer_thread_registry.h"
#include "sanitizer_common/sanitizer_vector.h"
#include "tsan_clock.h"
#include "tsan_defs.h"
#include "tsan_flags.h"
#include "tsan_mman.h"
#include "tsan_sync.h"
#include "tsan_trace.h"
#include "tsan_report.h"
#include "tsan_platform.h"
#include "tsan_mutexset.h"
#include "tsan_ignoreset.h"
#include "tsan_stack_trace.h"

#if SANITIZER_WORDSIZE != 64
# error "ThreadSanitizer is supported only on 64-bit platforms"
#endif

namespace __tsan {

#if !SANITIZER_GO
struct MapUnmapCallback;
#if defined(__mips64) || defined(__aarch64__) || defined(__powerpc__)
static const uptr kAllocatorRegionSizeLog = 20;
static const uptr kAllocatorNumRegions =
    SANITIZER_MMAP_RANGE_SIZE >> kAllocatorRegionSizeLog;
using ByteMap = TwoLevelByteMap<(kAllocatorNumRegions >> 12), 1 << 12,
                                LocalAddressSpaceView, MapUnmapCallback>;
struct AP32 {
  static const uptr kSpaceBeg = 0;
  static const u64 kSpaceSize = SANITIZER_MMAP_RANGE_SIZE;
  static const uptr kMetadataSize = 0;
  typedef __sanitizer::CompactSizeClassMap SizeClassMap;
  static const uptr kRegionSizeLog = kAllocatorRegionSizeLog;
  using AddressSpaceView = LocalAddressSpaceView;
  using ByteMap = __tsan::ByteMap;
  typedef __tsan::MapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
};
typedef SizeClassAllocator32<AP32> PrimaryAllocator;
#else
struct AP64 {  // Allocator64 parameters. Deliberately using a short name.
  static const uptr kSpaceBeg = Mapping::kHeapMemBeg;
  static const uptr kSpaceSize = Mapping::kHeapMemEnd - Mapping::kHeapMemBeg;
  static const uptr kMetadataSize = 0;
  typedef DefaultSizeClassMap SizeClassMap;
  typedef __tsan::MapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = LocalAddressSpaceView;
};
typedef SizeClassAllocator64<AP64> PrimaryAllocator;
#endif
typedef SizeClassAllocatorLocalCache<PrimaryAllocator> AllocatorCache;
typedef LargeMmapAllocator<MapUnmapCallback> SecondaryAllocator;
typedef CombinedAllocator<PrimaryAllocator, AllocatorCache,
    SecondaryAllocator> Allocator;
Allocator *allocator();
#endif

void TsanCheckFailed(const char *file, int line, const char *cond,
                     u64 v1, u64 v2);

const u64 kShadowRodata = (u64)-1;  // .rodata shadow marker

// FastState (from most significant bit):
//   ignore          : 1
//   tid             : kTidBits
//   unused          : -
//   history_size    : 3
//   epoch           : kClkBits
class FastState {
 public:
  FastState(u64 tid, u64 epoch) {
    x_ = tid << kTidShift;
    x_ |= epoch;
    DCHECK_EQ(tid, this->tid());
    DCHECK_EQ(epoch, this->epoch());
    DCHECK_EQ(GetIgnoreBit(), false);
  }

  explicit FastState(u64 x)
      : x_(x) {
  }

  u64 raw() const {
    return x_;
  }

  u64 tid() const {
    u64 res = (x_ & ~kIgnoreBit) >> kTidShift;
    return res;
  }

  u64 TidWithIgnore() const {
    u64 res = x_ >> kTidShift;
    return res;
  }

  u64 epoch() const {
    u64 res = x_ & ((1ull << kClkBits) - 1);
    return res;
  }

  void IncrementEpoch() {
    u64 old_epoch = epoch();
    x_ += 1;
    DCHECK_EQ(old_epoch + 1, epoch());
    (void)old_epoch;
  }

  void SetIgnoreBit() { x_ |= kIgnoreBit; }
  void ClearIgnoreBit() { x_ &= ~kIgnoreBit; }
  bool GetIgnoreBit() const { return (s64)x_ < 0; }

  void SetHistorySize(int hs) {
    CHECK_GE(hs, 0);
    CHECK_LE(hs, 7);
    x_ = (x_ & ~(kHistoryMask << kHistoryShift)) | (u64(hs) << kHistoryShift);
  }

  ALWAYS_INLINE
  int GetHistorySize() const {
    return (int)((x_ >> kHistoryShift) & kHistoryMask);
  }

  void ClearHistorySize() {
    SetHistorySize(0);
  }

  ALWAYS_INLINE
  u64 GetTracePos() const {
    const int hs = GetHistorySize();
    // When hs == 0, the trace consists of 2 parts.
    const u64 mask = (1ull << (kTracePartSizeBits + hs + 1)) - 1;
    return epoch() & mask;
  }

 private:
  friend class Shadow;
  static const int kTidShift = 64 - kTidBits - 1;
  static const u64 kIgnoreBit = 1ull << 63;
  static const u64 kFreedBit = 1ull << 63;
  static const u64 kHistoryShift = kClkBits;
  static const u64 kHistoryMask = 7;
  u64 x_;
};

// Shadow (from most significant bit):
//   freed           : 1
//   tid             : kTidBits
//   is_atomic       : 1
//   is_read         : 1
//   size_log        : 2
//   addr0           : 3
//   epoch           : kClkBits
class Shadow : public FastState {
 public:
  explicit Shadow(u64 x)
      : FastState(x) {
  }

  explicit Shadow(const FastState &s)
      : FastState(s.x_) {
    ClearHistorySize();
  }

  void SetAddr0AndSizeLog(u64 addr0, unsigned kAccessSizeLog) {
    DCHECK_EQ((x_ >> kClkBits) & 31, 0);
    DCHECK_LE(addr0, 7);
    DCHECK_LE(kAccessSizeLog, 3);
    x_ |= ((kAccessSizeLog << 3) | addr0) << kClkBits;
    DCHECK_EQ(kAccessSizeLog, size_log());
    DCHECK_EQ(addr0, this->addr0());
  }

  void SetWrite(unsigned kAccessIsWrite) {
    DCHECK_EQ(x_ & kReadBit, 0);
    if (!kAccessIsWrite)
      x_ |= kReadBit;
    DCHECK_EQ(kAccessIsWrite, IsWrite());
  }

  void SetAtomic(bool kIsAtomic) {
    DCHECK(!IsAtomic());
    if (kIsAtomic)
      x_ |= kAtomicBit;
    DCHECK_EQ(IsAtomic(), kIsAtomic);
  }

  bool IsAtomic() const {
    return x_ & kAtomicBit;
  }

  bool IsZero() const {
    return x_ == 0;
  }

  static inline bool TidsAreEqual(const Shadow s1, const Shadow s2) {
    u64 shifted_xor = (s1.x_ ^ s2.x_) >> kTidShift;
    DCHECK_EQ(shifted_xor == 0, s1.TidWithIgnore() == s2.TidWithIgnore());
    return shifted_xor == 0;
  }

  static ALWAYS_INLINE
  bool Addr0AndSizeAreEqual(const Shadow s1, const Shadow s2) {
    u64 masked_xor = ((s1.x_ ^ s2.x_) >> kClkBits) & 31;
    return masked_xor == 0;
  }

  static ALWAYS_INLINE bool TwoRangesIntersect(Shadow s1, Shadow s2,
      unsigned kS2AccessSize) {
    bool res = false;
    u64 diff = s1.addr0() - s2.addr0();
    if ((s64)diff < 0) {  // s1.addr0 < s2.addr0  // NOLINT
      // if (s1.addr0() + size1) > s2.addr0()) return true;
      if (s1.size() > -diff)
        res = true;
    } else {
      // if (s2.addr0() + kS2AccessSize > s1.addr0()) return true;
      if (kS2AccessSize > diff)
        res = true;
    }
    DCHECK_EQ(res, TwoRangesIntersectSlow(s1, s2));
    DCHECK_EQ(res, TwoRangesIntersectSlow(s2, s1));
    return res;
  }

  u64 ALWAYS_INLINE addr0() const { return (x_ >> kClkBits) & 7; }
  u64 ALWAYS_INLINE size() const { return 1ull << size_log(); }
  bool ALWAYS_INLINE IsWrite() const { return !IsRead(); }
  bool ALWAYS_INLINE IsRead() const { return x_ & kReadBit; }

  // The idea behind the freed bit is as follows.
  // When the memory is freed (or otherwise unaccessible) we write to the shadow
  // values with tid/epoch related to the free and the freed bit set.
  // During memory accesses processing the freed bit is considered
  // as msb of tid. So any access races with shadow with freed bit set
  // (it is as if write from a thread with which we never synchronized before).
  // This allows us to detect accesses to freed memory w/o additional
  // overheads in memory access processing and at the same time restore
  // tid/epoch of free.
  void MarkAsFreed() {
     x_ |= kFreedBit;
  }

  bool IsFreed() const {
    return x_ & kFreedBit;
  }

  bool GetFreedAndReset() {
    bool res = x_ & kFreedBit;
    x_ &= ~kFreedBit;
    return res;
  }

  bool ALWAYS_INLINE IsBothReadsOrAtomic(bool kIsWrite, bool kIsAtomic) const {
    bool v = x_ & ((u64(kIsWrite ^ 1) << kReadShift)
        | (u64(kIsAtomic) << kAtomicShift));
    DCHECK_EQ(v, (!IsWrite() && !kIsWrite) || (IsAtomic() && kIsAtomic));
    return v;
  }

  bool ALWAYS_INLINE IsRWNotWeaker(bool kIsWrite, bool kIsAtomic) const {
    bool v = ((x_ >> kReadShift) & 3)
        <= u64((kIsWrite ^ 1) | (kIsAtomic << 1));
    DCHECK_EQ(v, (IsAtomic() < kIsAtomic) ||
        (IsAtomic() == kIsAtomic && !IsWrite() <= !kIsWrite));
    return v;
  }

  bool ALWAYS_INLINE IsRWWeakerOrEqual(bool kIsWrite, bool kIsAtomic) const {
    bool v = ((x_ >> kReadShift) & 3)
        >= u64((kIsWrite ^ 1) | (kIsAtomic << 1));
    DCHECK_EQ(v, (IsAtomic() > kIsAtomic) ||
        (IsAtomic() == kIsAtomic && !IsWrite() >= !kIsWrite));
    return v;
  }

 private:
  static const u64 kReadShift   = 5 + kClkBits;
  static const u64 kReadBit     = 1ull << kReadShift;
  static const u64 kAtomicShift = 6 + kClkBits;
  static const u64 kAtomicBit   = 1ull << kAtomicShift;

  u64 size_log() const { return (x_ >> (3 + kClkBits)) & 3; }

  static bool TwoRangesIntersectSlow(const Shadow s1, const Shadow s2) {
    if (s1.addr0() == s2.addr0()) return true;
    if (s1.addr0() < s2.addr0() && s1.addr0() + s1.size() > s2.addr0())
      return true;
    if (s2.addr0() < s1.addr0() && s2.addr0() + s2.size() > s1.addr0())
      return true;
    return false;
  }
};

struct ThreadSignalContext;

struct JmpBuf {
  uptr sp;
  uptr mangled_sp;
  int int_signal_send;
  bool in_blocking_func;
  uptr in_signal_handler;
  uptr *shadow_stack_pos;
};

// A Processor represents a physical thread, or a P for Go.
// It is used to store internal resources like allocate cache, and does not
// participate in race-detection logic (invisible to end user).
// In C++ it is tied to an OS thread just like ThreadState, however ideally
// it should be tied to a CPU (this way we will have fewer allocator caches).
// In Go it is tied to a P, so there are significantly fewer Processor's than
// ThreadState's (which are tied to Gs).
// A ThreadState must be wired with a Processor to handle events.
struct Processor {
  ThreadState *thr; // currently wired thread, or nullptr
#if !SANITIZER_GO
  AllocatorCache alloc_cache;
  InternalAllocatorCache internal_alloc_cache;
#endif
  DenseSlabAllocCache block_cache;
  DenseSlabAllocCache sync_cache;
  DenseSlabAllocCache clock_cache;
  DDPhysicalThread *dd_pt;
};

#if !SANITIZER_GO
// ScopedGlobalProcessor temporary setups a global processor for the current
// thread, if it does not have one. Intended for interceptors that can run
// at the very thread end, when we already destroyed the thread processor.
struct ScopedGlobalProcessor {
  ScopedGlobalProcessor();
  ~ScopedGlobalProcessor();
};
#endif

// This struct is stored in TLS.
struct ThreadState {
  FastState fast_state;
  // Synch epoch represents the threads's epoch before the last synchronization
  // action. It allows to reduce number of shadow state updates.
  // For example, fast_synch_epoch=100, last write to addr X was at epoch=150,
  // if we are processing write to X from the same thread at epoch=200,
  // we do nothing, because both writes happen in the same 'synch epoch'.
  // That is, if another memory access does not race with the former write,
  // it does not race with the latter as well.
  // QUESTION: can we can squeeze this into ThreadState::Fast?
  // E.g. ThreadState::Fast is a 44-bit, 32 are taken by synch_epoch and 12 are
  // taken by epoch between synchs.
  // This way we can save one load from tls.
  u64 fast_synch_epoch;
  // This is a slow path flag. On fast path, fast_state.GetIgnoreBit() is read.
  // We do not distinguish beteween ignoring reads and writes
  // for better performance.
  int ignore_reads_and_writes;
  int ignore_sync;
  int suppress_reports;
  // Go does not support ignores.
#if !SANITIZER_GO
  IgnoreSet mop_ignore_set;
  IgnoreSet sync_ignore_set;
#endif
  // C/C++ uses fixed size shadow stack embed into Trace.
  // Go uses malloc-allocated shadow stack with dynamic size.
  uptr *shadow_stack;
  uptr *shadow_stack_end;
  uptr *shadow_stack_pos;
  u64 *racy_shadow_addr;
  u64 racy_state[2];
  MutexSet mset;
  ThreadClock clock;
#if !SANITIZER_GO
  Vector<JmpBuf> jmp_bufs;
  int ignore_interceptors;
#endif
#if TSAN_COLLECT_STATS
  u64 stat[StatCnt];
#endif
  const int tid;
  const int unique_id;
  bool in_symbolizer;
  bool in_ignored_lib;
  bool is_inited;
  bool is_dead;
  bool is_freeing;
  bool is_vptr_access;
  const uptr stk_addr;
  const uptr stk_size;
  const uptr tls_addr;
  const uptr tls_size;
  ThreadContext *tctx;

#if SANITIZER_DEBUG && !SANITIZER_GO
  InternalDeadlockDetector internal_deadlock_detector;
#endif
  DDLogicalThread *dd_lt;

  // Current wired Processor, or nullptr. Required to handle any events.
  Processor *proc1;
#if !SANITIZER_GO
  Processor *proc() { return proc1; }
#else
  Processor *proc();
#endif

  atomic_uintptr_t in_signal_handler;
  ThreadSignalContext *signal_ctx;

#if !SANITIZER_GO
  u32 last_sleep_stack_id;
  ThreadClock last_sleep_clock;
#endif

  // Set in regions of runtime that must be signal-safe and fork-safe.
  // If set, malloc must not be called.
  int nomalloc;

  const ReportDesc *current_report;

  explicit ThreadState(Context *ctx, int tid, int unique_id, u64 epoch,
                       unsigned reuse_count,
                       uptr stk_addr, uptr stk_size,
                       uptr tls_addr, uptr tls_size);
};

#if !SANITIZER_GO
#if SANITIZER_MAC || SANITIZER_ANDROID
ThreadState *cur_thread();
void cur_thread_finalize();
#else
__attribute__((tls_model("initial-exec")))
extern THREADLOCAL char cur_thread_placeholder[];
INLINE ThreadState *cur_thread() {
  return reinterpret_cast<ThreadState *>(&cur_thread_placeholder);
}
INLINE void cur_thread_finalize() { }
#endif  // SANITIZER_MAC || SANITIZER_ANDROID
#endif  // SANITIZER_GO

class ThreadContext : public ThreadContextBase {
 public:
  explicit ThreadContext(int tid);
  ~ThreadContext();
  ThreadState *thr;
  u32 creation_stack_id;
  SyncClock sync;
  // Epoch at which the thread had started.
  // If we see an event from the thread stamped by an older epoch,
  // the event is from a dead thread that shared tid with this thread.
  u64 epoch0;
  u64 epoch1;

  // Override superclass callbacks.
  void OnDead() override;
  void OnJoined(void *arg) override;
  void OnFinished() override;
  void OnStarted(void *arg) override;
  void OnCreated(void *arg) override;
  void OnReset() override;
  void OnDetached(void *arg) override;
};

struct RacyStacks {
  MD5Hash hash[2];
  bool operator==(const RacyStacks &other) const {
    if (hash[0] == other.hash[0] && hash[1] == other.hash[1])
      return true;
    if (hash[0] == other.hash[1] && hash[1] == other.hash[0])
      return true;
    return false;
  }
};

struct RacyAddress {
  uptr addr_min;
  uptr addr_max;
};

struct FiredSuppression {
  ReportType type;
  uptr pc_or_addr;
  Suppression *supp;
};

struct Context {
  Context();

  bool initialized;
#if !SANITIZER_GO
  bool after_multithreaded_fork;
#endif

  MetaMap metamap;

  Mutex report_mtx;
  int nreported;
  int nmissed_expected;
  atomic_uint64_t last_symbolize_time_ns;

  void *background_thread;
  atomic_uint32_t stop_background_thread;

  ThreadRegistry *thread_registry;

  Mutex racy_mtx;
  Vector<RacyStacks> racy_stacks;
  Vector<RacyAddress> racy_addresses;
  // Number of fired suppressions may be large enough.
  Mutex fired_suppressions_mtx;
  InternalMmapVector<FiredSuppression> fired_suppressions;
  DDetector *dd;

  ClockAlloc clock_alloc;

  Flags flags;

  u64 stat[StatCnt];
  u64 int_alloc_cnt[MBlockTypeCount];
  u64 int_alloc_siz[MBlockTypeCount];
};

extern Context *ctx;  // The one and the only global runtime context.

ALWAYS_INLINE Flags *flags() {
  return &ctx->flags;
}

struct ScopedIgnoreInterceptors {
  ScopedIgnoreInterceptors() {
#if !SANITIZER_GO
    cur_thread()->ignore_interceptors++;
#endif
  }

  ~ScopedIgnoreInterceptors() {
#if !SANITIZER_GO
    cur_thread()->ignore_interceptors--;
#endif
  }
};

const char *GetObjectTypeFromTag(uptr tag);
const char *GetReportHeaderFromTag(uptr tag);
uptr TagFromShadowStackFrame(uptr pc);

class ScopedReportBase {
 public:
  void AddMemoryAccess(uptr addr, uptr external_tag, Shadow s, StackTrace stack,
                       const MutexSet *mset);
  void AddStack(StackTrace stack, bool suppressable = false);
  void AddThread(const ThreadContext *tctx, bool suppressable = false);
  void AddThread(int unique_tid, bool suppressable = false);
  void AddUniqueTid(int unique_tid);
  void AddMutex(const SyncVar *s);
  u64 AddMutex(u64 id);
  void AddLocation(uptr addr, uptr size);
  void AddSleep(u32 stack_id);
  void SetCount(int count);

  const ReportDesc *GetReport() const;

 protected:
  ScopedReportBase(ReportType typ, uptr tag);
  ~ScopedReportBase();

 private:
  ReportDesc *rep_;
  // Symbolizer makes lots of intercepted calls. If we try to process them,
  // at best it will cause deadlocks on internal mutexes.
  ScopedIgnoreInterceptors ignore_interceptors_;

  void AddDeadMutex(u64 id);

  ScopedReportBase(const ScopedReportBase &) = delete;
  void operator=(const ScopedReportBase &) = delete;
};

class ScopedReport : public ScopedReportBase {
 public:
  explicit ScopedReport(ReportType typ, uptr tag = kExternalTagNone);
  ~ScopedReport();

 private:
  ScopedErrorReportLock lock_;
};

ThreadContext *IsThreadStackOrTls(uptr addr, bool *is_stack);
void RestoreStack(int tid, const u64 epoch, VarSizeStackTrace *stk,
                  MutexSet *mset, uptr *tag = nullptr);

// The stack could look like:
//   <start> | <main> | <foo> | tag | <bar>
// This will extract the tag and keep:
//   <start> | <main> | <foo> | <bar>
template<typename StackTraceTy>
void ExtractTagFromStack(StackTraceTy *stack, uptr *tag = nullptr) {
  if (stack->size < 2) return;
  uptr possible_tag_pc = stack->trace[stack->size - 2];
  uptr possible_tag = TagFromShadowStackFrame(possible_tag_pc);
  if (possible_tag == kExternalTagNone) return;
  stack->trace_buffer[stack->size - 2] = stack->trace_buffer[stack->size - 1];
  stack->size -= 1;
  if (tag) *tag = possible_tag;
}

template<typename StackTraceTy>
void ObtainCurrentStack(ThreadState *thr, uptr toppc, StackTraceTy *stack,
                        uptr *tag = nullptr) {
  uptr size = thr->shadow_stack_pos - thr->shadow_stack;
  uptr start = 0;
  if (size + !!toppc > kStackTraceMax) {
    start = size + !!toppc - kStackTraceMax;
    size = kStackTraceMax - !!toppc;
  }
  stack->Init(&thr->shadow_stack[start], size, toppc);
  ExtractTagFromStack(stack, tag);
}

#define GET_STACK_TRACE_FATAL(thr, pc) \
  VarSizeStackTrace stack; \
  ObtainCurrentStack(thr, pc, &stack); \
  stack.ReverseOrder();

#if TSAN_COLLECT_STATS
void StatAggregate(u64 *dst, u64 *src);
void StatOutput(u64 *stat);
#endif

void ALWAYS_INLINE StatInc(ThreadState *thr, StatType typ, u64 n = 1) {
#if TSAN_COLLECT_STATS
  thr->stat[typ] += n;
#endif
}
void ALWAYS_INLINE StatSet(ThreadState *thr, StatType typ, u64 n) {
#if TSAN_COLLECT_STATS
  thr->stat[typ] = n;
#endif
}

void MapShadow(uptr addr, uptr size);
void MapThreadTrace(uptr addr, uptr size, const char *name);
void DontNeedShadowFor(uptr addr, uptr size);
void InitializeShadowMemory();
void InitializeInterceptors();
void InitializeLibIgnore();
void InitializeDynamicAnnotations();

void ForkBefore(ThreadState *thr, uptr pc);
void ForkParentAfter(ThreadState *thr, uptr pc);
void ForkChildAfter(ThreadState *thr, uptr pc);

void ReportRace(ThreadState *thr);
bool OutputReport(ThreadState *thr, const ScopedReport &srep);
bool IsFiredSuppression(Context *ctx, ReportType type, StackTrace trace);
bool IsExpectedReport(uptr addr, uptr size);
void PrintMatchedBenignRaces();

#if defined(TSAN_DEBUG_OUTPUT) && TSAN_DEBUG_OUTPUT >= 1
# define DPrintf Printf
#else
# define DPrintf(...)
#endif

#if defined(TSAN_DEBUG_OUTPUT) && TSAN_DEBUG_OUTPUT >= 2
# define DPrintf2 Printf
#else
# define DPrintf2(...)
#endif

u32 CurrentStackId(ThreadState *thr, uptr pc);
ReportStack *SymbolizeStackId(u32 stack_id);
void PrintCurrentStack(ThreadState *thr, uptr pc);
void PrintCurrentStackSlow(uptr pc);  // uses libunwind

void Initialize(ThreadState *thr);
void MaybeSpawnBackgroundThread();
int Finalize(ThreadState *thr);

void OnUserAlloc(ThreadState *thr, uptr pc, uptr p, uptr sz, bool write);
void OnUserFree(ThreadState *thr, uptr pc, uptr p, bool write);

void MemoryAccess(ThreadState *thr, uptr pc, uptr addr,
    int kAccessSizeLog, bool kAccessIsWrite, bool kIsAtomic);
void MemoryAccessImpl(ThreadState *thr, uptr addr,
    int kAccessSizeLog, bool kAccessIsWrite, bool kIsAtomic,
    u64 *shadow_mem, Shadow cur);
void MemoryAccessRange(ThreadState *thr, uptr pc, uptr addr,
    uptr size, bool is_write);
void MemoryAccessRangeStep(ThreadState *thr, uptr pc, uptr addr,
    uptr size, uptr step, bool is_write);
void UnalignedMemoryAccess(ThreadState *thr, uptr pc, uptr addr,
    int size, bool kAccessIsWrite, bool kIsAtomic);

const int kSizeLog1 = 0;
const int kSizeLog2 = 1;
const int kSizeLog4 = 2;
const int kSizeLog8 = 3;

void ALWAYS_INLINE MemoryRead(ThreadState *thr, uptr pc,
                                     uptr addr, int kAccessSizeLog) {
  MemoryAccess(thr, pc, addr, kAccessSizeLog, false, false);
}

void ALWAYS_INLINE MemoryWrite(ThreadState *thr, uptr pc,
                                      uptr addr, int kAccessSizeLog) {
  MemoryAccess(thr, pc, addr, kAccessSizeLog, true, false);
}

void ALWAYS_INLINE MemoryReadAtomic(ThreadState *thr, uptr pc,
                                           uptr addr, int kAccessSizeLog) {
  MemoryAccess(thr, pc, addr, kAccessSizeLog, false, true);
}

void ALWAYS_INLINE MemoryWriteAtomic(ThreadState *thr, uptr pc,
                                            uptr addr, int kAccessSizeLog) {
  MemoryAccess(thr, pc, addr, kAccessSizeLog, true, true);
}

void MemoryResetRange(ThreadState *thr, uptr pc, uptr addr, uptr size);
void MemoryRangeFreed(ThreadState *thr, uptr pc, uptr addr, uptr size);
void MemoryRangeImitateWrite(ThreadState *thr, uptr pc, uptr addr, uptr size);

void ThreadIgnoreBegin(ThreadState *thr, uptr pc, bool save_stack = true);
void ThreadIgnoreEnd(ThreadState *thr, uptr pc);
void ThreadIgnoreSyncBegin(ThreadState *thr, uptr pc, bool save_stack = true);
void ThreadIgnoreSyncEnd(ThreadState *thr, uptr pc);

void FuncEntry(ThreadState *thr, uptr pc);
void FuncExit(ThreadState *thr);

int ThreadCreate(ThreadState *thr, uptr pc, uptr uid, bool detached);
void ThreadStart(ThreadState *thr, int tid, tid_t os_id, bool workerthread);
void ThreadFinish(ThreadState *thr);
int ThreadTid(ThreadState *thr, uptr pc, uptr uid);
void ThreadJoin(ThreadState *thr, uptr pc, int tid);
void ThreadDetach(ThreadState *thr, uptr pc, int tid);
void ThreadFinalize(ThreadState *thr);
void ThreadSetName(ThreadState *thr, const char *name);
int ThreadCount(ThreadState *thr);
void ProcessPendingSignals(ThreadState *thr);
void ThreadNotJoined(ThreadState *thr, uptr pc, int tid, uptr uid);

Processor *ProcCreate();
void ProcDestroy(Processor *proc);
void ProcWire(Processor *proc, ThreadState *thr);
void ProcUnwire(Processor *proc, ThreadState *thr);

// Note: the parameter is called flagz, because flags is already taken
// by the global function that returns flags.
void MutexCreate(ThreadState *thr, uptr pc, uptr addr, u32 flagz = 0);
void MutexDestroy(ThreadState *thr, uptr pc, uptr addr, u32 flagz = 0);
void MutexPreLock(ThreadState *thr, uptr pc, uptr addr, u32 flagz = 0);
void MutexPostLock(ThreadState *thr, uptr pc, uptr addr, u32 flagz = 0,
    int rec = 1);
int  MutexUnlock(ThreadState *thr, uptr pc, uptr addr, u32 flagz = 0);
void MutexPreReadLock(ThreadState *thr, uptr pc, uptr addr, u32 flagz = 0);
void MutexPostReadLock(ThreadState *thr, uptr pc, uptr addr, u32 flagz = 0);
void MutexReadUnlock(ThreadState *thr, uptr pc, uptr addr);
void MutexReadOrWriteUnlock(ThreadState *thr, uptr pc, uptr addr);
void MutexRepair(ThreadState *thr, uptr pc, uptr addr);  // call on EOWNERDEAD
void MutexInvalidAccess(ThreadState *thr, uptr pc, uptr addr);

void Acquire(ThreadState *thr, uptr pc, uptr addr);
// AcquireGlobal synchronizes the current thread with all other threads.
// In terms of happens-before relation, it draws a HB edge from all threads
// (where they happen to execute right now) to the current thread. We use it to
// handle Go finalizers. Namely, finalizer goroutine executes AcquireGlobal
// right before executing finalizers. This provides a coarse, but simple
// approximation of the actual required synchronization.
void AcquireGlobal(ThreadState *thr, uptr pc);
void Release(ThreadState *thr, uptr pc, uptr addr);
void ReleaseStore(ThreadState *thr, uptr pc, uptr addr);
void AfterSleep(ThreadState *thr, uptr pc);
void AcquireImpl(ThreadState *thr, uptr pc, SyncClock *c);
void ReleaseImpl(ThreadState *thr, uptr pc, SyncClock *c);
void ReleaseStoreImpl(ThreadState *thr, uptr pc, SyncClock *c);
void AcquireReleaseImpl(ThreadState *thr, uptr pc, SyncClock *c);

// The hacky call uses custom calling convention and an assembly thunk.
// It is considerably faster that a normal call for the caller
// if it is not executed (it is intended for slow paths from hot functions).
// The trick is that the call preserves all registers and the compiler
// does not treat it as a call.
// If it does not work for you, use normal call.
#if !SANITIZER_DEBUG && defined(__x86_64__) && !SANITIZER_MAC
// The caller may not create the stack frame for itself at all,
// so we create a reserve stack frame for it (1024b must be enough).
#define HACKY_CALL(f) \
  __asm__ __volatile__("sub $1024, %%rsp;" \
                       CFI_INL_ADJUST_CFA_OFFSET(1024) \
                       ".hidden " #f "_thunk;" \
                       "call " #f "_thunk;" \
                       "add $1024, %%rsp;" \
                       CFI_INL_ADJUST_CFA_OFFSET(-1024) \
                       ::: "memory", "cc");
#else
#define HACKY_CALL(f) f()
#endif

void TraceSwitch(ThreadState *thr);
uptr TraceTopPC(ThreadState *thr);
uptr TraceSize();
uptr TraceParts();
Trace *ThreadTrace(int tid);

extern "C" void __tsan_trace_switch();
void ALWAYS_INLINE TraceAddEvent(ThreadState *thr, FastState fs,
                                        EventType typ, u64 addr) {
  if (!kCollectHistory)
    return;
  DCHECK_GE((int)typ, 0);
  DCHECK_LE((int)typ, 7);
  DCHECK_EQ(GetLsb(addr, kEventPCBits), addr);
  StatInc(thr, StatEvents);
  u64 pos = fs.GetTracePos();
  if (UNLIKELY((pos % kTracePartSize) == 0)) {
#if !SANITIZER_GO
    HACKY_CALL(__tsan_trace_switch);
#else
    TraceSwitch(thr);
#endif
  }
  Event *trace = (Event*)GetThreadTrace(fs.tid());
  Event *evp = &trace[pos];
  Event ev = (u64)addr | ((u64)typ << kEventPCBits);
  *evp = ev;
}

#if !SANITIZER_GO
uptr ALWAYS_INLINE HeapEnd() {
  return HeapMemEnd() + PrimaryAllocator::AdditionalSize();
}
#endif

}  // namespace __tsan

#endif  // TSAN_RTL_H

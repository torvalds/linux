//=-- lsan_common.cc ------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// Implementation of common leak checking functionality.
//
//===----------------------------------------------------------------------===//

#include "lsan_common.h"

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_procmaps.h"
#include "sanitizer_common/sanitizer_report_decorator.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "sanitizer_common/sanitizer_suppressions.h"
#include "sanitizer_common/sanitizer_thread_registry.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"

#if CAN_SANITIZE_LEAKS
namespace __lsan {

// This mutex is used to prevent races between DoLeakCheck and IgnoreObject, and
// also to protect the global list of root regions.
BlockingMutex global_mutex(LINKER_INITIALIZED);

Flags lsan_flags;

void DisableCounterUnderflow() {
  if (common_flags()->detect_leaks) {
    Report("Unmatched call to __lsan_enable().\n");
    Die();
  }
}

void Flags::SetDefaults() {
#define LSAN_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "lsan_flags.inc"
#undef LSAN_FLAG
}

void RegisterLsanFlags(FlagParser *parser, Flags *f) {
#define LSAN_FLAG(Type, Name, DefaultValue, Description) \
  RegisterFlag(parser, #Name, Description, &f->Name);
#include "lsan_flags.inc"
#undef LSAN_FLAG
}

#define LOG_POINTERS(...)                           \
  do {                                              \
    if (flags()->log_pointers) Report(__VA_ARGS__); \
  } while (0)

#define LOG_THREADS(...)                           \
  do {                                             \
    if (flags()->log_threads) Report(__VA_ARGS__); \
  } while (0)

ALIGNED(64) static char suppression_placeholder[sizeof(SuppressionContext)];
static SuppressionContext *suppression_ctx = nullptr;
static const char kSuppressionLeak[] = "leak";
static const char *kSuppressionTypes[] = { kSuppressionLeak };
static const char kStdSuppressions[] =
#if SANITIZER_SUPPRESS_LEAK_ON_PTHREAD_EXIT
  // For more details refer to the SANITIZER_SUPPRESS_LEAK_ON_PTHREAD_EXIT
  // definition.
  "leak:*pthread_exit*\n"
#endif  // SANITIZER_SUPPRESS_LEAK_ON_PTHREAD_EXIT
#if SANITIZER_MAC
  // For Darwin and os_log/os_trace: https://reviews.llvm.org/D35173
  "leak:*_os_trace*\n"
#endif
  // TLS leak in some glibc versions, described in
  // https://sourceware.org/bugzilla/show_bug.cgi?id=12650.
  "leak:*tls_get_addr*\n";

void InitializeSuppressions() {
  CHECK_EQ(nullptr, suppression_ctx);
  suppression_ctx = new (suppression_placeholder) // NOLINT
      SuppressionContext(kSuppressionTypes, ARRAY_SIZE(kSuppressionTypes));
  suppression_ctx->ParseFromFile(flags()->suppressions);
  if (&__lsan_default_suppressions)
    suppression_ctx->Parse(__lsan_default_suppressions());
  suppression_ctx->Parse(kStdSuppressions);
}

static SuppressionContext *GetSuppressionContext() {
  CHECK(suppression_ctx);
  return suppression_ctx;
}

static InternalMmapVector<RootRegion> *root_regions;

InternalMmapVector<RootRegion> const *GetRootRegions() { return root_regions; }

void InitializeRootRegions() {
  CHECK(!root_regions);
  ALIGNED(64) static char placeholder[sizeof(InternalMmapVector<RootRegion>)];
  root_regions = new (placeholder) InternalMmapVector<RootRegion>();  // NOLINT
}

const char *MaybeCallLsanDefaultOptions() {
  return (&__lsan_default_options) ? __lsan_default_options() : "";
}

void InitCommonLsan() {
  InitializeRootRegions();
  if (common_flags()->detect_leaks) {
    // Initialization which can fail or print warnings should only be done if
    // LSan is actually enabled.
    InitializeSuppressions();
    InitializePlatformSpecificModules();
  }
}

class Decorator: public __sanitizer::SanitizerCommonDecorator {
 public:
  Decorator() : SanitizerCommonDecorator() { }
  const char *Error() { return Red(); }
  const char *Leak() { return Blue(); }
};

static inline bool CanBeAHeapPointer(uptr p) {
  // Since our heap is located in mmap-ed memory, we can assume a sensible lower
  // bound on heap addresses.
  const uptr kMinAddress = 4 * 4096;
  if (p < kMinAddress) return false;
#if defined(__x86_64__)
  // Accept only canonical form user-space addresses.
  return ((p >> 47) == 0);
#elif defined(__mips64)
  return ((p >> 40) == 0);
#elif defined(__aarch64__)
  unsigned runtimeVMA =
    (MostSignificantSetBitIndex(GET_CURRENT_FRAME()) + 1);
  return ((p >> runtimeVMA) == 0);
#else
  return true;
#endif
}

// Scans the memory range, looking for byte patterns that point into allocator
// chunks. Marks those chunks with |tag| and adds them to |frontier|.
// There are two usage modes for this function: finding reachable chunks
// (|tag| = kReachable) and finding indirectly leaked chunks
// (|tag| = kIndirectlyLeaked). In the second case, there's no flood fill,
// so |frontier| = 0.
void ScanRangeForPointers(uptr begin, uptr end,
                          Frontier *frontier,
                          const char *region_type, ChunkTag tag) {
  CHECK(tag == kReachable || tag == kIndirectlyLeaked);
  const uptr alignment = flags()->pointer_alignment();
  LOG_POINTERS("Scanning %s range %p-%p.\n", region_type, begin, end);
  uptr pp = begin;
  if (pp % alignment)
    pp = pp + alignment - pp % alignment;
  for (; pp + sizeof(void *) <= end; pp += alignment) {  // NOLINT
    void *p = *reinterpret_cast<void **>(pp);
    if (!CanBeAHeapPointer(reinterpret_cast<uptr>(p))) continue;
    uptr chunk = PointsIntoChunk(p);
    if (!chunk) continue;
    // Pointers to self don't count. This matters when tag == kIndirectlyLeaked.
    if (chunk == begin) continue;
    LsanMetadata m(chunk);
    if (m.tag() == kReachable || m.tag() == kIgnored) continue;

    // Do this check relatively late so we can log only the interesting cases.
    if (!flags()->use_poisoned && WordIsPoisoned(pp)) {
      LOG_POINTERS(
          "%p is poisoned: ignoring %p pointing into chunk %p-%p of size "
          "%zu.\n",
          pp, p, chunk, chunk + m.requested_size(), m.requested_size());
      continue;
    }

    m.set_tag(tag);
    LOG_POINTERS("%p: found %p pointing into chunk %p-%p of size %zu.\n", pp, p,
                 chunk, chunk + m.requested_size(), m.requested_size());
    if (frontier)
      frontier->push_back(chunk);
  }
}

// Scans a global range for pointers
void ScanGlobalRange(uptr begin, uptr end, Frontier *frontier) {
  uptr allocator_begin = 0, allocator_end = 0;
  GetAllocatorGlobalRange(&allocator_begin, &allocator_end);
  if (begin <= allocator_begin && allocator_begin < end) {
    CHECK_LE(allocator_begin, allocator_end);
    CHECK_LE(allocator_end, end);
    if (begin < allocator_begin)
      ScanRangeForPointers(begin, allocator_begin, frontier, "GLOBAL",
                           kReachable);
    if (allocator_end < end)
      ScanRangeForPointers(allocator_end, end, frontier, "GLOBAL", kReachable);
  } else {
    ScanRangeForPointers(begin, end, frontier, "GLOBAL", kReachable);
  }
}

void ForEachExtraStackRangeCb(uptr begin, uptr end, void* arg) {
  Frontier *frontier = reinterpret_cast<Frontier *>(arg);
  ScanRangeForPointers(begin, end, frontier, "FAKE STACK", kReachable);
}

// Scans thread data (stacks and TLS) for heap pointers.
static void ProcessThreads(SuspendedThreadsList const &suspended_threads,
                           Frontier *frontier) {
  InternalMmapVector<uptr> registers(suspended_threads.RegisterCount());
  uptr registers_begin = reinterpret_cast<uptr>(registers.data());
  uptr registers_end =
      reinterpret_cast<uptr>(registers.data() + registers.size());
  for (uptr i = 0; i < suspended_threads.ThreadCount(); i++) {
    tid_t os_id = static_cast<tid_t>(suspended_threads.GetThreadID(i));
    LOG_THREADS("Processing thread %d.\n", os_id);
    uptr stack_begin, stack_end, tls_begin, tls_end, cache_begin, cache_end;
    DTLS *dtls;
    bool thread_found = GetThreadRangesLocked(os_id, &stack_begin, &stack_end,
                                              &tls_begin, &tls_end,
                                              &cache_begin, &cache_end, &dtls);
    if (!thread_found) {
      // If a thread can't be found in the thread registry, it's probably in the
      // process of destruction. Log this event and move on.
      LOG_THREADS("Thread %d not found in registry.\n", os_id);
      continue;
    }
    uptr sp;
    PtraceRegistersStatus have_registers =
        suspended_threads.GetRegistersAndSP(i, registers.data(), &sp);
    if (have_registers != REGISTERS_AVAILABLE) {
      Report("Unable to get registers from thread %d.\n", os_id);
      // If unable to get SP, consider the entire stack to be reachable unless
      // GetRegistersAndSP failed with ESRCH.
      if (have_registers == REGISTERS_UNAVAILABLE_FATAL) continue;
      sp = stack_begin;
    }

    if (flags()->use_registers && have_registers)
      ScanRangeForPointers(registers_begin, registers_end, frontier,
                           "REGISTERS", kReachable);

    if (flags()->use_stacks) {
      LOG_THREADS("Stack at %p-%p (SP = %p).\n", stack_begin, stack_end, sp);
      if (sp < stack_begin || sp >= stack_end) {
        // SP is outside the recorded stack range (e.g. the thread is running a
        // signal handler on alternate stack, or swapcontext was used).
        // Again, consider the entire stack range to be reachable.
        LOG_THREADS("WARNING: stack pointer not in stack range.\n");
        uptr page_size = GetPageSizeCached();
        int skipped = 0;
        while (stack_begin < stack_end &&
               !IsAccessibleMemoryRange(stack_begin, 1)) {
          skipped++;
          stack_begin += page_size;
        }
        LOG_THREADS("Skipped %d guard page(s) to obtain stack %p-%p.\n",
                    skipped, stack_begin, stack_end);
      } else {
        // Shrink the stack range to ignore out-of-scope values.
        stack_begin = sp;
      }
      ScanRangeForPointers(stack_begin, stack_end, frontier, "STACK",
                           kReachable);
      ForEachExtraStackRange(os_id, ForEachExtraStackRangeCb, frontier);
    }

    if (flags()->use_tls) {
      if (tls_begin) {
        LOG_THREADS("TLS at %p-%p.\n", tls_begin, tls_end);
        // If the tls and cache ranges don't overlap, scan full tls range,
        // otherwise, only scan the non-overlapping portions
        if (cache_begin == cache_end || tls_end < cache_begin ||
            tls_begin > cache_end) {
          ScanRangeForPointers(tls_begin, tls_end, frontier, "TLS", kReachable);
        } else {
          if (tls_begin < cache_begin)
            ScanRangeForPointers(tls_begin, cache_begin, frontier, "TLS",
                                 kReachable);
          if (tls_end > cache_end)
            ScanRangeForPointers(cache_end, tls_end, frontier, "TLS",
                                 kReachable);
        }
      }
      if (dtls && !DTLSInDestruction(dtls)) {
        for (uptr j = 0; j < dtls->dtv_size; ++j) {
          uptr dtls_beg = dtls->dtv[j].beg;
          uptr dtls_end = dtls_beg + dtls->dtv[j].size;
          if (dtls_beg < dtls_end) {
            LOG_THREADS("DTLS %zu at %p-%p.\n", j, dtls_beg, dtls_end);
            ScanRangeForPointers(dtls_beg, dtls_end, frontier, "DTLS",
                                 kReachable);
          }
        }
      } else {
        // We are handling a thread with DTLS under destruction. Log about
        // this and continue.
        LOG_THREADS("Thread %d has DTLS under destruction.\n", os_id);
      }
    }
  }
}

void ScanRootRegion(Frontier *frontier, const RootRegion &root_region,
                    uptr region_begin, uptr region_end, bool is_readable) {
  uptr intersection_begin = Max(root_region.begin, region_begin);
  uptr intersection_end = Min(region_end, root_region.begin + root_region.size);
  if (intersection_begin >= intersection_end) return;
  LOG_POINTERS("Root region %p-%p intersects with mapped region %p-%p (%s)\n",
               root_region.begin, root_region.begin + root_region.size,
               region_begin, region_end,
               is_readable ? "readable" : "unreadable");
  if (is_readable)
    ScanRangeForPointers(intersection_begin, intersection_end, frontier, "ROOT",
                         kReachable);
}

static void ProcessRootRegion(Frontier *frontier,
                              const RootRegion &root_region) {
  MemoryMappingLayout proc_maps(/*cache_enabled*/ true);
  MemoryMappedSegment segment;
  while (proc_maps.Next(&segment)) {
    ScanRootRegion(frontier, root_region, segment.start, segment.end,
                   segment.IsReadable());
  }
}

// Scans root regions for heap pointers.
static void ProcessRootRegions(Frontier *frontier) {
  if (!flags()->use_root_regions) return;
  CHECK(root_regions);
  for (uptr i = 0; i < root_regions->size(); i++) {
    ProcessRootRegion(frontier, (*root_regions)[i]);
  }
}

static void FloodFillTag(Frontier *frontier, ChunkTag tag) {
  while (frontier->size()) {
    uptr next_chunk = frontier->back();
    frontier->pop_back();
    LsanMetadata m(next_chunk);
    ScanRangeForPointers(next_chunk, next_chunk + m.requested_size(), frontier,
                         "HEAP", tag);
  }
}

// ForEachChunk callback. If the chunk is marked as leaked, marks all chunks
// which are reachable from it as indirectly leaked.
static void MarkIndirectlyLeakedCb(uptr chunk, void *arg) {
  chunk = GetUserBegin(chunk);
  LsanMetadata m(chunk);
  if (m.allocated() && m.tag() != kReachable) {
    ScanRangeForPointers(chunk, chunk + m.requested_size(),
                         /* frontier */ nullptr, "HEAP", kIndirectlyLeaked);
  }
}

// ForEachChunk callback. If chunk is marked as ignored, adds its address to
// frontier.
static void CollectIgnoredCb(uptr chunk, void *arg) {
  CHECK(arg);
  chunk = GetUserBegin(chunk);
  LsanMetadata m(chunk);
  if (m.allocated() && m.tag() == kIgnored) {
    LOG_POINTERS("Ignored: chunk %p-%p of size %zu.\n",
                 chunk, chunk + m.requested_size(), m.requested_size());
    reinterpret_cast<Frontier *>(arg)->push_back(chunk);
  }
}

static uptr GetCallerPC(u32 stack_id, StackDepotReverseMap *map) {
  CHECK(stack_id);
  StackTrace stack = map->Get(stack_id);
  // The top frame is our malloc/calloc/etc. The next frame is the caller.
  if (stack.size >= 2)
    return stack.trace[1];
  return 0;
}

struct InvalidPCParam {
  Frontier *frontier;
  StackDepotReverseMap *stack_depot_reverse_map;
  bool skip_linker_allocations;
};

// ForEachChunk callback. If the caller pc is invalid or is within the linker,
// mark as reachable. Called by ProcessPlatformSpecificAllocations.
static void MarkInvalidPCCb(uptr chunk, void *arg) {
  CHECK(arg);
  InvalidPCParam *param = reinterpret_cast<InvalidPCParam *>(arg);
  chunk = GetUserBegin(chunk);
  LsanMetadata m(chunk);
  if (m.allocated() && m.tag() != kReachable && m.tag() != kIgnored) {
    u32 stack_id = m.stack_trace_id();
    uptr caller_pc = 0;
    if (stack_id > 0)
      caller_pc = GetCallerPC(stack_id, param->stack_depot_reverse_map);
    // If caller_pc is unknown, this chunk may be allocated in a coroutine. Mark
    // it as reachable, as we can't properly report its allocation stack anyway.
    if (caller_pc == 0 || (param->skip_linker_allocations &&
                           GetLinker()->containsAddress(caller_pc))) {
      m.set_tag(kReachable);
      param->frontier->push_back(chunk);
    }
  }
}

// On Linux, treats all chunks allocated from ld-linux.so as reachable, which
// covers dynamically allocated TLS blocks, internal dynamic loader's loaded
// modules accounting etc.
// Dynamic TLS blocks contain the TLS variables of dynamically loaded modules.
// They are allocated with a __libc_memalign() call in allocate_and_init()
// (elf/dl-tls.c). Glibc won't tell us the address ranges occupied by those
// blocks, but we can make sure they come from our own allocator by intercepting
// __libc_memalign(). On top of that, there is no easy way to reach them. Their
// addresses are stored in a dynamically allocated array (the DTV) which is
// referenced from the static TLS. Unfortunately, we can't just rely on the DTV
// being reachable from the static TLS, and the dynamic TLS being reachable from
// the DTV. This is because the initial DTV is allocated before our interception
// mechanism kicks in, and thus we don't recognize it as allocated memory. We
// can't special-case it either, since we don't know its size.
// Our solution is to include in the root set all allocations made from
// ld-linux.so (which is where allocate_and_init() is implemented). This is
// guaranteed to include all dynamic TLS blocks (and possibly other allocations
// which we don't care about).
// On all other platforms, this simply checks to ensure that the caller pc is
// valid before reporting chunks as leaked.
void ProcessPC(Frontier *frontier) {
  StackDepotReverseMap stack_depot_reverse_map;
  InvalidPCParam arg;
  arg.frontier = frontier;
  arg.stack_depot_reverse_map = &stack_depot_reverse_map;
  arg.skip_linker_allocations =
      flags()->use_tls && flags()->use_ld_allocations && GetLinker() != nullptr;
  ForEachChunk(MarkInvalidPCCb, &arg);
}

// Sets the appropriate tag on each chunk.
static void ClassifyAllChunks(SuspendedThreadsList const &suspended_threads) {
  // Holds the flood fill frontier.
  Frontier frontier;

  ForEachChunk(CollectIgnoredCb, &frontier);
  ProcessGlobalRegions(&frontier);
  ProcessThreads(suspended_threads, &frontier);
  ProcessRootRegions(&frontier);
  FloodFillTag(&frontier, kReachable);

  CHECK_EQ(0, frontier.size());
  ProcessPC(&frontier);

  // The check here is relatively expensive, so we do this in a separate flood
  // fill. That way we can skip the check for chunks that are reachable
  // otherwise.
  LOG_POINTERS("Processing platform-specific allocations.\n");
  ProcessPlatformSpecificAllocations(&frontier);
  FloodFillTag(&frontier, kReachable);

  // Iterate over leaked chunks and mark those that are reachable from other
  // leaked chunks.
  LOG_POINTERS("Scanning leaked chunks.\n");
  ForEachChunk(MarkIndirectlyLeakedCb, nullptr);
}

// ForEachChunk callback. Resets the tags to pre-leak-check state.
static void ResetTagsCb(uptr chunk, void *arg) {
  (void)arg;
  chunk = GetUserBegin(chunk);
  LsanMetadata m(chunk);
  if (m.allocated() && m.tag() != kIgnored)
    m.set_tag(kDirectlyLeaked);
}

static void PrintStackTraceById(u32 stack_trace_id) {
  CHECK(stack_trace_id);
  StackDepotGet(stack_trace_id).Print();
}

// ForEachChunk callback. Aggregates information about unreachable chunks into
// a LeakReport.
static void CollectLeaksCb(uptr chunk, void *arg) {
  CHECK(arg);
  LeakReport *leak_report = reinterpret_cast<LeakReport *>(arg);
  chunk = GetUserBegin(chunk);
  LsanMetadata m(chunk);
  if (!m.allocated()) return;
  if (m.tag() == kDirectlyLeaked || m.tag() == kIndirectlyLeaked) {
    u32 resolution = flags()->resolution;
    u32 stack_trace_id = 0;
    if (resolution > 0) {
      StackTrace stack = StackDepotGet(m.stack_trace_id());
      stack.size = Min(stack.size, resolution);
      stack_trace_id = StackDepotPut(stack);
    } else {
      stack_trace_id = m.stack_trace_id();
    }
    leak_report->AddLeakedChunk(chunk, stack_trace_id, m.requested_size(),
                                m.tag());
  }
}

static void PrintMatchedSuppressions() {
  InternalMmapVector<Suppression *> matched;
  GetSuppressionContext()->GetMatched(&matched);
  if (!matched.size())
    return;
  const char *line = "-----------------------------------------------------";
  Printf("%s\n", line);
  Printf("Suppressions used:\n");
  Printf("  count      bytes template\n");
  for (uptr i = 0; i < matched.size(); i++)
    Printf("%7zu %10zu %s\n", static_cast<uptr>(atomic_load_relaxed(
        &matched[i]->hit_count)), matched[i]->weight, matched[i]->templ);
  Printf("%s\n\n", line);
}

struct CheckForLeaksParam {
  bool success;
  LeakReport leak_report;
};

static void ReportIfNotSuspended(ThreadContextBase *tctx, void *arg) {
  const InternalMmapVector<tid_t> &suspended_threads =
      *(const InternalMmapVector<tid_t> *)arg;
  if (tctx->status == ThreadStatusRunning) {
    uptr i = InternalLowerBound(suspended_threads, 0, suspended_threads.size(),
                                tctx->os_id, CompareLess<int>());
    if (i >= suspended_threads.size() || suspended_threads[i] != tctx->os_id)
      Report("Running thread %d was not suspended. False leaks are possible.\n",
             tctx->os_id);
  };
}

static void ReportUnsuspendedThreads(
    const SuspendedThreadsList &suspended_threads) {
  InternalMmapVector<tid_t> threads(suspended_threads.ThreadCount());
  for (uptr i = 0; i < suspended_threads.ThreadCount(); ++i)
    threads[i] = suspended_threads.GetThreadID(i);

  Sort(threads.data(), threads.size());

  GetThreadRegistryLocked()->RunCallbackForEachThreadLocked(
      &ReportIfNotSuspended, &threads);
}

static void CheckForLeaksCallback(const SuspendedThreadsList &suspended_threads,
                                  void *arg) {
  CheckForLeaksParam *param = reinterpret_cast<CheckForLeaksParam *>(arg);
  CHECK(param);
  CHECK(!param->success);
  ReportUnsuspendedThreads(suspended_threads);
  ClassifyAllChunks(suspended_threads);
  ForEachChunk(CollectLeaksCb, &param->leak_report);
  // Clean up for subsequent leak checks. This assumes we did not overwrite any
  // kIgnored tags.
  ForEachChunk(ResetTagsCb, nullptr);
  param->success = true;
}

static bool CheckForLeaks() {
  if (&__lsan_is_turned_off && __lsan_is_turned_off())
      return false;
  EnsureMainThreadIDIsCorrect();
  CheckForLeaksParam param;
  param.success = false;
  LockThreadRegistry();
  LockAllocator();
  DoStopTheWorld(CheckForLeaksCallback, &param);
  UnlockAllocator();
  UnlockThreadRegistry();

  if (!param.success) {
    Report("LeakSanitizer has encountered a fatal error.\n");
    Report(
        "HINT: For debugging, try setting environment variable "
        "LSAN_OPTIONS=verbosity=1:log_threads=1\n");
    Report(
        "HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)\n");
    Die();
  }
  param.leak_report.ApplySuppressions();
  uptr unsuppressed_count = param.leak_report.UnsuppressedLeakCount();
  if (unsuppressed_count > 0) {
    Decorator d;
    Printf("\n"
           "================================================================="
           "\n");
    Printf("%s", d.Error());
    Report("ERROR: LeakSanitizer: detected memory leaks\n");
    Printf("%s", d.Default());
    param.leak_report.ReportTopLeaks(flags()->max_leaks);
  }
  if (common_flags()->print_suppressions)
    PrintMatchedSuppressions();
  if (unsuppressed_count > 0) {
    param.leak_report.PrintSummary();
    return true;
  }
  return false;
}

static bool has_reported_leaks = false;
bool HasReportedLeaks() { return has_reported_leaks; }

void DoLeakCheck() {
  BlockingMutexLock l(&global_mutex);
  static bool already_done;
  if (already_done) return;
  already_done = true;
  has_reported_leaks = CheckForLeaks();
  if (has_reported_leaks) HandleLeaks();
}

static int DoRecoverableLeakCheck() {
  BlockingMutexLock l(&global_mutex);
  bool have_leaks = CheckForLeaks();
  return have_leaks ? 1 : 0;
}

void DoRecoverableLeakCheckVoid() { DoRecoverableLeakCheck(); }

static Suppression *GetSuppressionForAddr(uptr addr) {
  Suppression *s = nullptr;

  // Suppress by module name.
  SuppressionContext *suppressions = GetSuppressionContext();
  if (const char *module_name =
          Symbolizer::GetOrInit()->GetModuleNameForPc(addr))
    if (suppressions->Match(module_name, kSuppressionLeak, &s))
      return s;

  // Suppress by file or function name.
  SymbolizedStack *frames = Symbolizer::GetOrInit()->SymbolizePC(addr);
  for (SymbolizedStack *cur = frames; cur; cur = cur->next) {
    if (suppressions->Match(cur->info.function, kSuppressionLeak, &s) ||
        suppressions->Match(cur->info.file, kSuppressionLeak, &s)) {
      break;
    }
  }
  frames->ClearAll();
  return s;
}

static Suppression *GetSuppressionForStack(u32 stack_trace_id) {
  StackTrace stack = StackDepotGet(stack_trace_id);
  for (uptr i = 0; i < stack.size; i++) {
    Suppression *s = GetSuppressionForAddr(
        StackTrace::GetPreviousInstructionPc(stack.trace[i]));
    if (s) return s;
  }
  return nullptr;
}

///// LeakReport implementation. /////

// A hard limit on the number of distinct leaks, to avoid quadratic complexity
// in LeakReport::AddLeakedChunk(). We don't expect to ever see this many leaks
// in real-world applications.
// FIXME: Get rid of this limit by changing the implementation of LeakReport to
// use a hash table.
const uptr kMaxLeaksConsidered = 5000;

void LeakReport::AddLeakedChunk(uptr chunk, u32 stack_trace_id,
                                uptr leaked_size, ChunkTag tag) {
  CHECK(tag == kDirectlyLeaked || tag == kIndirectlyLeaked);
  bool is_directly_leaked = (tag == kDirectlyLeaked);
  uptr i;
  for (i = 0; i < leaks_.size(); i++) {
    if (leaks_[i].stack_trace_id == stack_trace_id &&
        leaks_[i].is_directly_leaked == is_directly_leaked) {
      leaks_[i].hit_count++;
      leaks_[i].total_size += leaked_size;
      break;
    }
  }
  if (i == leaks_.size()) {
    if (leaks_.size() == kMaxLeaksConsidered) return;
    Leak leak = { next_id_++, /* hit_count */ 1, leaked_size, stack_trace_id,
                  is_directly_leaked, /* is_suppressed */ false };
    leaks_.push_back(leak);
  }
  if (flags()->report_objects) {
    LeakedObject obj = {leaks_[i].id, chunk, leaked_size};
    leaked_objects_.push_back(obj);
  }
}

static bool LeakComparator(const Leak &leak1, const Leak &leak2) {
  if (leak1.is_directly_leaked == leak2.is_directly_leaked)
    return leak1.total_size > leak2.total_size;
  else
    return leak1.is_directly_leaked;
}

void LeakReport::ReportTopLeaks(uptr num_leaks_to_report) {
  CHECK(leaks_.size() <= kMaxLeaksConsidered);
  Printf("\n");
  if (leaks_.size() == kMaxLeaksConsidered)
    Printf("Too many leaks! Only the first %zu leaks encountered will be "
           "reported.\n",
           kMaxLeaksConsidered);

  uptr unsuppressed_count = UnsuppressedLeakCount();
  if (num_leaks_to_report > 0 && num_leaks_to_report < unsuppressed_count)
    Printf("The %zu top leak(s):\n", num_leaks_to_report);
  Sort(leaks_.data(), leaks_.size(), &LeakComparator);
  uptr leaks_reported = 0;
  for (uptr i = 0; i < leaks_.size(); i++) {
    if (leaks_[i].is_suppressed) continue;
    PrintReportForLeak(i);
    leaks_reported++;
    if (leaks_reported == num_leaks_to_report) break;
  }
  if (leaks_reported < unsuppressed_count) {
    uptr remaining = unsuppressed_count - leaks_reported;
    Printf("Omitting %zu more leak(s).\n", remaining);
  }
}

void LeakReport::PrintReportForLeak(uptr index) {
  Decorator d;
  Printf("%s", d.Leak());
  Printf("%s leak of %zu byte(s) in %zu object(s) allocated from:\n",
         leaks_[index].is_directly_leaked ? "Direct" : "Indirect",
         leaks_[index].total_size, leaks_[index].hit_count);
  Printf("%s", d.Default());

  PrintStackTraceById(leaks_[index].stack_trace_id);

  if (flags()->report_objects) {
    Printf("Objects leaked above:\n");
    PrintLeakedObjectsForLeak(index);
    Printf("\n");
  }
}

void LeakReport::PrintLeakedObjectsForLeak(uptr index) {
  u32 leak_id = leaks_[index].id;
  for (uptr j = 0; j < leaked_objects_.size(); j++) {
    if (leaked_objects_[j].leak_id == leak_id)
      Printf("%p (%zu bytes)\n", leaked_objects_[j].addr,
             leaked_objects_[j].size);
  }
}

void LeakReport::PrintSummary() {
  CHECK(leaks_.size() <= kMaxLeaksConsidered);
  uptr bytes = 0, allocations = 0;
  for (uptr i = 0; i < leaks_.size(); i++) {
      if (leaks_[i].is_suppressed) continue;
      bytes += leaks_[i].total_size;
      allocations += leaks_[i].hit_count;
  }
  InternalScopedString summary(kMaxSummaryLength);
  summary.append("%zu byte(s) leaked in %zu allocation(s).", bytes,
                 allocations);
  ReportErrorSummary(summary.data());
}

void LeakReport::ApplySuppressions() {
  for (uptr i = 0; i < leaks_.size(); i++) {
    Suppression *s = GetSuppressionForStack(leaks_[i].stack_trace_id);
    if (s) {
      s->weight += leaks_[i].total_size;
      atomic_store_relaxed(&s->hit_count, atomic_load_relaxed(&s->hit_count) +
          leaks_[i].hit_count);
      leaks_[i].is_suppressed = true;
    }
  }
}

uptr LeakReport::UnsuppressedLeakCount() {
  uptr result = 0;
  for (uptr i = 0; i < leaks_.size(); i++)
    if (!leaks_[i].is_suppressed) result++;
  return result;
}

} // namespace __lsan
#else // CAN_SANITIZE_LEAKS
namespace __lsan {
void InitCommonLsan() { }
void DoLeakCheck() { }
void DoRecoverableLeakCheckVoid() { }
void DisableInThisThread() { }
void EnableInThisThread() { }
}
#endif // CAN_SANITIZE_LEAKS

using namespace __lsan;  // NOLINT

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __lsan_ignore_object(const void *p) {
#if CAN_SANITIZE_LEAKS
  if (!common_flags()->detect_leaks)
    return;
  // Cannot use PointsIntoChunk or LsanMetadata here, since the allocator is not
  // locked.
  BlockingMutexLock l(&global_mutex);
  IgnoreObjectResult res = IgnoreObjectLocked(p);
  if (res == kIgnoreObjectInvalid)
    VReport(1, "__lsan_ignore_object(): no heap object found at %p", p);
  if (res == kIgnoreObjectAlreadyIgnored)
    VReport(1, "__lsan_ignore_object(): "
           "heap object at %p is already being ignored\n", p);
  if (res == kIgnoreObjectSuccess)
    VReport(1, "__lsan_ignore_object(): ignoring heap object at %p\n", p);
#endif // CAN_SANITIZE_LEAKS
}

SANITIZER_INTERFACE_ATTRIBUTE
void __lsan_register_root_region(const void *begin, uptr size) {
#if CAN_SANITIZE_LEAKS
  BlockingMutexLock l(&global_mutex);
  CHECK(root_regions);
  RootRegion region = {reinterpret_cast<uptr>(begin), size};
  root_regions->push_back(region);
  VReport(1, "Registered root region at %p of size %llu\n", begin, size);
#endif // CAN_SANITIZE_LEAKS
}

SANITIZER_INTERFACE_ATTRIBUTE
void __lsan_unregister_root_region(const void *begin, uptr size) {
#if CAN_SANITIZE_LEAKS
  BlockingMutexLock l(&global_mutex);
  CHECK(root_regions);
  bool removed = false;
  for (uptr i = 0; i < root_regions->size(); i++) {
    RootRegion region = (*root_regions)[i];
    if (region.begin == reinterpret_cast<uptr>(begin) && region.size == size) {
      removed = true;
      uptr last_index = root_regions->size() - 1;
      (*root_regions)[i] = (*root_regions)[last_index];
      root_regions->pop_back();
      VReport(1, "Unregistered root region at %p of size %llu\n", begin, size);
      break;
    }
  }
  if (!removed) {
    Report(
        "__lsan_unregister_root_region(): region at %p of size %llu has not "
        "been registered.\n",
        begin, size);
    Die();
  }
#endif // CAN_SANITIZE_LEAKS
}

SANITIZER_INTERFACE_ATTRIBUTE
void __lsan_disable() {
#if CAN_SANITIZE_LEAKS
  __lsan::DisableInThisThread();
#endif
}

SANITIZER_INTERFACE_ATTRIBUTE
void __lsan_enable() {
#if CAN_SANITIZE_LEAKS
  __lsan::EnableInThisThread();
#endif
}

SANITIZER_INTERFACE_ATTRIBUTE
void __lsan_do_leak_check() {
#if CAN_SANITIZE_LEAKS
  if (common_flags()->detect_leaks)
    __lsan::DoLeakCheck();
#endif // CAN_SANITIZE_LEAKS
}

SANITIZER_INTERFACE_ATTRIBUTE
int __lsan_do_recoverable_leak_check() {
#if CAN_SANITIZE_LEAKS
  if (common_flags()->detect_leaks)
    return __lsan::DoRecoverableLeakCheck();
#endif // CAN_SANITIZE_LEAKS
  return 0;
}

#if !SANITIZER_SUPPORTS_WEAK_HOOKS
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE
const char * __lsan_default_options() {
  return "";
}

SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE
int __lsan_is_turned_off() {
  return 0;
}

SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE
const char *__lsan_default_suppressions() {
  return "";
}
#endif
} // extern "C"

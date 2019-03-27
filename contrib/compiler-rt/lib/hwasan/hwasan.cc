//===-- hwasan.cc ---------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
// HWAddressSanitizer runtime.
//===----------------------------------------------------------------------===//

#include "hwasan.h"
#include "hwasan_checks.h"
#include "hwasan_poisoning.h"
#include "hwasan_report.h"
#include "hwasan_thread.h"
#include "hwasan_thread_list.h"
#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_procmaps.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "sanitizer_common/sanitizer_symbolizer.h"
#include "ubsan/ubsan_flags.h"
#include "ubsan/ubsan_init.h"

// ACHTUNG! No system header includes in this file.

using namespace __sanitizer;

namespace __hwasan {

void EnterSymbolizer() {
  Thread *t = GetCurrentThread();
  CHECK(t);
  t->EnterSymbolizer();
}
void ExitSymbolizer() {
  Thread *t = GetCurrentThread();
  CHECK(t);
  t->LeaveSymbolizer();
}
bool IsInSymbolizer() {
  Thread *t = GetCurrentThread();
  return t && t->InSymbolizer();
}

static Flags hwasan_flags;

Flags *flags() {
  return &hwasan_flags;
}

int hwasan_inited = 0;
int hwasan_shadow_inited = 0;
bool hwasan_init_is_running;

int hwasan_report_count = 0;

void Flags::SetDefaults() {
#define HWASAN_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "hwasan_flags.inc"
#undef HWASAN_FLAG
}

static void RegisterHwasanFlags(FlagParser *parser, Flags *f) {
#define HWASAN_FLAG(Type, Name, DefaultValue, Description) \
  RegisterFlag(parser, #Name, Description, &f->Name);
#include "hwasan_flags.inc"
#undef HWASAN_FLAG
}

static void InitializeFlags() {
  SetCommonFlagsDefaults();
  {
    CommonFlags cf;
    cf.CopyFrom(*common_flags());
    cf.external_symbolizer_path = GetEnv("HWASAN_SYMBOLIZER_PATH");
    cf.malloc_context_size = 20;
    cf.handle_ioctl = true;
    // FIXME: test and enable.
    cf.check_printf = false;
    cf.intercept_tls_get_addr = true;
    cf.exitcode = 99;
    // Sigtrap is used in error reporting.
    cf.handle_sigtrap = kHandleSignalExclusive;

#if SANITIZER_ANDROID
    // Let platform handle other signals. It is better at reporting them then we
    // are.
    cf.handle_segv = kHandleSignalNo;
    cf.handle_sigbus = kHandleSignalNo;
    cf.handle_abort = kHandleSignalNo;
    cf.handle_sigill = kHandleSignalNo;
    cf.handle_sigfpe = kHandleSignalNo;
#endif
    OverrideCommonFlags(cf);
  }

  Flags *f = flags();
  f->SetDefaults();

  FlagParser parser;
  RegisterHwasanFlags(&parser, f);
  RegisterCommonFlags(&parser);

#if HWASAN_CONTAINS_UBSAN
  __ubsan::Flags *uf = __ubsan::flags();
  uf->SetDefaults();

  FlagParser ubsan_parser;
  __ubsan::RegisterUbsanFlags(&ubsan_parser, uf);
  RegisterCommonFlags(&ubsan_parser);
#endif

  // Override from user-specified string.
  if (__hwasan_default_options)
    parser.ParseString(__hwasan_default_options());
#if HWASAN_CONTAINS_UBSAN
  const char *ubsan_default_options = __ubsan::MaybeCallUbsanDefaultOptions();
  ubsan_parser.ParseString(ubsan_default_options);
#endif

  const char *hwasan_options = GetEnv("HWASAN_OPTIONS");
  parser.ParseString(hwasan_options);
#if HWASAN_CONTAINS_UBSAN
  ubsan_parser.ParseString(GetEnv("UBSAN_OPTIONS"));
#endif
  VPrintf(1, "HWASAN_OPTIONS: %s\n",
          hwasan_options ? hwasan_options : "<empty>");

  InitializeCommonFlags();

  if (Verbosity()) ReportUnrecognizedFlags();

  if (common_flags()->help) parser.PrintFlagDescriptions();
}

void GetStackTrace(BufferedStackTrace *stack, uptr max_s, uptr pc, uptr bp,
                   void *context, bool request_fast_unwind) {
  Thread *t = GetCurrentThread();
  if (!t) {
    // the thread is still being created.
    stack->size = 0;
    return;
  }
  if (!StackTrace::WillUseFastUnwind(request_fast_unwind)) {
    // Block reports from our interceptors during _Unwind_Backtrace.
    SymbolizerScope sym_scope;
    return stack->Unwind(max_s, pc, bp, context, 0, 0, request_fast_unwind);
  }
  stack->Unwind(max_s, pc, bp, context, t->stack_top(), t->stack_bottom(),
                request_fast_unwind);
}

static void HWAsanCheckFailed(const char *file, int line, const char *cond,
                              u64 v1, u64 v2) {
  Report("HWAddressSanitizer CHECK failed: %s:%d \"%s\" (0x%zx, 0x%zx)\n", file,
         line, cond, (uptr)v1, (uptr)v2);
  PRINT_CURRENT_STACK_CHECK();
  Die();
}

static constexpr uptr kMemoryUsageBufferSize = 4096;

static void HwasanFormatMemoryUsage(InternalScopedString &s) {
  HwasanThreadList &thread_list = hwasanThreadList();
  auto thread_stats = thread_list.GetThreadStats();
  auto *sds = StackDepotGetStats();
  AllocatorStatCounters asc;
  GetAllocatorStats(asc);
  s.append(
      "HWASAN pid: %d rss: %zd threads: %zd stacks: %zd"
      " thr_aux: %zd stack_depot: %zd uniq_stacks: %zd"
      " heap: %zd",
      internal_getpid(), GetRSS(), thread_stats.n_live_threads,
      thread_stats.total_stack_size,
      thread_stats.n_live_threads * thread_list.MemoryUsedPerThread(),
      sds->allocated, sds->n_uniq_ids, asc[AllocatorStatMapped]);
}

#if SANITIZER_ANDROID
static char *memory_usage_buffer = nullptr;

#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_ANON_NAME 0

static void InitMemoryUsage() {
  memory_usage_buffer =
      (char *)MmapOrDie(kMemoryUsageBufferSize, "memory usage string");
  CHECK(memory_usage_buffer);
  memory_usage_buffer[0] = '\0';
  CHECK(internal_prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME,
                       (uptr)memory_usage_buffer, kMemoryUsageBufferSize,
                       (uptr)memory_usage_buffer) == 0);
}

void UpdateMemoryUsage() {
  if (!flags()->export_memory_stats)
    return;
  if (!memory_usage_buffer)
    InitMemoryUsage();
  InternalScopedString s(kMemoryUsageBufferSize);
  HwasanFormatMemoryUsage(s);
  internal_strncpy(memory_usage_buffer, s.data(), kMemoryUsageBufferSize - 1);
  memory_usage_buffer[kMemoryUsageBufferSize - 1] = '\0';
}
#else
void UpdateMemoryUsage() {}
#endif

struct FrameDescription {
  uptr PC;
  const char *Descr;
};

struct FrameDescriptionArray {
  FrameDescription *beg, *end;
};

static InternalMmapVectorNoCtor<FrameDescriptionArray> AllFrames;

void InitFrameDescriptors(uptr b, uptr e) {
  FrameDescription *beg = reinterpret_cast<FrameDescription *>(b);
  FrameDescription *end = reinterpret_cast<FrameDescription *>(e);
  if (beg == end)
    return;
  AllFrames.push_back({beg, end});
  if (Verbosity())
    for (FrameDescription *frame_descr = beg; frame_descr < end; frame_descr++)
      Printf("Frame: %p %s\n", frame_descr->PC, frame_descr->Descr);
}

const char *GetStackFrameDescr(uptr pc) {
  for (uptr i = 0, n = AllFrames.size(); i < n; i++)
    for (auto p = AllFrames[i].beg; p < AllFrames[i].end; p++)
      if (p->PC == pc)
        return p->Descr;
  return nullptr;
}

} // namespace __hwasan

// Interface.

using namespace __hwasan;

uptr __hwasan_shadow_memory_dynamic_address;  // Global interface symbol.

void __hwasan_shadow_init() {
  if (hwasan_shadow_inited) return;
  if (!InitShadow()) {
    Printf("FATAL: HWAddressSanitizer cannot mmap the shadow memory.\n");
    DumpProcessMap();
    Die();
  }
  hwasan_shadow_inited = 1;
}

void __hwasan_init_frames(uptr beg, uptr end) {
  InitFrameDescriptors(beg, end);
}

void __hwasan_init() {
  CHECK(!hwasan_init_is_running);
  if (hwasan_inited) return;
  hwasan_init_is_running = 1;
  SanitizerToolName = "HWAddressSanitizer";

  InitTlsSize();

  CacheBinaryName();
  InitializeFlags();

  // Install tool-specific callbacks in sanitizer_common.
  SetCheckFailedCallback(HWAsanCheckFailed);

  __sanitizer_set_report_path(common_flags()->log_path);

  AndroidTestTlsSlot();

  DisableCoreDumperIfNecessary();

  __hwasan_shadow_init();

  InitThreads();
  hwasanThreadList().CreateCurrentThread();

  MadviseShadow();

  SetPrintfAndReportCallback(AppendToErrorMessageBuffer);
  // This may call libc -> needs initialized shadow.
  AndroidLogInit();

  InitializeInterceptors();
  InstallDeadlySignalHandlers(HwasanOnDeadlySignal);
  InstallAtExitHandler(); // Needs __cxa_atexit interceptor.

  Symbolizer::GetOrInit()->AddHooks(EnterSymbolizer, ExitSymbolizer);

  InitializeCoverage(common_flags()->coverage, common_flags()->coverage_dir);

  HwasanTSDInit();
  HwasanTSDThreadInit();

  HwasanAllocatorInit();

#if HWASAN_CONTAINS_UBSAN
  __ubsan::InitAsPlugin();
#endif

  VPrintf(1, "HWAddressSanitizer init done\n");

  hwasan_init_is_running = 0;
  hwasan_inited = 1;
}

void __hwasan_print_shadow(const void *p, uptr sz) {
  uptr ptr_raw = UntagAddr(reinterpret_cast<uptr>(p));
  uptr shadow_first = MemToShadow(ptr_raw);
  uptr shadow_last = MemToShadow(ptr_raw + sz - 1);
  Printf("HWASan shadow map for %zx .. %zx (pointer tag %x)\n", ptr_raw,
         ptr_raw + sz, GetTagFromPointer((uptr)p));
  for (uptr s = shadow_first; s <= shadow_last; ++s)
    Printf("  %zx: %x\n", ShadowToMem(s), *(tag_t *)s);
}

sptr __hwasan_test_shadow(const void *p, uptr sz) {
  if (sz == 0)
    return -1;
  tag_t ptr_tag = GetTagFromPointer((uptr)p);
  if (ptr_tag == 0)
    return -1;
  uptr ptr_raw = UntagAddr(reinterpret_cast<uptr>(p));
  uptr shadow_first = MemToShadow(ptr_raw);
  uptr shadow_last = MemToShadow(ptr_raw + sz - 1);
  for (uptr s = shadow_first; s <= shadow_last; ++s)
    if (*(tag_t*)s != ptr_tag)
      return ShadowToMem(s) - ptr_raw;
  return -1;
}

u16 __sanitizer_unaligned_load16(const uu16 *p) {
  return *p;
}
u32 __sanitizer_unaligned_load32(const uu32 *p) {
  return *p;
}
u64 __sanitizer_unaligned_load64(const uu64 *p) {
  return *p;
}
void __sanitizer_unaligned_store16(uu16 *p, u16 x) {
  *p = x;
}
void __sanitizer_unaligned_store32(uu32 *p, u32 x) {
  *p = x;
}
void __sanitizer_unaligned_store64(uu64 *p, u64 x) {
  *p = x;
}

void __hwasan_loadN(uptr p, uptr sz) {
  CheckAddressSized<ErrorAction::Abort, AccessType::Load>(p, sz);
}
void __hwasan_load1(uptr p) {
  CheckAddress<ErrorAction::Abort, AccessType::Load, 0>(p);
}
void __hwasan_load2(uptr p) {
  CheckAddress<ErrorAction::Abort, AccessType::Load, 1>(p);
}
void __hwasan_load4(uptr p) {
  CheckAddress<ErrorAction::Abort, AccessType::Load, 2>(p);
}
void __hwasan_load8(uptr p) {
  CheckAddress<ErrorAction::Abort, AccessType::Load, 3>(p);
}
void __hwasan_load16(uptr p) {
  CheckAddress<ErrorAction::Abort, AccessType::Load, 4>(p);
}

void __hwasan_loadN_noabort(uptr p, uptr sz) {
  CheckAddressSized<ErrorAction::Recover, AccessType::Load>(p, sz);
}
void __hwasan_load1_noabort(uptr p) {
  CheckAddress<ErrorAction::Recover, AccessType::Load, 0>(p);
}
void __hwasan_load2_noabort(uptr p) {
  CheckAddress<ErrorAction::Recover, AccessType::Load, 1>(p);
}
void __hwasan_load4_noabort(uptr p) {
  CheckAddress<ErrorAction::Recover, AccessType::Load, 2>(p);
}
void __hwasan_load8_noabort(uptr p) {
  CheckAddress<ErrorAction::Recover, AccessType::Load, 3>(p);
}
void __hwasan_load16_noabort(uptr p) {
  CheckAddress<ErrorAction::Recover, AccessType::Load, 4>(p);
}

void __hwasan_storeN(uptr p, uptr sz) {
  CheckAddressSized<ErrorAction::Abort, AccessType::Store>(p, sz);
}
void __hwasan_store1(uptr p) {
  CheckAddress<ErrorAction::Abort, AccessType::Store, 0>(p);
}
void __hwasan_store2(uptr p) {
  CheckAddress<ErrorAction::Abort, AccessType::Store, 1>(p);
}
void __hwasan_store4(uptr p) {
  CheckAddress<ErrorAction::Abort, AccessType::Store, 2>(p);
}
void __hwasan_store8(uptr p) {
  CheckAddress<ErrorAction::Abort, AccessType::Store, 3>(p);
}
void __hwasan_store16(uptr p) {
  CheckAddress<ErrorAction::Abort, AccessType::Store, 4>(p);
}

void __hwasan_storeN_noabort(uptr p, uptr sz) {
  CheckAddressSized<ErrorAction::Recover, AccessType::Store>(p, sz);
}
void __hwasan_store1_noabort(uptr p) {
  CheckAddress<ErrorAction::Recover, AccessType::Store, 0>(p);
}
void __hwasan_store2_noabort(uptr p) {
  CheckAddress<ErrorAction::Recover, AccessType::Store, 1>(p);
}
void __hwasan_store4_noabort(uptr p) {
  CheckAddress<ErrorAction::Recover, AccessType::Store, 2>(p);
}
void __hwasan_store8_noabort(uptr p) {
  CheckAddress<ErrorAction::Recover, AccessType::Store, 3>(p);
}
void __hwasan_store16_noabort(uptr p) {
  CheckAddress<ErrorAction::Recover, AccessType::Store, 4>(p);
}

void __hwasan_tag_memory(uptr p, u8 tag, uptr sz) {
  TagMemoryAligned(p, sz, tag);
}

uptr __hwasan_tag_pointer(uptr p, u8 tag) {
  return AddTagToPointer(p, tag);
}

void __hwasan_handle_longjmp(const void *sp_dst) {
  uptr dst = (uptr)sp_dst;
  // HWASan does not support tagged SP.
  CHECK(GetTagFromPointer(dst) == 0);

  uptr sp = (uptr)__builtin_frame_address(0);
  static const uptr kMaxExpectedCleanupSize = 64 << 20;  // 64M
  if (dst < sp || dst - sp > kMaxExpectedCleanupSize) {
    Report(
        "WARNING: HWASan is ignoring requested __hwasan_handle_longjmp: "
        "stack top: %p; target %p; distance: %p (%zd)\n"
        "False positive error reports may follow\n",
        (void *)sp, (void *)dst, dst - sp);
    return;
  }
  TagMemory(sp, dst - sp, 0);
}

void __hwasan_print_memory_usage() {
  InternalScopedString s(kMemoryUsageBufferSize);
  HwasanFormatMemoryUsage(s);
  Printf("%s\n", s.data());
}

static const u8 kFallbackTag = 0xBB;

u8 __hwasan_generate_tag() {
  Thread *t = GetCurrentThread();
  if (!t) return kFallbackTag;
  return t->GenerateRandomTag();
}

#if !SANITIZER_SUPPORTS_WEAK_HOOKS
extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE
const char* __hwasan_default_options() { return ""; }
}  // extern "C"
#endif

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_print_stack_trace() {
  GET_FATAL_STACK_TRACE_PC_BP(StackTrace::GetCurrentPc(), GET_CURRENT_FRAME());
  stack.Print();
}
} // extern "C"

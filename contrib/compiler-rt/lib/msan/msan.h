//===-- msan.h --------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
// Private MSan header.
//===----------------------------------------------------------------------===//

#ifndef MSAN_H
#define MSAN_H

#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "msan_interface_internal.h"
#include "msan_flags.h"
#include "ubsan/ubsan_platform.h"

#ifndef MSAN_REPLACE_OPERATORS_NEW_AND_DELETE
# define MSAN_REPLACE_OPERATORS_NEW_AND_DELETE 1
#endif

#ifndef MSAN_CONTAINS_UBSAN
# define MSAN_CONTAINS_UBSAN CAN_SANITIZE_UB
#endif

struct MappingDesc {
  uptr start;
  uptr end;
  enum Type {
    INVALID, APP, SHADOW, ORIGIN
  } type;
  const char *name;
};


#if SANITIZER_LINUX && defined(__mips64)

// MIPS64 maps:
// - 0x0000000000-0x0200000000: Program own segments
// - 0xa200000000-0xc000000000: PIE program segments
// - 0xe200000000-0xffffffffff: libraries segments.
const MappingDesc kMemoryLayout[] = {
    {0x000000000000ULL, 0x000200000000ULL, MappingDesc::APP, "app-1"},
    {0x000200000000ULL, 0x002200000000ULL, MappingDesc::INVALID, "invalid"},
    {0x002200000000ULL, 0x004000000000ULL, MappingDesc::SHADOW, "shadow-2"},
    {0x004000000000ULL, 0x004200000000ULL, MappingDesc::INVALID, "invalid"},
    {0x004200000000ULL, 0x006000000000ULL, MappingDesc::ORIGIN, "origin-2"},
    {0x006000000000ULL, 0x006200000000ULL, MappingDesc::INVALID, "invalid"},
    {0x006200000000ULL, 0x008000000000ULL, MappingDesc::SHADOW, "shadow-3"},
    {0x008000000000ULL, 0x008200000000ULL, MappingDesc::SHADOW, "shadow-1"},
    {0x008200000000ULL, 0x00a000000000ULL, MappingDesc::ORIGIN, "origin-3"},
    {0x00a000000000ULL, 0x00a200000000ULL, MappingDesc::ORIGIN, "origin-1"},
    {0x00a200000000ULL, 0x00c000000000ULL, MappingDesc::APP, "app-2"},
    {0x00c000000000ULL, 0x00e200000000ULL, MappingDesc::INVALID, "invalid"},
    {0x00e200000000ULL, 0x00ffffffffffULL, MappingDesc::APP, "app-3"}};

#define MEM_TO_SHADOW(mem) (((uptr)(mem)) ^ 0x8000000000ULL)
#define SHADOW_TO_ORIGIN(shadow) (((uptr)(shadow)) + 0x2000000000ULL)

#elif SANITIZER_LINUX && defined(__aarch64__)

// The mapping describes both 39-bits, 42-bits, and 48-bits VMA.  AArch64
// maps:
// - 0x0000000000000-0x0000010000000: 39/42/48-bits program own segments
// - 0x0005500000000-0x0005600000000: 39-bits PIE program segments
// - 0x0007f80000000-0x0007fffffffff: 39-bits libraries segments
// - 0x002aa00000000-0x002ab00000000: 42-bits PIE program segments
// - 0x003ff00000000-0x003ffffffffff: 42-bits libraries segments
// - 0x0aaaaa0000000-0x0aaab00000000: 48-bits PIE program segments
// - 0xffff000000000-0x1000000000000: 48-bits libraries segments
// It is fragmented in multiples segments to increase the memory available
// on 42-bits (12.21% of total VMA available for 42-bits and 13.28 for
// 39 bits). The 48-bits segments only cover the usual PIE/default segments
// plus some more segments (262144GB total, 0.39% total VMA).
const MappingDesc kMemoryLayout[] = {
    {0x00000000000ULL, 0x01000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x01000000000ULL, 0x02000000000ULL, MappingDesc::SHADOW, "shadow-2"},
    {0x02000000000ULL, 0x03000000000ULL, MappingDesc::ORIGIN, "origin-2"},
    {0x03000000000ULL, 0x04000000000ULL, MappingDesc::SHADOW, "shadow-1"},
    {0x04000000000ULL, 0x05000000000ULL, MappingDesc::ORIGIN, "origin-1"},
    {0x05000000000ULL, 0x06000000000ULL, MappingDesc::APP, "app-1"},
    {0x06000000000ULL, 0x07000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x07000000000ULL, 0x08000000000ULL, MappingDesc::APP, "app-2"},
    {0x08000000000ULL, 0x09000000000ULL, MappingDesc::INVALID, "invalid"},
    // The mappings below are used only for 42-bits VMA.
    {0x09000000000ULL, 0x0A000000000ULL, MappingDesc::SHADOW, "shadow-3"},
    {0x0A000000000ULL, 0x0B000000000ULL, MappingDesc::ORIGIN, "origin-3"},
    {0x0B000000000ULL, 0x0F000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x0F000000000ULL, 0x10000000000ULL, MappingDesc::APP, "app-3"},
    {0x10000000000ULL, 0x11000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x11000000000ULL, 0x12000000000ULL, MappingDesc::APP, "app-4"},
    {0x12000000000ULL, 0x17000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x17000000000ULL, 0x18000000000ULL, MappingDesc::SHADOW, "shadow-4"},
    {0x18000000000ULL, 0x19000000000ULL, MappingDesc::ORIGIN, "origin-4"},
    {0x19000000000ULL, 0x20000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x20000000000ULL, 0x21000000000ULL, MappingDesc::APP, "app-5"},
    {0x21000000000ULL, 0x26000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x26000000000ULL, 0x27000000000ULL, MappingDesc::SHADOW, "shadow-5"},
    {0x27000000000ULL, 0x28000000000ULL, MappingDesc::ORIGIN, "origin-5"},
    {0x28000000000ULL, 0x29000000000ULL, MappingDesc::SHADOW, "shadow-7"},
    {0x29000000000ULL, 0x2A000000000ULL, MappingDesc::ORIGIN, "origin-7"},
    {0x2A000000000ULL, 0x2B000000000ULL, MappingDesc::APP, "app-6"},
    {0x2B000000000ULL, 0x2C000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x2C000000000ULL, 0x2D000000000ULL, MappingDesc::SHADOW, "shadow-6"},
    {0x2D000000000ULL, 0x2E000000000ULL, MappingDesc::ORIGIN, "origin-6"},
    {0x2E000000000ULL, 0x2F000000000ULL, MappingDesc::APP, "app-7"},
    {0x2F000000000ULL, 0x39000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x39000000000ULL, 0x3A000000000ULL, MappingDesc::SHADOW, "shadow-9"},
    {0x3A000000000ULL, 0x3B000000000ULL, MappingDesc::ORIGIN, "origin-9"},
    {0x3B000000000ULL, 0x3C000000000ULL, MappingDesc::APP, "app-8"},
    {0x3C000000000ULL, 0x3D000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x3D000000000ULL, 0x3E000000000ULL, MappingDesc::SHADOW, "shadow-8"},
    {0x3E000000000ULL, 0x3F000000000ULL, MappingDesc::ORIGIN, "origin-8"},
    {0x3F000000000ULL, 0x40000000000ULL, MappingDesc::APP, "app-9"},
    // The mappings below are used only for 48-bits VMA.
    // TODO(unknown): 48-bit mapping ony covers the usual PIE, non-PIE
    // segments and some more segments totalizing 262144GB of VMA (which cover
    // only 0.32% of all 48-bit VMA). Memory avaliability can be increase by
    // adding multiple application segments like 39 and 42 mapping.
    {0x0040000000000ULL, 0x0041000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x0041000000000ULL, 0x0042000000000ULL, MappingDesc::APP, "app-10"},
    {0x0042000000000ULL, 0x0047000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x0047000000000ULL, 0x0048000000000ULL, MappingDesc::SHADOW, "shadow-10"},
    {0x0048000000000ULL, 0x0049000000000ULL, MappingDesc::ORIGIN, "origin-10"},
    {0x0049000000000ULL, 0x0050000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x0050000000000ULL, 0x0051000000000ULL, MappingDesc::APP, "app-11"},
    {0x0051000000000ULL, 0x0056000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x0056000000000ULL, 0x0057000000000ULL, MappingDesc::SHADOW, "shadow-11"},
    {0x0057000000000ULL, 0x0058000000000ULL, MappingDesc::ORIGIN, "origin-11"},
    {0x0058000000000ULL, 0x0059000000000ULL, MappingDesc::APP, "app-12"},
    {0x0059000000000ULL, 0x005E000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x005E000000000ULL, 0x005F000000000ULL, MappingDesc::SHADOW, "shadow-12"},
    {0x005F000000000ULL, 0x0060000000000ULL, MappingDesc::ORIGIN, "origin-12"},
    {0x0060000000000ULL, 0x0061000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x0061000000000ULL, 0x0062000000000ULL, MappingDesc::APP, "app-13"},
    {0x0062000000000ULL, 0x0067000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x0067000000000ULL, 0x0068000000000ULL, MappingDesc::SHADOW, "shadow-13"},
    {0x0068000000000ULL, 0x0069000000000ULL, MappingDesc::ORIGIN, "origin-13"},
    {0x0069000000000ULL, 0x0AAAAA0000000ULL, MappingDesc::INVALID, "invalid"},
    {0x0AAAAA0000000ULL, 0x0AAAB00000000ULL, MappingDesc::APP, "app-14"},
    {0x0AAAB00000000ULL, 0x0AACAA0000000ULL, MappingDesc::INVALID, "invalid"},
    {0x0AACAA0000000ULL, 0x0AACB00000000ULL, MappingDesc::SHADOW, "shadow-14"},
    {0x0AACB00000000ULL, 0x0AADAA0000000ULL, MappingDesc::INVALID, "invalid"},
    {0x0AADAA0000000ULL, 0x0AADB00000000ULL, MappingDesc::ORIGIN, "origin-14"},
    {0x0AADB00000000ULL, 0x0FF9F00000000ULL, MappingDesc::INVALID, "invalid"},
    {0x0FF9F00000000ULL, 0x0FFA000000000ULL, MappingDesc::SHADOW, "shadow-15"},
    {0x0FFA000000000ULL, 0x0FFAF00000000ULL, MappingDesc::INVALID, "invalid"},
    {0x0FFAF00000000ULL, 0x0FFB000000000ULL, MappingDesc::ORIGIN, "origin-15"},
    {0x0FFB000000000ULL, 0x0FFFF00000000ULL, MappingDesc::INVALID, "invalid"},
    {0x0FFFF00000000ULL, 0x1000000000000ULL, MappingDesc::APP, "app-15"},
};
# define MEM_TO_SHADOW(mem) ((uptr)mem ^ 0x6000000000ULL)
# define SHADOW_TO_ORIGIN(shadow) (((uptr)(shadow)) + 0x1000000000ULL)

#elif SANITIZER_LINUX && SANITIZER_PPC64
const MappingDesc kMemoryLayout[] = {
    {0x000000000000ULL, 0x000200000000ULL, MappingDesc::APP, "low memory"},
    {0x000200000000ULL, 0x080000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x080000000000ULL, 0x180200000000ULL, MappingDesc::SHADOW, "shadow"},
    {0x180200000000ULL, 0x1C0000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x1C0000000000ULL, 0x2C0200000000ULL, MappingDesc::ORIGIN, "origin"},
    {0x2C0200000000ULL, 0x300000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x300000000000ULL, 0x800000000000ULL, MappingDesc::APP, "high memory"}};

// Various kernels use different low end ranges but we can combine them into one
// big range. They also use different high end ranges but we can map them all to
// one range.
// Maps low and high app ranges to contiguous space with zero base:
//   Low:  0000 0000 0000 - 0001 ffff ffff  ->  1000 0000 0000 - 1001 ffff ffff
//   High: 3000 0000 0000 - 3fff ffff ffff  ->  0000 0000 0000 - 0fff ffff ffff
//   High: 4000 0000 0000 - 4fff ffff ffff  ->  0000 0000 0000 - 0fff ffff ffff
//   High: 7000 0000 0000 - 7fff ffff ffff  ->  0000 0000 0000 - 0fff ffff ffff
#define LINEARIZE_MEM(mem) \
  (((uptr)(mem) & ~0xE00000000000ULL) ^ 0x100000000000ULL)
#define MEM_TO_SHADOW(mem) (LINEARIZE_MEM((mem)) + 0x080000000000ULL)
#define SHADOW_TO_ORIGIN(shadow) (((uptr)(shadow)) + 0x140000000000ULL)

#elif SANITIZER_FREEBSD && SANITIZER_WORDSIZE == 64

// Low memory: main binary, MAP_32BIT mappings and modules
// High memory: heap, modules and main thread stack
const MappingDesc kMemoryLayout[] = {
    {0x000000000000ULL, 0x010000000000ULL, MappingDesc::APP, "low memory"},
    {0x010000000000ULL, 0x100000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x100000000000ULL, 0x310000000000ULL, MappingDesc::SHADOW, "shadow"},
    {0x310000000000ULL, 0x380000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x380000000000ULL, 0x590000000000ULL, MappingDesc::ORIGIN, "origin"},
    {0x590000000000ULL, 0x600000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x600000000000ULL, 0x800000000000ULL, MappingDesc::APP, "high memory"}};

// Maps low and high app ranges to contiguous space with zero base:
//   Low:  0000 0000 0000 - 00ff ffff ffff  ->  2000 0000 0000 - 20ff ffff ffff
//   High: 6000 0000 0000 - 7fff ffff ffff  ->  0000 0000 0000 - 1fff ffff ffff
#define LINEARIZE_MEM(mem) \
  (((uptr)(mem) & ~0xc00000000000ULL) ^ 0x200000000000ULL)
#define MEM_TO_SHADOW(mem) (LINEARIZE_MEM((mem)) + 0x100000000000ULL)
#define SHADOW_TO_ORIGIN(shadow) (((uptr)(shadow)) + 0x280000000000)

#elif SANITIZER_NETBSD || (SANITIZER_LINUX && SANITIZER_WORDSIZE == 64)

#ifdef MSAN_LINUX_X86_64_OLD_MAPPING
// Requries PIE binary and ASLR enabled.
// Main thread stack and DSOs at 0x7f0000000000 (sometimes 0x7e0000000000).
// Heap at 0x600000000000.
const MappingDesc kMemoryLayout[] = {
    {0x000000000000ULL, 0x200000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x200000000000ULL, 0x400000000000ULL, MappingDesc::SHADOW, "shadow"},
    {0x400000000000ULL, 0x600000000000ULL, MappingDesc::ORIGIN, "origin"},
    {0x600000000000ULL, 0x800000000000ULL, MappingDesc::APP, "app"}};

#define MEM_TO_SHADOW(mem) (((uptr)(mem)) & ~0x400000000000ULL)
#define SHADOW_TO_ORIGIN(mem) (((uptr)(mem)) + 0x200000000000ULL)
#else  // MSAN_LINUX_X86_64_OLD_MAPPING
// All of the following configurations are supported.
// ASLR disabled: main executable and DSOs at 0x555550000000
// PIE and ASLR: main executable and DSOs at 0x7f0000000000
// non-PIE: main executable below 0x100000000, DSOs at 0x7f0000000000
// Heap at 0x700000000000.
const MappingDesc kMemoryLayout[] = {
    {0x000000000000ULL, 0x010000000000ULL, MappingDesc::APP, "app-1"},
    {0x010000000000ULL, 0x100000000000ULL, MappingDesc::SHADOW, "shadow-2"},
    {0x100000000000ULL, 0x110000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x110000000000ULL, 0x200000000000ULL, MappingDesc::ORIGIN, "origin-2"},
    {0x200000000000ULL, 0x300000000000ULL, MappingDesc::SHADOW, "shadow-3"},
    {0x300000000000ULL, 0x400000000000ULL, MappingDesc::ORIGIN, "origin-3"},
    {0x400000000000ULL, 0x500000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x500000000000ULL, 0x510000000000ULL, MappingDesc::SHADOW, "shadow-1"},
    {0x510000000000ULL, 0x600000000000ULL, MappingDesc::APP, "app-2"},
    {0x600000000000ULL, 0x610000000000ULL, MappingDesc::ORIGIN, "origin-1"},
    {0x610000000000ULL, 0x700000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x700000000000ULL, 0x800000000000ULL, MappingDesc::APP, "app-3"}};
#define MEM_TO_SHADOW(mem) (((uptr)(mem)) ^ 0x500000000000ULL)
#define SHADOW_TO_ORIGIN(mem) (((uptr)(mem)) + 0x100000000000ULL)
#endif  // MSAN_LINUX_X86_64_OLD_MAPPING

#else
#error "Unsupported platform"
#endif

const uptr kMemoryLayoutSize = sizeof(kMemoryLayout) / sizeof(kMemoryLayout[0]);

#define MEM_TO_ORIGIN(mem) (SHADOW_TO_ORIGIN(MEM_TO_SHADOW((mem))))

#ifndef __clang__
__attribute__((optimize("unroll-loops")))
#endif
inline bool addr_is_type(uptr addr, MappingDesc::Type mapping_type) {
// It is critical for performance that this loop is unrolled (because then it is
// simplified into just a few constant comparisons).
#ifdef __clang__
#pragma unroll
#endif
  for (unsigned i = 0; i < kMemoryLayoutSize; ++i)
    if (kMemoryLayout[i].type == mapping_type &&
        addr >= kMemoryLayout[i].start && addr < kMemoryLayout[i].end)
      return true;
  return false;
}

#define MEM_IS_APP(mem) addr_is_type((uptr)(mem), MappingDesc::APP)
#define MEM_IS_SHADOW(mem) addr_is_type((uptr)(mem), MappingDesc::SHADOW)
#define MEM_IS_ORIGIN(mem) addr_is_type((uptr)(mem), MappingDesc::ORIGIN)

// These constants must be kept in sync with the ones in MemorySanitizer.cc.
const int kMsanParamTlsSize = 800;
const int kMsanRetvalTlsSize = 800;

namespace __msan {
extern int msan_inited;
extern bool msan_init_is_running;
extern int msan_report_count;

bool ProtectRange(uptr beg, uptr end);
bool InitShadow(bool init_origins);
char *GetProcSelfMaps();
void InitializeInterceptors();

void MsanAllocatorInit();
void MsanAllocatorThreadFinish();
void MsanDeallocate(StackTrace *stack, void *ptr);

void *msan_malloc(uptr size, StackTrace *stack);
void *msan_calloc(uptr nmemb, uptr size, StackTrace *stack);
void *msan_realloc(void *ptr, uptr size, StackTrace *stack);
void *msan_valloc(uptr size, StackTrace *stack);
void *msan_pvalloc(uptr size, StackTrace *stack);
void *msan_aligned_alloc(uptr alignment, uptr size, StackTrace *stack);
void *msan_memalign(uptr alignment, uptr size, StackTrace *stack);
int msan_posix_memalign(void **memptr, uptr alignment, uptr size,
                        StackTrace *stack);

void InstallTrapHandler();
void InstallAtExitHandler();

const char *GetStackOriginDescr(u32 id, uptr *pc);

void EnterSymbolizer();
void ExitSymbolizer();
bool IsInSymbolizer();

struct SymbolizerScope {
  SymbolizerScope() { EnterSymbolizer(); }
  ~SymbolizerScope() { ExitSymbolizer(); }
};

void PrintWarning(uptr pc, uptr bp);
void PrintWarningWithOrigin(uptr pc, uptr bp, u32 origin);

void GetStackTrace(BufferedStackTrace *stack, uptr max_s, uptr pc, uptr bp,
                   void *context, bool request_fast_unwind);

// Unpoison first n function arguments.
void UnpoisonParam(uptr n);
void UnpoisonThreadLocalState();

// Returns a "chained" origin id, pointing to the given stack trace followed by
// the previous origin id.
u32 ChainOrigin(u32 id, StackTrace *stack);

const int STACK_TRACE_TAG_POISON = StackTrace::TAG_CUSTOM + 1;

#define GET_MALLOC_STACK_TRACE                                            \
  BufferedStackTrace stack;                                               \
  if (__msan_get_track_origins() && msan_inited)                          \
  GetStackTrace(&stack, common_flags()->malloc_context_size,              \
                StackTrace::GetCurrentPc(), GET_CURRENT_FRAME(), nullptr, \
                common_flags()->fast_unwind_on_malloc)

// For platforms which support slow unwinder only, we restrict the store context
// size to 1, basically only storing the current pc. We do this because the slow
// unwinder which is based on libunwind is not async signal safe and causes
// random freezes in forking applications as well as in signal handlers.
#define GET_STORE_STACK_TRACE_PC_BP(pc, bp)                               \
  BufferedStackTrace stack;                                               \
  if (__msan_get_track_origins() > 1 && msan_inited) {                    \
    if (!SANITIZER_CAN_FAST_UNWIND)                                       \
      GetStackTrace(&stack, Min(1, flags()->store_context_size), pc, bp,  \
                    nullptr, false);                                      \
    else                                                                  \
      GetStackTrace(&stack, flags()->store_context_size, pc, bp, nullptr, \
                    common_flags()->fast_unwind_on_malloc);               \
  }

#define GET_STORE_STACK_TRACE \
  GET_STORE_STACK_TRACE_PC_BP(StackTrace::GetCurrentPc(), GET_CURRENT_FRAME())

#define GET_FATAL_STACK_TRACE_PC_BP(pc, bp)              \
  BufferedStackTrace stack;                              \
  if (msan_inited)                                       \
  GetStackTrace(&stack, kStackTraceMax, pc, bp, nullptr, \
                common_flags()->fast_unwind_on_fatal)

#define GET_FATAL_STACK_TRACE_HERE \
  GET_FATAL_STACK_TRACE_PC_BP(StackTrace::GetCurrentPc(), GET_CURRENT_FRAME())

#define PRINT_CURRENT_STACK_CHECK() \
  {                                 \
    GET_FATAL_STACK_TRACE_HERE;     \
    stack.Print();                  \
  }

class ScopedThreadLocalStateBackup {
 public:
  ScopedThreadLocalStateBackup() { Backup(); }
  ~ScopedThreadLocalStateBackup() { Restore(); }
  void Backup();
  void Restore();
 private:
  u64 va_arg_overflow_size_tls;
};

void MsanTSDInit(void (*destructor)(void *tsd));
void *MsanTSDGet();
void MsanTSDSet(void *tsd);
void MsanTSDDtor(void *tsd);

}  // namespace __msan

#define MSAN_MALLOC_HOOK(ptr, size)       \
  do {                                    \
    if (&__sanitizer_malloc_hook) {       \
      UnpoisonParam(2);                   \
      __sanitizer_malloc_hook(ptr, size); \
    }                                     \
    RunMallocHooks(ptr, size);            \
  } while (false)
#define MSAN_FREE_HOOK(ptr)       \
  do {                            \
    if (&__sanitizer_free_hook) { \
      UnpoisonParam(1);           \
      __sanitizer_free_hook(ptr); \
    }                             \
    RunFreeHooks(ptr);            \
  } while (false)

#endif  // MSAN_H

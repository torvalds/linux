//===-- asan_malloc_mac.cc ------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Mac-specific malloc interception.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_MAC

#include "asan_interceptors.h"
#include "asan_report.h"
#include "asan_stack.h"
#include "asan_stats.h"

using namespace __asan;
#define COMMON_MALLOC_ZONE_NAME "asan"
#define COMMON_MALLOC_ENTER() ENSURE_ASAN_INITED()
#define COMMON_MALLOC_SANITIZER_INITIALIZED asan_inited
#define COMMON_MALLOC_FORCE_LOCK() asan_mz_force_lock()
#define COMMON_MALLOC_FORCE_UNLOCK() asan_mz_force_unlock()
#define COMMON_MALLOC_MEMALIGN(alignment, size) \
  GET_STACK_TRACE_MALLOC; \
  void *p = asan_memalign(alignment, size, &stack, FROM_MALLOC)
#define COMMON_MALLOC_MALLOC(size) \
  GET_STACK_TRACE_MALLOC; \
  void *p = asan_malloc(size, &stack)
#define COMMON_MALLOC_REALLOC(ptr, size) \
  GET_STACK_TRACE_MALLOC; \
  void *p = asan_realloc(ptr, size, &stack);
#define COMMON_MALLOC_CALLOC(count, size) \
  GET_STACK_TRACE_MALLOC; \
  void *p = asan_calloc(count, size, &stack);
#define COMMON_MALLOC_POSIX_MEMALIGN(memptr, alignment, size) \
  GET_STACK_TRACE_MALLOC; \
  int res = asan_posix_memalign(memptr, alignment, size, &stack);
#define COMMON_MALLOC_VALLOC(size) \
  GET_STACK_TRACE_MALLOC; \
  void *p = asan_memalign(GetPageSizeCached(), size, &stack, FROM_MALLOC);
#define COMMON_MALLOC_FREE(ptr) \
  GET_STACK_TRACE_FREE; \
  asan_free(ptr, &stack, FROM_MALLOC);
#define COMMON_MALLOC_SIZE(ptr) \
  uptr size = asan_mz_size(ptr);
#define COMMON_MALLOC_FILL_STATS(zone, stats) \
  AsanMallocStats malloc_stats; \
  FillMallocStatistics(&malloc_stats); \
  CHECK(sizeof(malloc_statistics_t) == sizeof(AsanMallocStats)); \
  internal_memcpy(stats, &malloc_stats, sizeof(malloc_statistics_t));
#define COMMON_MALLOC_REPORT_UNKNOWN_REALLOC(ptr, zone_ptr, zone_name) \
  GET_STACK_TRACE_FREE; \
  ReportMacMzReallocUnknown((uptr)ptr, (uptr)zone_ptr, zone_name, &stack);
#define COMMON_MALLOC_NAMESPACE __asan

#include "sanitizer_common/sanitizer_malloc_mac.inc"

namespace COMMON_MALLOC_NAMESPACE {
bool HandleDlopenInit() {
  static_assert(SANITIZER_SUPPORTS_INIT_FOR_DLOPEN,
                "Expected SANITIZER_SUPPORTS_INIT_FOR_DLOPEN to be true");
  // We have no reliable way of knowing how we are being loaded
  // so make it a requirement on Apple platforms to set this environment
  // variable to indicate that we want to perform initialization via
  // dlopen().
  auto init_str = GetEnv("APPLE_ASAN_INIT_FOR_DLOPEN");
  if (!init_str)
    return false;
  if (internal_strncmp(init_str, "1", 1) != 0)
    return false;
  // When we are loaded via `dlopen()` path we still initialize the malloc zone
  // so Symbolication clients (e.g. `leaks`) that load the ASan allocator can
  // find an initialized malloc zone.
  InitMallocZoneFields();
  return true;
}
}  // namespace COMMON_MALLOC_NAMESPACE

#endif

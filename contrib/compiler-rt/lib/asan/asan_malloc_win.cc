//===-- asan_malloc_win.cc ------------------------------------------------===//
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
// Windows-specific malloc interception.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_WINDOWS
// Intentionally not including windows.h here, to avoid the risk of
// pulling in conflicting declarations of these functions. (With mingw-w64,
// there's a risk of windows.h pulling in stdint.h.)
typedef int BOOL;
typedef void *HANDLE;
typedef const void *LPCVOID;
typedef void *LPVOID;

#define HEAP_ZERO_MEMORY           0x00000008
#define HEAP_REALLOC_IN_PLACE_ONLY 0x00000010


#include "asan_allocator.h"
#include "asan_interceptors.h"
#include "asan_internal.h"
#include "asan_stack.h"
#include "interception/interception.h"

#include <stddef.h>

using namespace __asan;  // NOLINT

// MT: Simply defining functions with the same signature in *.obj
// files overrides the standard functions in the CRT.
// MD: Memory allocation functions are defined in the CRT .dll,
// so we have to intercept them before they are called for the first time.

#if ASAN_DYNAMIC
# define ALLOCATION_FUNCTION_ATTRIBUTE
#else
# define ALLOCATION_FUNCTION_ATTRIBUTE SANITIZER_INTERFACE_ATTRIBUTE
#endif

extern "C" {
ALLOCATION_FUNCTION_ATTRIBUTE
void free(void *ptr) {
  GET_STACK_TRACE_FREE;
  return asan_free(ptr, &stack, FROM_MALLOC);
}

ALLOCATION_FUNCTION_ATTRIBUTE
void _free_dbg(void *ptr, int) {
  free(ptr);
}

ALLOCATION_FUNCTION_ATTRIBUTE
void _free_base(void *ptr) {
  free(ptr);
}

ALLOCATION_FUNCTION_ATTRIBUTE
void *malloc(size_t size) {
  GET_STACK_TRACE_MALLOC;
  return asan_malloc(size, &stack);
}

ALLOCATION_FUNCTION_ATTRIBUTE
void *_malloc_base(size_t size) {
  return malloc(size);
}

ALLOCATION_FUNCTION_ATTRIBUTE
void *_malloc_dbg(size_t size, int, const char *, int) {
  return malloc(size);
}

ALLOCATION_FUNCTION_ATTRIBUTE
void *calloc(size_t nmemb, size_t size) {
  GET_STACK_TRACE_MALLOC;
  return asan_calloc(nmemb, size, &stack);
}

ALLOCATION_FUNCTION_ATTRIBUTE
void *_calloc_base(size_t nmemb, size_t size) {
  return calloc(nmemb, size);
}

ALLOCATION_FUNCTION_ATTRIBUTE
void *_calloc_dbg(size_t nmemb, size_t size, int, const char *, int) {
  return calloc(nmemb, size);
}

ALLOCATION_FUNCTION_ATTRIBUTE
void *_calloc_impl(size_t nmemb, size_t size, int *errno_tmp) {
  return calloc(nmemb, size);
}

ALLOCATION_FUNCTION_ATTRIBUTE
void *realloc(void *ptr, size_t size) {
  GET_STACK_TRACE_MALLOC;
  return asan_realloc(ptr, size, &stack);
}

ALLOCATION_FUNCTION_ATTRIBUTE
void *_realloc_dbg(void *ptr, size_t size, int) {
  UNREACHABLE("_realloc_dbg should not exist!");
  return 0;
}

ALLOCATION_FUNCTION_ATTRIBUTE
void *_realloc_base(void *ptr, size_t size) {
  return realloc(ptr, size);
}

ALLOCATION_FUNCTION_ATTRIBUTE
void *_recalloc(void *p, size_t n, size_t elem_size) {
  if (!p)
    return calloc(n, elem_size);
  const size_t size = n * elem_size;
  if (elem_size != 0 && size / elem_size != n)
    return 0;
  return realloc(p, size);
}

ALLOCATION_FUNCTION_ATTRIBUTE
void *_recalloc_base(void *p, size_t n, size_t elem_size) {
  return _recalloc(p, n, elem_size);
}

ALLOCATION_FUNCTION_ATTRIBUTE
size_t _msize(void *ptr) {
  GET_CURRENT_PC_BP_SP;
  (void)sp;
  return asan_malloc_usable_size(ptr, pc, bp);
}

ALLOCATION_FUNCTION_ATTRIBUTE
size_t _msize_base(void *ptr) {
  return _msize(ptr);
}

ALLOCATION_FUNCTION_ATTRIBUTE
void *_expand(void *memblock, size_t size) {
  // _expand is used in realloc-like functions to resize the buffer if possible.
  // We don't want memory to stand still while resizing buffers, so return 0.
  return 0;
}

ALLOCATION_FUNCTION_ATTRIBUTE
void *_expand_dbg(void *memblock, size_t size) {
  return _expand(memblock, size);
}

// TODO(timurrrr): Might want to add support for _aligned_* allocation
// functions to detect a bit more bugs.  Those functions seem to wrap malloc().

int _CrtDbgReport(int, const char*, int,
                  const char*, const char*, ...) {
  ShowStatsAndAbort();
}

int _CrtDbgReportW(int reportType, const wchar_t*, int,
                   const wchar_t*, const wchar_t*, ...) {
  ShowStatsAndAbort();
}

int _CrtSetReportMode(int, int) {
  return 0;
}
}  // extern "C"

INTERCEPTOR_WINAPI(LPVOID, HeapAlloc, HANDLE hHeap, DWORD dwFlags,
                   SIZE_T dwBytes) {
  GET_STACK_TRACE_MALLOC;
  void *p = asan_malloc(dwBytes, &stack);
  // Reading MSDN suggests that the *entire* usable allocation is zeroed out.
  // Otherwise it is difficult to HeapReAlloc with HEAP_ZERO_MEMORY.
  // https://blogs.msdn.microsoft.com/oldnewthing/20120316-00/?p=8083
  if (dwFlags == HEAP_ZERO_MEMORY)
    internal_memset(p, 0, asan_mz_size(p));
  else
    CHECK(dwFlags == 0 && "unsupported heap flags");
  return p;
}

INTERCEPTOR_WINAPI(BOOL, HeapFree, HANDLE hHeap, DWORD dwFlags, LPVOID lpMem) {
  CHECK(dwFlags == 0 && "unsupported heap flags");
  GET_STACK_TRACE_FREE;
  asan_free(lpMem, &stack, FROM_MALLOC);
  return true;
}

INTERCEPTOR_WINAPI(LPVOID, HeapReAlloc, HANDLE hHeap, DWORD dwFlags,
                   LPVOID lpMem, SIZE_T dwBytes) {
  GET_STACK_TRACE_MALLOC;
  // Realloc should never reallocate in place.
  if (dwFlags & HEAP_REALLOC_IN_PLACE_ONLY)
    return nullptr;
  CHECK(dwFlags == 0 && "unsupported heap flags");
  return asan_realloc(lpMem, dwBytes, &stack);
}

INTERCEPTOR_WINAPI(SIZE_T, HeapSize, HANDLE hHeap, DWORD dwFlags,
                   LPCVOID lpMem) {
  CHECK(dwFlags == 0 && "unsupported heap flags");
  GET_CURRENT_PC_BP_SP;
  (void)sp;
  return asan_malloc_usable_size(lpMem, pc, bp);
}

namespace __asan {

static void TryToOverrideFunction(const char *fname, uptr new_func) {
  // Failure here is not fatal. The CRT may not be present, and different CRT
  // versions use different symbols.
  if (!__interception::OverrideFunction(fname, new_func))
    VPrintf(2, "Failed to override function %s\n", fname);
}

void ReplaceSystemMalloc() {
#if defined(ASAN_DYNAMIC)
  TryToOverrideFunction("free", (uptr)free);
  TryToOverrideFunction("_free_base", (uptr)free);
  TryToOverrideFunction("malloc", (uptr)malloc);
  TryToOverrideFunction("_malloc_base", (uptr)malloc);
  TryToOverrideFunction("_malloc_crt", (uptr)malloc);
  TryToOverrideFunction("calloc", (uptr)calloc);
  TryToOverrideFunction("_calloc_base", (uptr)calloc);
  TryToOverrideFunction("_calloc_crt", (uptr)calloc);
  TryToOverrideFunction("realloc", (uptr)realloc);
  TryToOverrideFunction("_realloc_base", (uptr)realloc);
  TryToOverrideFunction("_realloc_crt", (uptr)realloc);
  TryToOverrideFunction("_recalloc", (uptr)_recalloc);
  TryToOverrideFunction("_recalloc_base", (uptr)_recalloc);
  TryToOverrideFunction("_recalloc_crt", (uptr)_recalloc);
  TryToOverrideFunction("_msize", (uptr)_msize);
  TryToOverrideFunction("_msize_base", (uptr)_msize);
  TryToOverrideFunction("_expand", (uptr)_expand);
  TryToOverrideFunction("_expand_base", (uptr)_expand);

  // Recent versions of ucrtbase.dll appear to be built with PGO and LTCG, which
  // enable cross-module inlining. This means our _malloc_base hook won't catch
  // all CRT allocations. This code here patches the import table of
  // ucrtbase.dll so that all attempts to use the lower-level win32 heap
  // allocation API will be directed to ASan's heap. We don't currently
  // intercept all calls to HeapAlloc. If we did, we would have to check on
  // HeapFree whether the pointer came from ASan of from the system.
#define INTERCEPT_UCRT_FUNCTION(func)                                         \
  if (!INTERCEPT_FUNCTION_DLLIMPORT("ucrtbase.dll",                           \
                                    "api-ms-win-core-heap-l1-1-0.dll", func)) \
    VPrintf(2, "Failed to intercept ucrtbase.dll import %s\n", #func);
  INTERCEPT_UCRT_FUNCTION(HeapAlloc);
  INTERCEPT_UCRT_FUNCTION(HeapFree);
  INTERCEPT_UCRT_FUNCTION(HeapReAlloc);
  INTERCEPT_UCRT_FUNCTION(HeapSize);
#undef INTERCEPT_UCRT_FUNCTION
#endif
}
}  // namespace __asan

#endif  // _WIN32

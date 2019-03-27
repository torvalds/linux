//===-- asan_malloc_linux.cc ----------------------------------------------===//
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
// Linux-specific malloc interception.
// We simply define functions like malloc, free, realloc, etc.
// They will replace the corresponding libc functions automagically.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_FREEBSD || SANITIZER_FUCHSIA || SANITIZER_LINUX || \
    SANITIZER_NETBSD || SANITIZER_RTEMS || SANITIZER_SOLARIS

#include "sanitizer_common/sanitizer_allocator_checks.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"
#include "asan_allocator.h"
#include "asan_interceptors.h"
#include "asan_internal.h"
#include "asan_malloc_local.h"
#include "asan_stack.h"

// ---------------------- Replacement functions ---------------- {{{1
using namespace __asan;  // NOLINT

static uptr allocated_for_dlsym;
static uptr last_dlsym_alloc_size_in_words;
static const uptr kDlsymAllocPoolSize = SANITIZER_RTEMS ? 4096 : 1024;
static uptr alloc_memory_for_dlsym[kDlsymAllocPoolSize];

static INLINE bool IsInDlsymAllocPool(const void *ptr) {
  uptr off = (uptr)ptr - (uptr)alloc_memory_for_dlsym;
  return off < allocated_for_dlsym * sizeof(alloc_memory_for_dlsym[0]);
}

static void *AllocateFromLocalPool(uptr size_in_bytes) {
  uptr size_in_words = RoundUpTo(size_in_bytes, kWordSize) / kWordSize;
  void *mem = (void*)&alloc_memory_for_dlsym[allocated_for_dlsym];
  last_dlsym_alloc_size_in_words = size_in_words;
  allocated_for_dlsym += size_in_words;
  CHECK_LT(allocated_for_dlsym, kDlsymAllocPoolSize);
  return mem;
}

static void DeallocateFromLocalPool(const void *ptr) {
  // Hack: since glibc 2.27 dlsym no longer uses stack-allocated memory to store
  // error messages and instead uses malloc followed by free. To avoid pool
  // exhaustion due to long object filenames, handle that special case here.
  uptr prev_offset = allocated_for_dlsym - last_dlsym_alloc_size_in_words;
  void *prev_mem = (void*)&alloc_memory_for_dlsym[prev_offset];
  if (prev_mem == ptr) {
    REAL(memset)(prev_mem, 0, last_dlsym_alloc_size_in_words * kWordSize);
    allocated_for_dlsym = prev_offset;
    last_dlsym_alloc_size_in_words = 0;
  }
}

static int PosixMemalignFromLocalPool(void **memptr, uptr alignment,
                                      uptr size_in_bytes) {
  if (UNLIKELY(!CheckPosixMemalignAlignment(alignment)))
    return errno_EINVAL;

  CHECK(alignment >= kWordSize);

  uptr addr = (uptr)&alloc_memory_for_dlsym[allocated_for_dlsym];
  uptr aligned_addr = RoundUpTo(addr, alignment);
  uptr aligned_size = RoundUpTo(size_in_bytes, kWordSize);

  uptr *end_mem = (uptr*)(aligned_addr + aligned_size);
  uptr allocated = end_mem - alloc_memory_for_dlsym;
  if (allocated >= kDlsymAllocPoolSize)
    return errno_ENOMEM;

  allocated_for_dlsym = allocated;
  *memptr = (void*)aligned_addr;
  return 0;
}

#if SANITIZER_RTEMS
void* MemalignFromLocalPool(uptr alignment, uptr size) {
  void *ptr = nullptr;
  alignment = Max(alignment, kWordSize);
  PosixMemalignFromLocalPool(&ptr, alignment, size);
  return ptr;
}

bool IsFromLocalPool(const void *ptr) {
  return IsInDlsymAllocPool(ptr);
}
#endif

static INLINE bool MaybeInDlsym() {
  // Fuchsia doesn't use dlsym-based interceptors.
  return !SANITIZER_FUCHSIA && asan_init_is_running;
}

static INLINE bool UseLocalPool() {
  return EarlyMalloc() || MaybeInDlsym();
}

static void *ReallocFromLocalPool(void *ptr, uptr size) {
  const uptr offset = (uptr)ptr - (uptr)alloc_memory_for_dlsym;
  const uptr copy_size = Min(size, kDlsymAllocPoolSize - offset);
  void *new_ptr;
  if (UNLIKELY(UseLocalPool())) {
    new_ptr = AllocateFromLocalPool(size);
  } else {
    ENSURE_ASAN_INITED();
    GET_STACK_TRACE_MALLOC;
    new_ptr = asan_malloc(size, &stack);
  }
  internal_memcpy(new_ptr, ptr, copy_size);
  return new_ptr;
}

INTERCEPTOR(void, free, void *ptr) {
  GET_STACK_TRACE_FREE;
  if (UNLIKELY(IsInDlsymAllocPool(ptr))) {
    DeallocateFromLocalPool(ptr);
    return;
  }
  asan_free(ptr, &stack, FROM_MALLOC);
}

#if SANITIZER_INTERCEPT_CFREE
INTERCEPTOR(void, cfree, void *ptr) {
  GET_STACK_TRACE_FREE;
  if (UNLIKELY(IsInDlsymAllocPool(ptr)))
    return;
  asan_free(ptr, &stack, FROM_MALLOC);
}
#endif // SANITIZER_INTERCEPT_CFREE

INTERCEPTOR(void*, malloc, uptr size) {
  if (UNLIKELY(UseLocalPool()))
    // Hack: dlsym calls malloc before REAL(malloc) is retrieved from dlsym.
    return AllocateFromLocalPool(size);
  ENSURE_ASAN_INITED();
  GET_STACK_TRACE_MALLOC;
  return asan_malloc(size, &stack);
}

INTERCEPTOR(void*, calloc, uptr nmemb, uptr size) {
  if (UNLIKELY(UseLocalPool()))
    // Hack: dlsym calls calloc before REAL(calloc) is retrieved from dlsym.
    return AllocateFromLocalPool(nmemb * size);
  ENSURE_ASAN_INITED();
  GET_STACK_TRACE_MALLOC;
  return asan_calloc(nmemb, size, &stack);
}

INTERCEPTOR(void*, realloc, void *ptr, uptr size) {
  if (UNLIKELY(IsInDlsymAllocPool(ptr)))
    return ReallocFromLocalPool(ptr, size);
  if (UNLIKELY(UseLocalPool()))
    return AllocateFromLocalPool(size);
  ENSURE_ASAN_INITED();
  GET_STACK_TRACE_MALLOC;
  return asan_realloc(ptr, size, &stack);
}

#if SANITIZER_INTERCEPT_MEMALIGN
INTERCEPTOR(void*, memalign, uptr boundary, uptr size) {
  GET_STACK_TRACE_MALLOC;
  return asan_memalign(boundary, size, &stack, FROM_MALLOC);
}

INTERCEPTOR(void*, __libc_memalign, uptr boundary, uptr size) {
  GET_STACK_TRACE_MALLOC;
  void *res = asan_memalign(boundary, size, &stack, FROM_MALLOC);
  DTLS_on_libc_memalign(res, size);
  return res;
}
#endif // SANITIZER_INTERCEPT_MEMALIGN

#if SANITIZER_INTERCEPT_ALIGNED_ALLOC
INTERCEPTOR(void*, aligned_alloc, uptr boundary, uptr size) {
  GET_STACK_TRACE_MALLOC;
  return asan_aligned_alloc(boundary, size, &stack);
}
#endif // SANITIZER_INTERCEPT_ALIGNED_ALLOC

INTERCEPTOR(uptr, malloc_usable_size, void *ptr) {
  GET_CURRENT_PC_BP_SP;
  (void)sp;
  return asan_malloc_usable_size(ptr, pc, bp);
}

#if SANITIZER_INTERCEPT_MALLOPT_AND_MALLINFO
// We avoid including malloc.h for portability reasons.
// man mallinfo says the fields are "long", but the implementation uses int.
// It doesn't matter much -- we just need to make sure that the libc's mallinfo
// is not called.
struct fake_mallinfo {
  int x[10];
};

INTERCEPTOR(struct fake_mallinfo, mallinfo, void) {
  struct fake_mallinfo res;
  REAL(memset)(&res, 0, sizeof(res));
  return res;
}

INTERCEPTOR(int, mallopt, int cmd, int value) {
  return 0;
}
#endif // SANITIZER_INTERCEPT_MALLOPT_AND_MALLINFO

INTERCEPTOR(int, posix_memalign, void **memptr, uptr alignment, uptr size) {
  if (UNLIKELY(UseLocalPool()))
    return PosixMemalignFromLocalPool(memptr, alignment, size);
  GET_STACK_TRACE_MALLOC;
  return asan_posix_memalign(memptr, alignment, size, &stack);
}

INTERCEPTOR(void*, valloc, uptr size) {
  GET_STACK_TRACE_MALLOC;
  return asan_valloc(size, &stack);
}

#if SANITIZER_INTERCEPT_PVALLOC
INTERCEPTOR(void*, pvalloc, uptr size) {
  GET_STACK_TRACE_MALLOC;
  return asan_pvalloc(size, &stack);
}
#endif // SANITIZER_INTERCEPT_PVALLOC

INTERCEPTOR(void, malloc_stats, void) {
  __asan_print_accumulated_stats();
}

#if SANITIZER_ANDROID
// Format of __libc_malloc_dispatch has changed in Android L.
// While we are moving towards a solution that does not depend on bionic
// internals, here is something to support both K* and L releases.
struct MallocDebugK {
  void *(*malloc)(uptr bytes);
  void (*free)(void *mem);
  void *(*calloc)(uptr n_elements, uptr elem_size);
  void *(*realloc)(void *oldMem, uptr bytes);
  void *(*memalign)(uptr alignment, uptr bytes);
  uptr (*malloc_usable_size)(void *mem);
};

struct MallocDebugL {
  void *(*calloc)(uptr n_elements, uptr elem_size);
  void (*free)(void *mem);
  fake_mallinfo (*mallinfo)(void);
  void *(*malloc)(uptr bytes);
  uptr (*malloc_usable_size)(void *mem);
  void *(*memalign)(uptr alignment, uptr bytes);
  int (*posix_memalign)(void **memptr, uptr alignment, uptr size);
  void* (*pvalloc)(uptr size);
  void *(*realloc)(void *oldMem, uptr bytes);
  void* (*valloc)(uptr size);
};

ALIGNED(32) const MallocDebugK asan_malloc_dispatch_k = {
    WRAP(malloc),  WRAP(free),     WRAP(calloc),
    WRAP(realloc), WRAP(memalign), WRAP(malloc_usable_size)};

ALIGNED(32) const MallocDebugL asan_malloc_dispatch_l = {
    WRAP(calloc),         WRAP(free),               WRAP(mallinfo),
    WRAP(malloc),         WRAP(malloc_usable_size), WRAP(memalign),
    WRAP(posix_memalign), WRAP(pvalloc),            WRAP(realloc),
    WRAP(valloc)};

namespace __asan {
void ReplaceSystemMalloc() {
  void **__libc_malloc_dispatch_p =
      (void **)AsanDlSymNext("__libc_malloc_dispatch");
  if (__libc_malloc_dispatch_p) {
    // Decide on K vs L dispatch format by the presence of
    // __libc_malloc_default_dispatch export in libc.
    void *default_dispatch_p = AsanDlSymNext("__libc_malloc_default_dispatch");
    if (default_dispatch_p)
      *__libc_malloc_dispatch_p = (void *)&asan_malloc_dispatch_k;
    else
      *__libc_malloc_dispatch_p = (void *)&asan_malloc_dispatch_l;
  }
}
}  // namespace __asan

#else  // SANITIZER_ANDROID

namespace __asan {
void ReplaceSystemMalloc() {
}
}  // namespace __asan
#endif  // SANITIZER_ANDROID

#endif  // SANITIZER_FREEBSD || SANITIZER_FUCHSIA || SANITIZER_LINUX ||
        // SANITIZER_NETBSD || SANITIZER_SOLARIS

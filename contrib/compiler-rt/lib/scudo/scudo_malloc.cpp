//===-- scudo_malloc.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Interceptors for malloc related functions.
///
//===----------------------------------------------------------------------===//

#include "scudo_allocator.h"

#include "interception/interception.h"
#include "sanitizer_common/sanitizer_platform_interceptors.h"

#include <stddef.h>

using namespace __scudo;

extern "C" {
INTERCEPTOR_ATTRIBUTE void free(void *ptr) {
  scudoDeallocate(ptr, 0, 0, FromMalloc);
}

INTERCEPTOR_ATTRIBUTE void *malloc(size_t size) {
  return scudoAllocate(size, 0, FromMalloc);
}

INTERCEPTOR_ATTRIBUTE void *realloc(void *ptr, size_t size) {
  return scudoRealloc(ptr, size);
}

INTERCEPTOR_ATTRIBUTE void *calloc(size_t nmemb, size_t size) {
  return scudoCalloc(nmemb, size);
}

INTERCEPTOR_ATTRIBUTE void *valloc(size_t size) {
  return scudoValloc(size);
}

INTERCEPTOR_ATTRIBUTE
int posix_memalign(void **memptr, size_t alignment, size_t size) {
  return scudoPosixMemalign(memptr, alignment, size);
}

#if SANITIZER_INTERCEPT_CFREE
INTERCEPTOR_ATTRIBUTE void cfree(void *ptr) ALIAS("free");
#endif

#if SANITIZER_INTERCEPT_MEMALIGN
INTERCEPTOR_ATTRIBUTE void *memalign(size_t alignment, size_t size) {
  return scudoAllocate(size, alignment, FromMemalign);
}

INTERCEPTOR_ATTRIBUTE
void *__libc_memalign(size_t alignment, size_t size) ALIAS("memalign");
#endif

#if SANITIZER_INTERCEPT_PVALLOC
INTERCEPTOR_ATTRIBUTE void *pvalloc(size_t size) {
  return scudoPvalloc(size);
}
#endif

#if SANITIZER_INTERCEPT_ALIGNED_ALLOC
INTERCEPTOR_ATTRIBUTE void *aligned_alloc(size_t alignment, size_t size) {
  return scudoAlignedAlloc(alignment, size);
}
#endif

#if SANITIZER_INTERCEPT_MALLOC_USABLE_SIZE
INTERCEPTOR_ATTRIBUTE size_t malloc_usable_size(void *ptr) {
  return scudoMallocUsableSize(ptr);
}
#endif

#if SANITIZER_INTERCEPT_MALLOPT_AND_MALLINFO
INTERCEPTOR_ATTRIBUTE int mallopt(int cmd, int value) {
  return 0;
}
#endif
}  // extern "C"

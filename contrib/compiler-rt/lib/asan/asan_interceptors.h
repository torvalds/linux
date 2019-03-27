//===-- asan_interceptors.h -------------------------------------*- C++ -*-===//
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
// ASan-private header for asan_interceptors.cc
//===----------------------------------------------------------------------===//
#ifndef ASAN_INTERCEPTORS_H
#define ASAN_INTERCEPTORS_H

#include "asan_internal.h"
#include "asan_interceptors_memintrinsics.h"
#include "interception/interception.h"
#include "sanitizer_common/sanitizer_platform_interceptors.h"

namespace __asan {

void InitializeAsanInterceptors();
void InitializePlatformInterceptors();

#define ENSURE_ASAN_INITED()      \
  do {                            \
    CHECK(!asan_init_is_running); \
    if (UNLIKELY(!asan_inited)) { \
      AsanInitFromRtl();          \
    }                             \
  } while (0)

}  // namespace __asan

// There is no general interception at all on Fuchsia and RTEMS.
// Only the functions in asan_interceptors_memintrinsics.h are
// really defined to replace libc functions.
#if !SANITIZER_FUCHSIA && !SANITIZER_RTEMS

// Use macro to describe if specific function should be
// intercepted on a given platform.
#if !SANITIZER_WINDOWS
# define ASAN_INTERCEPT_ATOLL_AND_STRTOLL 1
# define ASAN_INTERCEPT__LONGJMP 1
# define ASAN_INTERCEPT_INDEX 1
# define ASAN_INTERCEPT_PTHREAD_CREATE 1
#else
# define ASAN_INTERCEPT_ATOLL_AND_STRTOLL 0
# define ASAN_INTERCEPT__LONGJMP 0
# define ASAN_INTERCEPT_INDEX 0
# define ASAN_INTERCEPT_PTHREAD_CREATE 0
#endif

#if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD || \
    SANITIZER_SOLARIS
# define ASAN_USE_ALIAS_ATTRIBUTE_FOR_INDEX 1
#else
# define ASAN_USE_ALIAS_ATTRIBUTE_FOR_INDEX 0
#endif

#if (SANITIZER_LINUX && !SANITIZER_ANDROID) || SANITIZER_SOLARIS
# define ASAN_INTERCEPT_SWAPCONTEXT 1
#else
# define ASAN_INTERCEPT_SWAPCONTEXT 0
#endif

#if !SANITIZER_WINDOWS
# define ASAN_INTERCEPT_SIGLONGJMP 1
#else
# define ASAN_INTERCEPT_SIGLONGJMP 0
#endif

#if SANITIZER_LINUX && !SANITIZER_ANDROID
# define ASAN_INTERCEPT___LONGJMP_CHK 1
#else
# define ASAN_INTERCEPT___LONGJMP_CHK 0
#endif

#if ASAN_HAS_EXCEPTIONS && !SANITIZER_WINDOWS && !SANITIZER_SOLARIS && \
    !SANITIZER_NETBSD
# define ASAN_INTERCEPT___CXA_THROW 1
# define ASAN_INTERCEPT___CXA_RETHROW_PRIMARY_EXCEPTION 1
# if defined(_GLIBCXX_SJLJ_EXCEPTIONS) || (SANITIZER_IOS && defined(__arm__))
#  define ASAN_INTERCEPT__UNWIND_SJLJ_RAISEEXCEPTION 1
# else
#  define ASAN_INTERCEPT__UNWIND_RAISEEXCEPTION 1
# endif
#else
# define ASAN_INTERCEPT___CXA_THROW 0
# define ASAN_INTERCEPT___CXA_RETHROW_PRIMARY_EXCEPTION 0
# define ASAN_INTERCEPT__UNWIND_RAISEEXCEPTION 0
# define ASAN_INTERCEPT__UNWIND_SJLJ_RAISEEXCEPTION 0
#endif

#if !SANITIZER_WINDOWS
# define ASAN_INTERCEPT___CXA_ATEXIT 1
#else
# define ASAN_INTERCEPT___CXA_ATEXIT 0
#endif

#if SANITIZER_LINUX && !SANITIZER_ANDROID
# define ASAN_INTERCEPT___STRDUP 1
#else
# define ASAN_INTERCEPT___STRDUP 0
#endif

DECLARE_REAL(int, memcmp, const void *a1, const void *a2, uptr size)
DECLARE_REAL(char*, strchr, const char *str, int c)
DECLARE_REAL(SIZE_T, strlen, const char *s)
DECLARE_REAL(char*, strncpy, char *to, const char *from, uptr size)
DECLARE_REAL(uptr, strnlen, const char *s, uptr maxlen)
DECLARE_REAL(char*, strstr, const char *s1, const char *s2)

#if !SANITIZER_MAC
#define ASAN_INTERCEPT_FUNC(name)                                        \
  do {                                                                   \
    if ((!INTERCEPT_FUNCTION(name) || !REAL(name)))                      \
      VReport(1, "AddressSanitizer: failed to intercept '" #name "'\n"); \
  } while (0)
#define ASAN_INTERCEPT_FUNC_VER(name, ver)                                     \
  do {                                                                         \
    if ((!INTERCEPT_FUNCTION_VER(name, ver) || !REAL(name)))                   \
      VReport(                                                                 \
          1, "AddressSanitizer: failed to intercept '" #name "@@" #ver "'\n"); \
  } while (0)
#else
// OS X interceptors don't need to be initialized with INTERCEPT_FUNCTION.
#define ASAN_INTERCEPT_FUNC(name)
#endif  // SANITIZER_MAC

#endif  // !SANITIZER_FUCHSIA

#endif  // ASAN_INTERCEPTORS_H

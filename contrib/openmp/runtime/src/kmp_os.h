/*
 * kmp_os.h -- KPTS runtime header file.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef KMP_OS_H
#define KMP_OS_H

#include "kmp_config.h"
#include <stdlib.h>
#include <atomic>

#define KMP_FTN_PLAIN 1
#define KMP_FTN_APPEND 2
#define KMP_FTN_UPPER 3
/*
#define KMP_FTN_PREPEND 4
#define KMP_FTN_UAPPEND 5
*/

#define KMP_PTR_SKIP (sizeof(void *))

/* -------------------------- Compiler variations ------------------------ */

#define KMP_OFF 0
#define KMP_ON 1

#define KMP_MEM_CONS_VOLATILE 0
#define KMP_MEM_CONS_FENCE 1

#ifndef KMP_MEM_CONS_MODEL
#define KMP_MEM_CONS_MODEL KMP_MEM_CONS_VOLATILE
#endif

/* ------------------------- Compiler recognition ---------------------- */
#define KMP_COMPILER_ICC 0
#define KMP_COMPILER_GCC 0
#define KMP_COMPILER_CLANG 0
#define KMP_COMPILER_MSVC 0

#if defined(__INTEL_COMPILER)
#undef KMP_COMPILER_ICC
#define KMP_COMPILER_ICC 1
#elif defined(__clang__)
#undef KMP_COMPILER_CLANG
#define KMP_COMPILER_CLANG 1
#elif defined(__GNUC__)
#undef KMP_COMPILER_GCC
#define KMP_COMPILER_GCC 1
#elif defined(_MSC_VER)
#undef KMP_COMPILER_MSVC
#define KMP_COMPILER_MSVC 1
#else
#error Unknown compiler
#endif

#if (KMP_OS_LINUX || KMP_OS_WINDOWS) && !KMP_OS_CNK
#define KMP_AFFINITY_SUPPORTED 1
#if KMP_OS_WINDOWS && KMP_ARCH_X86_64
#define KMP_GROUP_AFFINITY 1
#else
#define KMP_GROUP_AFFINITY 0
#endif
#else
#define KMP_AFFINITY_SUPPORTED 0
#define KMP_GROUP_AFFINITY 0
#endif

/* Check for quad-precision extension. */
#define KMP_HAVE_QUAD 0
#if KMP_ARCH_X86 || KMP_ARCH_X86_64
#if KMP_COMPILER_ICC
/* _Quad is already defined for icc */
#undef KMP_HAVE_QUAD
#define KMP_HAVE_QUAD 1
#elif KMP_COMPILER_CLANG
/* Clang doesn't support a software-implemented
   128-bit extended precision type yet */
typedef long double _Quad;
#elif KMP_COMPILER_GCC
/* GCC on NetBSD lacks __multc3/__divtc3 builtins needed for quad */
#if !KMP_OS_NETBSD
typedef __float128 _Quad;
#undef KMP_HAVE_QUAD
#define KMP_HAVE_QUAD 1
#endif
#elif KMP_COMPILER_MSVC
typedef long double _Quad;
#endif
#else
#if __LDBL_MAX_EXP__ >= 16384 && KMP_COMPILER_GCC
typedef long double _Quad;
#undef KMP_HAVE_QUAD
#define KMP_HAVE_QUAD 1
#endif
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

#define KMP_USE_X87CONTROL 0
#if KMP_OS_WINDOWS
#define KMP_END_OF_LINE "\r\n"
typedef char kmp_int8;
typedef unsigned char kmp_uint8;
typedef short kmp_int16;
typedef unsigned short kmp_uint16;
typedef int kmp_int32;
typedef unsigned int kmp_uint32;
#define KMP_INT32_SPEC "d"
#define KMP_UINT32_SPEC "u"
#ifndef KMP_STRUCT64
typedef __int64 kmp_int64;
typedef unsigned __int64 kmp_uint64;
#define KMP_INT64_SPEC "I64d"
#define KMP_UINT64_SPEC "I64u"
#else
struct kmp_struct64 {
  kmp_int32 a, b;
};
typedef struct kmp_struct64 kmp_int64;
typedef struct kmp_struct64 kmp_uint64;
/* Not sure what to use for KMP_[U]INT64_SPEC here */
#endif
#if KMP_ARCH_X86 && KMP_MSVC_COMPAT
#undef KMP_USE_X87CONTROL
#define KMP_USE_X87CONTROL 1
#endif
#if KMP_ARCH_X86_64
#define KMP_INTPTR 1
typedef __int64 kmp_intptr_t;
typedef unsigned __int64 kmp_uintptr_t;
#define KMP_INTPTR_SPEC "I64d"
#define KMP_UINTPTR_SPEC "I64u"
#endif
#endif /* KMP_OS_WINDOWS */

#if KMP_OS_UNIX
#define KMP_END_OF_LINE "\n"
typedef char kmp_int8;
typedef unsigned char kmp_uint8;
typedef short kmp_int16;
typedef unsigned short kmp_uint16;
typedef int kmp_int32;
typedef unsigned int kmp_uint32;
typedef long long kmp_int64;
typedef unsigned long long kmp_uint64;
#define KMP_INT32_SPEC "d"
#define KMP_UINT32_SPEC "u"
#define KMP_INT64_SPEC "lld"
#define KMP_UINT64_SPEC "llu"
#endif /* KMP_OS_UNIX */

#if KMP_ARCH_X86 || KMP_ARCH_ARM || KMP_ARCH_MIPS
#define KMP_SIZE_T_SPEC KMP_UINT32_SPEC
#elif KMP_ARCH_X86_64 || KMP_ARCH_PPC64 || KMP_ARCH_AARCH64 || KMP_ARCH_MIPS64
#define KMP_SIZE_T_SPEC KMP_UINT64_SPEC
#else
#error "Can't determine size_t printf format specifier."
#endif

#if KMP_ARCH_X86
#define KMP_SIZE_T_MAX (0xFFFFFFFF)
#else
#define KMP_SIZE_T_MAX (0xFFFFFFFFFFFFFFFF)
#endif

typedef size_t kmp_size_t;
typedef float kmp_real32;
typedef double kmp_real64;

#ifndef KMP_INTPTR
#define KMP_INTPTR 1
typedef long kmp_intptr_t;
typedef unsigned long kmp_uintptr_t;
#define KMP_INTPTR_SPEC "ld"
#define KMP_UINTPTR_SPEC "lu"
#endif

#ifdef BUILD_I8
typedef kmp_int64 kmp_int;
typedef kmp_uint64 kmp_uint;
#else
typedef kmp_int32 kmp_int;
typedef kmp_uint32 kmp_uint;
#endif /* BUILD_I8 */
#define KMP_INT_MAX ((kmp_int32)0x7FFFFFFF)
#define KMP_INT_MIN ((kmp_int32)0x80000000)

#ifdef __cplusplus
// macros to cast out qualifiers and to re-interpret types
#define CCAST(type, var) const_cast<type>(var)
#define RCAST(type, var) reinterpret_cast<type>(var)
//-------------------------------------------------------------------------
// template for debug prints specification ( d, u, lld, llu ), and to obtain
// signed/unsigned flavors of a type
template <typename T> struct traits_t {};
// int
template <> struct traits_t<signed int> {
  typedef signed int signed_t;
  typedef unsigned int unsigned_t;
  typedef double floating_t;
  static char const *spec;
  static const signed_t max_value = 0x7fffffff;
  static const signed_t min_value = 0x80000000;
  static const int type_size = sizeof(signed_t);
};
// unsigned int
template <> struct traits_t<unsigned int> {
  typedef signed int signed_t;
  typedef unsigned int unsigned_t;
  typedef double floating_t;
  static char const *spec;
  static const unsigned_t max_value = 0xffffffff;
  static const unsigned_t min_value = 0x00000000;
  static const int type_size = sizeof(unsigned_t);
};
// long
template <> struct traits_t<signed long> {
  typedef signed long signed_t;
  typedef unsigned long unsigned_t;
  typedef long double floating_t;
  static char const *spec;
  static const int type_size = sizeof(signed_t);
};
// long long
template <> struct traits_t<signed long long> {
  typedef signed long long signed_t;
  typedef unsigned long long unsigned_t;
  typedef long double floating_t;
  static char const *spec;
  static const signed_t max_value = 0x7fffffffffffffffLL;
  static const signed_t min_value = 0x8000000000000000LL;
  static const int type_size = sizeof(signed_t);
};
// unsigned long long
template <> struct traits_t<unsigned long long> {
  typedef signed long long signed_t;
  typedef unsigned long long unsigned_t;
  typedef long double floating_t;
  static char const *spec;
  static const unsigned_t max_value = 0xffffffffffffffffLL;
  static const unsigned_t min_value = 0x0000000000000000LL;
  static const int type_size = sizeof(unsigned_t);
};
//-------------------------------------------------------------------------
#else
#define CCAST(type, var) (type)(var)
#define RCAST(type, var) (type)(var)
#endif // __cplusplus

#define KMP_EXPORT extern /* export declaration in guide libraries */

#if __GNUC__ >= 4 && !defined(__MINGW32__)
#define __forceinline __inline
#endif

#if KMP_OS_WINDOWS
#include <windows.h>

static inline int KMP_GET_PAGE_SIZE(void) {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwPageSize;
}
#else
#define KMP_GET_PAGE_SIZE() getpagesize()
#endif

#define PAGE_ALIGNED(_addr)                                                    \
  (!((size_t)_addr & (size_t)(KMP_GET_PAGE_SIZE() - 1)))
#define ALIGN_TO_PAGE(x)                                                       \
  (void *)(((size_t)(x)) & ~((size_t)(KMP_GET_PAGE_SIZE() - 1)))

/* ---------- Support for cache alignment, padding, etc. ----------------*/

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define INTERNODE_CACHE_LINE 4096 /* for multi-node systems */

/* Define the default size of the cache line */
#ifndef CACHE_LINE
#define CACHE_LINE 128 /* cache line size in bytes */
#else
#if (CACHE_LINE < 64) && !defined(KMP_OS_DARWIN)
// 2006-02-13: This produces too many warnings on OS X*. Disable for now
#warning CACHE_LINE is too small.
#endif
#endif /* CACHE_LINE */

#define KMP_CACHE_PREFETCH(ADDR) /* nothing */

// Define attribute that indicates a function does not return
#if __cplusplus >= 201103L
#define KMP_NORETURN [[noreturn]]
#elif KMP_OS_WINDOWS
#define KMP_NORETURN __declspec(noreturn)
#else
#define KMP_NORETURN __attribute__((noreturn))
#endif

#if KMP_OS_WINDOWS && KMP_MSVC_COMPAT
#define KMP_ALIGN(bytes) __declspec(align(bytes))
#define KMP_THREAD_LOCAL __declspec(thread)
#define KMP_ALIAS /* Nothing */
#else
#define KMP_ALIGN(bytes) __attribute__((aligned(bytes)))
#define KMP_THREAD_LOCAL __thread
#define KMP_ALIAS(alias_of) __attribute__((alias(alias_of)))
#endif

#if KMP_HAVE_WEAK_ATTRIBUTE
#define KMP_WEAK_ATTRIBUTE __attribute__((weak))
#else
#define KMP_WEAK_ATTRIBUTE /* Nothing */
#endif

// Define KMP_VERSION_SYMBOL and KMP_EXPAND_NAME
#ifndef KMP_STR
#define KMP_STR(x) _KMP_STR(x)
#define _KMP_STR(x) #x
#endif

#ifdef KMP_USE_VERSION_SYMBOLS
// If using versioned symbols, KMP_EXPAND_NAME prepends
// __kmp_api_ to the real API name
#define KMP_EXPAND_NAME(api_name) _KMP_EXPAND_NAME(api_name)
#define _KMP_EXPAND_NAME(api_name) __kmp_api_##api_name
#define KMP_VERSION_SYMBOL(api_name, ver_num, ver_str)                         \
  _KMP_VERSION_SYMBOL(api_name, ver_num, ver_str, "VERSION")
#define _KMP_VERSION_SYMBOL(api_name, ver_num, ver_str, default_ver)            \
  __typeof__(__kmp_api_##api_name) __kmp_api_##api_name##_##ver_num##_alias     \
      __attribute__((alias(KMP_STR(__kmp_api_##api_name))));                    \
  __asm__(                                                                      \
      ".symver " KMP_STR(__kmp_api_##api_name##_##ver_num##_alias) "," KMP_STR( \
          api_name) "@" ver_str "\n\t");                                        \
  __asm__(".symver " KMP_STR(__kmp_api_##api_name) "," KMP_STR(                 \
      api_name) "@@" default_ver "\n\t")
#else // KMP_USE_VERSION_SYMBOLS
#define KMP_EXPAND_NAME(api_name) api_name
#define KMP_VERSION_SYMBOL(api_name, ver_num, ver_str) /* Nothing */
#endif // KMP_USE_VERSION_SYMBOLS

/* Temporary note: if performance testing of this passes, we can remove
   all references to KMP_DO_ALIGN and replace with KMP_ALIGN.  */
#define KMP_DO_ALIGN(bytes) KMP_ALIGN(bytes)
#define KMP_ALIGN_CACHE KMP_ALIGN(CACHE_LINE)
#define KMP_ALIGN_CACHE_INTERNODE KMP_ALIGN(INTERNODE_CACHE_LINE)

/* General purpose fence types for memory operations */
enum kmp_mem_fence_type {
  kmp_no_fence, /* No memory fence */
  kmp_acquire_fence, /* Acquire (read) memory fence */
  kmp_release_fence, /* Release (write) memory fence */
  kmp_full_fence /* Full (read+write) memory fence */
};

// Synchronization primitives

#if KMP_ASM_INTRINS && KMP_OS_WINDOWS

#if KMP_MSVC_COMPAT && !KMP_COMPILER_CLANG
#pragma intrinsic(InterlockedExchangeAdd)
#pragma intrinsic(InterlockedCompareExchange)
#pragma intrinsic(InterlockedExchange)
#pragma intrinsic(InterlockedExchange64)
#endif

// Using InterlockedIncrement / InterlockedDecrement causes a library loading
// ordering problem, so we use InterlockedExchangeAdd instead.
#define KMP_TEST_THEN_INC32(p) InterlockedExchangeAdd((volatile long *)(p), 1)
#define KMP_TEST_THEN_INC_ACQ32(p)                                             \
  InterlockedExchangeAdd((volatile long *)(p), 1)
#define KMP_TEST_THEN_ADD4_32(p) InterlockedExchangeAdd((volatile long *)(p), 4)
#define KMP_TEST_THEN_ADD4_ACQ32(p)                                            \
  InterlockedExchangeAdd((volatile long *)(p), 4)
#define KMP_TEST_THEN_DEC32(p) InterlockedExchangeAdd((volatile long *)(p), -1)
#define KMP_TEST_THEN_DEC_ACQ32(p)                                             \
  InterlockedExchangeAdd((volatile long *)(p), -1)
#define KMP_TEST_THEN_ADD32(p, v)                                              \
  InterlockedExchangeAdd((volatile long *)(p), (v))

#define KMP_COMPARE_AND_STORE_RET32(p, cv, sv)                                 \
  InterlockedCompareExchange((volatile long *)(p), (long)(sv), (long)(cv))

#define KMP_XCHG_FIXED32(p, v)                                                 \
  InterlockedExchange((volatile long *)(p), (long)(v))
#define KMP_XCHG_FIXED64(p, v)                                                 \
  InterlockedExchange64((volatile kmp_int64 *)(p), (kmp_int64)(v))

inline kmp_real32 KMP_XCHG_REAL32(volatile kmp_real32 *p, kmp_real32 v) {
  kmp_int32 tmp = InterlockedExchange((volatile long *)p, *(long *)&v);
  return *(kmp_real32 *)&tmp;
}

// Routines that we still need to implement in assembly.
extern kmp_int8 __kmp_test_then_add8(volatile kmp_int8 *p, kmp_int8 v);
extern kmp_int8 __kmp_test_then_or8(volatile kmp_int8 *p, kmp_int8 v);
extern kmp_int8 __kmp_test_then_and8(volatile kmp_int8 *p, kmp_int8 v);
extern kmp_int32 __kmp_test_then_add32(volatile kmp_int32 *p, kmp_int32 v);
extern kmp_uint32 __kmp_test_then_or32(volatile kmp_uint32 *p, kmp_uint32 v);
extern kmp_uint32 __kmp_test_then_and32(volatile kmp_uint32 *p, kmp_uint32 v);
extern kmp_int64 __kmp_test_then_add64(volatile kmp_int64 *p, kmp_int64 v);
extern kmp_uint64 __kmp_test_then_or64(volatile kmp_uint64 *p, kmp_uint64 v);
extern kmp_uint64 __kmp_test_then_and64(volatile kmp_uint64 *p, kmp_uint64 v);

extern kmp_int8 __kmp_compare_and_store8(volatile kmp_int8 *p, kmp_int8 cv,
                                         kmp_int8 sv);
extern kmp_int16 __kmp_compare_and_store16(volatile kmp_int16 *p, kmp_int16 cv,
                                           kmp_int16 sv);
extern kmp_int32 __kmp_compare_and_store32(volatile kmp_int32 *p, kmp_int32 cv,
                                           kmp_int32 sv);
extern kmp_int32 __kmp_compare_and_store64(volatile kmp_int64 *p, kmp_int64 cv,
                                           kmp_int64 sv);
extern kmp_int8 __kmp_compare_and_store_ret8(volatile kmp_int8 *p, kmp_int8 cv,
                                             kmp_int8 sv);
extern kmp_int16 __kmp_compare_and_store_ret16(volatile kmp_int16 *p,
                                               kmp_int16 cv, kmp_int16 sv);
extern kmp_int32 __kmp_compare_and_store_ret32(volatile kmp_int32 *p,
                                               kmp_int32 cv, kmp_int32 sv);
extern kmp_int64 __kmp_compare_and_store_ret64(volatile kmp_int64 *p,
                                               kmp_int64 cv, kmp_int64 sv);

extern kmp_int8 __kmp_xchg_fixed8(volatile kmp_int8 *p, kmp_int8 v);
extern kmp_int16 __kmp_xchg_fixed16(volatile kmp_int16 *p, kmp_int16 v);
extern kmp_int32 __kmp_xchg_fixed32(volatile kmp_int32 *p, kmp_int32 v);
extern kmp_int64 __kmp_xchg_fixed64(volatile kmp_int64 *p, kmp_int64 v);
extern kmp_real32 __kmp_xchg_real32(volatile kmp_real32 *p, kmp_real32 v);
extern kmp_real64 __kmp_xchg_real64(volatile kmp_real64 *p, kmp_real64 v);

//#define KMP_TEST_THEN_INC32(p) __kmp_test_then_add32((p), 1)
//#define KMP_TEST_THEN_INC_ACQ32(p) __kmp_test_then_add32((p), 1)
#define KMP_TEST_THEN_INC64(p) __kmp_test_then_add64((p), 1LL)
#define KMP_TEST_THEN_INC_ACQ64(p) __kmp_test_then_add64((p), 1LL)
//#define KMP_TEST_THEN_ADD4_32(p) __kmp_test_then_add32((p), 4)
//#define KMP_TEST_THEN_ADD4_ACQ32(p) __kmp_test_then_add32((p), 4)
#define KMP_TEST_THEN_ADD4_64(p) __kmp_test_then_add64((p), 4LL)
#define KMP_TEST_THEN_ADD4_ACQ64(p) __kmp_test_then_add64((p), 4LL)
//#define KMP_TEST_THEN_DEC32(p) __kmp_test_then_add32((p), -1)
//#define KMP_TEST_THEN_DEC_ACQ32(p) __kmp_test_then_add32((p), -1)
#define KMP_TEST_THEN_DEC64(p) __kmp_test_then_add64((p), -1LL)
#define KMP_TEST_THEN_DEC_ACQ64(p) __kmp_test_then_add64((p), -1LL)
//#define KMP_TEST_THEN_ADD32(p, v) __kmp_test_then_add32((p), (v))
#define KMP_TEST_THEN_ADD8(p, v) __kmp_test_then_add8((p), (v))
#define KMP_TEST_THEN_ADD64(p, v) __kmp_test_then_add64((p), (v))

#define KMP_TEST_THEN_OR8(p, v) __kmp_test_then_or8((p), (v))
#define KMP_TEST_THEN_AND8(p, v) __kmp_test_then_and8((p), (v))
#define KMP_TEST_THEN_OR32(p, v) __kmp_test_then_or32((p), (v))
#define KMP_TEST_THEN_AND32(p, v) __kmp_test_then_and32((p), (v))
#define KMP_TEST_THEN_OR64(p, v) __kmp_test_then_or64((p), (v))
#define KMP_TEST_THEN_AND64(p, v) __kmp_test_then_and64((p), (v))

#define KMP_COMPARE_AND_STORE_ACQ8(p, cv, sv)                                  \
  __kmp_compare_and_store8((p), (cv), (sv))
#define KMP_COMPARE_AND_STORE_REL8(p, cv, sv)                                  \
  __kmp_compare_and_store8((p), (cv), (sv))
#define KMP_COMPARE_AND_STORE_ACQ16(p, cv, sv)                                 \
  __kmp_compare_and_store16((p), (cv), (sv))
#define KMP_COMPARE_AND_STORE_REL16(p, cv, sv)                                 \
  __kmp_compare_and_store16((p), (cv), (sv))
#define KMP_COMPARE_AND_STORE_ACQ32(p, cv, sv)                                 \
  __kmp_compare_and_store32((volatile kmp_int32 *)(p), (kmp_int32)(cv),        \
                            (kmp_int32)(sv))
#define KMP_COMPARE_AND_STORE_REL32(p, cv, sv)                                 \
  __kmp_compare_and_store32((volatile kmp_int32 *)(p), (kmp_int32)(cv),        \
                            (kmp_int32)(sv))
#define KMP_COMPARE_AND_STORE_ACQ64(p, cv, sv)                                 \
  __kmp_compare_and_store64((volatile kmp_int64 *)(p), (kmp_int64)(cv),        \
                            (kmp_int64)(sv))
#define KMP_COMPARE_AND_STORE_REL64(p, cv, sv)                                 \
  __kmp_compare_and_store64((volatile kmp_int64 *)(p), (kmp_int64)(cv),        \
                            (kmp_int64)(sv))

#if KMP_ARCH_X86
#define KMP_COMPARE_AND_STORE_PTR(p, cv, sv)                                   \
  __kmp_compare_and_store32((volatile kmp_int32 *)(p), (kmp_int32)(cv),        \
                            (kmp_int32)(sv))
#else /* 64 bit pointers */
#define KMP_COMPARE_AND_STORE_PTR(p, cv, sv)                                   \
  __kmp_compare_and_store64((volatile kmp_int64 *)(p), (kmp_int64)(cv),        \
                            (kmp_int64)(sv))
#endif /* KMP_ARCH_X86 */

#define KMP_COMPARE_AND_STORE_RET8(p, cv, sv)                                  \
  __kmp_compare_and_store_ret8((p), (cv), (sv))
#define KMP_COMPARE_AND_STORE_RET16(p, cv, sv)                                 \
  __kmp_compare_and_store_ret16((p), (cv), (sv))
#define KMP_COMPARE_AND_STORE_RET64(p, cv, sv)                                 \
  __kmp_compare_and_store_ret64((volatile kmp_int64 *)(p), (kmp_int64)(cv),    \
                                (kmp_int64)(sv))

#define KMP_XCHG_FIXED8(p, v)                                                  \
  __kmp_xchg_fixed8((volatile kmp_int8 *)(p), (kmp_int8)(v));
#define KMP_XCHG_FIXED16(p, v) __kmp_xchg_fixed16((p), (v));
//#define KMP_XCHG_FIXED32(p, v) __kmp_xchg_fixed32((p), (v));
//#define KMP_XCHG_FIXED64(p, v) __kmp_xchg_fixed64((p), (v));
//#define KMP_XCHG_REAL32(p, v) __kmp_xchg_real32((p), (v));
#define KMP_XCHG_REAL64(p, v) __kmp_xchg_real64((p), (v));

#elif (KMP_ASM_INTRINS && KMP_OS_UNIX) || !(KMP_ARCH_X86 || KMP_ARCH_X86_64)

/* cast p to correct type so that proper intrinsic will be used */
#define KMP_TEST_THEN_INC32(p)                                                 \
  __sync_fetch_and_add((volatile kmp_int32 *)(p), 1)
#define KMP_TEST_THEN_INC_ACQ32(p)                                             \
  __sync_fetch_and_add((volatile kmp_int32 *)(p), 1)
#define KMP_TEST_THEN_INC64(p)                                                 \
  __sync_fetch_and_add((volatile kmp_int64 *)(p), 1LL)
#define KMP_TEST_THEN_INC_ACQ64(p)                                             \
  __sync_fetch_and_add((volatile kmp_int64 *)(p), 1LL)
#define KMP_TEST_THEN_ADD4_32(p)                                               \
  __sync_fetch_and_add((volatile kmp_int32 *)(p), 4)
#define KMP_TEST_THEN_ADD4_ACQ32(p)                                            \
  __sync_fetch_and_add((volatile kmp_int32 *)(p), 4)
#define KMP_TEST_THEN_ADD4_64(p)                                               \
  __sync_fetch_and_add((volatile kmp_int64 *)(p), 4LL)
#define KMP_TEST_THEN_ADD4_ACQ64(p)                                            \
  __sync_fetch_and_add((volatile kmp_int64 *)(p), 4LL)
#define KMP_TEST_THEN_DEC32(p)                                                 \
  __sync_fetch_and_sub((volatile kmp_int32 *)(p), 1)
#define KMP_TEST_THEN_DEC_ACQ32(p)                                             \
  __sync_fetch_and_sub((volatile kmp_int32 *)(p), 1)
#define KMP_TEST_THEN_DEC64(p)                                                 \
  __sync_fetch_and_sub((volatile kmp_int64 *)(p), 1LL)
#define KMP_TEST_THEN_DEC_ACQ64(p)                                             \
  __sync_fetch_and_sub((volatile kmp_int64 *)(p), 1LL)
#define KMP_TEST_THEN_ADD8(p, v)                                               \
  __sync_fetch_and_add((volatile kmp_int8 *)(p), (kmp_int8)(v))
#define KMP_TEST_THEN_ADD32(p, v)                                              \
  __sync_fetch_and_add((volatile kmp_int32 *)(p), (kmp_int32)(v))
#define KMP_TEST_THEN_ADD64(p, v)                                              \
  __sync_fetch_and_add((volatile kmp_int64 *)(p), (kmp_int64)(v))

#define KMP_TEST_THEN_OR8(p, v)                                                \
  __sync_fetch_and_or((volatile kmp_int8 *)(p), (kmp_int8)(v))
#define KMP_TEST_THEN_AND8(p, v)                                               \
  __sync_fetch_and_and((volatile kmp_int8 *)(p), (kmp_int8)(v))
#define KMP_TEST_THEN_OR32(p, v)                                               \
  __sync_fetch_and_or((volatile kmp_uint32 *)(p), (kmp_uint32)(v))
#define KMP_TEST_THEN_AND32(p, v)                                              \
  __sync_fetch_and_and((volatile kmp_uint32 *)(p), (kmp_uint32)(v))
#define KMP_TEST_THEN_OR64(p, v)                                               \
  __sync_fetch_and_or((volatile kmp_uint64 *)(p), (kmp_uint64)(v))
#define KMP_TEST_THEN_AND64(p, v)                                              \
  __sync_fetch_and_and((volatile kmp_uint64 *)(p), (kmp_uint64)(v))

#define KMP_COMPARE_AND_STORE_ACQ8(p, cv, sv)                                  \
  __sync_bool_compare_and_swap((volatile kmp_uint8 *)(p), (kmp_uint8)(cv),     \
                               (kmp_uint8)(sv))
#define KMP_COMPARE_AND_STORE_REL8(p, cv, sv)                                  \
  __sync_bool_compare_and_swap((volatile kmp_uint8 *)(p), (kmp_uint8)(cv),     \
                               (kmp_uint8)(sv))
#define KMP_COMPARE_AND_STORE_ACQ16(p, cv, sv)                                 \
  __sync_bool_compare_and_swap((volatile kmp_uint16 *)(p), (kmp_uint16)(cv),   \
                               (kmp_uint16)(sv))
#define KMP_COMPARE_AND_STORE_REL16(p, cv, sv)                                 \
  __sync_bool_compare_and_swap((volatile kmp_uint16 *)(p), (kmp_uint16)(cv),   \
                               (kmp_uint16)(sv))
#define KMP_COMPARE_AND_STORE_ACQ32(p, cv, sv)                                 \
  __sync_bool_compare_and_swap((volatile kmp_uint32 *)(p), (kmp_uint32)(cv),   \
                               (kmp_uint32)(sv))
#define KMP_COMPARE_AND_STORE_REL32(p, cv, sv)                                 \
  __sync_bool_compare_and_swap((volatile kmp_uint32 *)(p), (kmp_uint32)(cv),   \
                               (kmp_uint32)(sv))
#define KMP_COMPARE_AND_STORE_ACQ64(p, cv, sv)                                 \
  __sync_bool_compare_and_swap((volatile kmp_uint64 *)(p), (kmp_uint64)(cv),   \
                               (kmp_uint64)(sv))
#define KMP_COMPARE_AND_STORE_REL64(p, cv, sv)                                 \
  __sync_bool_compare_and_swap((volatile kmp_uint64 *)(p), (kmp_uint64)(cv),   \
                               (kmp_uint64)(sv))
#define KMP_COMPARE_AND_STORE_PTR(p, cv, sv)                                   \
  __sync_bool_compare_and_swap((void *volatile *)(p), (void *)(cv),            \
                               (void *)(sv))

#define KMP_COMPARE_AND_STORE_RET8(p, cv, sv)                                  \
  __sync_val_compare_and_swap((volatile kmp_uint8 *)(p), (kmp_uint8)(cv),      \
                              (kmp_uint8)(sv))
#define KMP_COMPARE_AND_STORE_RET16(p, cv, sv)                                 \
  __sync_val_compare_and_swap((volatile kmp_uint16 *)(p), (kmp_uint16)(cv),    \
                              (kmp_uint16)(sv))
#define KMP_COMPARE_AND_STORE_RET32(p, cv, sv)                                 \
  __sync_val_compare_and_swap((volatile kmp_uint32 *)(p), (kmp_uint32)(cv),    \
                              (kmp_uint32)(sv))
#define KMP_COMPARE_AND_STORE_RET64(p, cv, sv)                                 \
  __sync_val_compare_and_swap((volatile kmp_uint64 *)(p), (kmp_uint64)(cv),    \
                              (kmp_uint64)(sv))

#define KMP_XCHG_FIXED8(p, v)                                                  \
  __sync_lock_test_and_set((volatile kmp_uint8 *)(p), (kmp_uint8)(v))
#define KMP_XCHG_FIXED16(p, v)                                                 \
  __sync_lock_test_and_set((volatile kmp_uint16 *)(p), (kmp_uint16)(v))
#define KMP_XCHG_FIXED32(p, v)                                                 \
  __sync_lock_test_and_set((volatile kmp_uint32 *)(p), (kmp_uint32)(v))
#define KMP_XCHG_FIXED64(p, v)                                                 \
  __sync_lock_test_and_set((volatile kmp_uint64 *)(p), (kmp_uint64)(v))

inline kmp_real32 KMP_XCHG_REAL32(volatile kmp_real32 *p, kmp_real32 v) {
  kmp_int32 tmp =
      __sync_lock_test_and_set((volatile kmp_uint32 *)(p), *(kmp_uint32 *)&v);
  return *(kmp_real32 *)&tmp;
}

inline kmp_real64 KMP_XCHG_REAL64(volatile kmp_real64 *p, kmp_real64 v) {
  kmp_int64 tmp =
      __sync_lock_test_and_set((volatile kmp_uint64 *)(p), *(kmp_uint64 *)&v);
  return *(kmp_real64 *)&tmp;
}

#else

extern kmp_int8 __kmp_test_then_add8(volatile kmp_int8 *p, kmp_int8 v);
extern kmp_int8 __kmp_test_then_or8(volatile kmp_int8 *p, kmp_int8 v);
extern kmp_int8 __kmp_test_then_and8(volatile kmp_int8 *p, kmp_int8 v);
extern kmp_int32 __kmp_test_then_add32(volatile kmp_int32 *p, kmp_int32 v);
extern kmp_uint32 __kmp_test_then_or32(volatile kmp_uint32 *p, kmp_uint32 v);
extern kmp_uint32 __kmp_test_then_and32(volatile kmp_uint32 *p, kmp_uint32 v);
extern kmp_int64 __kmp_test_then_add64(volatile kmp_int64 *p, kmp_int64 v);
extern kmp_uint64 __kmp_test_then_or64(volatile kmp_uint64 *p, kmp_uint64 v);
extern kmp_uint64 __kmp_test_then_and64(volatile kmp_uint64 *p, kmp_uint64 v);

extern kmp_int8 __kmp_compare_and_store8(volatile kmp_int8 *p, kmp_int8 cv,
                                         kmp_int8 sv);
extern kmp_int16 __kmp_compare_and_store16(volatile kmp_int16 *p, kmp_int16 cv,
                                           kmp_int16 sv);
extern kmp_int32 __kmp_compare_and_store32(volatile kmp_int32 *p, kmp_int32 cv,
                                           kmp_int32 sv);
extern kmp_int32 __kmp_compare_and_store64(volatile kmp_int64 *p, kmp_int64 cv,
                                           kmp_int64 sv);
extern kmp_int8 __kmp_compare_and_store_ret8(volatile kmp_int8 *p, kmp_int8 cv,
                                             kmp_int8 sv);
extern kmp_int16 __kmp_compare_and_store_ret16(volatile kmp_int16 *p,
                                               kmp_int16 cv, kmp_int16 sv);
extern kmp_int32 __kmp_compare_and_store_ret32(volatile kmp_int32 *p,
                                               kmp_int32 cv, kmp_int32 sv);
extern kmp_int64 __kmp_compare_and_store_ret64(volatile kmp_int64 *p,
                                               kmp_int64 cv, kmp_int64 sv);

extern kmp_int8 __kmp_xchg_fixed8(volatile kmp_int8 *p, kmp_int8 v);
extern kmp_int16 __kmp_xchg_fixed16(volatile kmp_int16 *p, kmp_int16 v);
extern kmp_int32 __kmp_xchg_fixed32(volatile kmp_int32 *p, kmp_int32 v);
extern kmp_int64 __kmp_xchg_fixed64(volatile kmp_int64 *p, kmp_int64 v);
extern kmp_real32 __kmp_xchg_real32(volatile kmp_real32 *p, kmp_real32 v);
extern kmp_real64 __kmp_xchg_real64(volatile kmp_real64 *p, kmp_real64 v);

#define KMP_TEST_THEN_INC32(p)                                                 \
  __kmp_test_then_add32((volatile kmp_int32 *)(p), 1)
#define KMP_TEST_THEN_INC_ACQ32(p)                                             \
  __kmp_test_then_add32((volatile kmp_int32 *)(p), 1)
#define KMP_TEST_THEN_INC64(p)                                                 \
  __kmp_test_then_add64((volatile kmp_int64 *)(p), 1LL)
#define KMP_TEST_THEN_INC_ACQ64(p)                                             \
  __kmp_test_then_add64((volatile kmp_int64 *)(p), 1LL)
#define KMP_TEST_THEN_ADD4_32(p)                                               \
  __kmp_test_then_add32((volatile kmp_int32 *)(p), 4)
#define KMP_TEST_THEN_ADD4_ACQ32(p)                                            \
  __kmp_test_then_add32((volatile kmp_int32 *)(p), 4)
#define KMP_TEST_THEN_ADD4_64(p)                                               \
  __kmp_test_then_add64((volatile kmp_int64 *)(p), 4LL)
#define KMP_TEST_THEN_ADD4_ACQ64(p)                                            \
  __kmp_test_then_add64((volatile kmp_int64 *)(p), 4LL)
#define KMP_TEST_THEN_DEC32(p)                                                 \
  __kmp_test_then_add32((volatile kmp_int32 *)(p), -1)
#define KMP_TEST_THEN_DEC_ACQ32(p)                                             \
  __kmp_test_then_add32((volatile kmp_int32 *)(p), -1)
#define KMP_TEST_THEN_DEC64(p)                                                 \
  __kmp_test_then_add64((volatile kmp_int64 *)(p), -1LL)
#define KMP_TEST_THEN_DEC_ACQ64(p)                                             \
  __kmp_test_then_add64((volatile kmp_int64 *)(p), -1LL)
#define KMP_TEST_THEN_ADD8(p, v)                                               \
  __kmp_test_then_add8((volatile kmp_int8 *)(p), (kmp_int8)(v))
#define KMP_TEST_THEN_ADD32(p, v)                                              \
  __kmp_test_then_add32((volatile kmp_int32 *)(p), (kmp_int32)(v))
#define KMP_TEST_THEN_ADD64(p, v)                                              \
  __kmp_test_then_add64((volatile kmp_int64 *)(p), (kmp_int64)(v))

#define KMP_TEST_THEN_OR8(p, v)                                                \
  __kmp_test_then_or8((volatile kmp_int8 *)(p), (kmp_int8)(v))
#define KMP_TEST_THEN_AND8(p, v)                                               \
  __kmp_test_then_and8((volatile kmp_int8 *)(p), (kmp_int8)(v))
#define KMP_TEST_THEN_OR32(p, v)                                               \
  __kmp_test_then_or32((volatile kmp_uint32 *)(p), (kmp_uint32)(v))
#define KMP_TEST_THEN_AND32(p, v)                                              \
  __kmp_test_then_and32((volatile kmp_uint32 *)(p), (kmp_uint32)(v))
#define KMP_TEST_THEN_OR64(p, v)                                               \
  __kmp_test_then_or64((volatile kmp_uint64 *)(p), (kmp_uint64)(v))
#define KMP_TEST_THEN_AND64(p, v)                                              \
  __kmp_test_then_and64((volatile kmp_uint64 *)(p), (kmp_uint64)(v))

#define KMP_COMPARE_AND_STORE_ACQ8(p, cv, sv)                                  \
  __kmp_compare_and_store8((volatile kmp_int8 *)(p), (kmp_int8)(cv),           \
                           (kmp_int8)(sv))
#define KMP_COMPARE_AND_STORE_REL8(p, cv, sv)                                  \
  __kmp_compare_and_store8((volatile kmp_int8 *)(p), (kmp_int8)(cv),           \
                           (kmp_int8)(sv))
#define KMP_COMPARE_AND_STORE_ACQ16(p, cv, sv)                                 \
  __kmp_compare_and_store16((volatile kmp_int16 *)(p), (kmp_int16)(cv),        \
                            (kmp_int16)(sv))
#define KMP_COMPARE_AND_STORE_REL16(p, cv, sv)                                 \
  __kmp_compare_and_store16((volatile kmp_int16 *)(p), (kmp_int16)(cv),        \
                            (kmp_int16)(sv))
#define KMP_COMPARE_AND_STORE_ACQ32(p, cv, sv)                                 \
  __kmp_compare_and_store32((volatile kmp_int32 *)(p), (kmp_int32)(cv),        \
                            (kmp_int32)(sv))
#define KMP_COMPARE_AND_STORE_REL32(p, cv, sv)                                 \
  __kmp_compare_and_store32((volatile kmp_int32 *)(p), (kmp_int32)(cv),        \
                            (kmp_int32)(sv))
#define KMP_COMPARE_AND_STORE_ACQ64(p, cv, sv)                                 \
  __kmp_compare_and_store64((volatile kmp_int64 *)(p), (kmp_int64)(cv),        \
                            (kmp_int64)(sv))
#define KMP_COMPARE_AND_STORE_REL64(p, cv, sv)                                 \
  __kmp_compare_and_store64((volatile kmp_int64 *)(p), (kmp_int64)(cv),        \
                            (kmp_int64)(sv))

#if KMP_ARCH_X86
#define KMP_COMPARE_AND_STORE_PTR(p, cv, sv)                                   \
  __kmp_compare_and_store32((volatile kmp_int32 *)(p), (kmp_int32)(cv),        \
                            (kmp_int32)(sv))
#else /* 64 bit pointers */
#define KMP_COMPARE_AND_STORE_PTR(p, cv, sv)                                   \
  __kmp_compare_and_store64((volatile kmp_int64 *)(p), (kmp_int64)(cv),        \
                            (kmp_int64)(sv))
#endif /* KMP_ARCH_X86 */

#define KMP_COMPARE_AND_STORE_RET8(p, cv, sv)                                  \
  __kmp_compare_and_store_ret8((p), (cv), (sv))
#define KMP_COMPARE_AND_STORE_RET16(p, cv, sv)                                 \
  __kmp_compare_and_store_ret16((p), (cv), (sv))
#define KMP_COMPARE_AND_STORE_RET32(p, cv, sv)                                 \
  __kmp_compare_and_store_ret32((volatile kmp_int32 *)(p), (kmp_int32)(cv),    \
                                (kmp_int32)(sv))
#define KMP_COMPARE_AND_STORE_RET64(p, cv, sv)                                 \
  __kmp_compare_and_store_ret64((volatile kmp_int64 *)(p), (kmp_int64)(cv),    \
                                (kmp_int64)(sv))

#define KMP_XCHG_FIXED8(p, v)                                                  \
  __kmp_xchg_fixed8((volatile kmp_int8 *)(p), (kmp_int8)(v));
#define KMP_XCHG_FIXED16(p, v) __kmp_xchg_fixed16((p), (v));
#define KMP_XCHG_FIXED32(p, v) __kmp_xchg_fixed32((p), (v));
#define KMP_XCHG_FIXED64(p, v) __kmp_xchg_fixed64((p), (v));
#define KMP_XCHG_REAL32(p, v) __kmp_xchg_real32((p), (v));
#define KMP_XCHG_REAL64(p, v) __kmp_xchg_real64((p), (v));

#endif /* KMP_ASM_INTRINS */

/* ------------- relaxed consistency memory model stuff ------------------ */

#if KMP_OS_WINDOWS
#ifdef __ABSOFT_WIN
#define KMP_MB() asm("nop")
#define KMP_IMB() asm("nop")
#else
#define KMP_MB() /* _asm{ nop } */
#define KMP_IMB() /* _asm{ nop } */
#endif
#endif /* KMP_OS_WINDOWS */

#if KMP_ARCH_PPC64 || KMP_ARCH_ARM || KMP_ARCH_AARCH64 || KMP_ARCH_MIPS ||     \
    KMP_ARCH_MIPS64
#define KMP_MB() __sync_synchronize()
#endif

#ifndef KMP_MB
#define KMP_MB() /* nothing to do */
#endif

#ifndef KMP_IMB
#define KMP_IMB() /* nothing to do */
#endif

#ifndef KMP_ST_REL32
#define KMP_ST_REL32(A, D) (*(A) = (D))
#endif

#ifndef KMP_ST_REL64
#define KMP_ST_REL64(A, D) (*(A) = (D))
#endif

#ifndef KMP_LD_ACQ32
#define KMP_LD_ACQ32(A) (*(A))
#endif

#ifndef KMP_LD_ACQ64
#define KMP_LD_ACQ64(A) (*(A))
#endif

/* ------------------------------------------------------------------------ */
// FIXME - maybe this should this be
//
// #define TCR_4(a)    (*(volatile kmp_int32 *)(&a))
// #define TCW_4(a,b)  (a) = (*(volatile kmp_int32 *)&(b))
//
// #define TCR_8(a)    (*(volatile kmp_int64 *)(a))
// #define TCW_8(a,b)  (a) = (*(volatile kmp_int64 *)(&b))
//
// I'm fairly certain this is the correct thing to do, but I'm afraid
// of performance regressions.

#define TCR_1(a) (a)
#define TCW_1(a, b) (a) = (b)
#define TCR_4(a) (a)
#define TCW_4(a, b) (a) = (b)
#define TCI_4(a) (++(a))
#define TCD_4(a) (--(a))
#define TCR_8(a) (a)
#define TCW_8(a, b) (a) = (b)
#define TCI_8(a) (++(a))
#define TCD_8(a) (--(a))
#define TCR_SYNC_4(a) (a)
#define TCW_SYNC_4(a, b) (a) = (b)
#define TCX_SYNC_4(a, b, c)                                                    \
  KMP_COMPARE_AND_STORE_REL32((volatile kmp_int32 *)(volatile void *)&(a),     \
                              (kmp_int32)(b), (kmp_int32)(c))
#define TCR_SYNC_8(a) (a)
#define TCW_SYNC_8(a, b) (a) = (b)
#define TCX_SYNC_8(a, b, c)                                                    \
  KMP_COMPARE_AND_STORE_REL64((volatile kmp_int64 *)(volatile void *)&(a),     \
                              (kmp_int64)(b), (kmp_int64)(c))

#if KMP_ARCH_X86 || KMP_ARCH_MIPS
// What about ARM?
#define TCR_PTR(a) ((void *)TCR_4(a))
#define TCW_PTR(a, b) TCW_4((a), (b))
#define TCR_SYNC_PTR(a) ((void *)TCR_SYNC_4(a))
#define TCW_SYNC_PTR(a, b) TCW_SYNC_4((a), (b))
#define TCX_SYNC_PTR(a, b, c) ((void *)TCX_SYNC_4((a), (b), (c)))

#else /* 64 bit pointers */

#define TCR_PTR(a) ((void *)TCR_8(a))
#define TCW_PTR(a, b) TCW_8((a), (b))
#define TCR_SYNC_PTR(a) ((void *)TCR_SYNC_8(a))
#define TCW_SYNC_PTR(a, b) TCW_SYNC_8((a), (b))
#define TCX_SYNC_PTR(a, b, c) ((void *)TCX_SYNC_8((a), (b), (c)))

#endif /* KMP_ARCH_X86 */

/* If these FTN_{TRUE,FALSE} values change, may need to change several places
   where they are used to check that language is Fortran, not C. */

#ifndef FTN_TRUE
#define FTN_TRUE TRUE
#endif

#ifndef FTN_FALSE
#define FTN_FALSE FALSE
#endif

typedef void (*microtask_t)(int *gtid, int *npr, ...);

#ifdef USE_VOLATILE_CAST
#define VOLATILE_CAST(x) (volatile x)
#else
#define VOLATILE_CAST(x) (x)
#endif

#define KMP_WAIT_YIELD __kmp_wait_yield_4
#define KMP_WAIT_YIELD_PTR __kmp_wait_yield_4_ptr
#define KMP_EQ __kmp_eq_4
#define KMP_NEQ __kmp_neq_4
#define KMP_LT __kmp_lt_4
#define KMP_GE __kmp_ge_4
#define KMP_LE __kmp_le_4

/* Workaround for Intel(R) 64 code gen bug when taking address of static array
 * (Intel(R) 64 Tracker #138) */
#if (KMP_ARCH_X86_64 || KMP_ARCH_PPC64) && KMP_OS_LINUX
#define STATIC_EFI2_WORKAROUND
#else
#define STATIC_EFI2_WORKAROUND static
#endif

// Support of BGET usage
#ifndef KMP_USE_BGET
#define KMP_USE_BGET 1
#endif

// Switches for OSS builds
#ifndef USE_CMPXCHG_FIX
#define USE_CMPXCHG_FIX 1
#endif

// Enable dynamic user lock
#if OMP_45_ENABLED
#define KMP_USE_DYNAMIC_LOCK 1
#endif

// Enable Intel(R) Transactional Synchronization Extensions (Intel(R) TSX) if
// dynamic user lock is turned on
#if KMP_USE_DYNAMIC_LOCK
// Visual studio can't handle the asm sections in this code
#define KMP_USE_TSX (KMP_ARCH_X86 || KMP_ARCH_X86_64) && !KMP_COMPILER_MSVC
#ifdef KMP_USE_ADAPTIVE_LOCKS
#undef KMP_USE_ADAPTIVE_LOCKS
#endif
#define KMP_USE_ADAPTIVE_LOCKS KMP_USE_TSX
#endif

// Enable tick time conversion of ticks to seconds
#if KMP_STATS_ENABLED
#define KMP_HAVE_TICK_TIME                                                     \
  (KMP_OS_LINUX && (KMP_MIC || KMP_ARCH_X86 || KMP_ARCH_X86_64))
#endif

// Warning levels
enum kmp_warnings_level {
  kmp_warnings_off = 0, /* No warnings */
  kmp_warnings_low, /* Minimal warnings (default) */
  kmp_warnings_explicit = 6, /* Explicitly set to ON - more warnings */
  kmp_warnings_verbose /* reserved */
};

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

// Macros for C++11 atomic functions
#define KMP_ATOMIC_LD(p, order) (p)->load(std::memory_order_##order)
#define KMP_ATOMIC_OP(op, p, v, order) (p)->op(v, std::memory_order_##order)

// For non-default load/store
#define KMP_ATOMIC_LD_ACQ(p) KMP_ATOMIC_LD(p, acquire)
#define KMP_ATOMIC_LD_RLX(p) KMP_ATOMIC_LD(p, relaxed)
#define KMP_ATOMIC_ST_REL(p, v) KMP_ATOMIC_OP(store, p, v, release)
#define KMP_ATOMIC_ST_RLX(p, v) KMP_ATOMIC_OP(store, p, v, relaxed)

// For non-default fetch_<op>
#define KMP_ATOMIC_ADD(p, v) KMP_ATOMIC_OP(fetch_add, p, v, acq_rel)
#define KMP_ATOMIC_SUB(p, v) KMP_ATOMIC_OP(fetch_sub, p, v, acq_rel)
#define KMP_ATOMIC_AND(p, v) KMP_ATOMIC_OP(fetch_and, p, v, acq_rel)
#define KMP_ATOMIC_OR(p, v) KMP_ATOMIC_OP(fetch_or, p, v, acq_rel)
#define KMP_ATOMIC_INC(p) KMP_ATOMIC_OP(fetch_add, p, 1, acq_rel)
#define KMP_ATOMIC_DEC(p) KMP_ATOMIC_OP(fetch_sub, p, 1, acq_rel)
#define KMP_ATOMIC_ADD_RLX(p, v) KMP_ATOMIC_OP(fetch_add, p, v, relaxed)
#define KMP_ATOMIC_INC_RLX(p) KMP_ATOMIC_OP(fetch_add, p, 1, relaxed)

// Callers of the following functions cannot see the side effect on "expected".
template <typename T>
bool __kmp_atomic_compare_store(std::atomic<T> *p, T expected, T desired) {
  return p->compare_exchange_strong(
      expected, desired, std::memory_order_acq_rel, std::memory_order_relaxed);
}

template <typename T>
bool __kmp_atomic_compare_store_acq(std::atomic<T> *p, T expected, T desired) {
  return p->compare_exchange_strong(
      expected, desired, std::memory_order_acquire, std::memory_order_relaxed);
}

template <typename T>
bool __kmp_atomic_compare_store_rel(std::atomic<T> *p, T expected, T desired) {
  return p->compare_exchange_strong(
      expected, desired, std::memory_order_release, std::memory_order_relaxed);
}

#endif /* KMP_OS_H */
// Safe C API
#include "kmp_safe_c_api.h"

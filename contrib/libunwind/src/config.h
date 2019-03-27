//===----------------------------- config.h -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//
//  Defines macros used within libunwind project.
//
//===----------------------------------------------------------------------===//


#ifndef LIBUNWIND_CONFIG_H
#define LIBUNWIND_CONFIG_H

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

// Define static_assert() unless already defined by compiler.
#ifndef __has_feature
  #define __has_feature(__x) 0
#endif
#if !(__has_feature(cxx_static_assert)) && !defined(static_assert)
  #define static_assert(__b, __m) \
      extern int compile_time_assert_failed[ ( __b ) ? 1 : -1 ]  \
                                                  __attribute__( ( unused ) );
#endif

// Platform specific configuration defines.
#ifdef __APPLE__
  #if defined(FOR_DYLD)
    #define _LIBUNWIND_SUPPORT_COMPACT_UNWIND
  #else
    #define _LIBUNWIND_SUPPORT_COMPACT_UNWIND
    #define _LIBUNWIND_SUPPORT_DWARF_UNWIND   1
  #endif
#elif defined(_WIN32)
  #ifdef __SEH__
    #define _LIBUNWIND_SUPPORT_SEH_UNWIND 1
  #else
    #define _LIBUNWIND_SUPPORT_DWARF_UNWIND 1
  #endif
#else
  #if defined(__ARM_DWARF_EH__) || !defined(__arm__)
    #define _LIBUNWIND_SUPPORT_DWARF_UNWIND 1
    #define _LIBUNWIND_SUPPORT_DWARF_INDEX 1
  #endif
#endif

#if defined(_LIBUNWIND_DISABLE_VISIBILITY_ANNOTATIONS)
  #define _LIBUNWIND_EXPORT
  #define _LIBUNWIND_HIDDEN
#else
  #if !defined(__ELF__) && !defined(__MACH__)
    #define _LIBUNWIND_EXPORT __declspec(dllexport)
    #define _LIBUNWIND_HIDDEN
  #else
    #define _LIBUNWIND_EXPORT __attribute__((visibility("default")))
    #define _LIBUNWIND_HIDDEN __attribute__((visibility("hidden")))
  #endif
#endif

#if (defined(__APPLE__) && defined(__arm__)) || defined(__USING_SJLJ_EXCEPTIONS__)
#define _LIBUNWIND_BUILD_SJLJ_APIS
#endif

#if defined(__i386__) || defined(__x86_64__) || defined(__ppc__) || defined(__ppc64__) || defined(__powerpc64__)
#define _LIBUNWIND_SUPPORT_FRAME_APIS
#endif

#if defined(__i386__) || defined(__x86_64__) ||                                \
    defined(__ppc__) || defined(__ppc64__) || defined(__powerpc64__) ||        \
    (!defined(__APPLE__) && defined(__arm__)) ||                               \
    (defined(__arm64__) || defined(__aarch64__)) ||                            \
    defined(__mips__) ||                                                       \
    defined(__riscv)
#if !defined(_LIBUNWIND_BUILD_SJLJ_APIS)
#define _LIBUNWIND_BUILD_ZERO_COST_APIS
#endif
#endif

#if defined(__powerpc64__) && defined(_ARCH_PWR8)
#define PPC64_HAS_VMX
#endif

#if defined(NDEBUG) && defined(_LIBUNWIND_IS_BAREMETAL)
#define _LIBUNWIND_ABORT(msg)                                                  \
  do {                                                                         \
    abort();                                                                   \
  } while (0)
#else
#define _LIBUNWIND_ABORT(msg)                                                  \
  do {                                                                         \
    fprintf(stderr, "libunwind: %s %s:%d - %s\n", __func__, __FILE__,          \
            __LINE__, msg);                                                    \
    fflush(stderr);                                                            \
    abort();                                                                   \
  } while (0)
#endif

#if defined(NDEBUG) && defined(_LIBUNWIND_IS_BAREMETAL)
#define _LIBUNWIND_LOG0(msg)
#define _LIBUNWIND_LOG(msg, ...)
#else
#define _LIBUNWIND_LOG0(msg)                                               \
  fprintf(stderr, "libunwind: " msg "\n")
#define _LIBUNWIND_LOG(msg, ...)                                               \
  fprintf(stderr, "libunwind: " msg "\n", __VA_ARGS__)
#endif

#if defined(NDEBUG)
  #define _LIBUNWIND_LOG_IF_FALSE(x) x
#else
  #define _LIBUNWIND_LOG_IF_FALSE(x)                                           \
    do {                                                                       \
      bool _ret = x;                                                           \
      if (!_ret)                                                               \
        _LIBUNWIND_LOG("" #x " failed in %s", __FUNCTION__);                   \
    } while (0)
#endif

// Macros that define away in non-Debug builds
#ifdef NDEBUG
  #define _LIBUNWIND_DEBUG_LOG(msg, ...)
  #define _LIBUNWIND_TRACE_API(msg, ...)
  #define _LIBUNWIND_TRACING_UNWINDING (0)
  #define _LIBUNWIND_TRACING_DWARF (0)
  #define _LIBUNWIND_TRACE_UNWINDING(msg, ...)
  #define _LIBUNWIND_TRACE_DWARF(...)
#else
  #ifdef __cplusplus
    extern "C" {
  #endif
    extern  bool logAPIs();
    extern  bool logUnwinding();
    extern  bool logDWARF();
  #ifdef __cplusplus
    }
  #endif
  #define _LIBUNWIND_DEBUG_LOG(msg, ...)  _LIBUNWIND_LOG(msg, __VA_ARGS__)
  #define _LIBUNWIND_TRACE_API(msg, ...)                                       \
    do {                                                                       \
      if (logAPIs())                                                           \
        _LIBUNWIND_LOG(msg, __VA_ARGS__);                                      \
    } while (0)
  #define _LIBUNWIND_TRACING_UNWINDING logUnwinding()
  #define _LIBUNWIND_TRACING_DWARF logDWARF()
  #define _LIBUNWIND_TRACE_UNWINDING(msg, ...)                                 \
    do {                                                                       \
      if (logUnwinding())                                                      \
        _LIBUNWIND_LOG(msg, __VA_ARGS__);                                      \
    } while (0)
  #define _LIBUNWIND_TRACE_DWARF(...)                                          \
    do {                                                                       \
      if (logDWARF())                                                          \
        fprintf(stderr, __VA_ARGS__);                                          \
    } while (0)
#endif

#ifdef __cplusplus
// Used to fit UnwindCursor and Registers_xxx types against unw_context_t /
// unw_cursor_t sized memory blocks.
#if defined(_LIBUNWIND_IS_NATIVE_ONLY)
# define COMP_OP ==
#else
# define COMP_OP <=
#endif
template <typename _Type, typename _Mem>
struct check_fit {
  template <typename T>
  struct blk_count {
    static const size_t count =
      (sizeof(T) + sizeof(uint64_t) - 1) / sizeof(uint64_t);
  };
  static const bool does_fit =
    (blk_count<_Type>::count COMP_OP blk_count<_Mem>::count);
};
#undef COMP_OP
#endif // __cplusplus

#endif // LIBUNWIND_CONFIG_H

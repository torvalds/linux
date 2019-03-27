/*
 * kmp_debug.h -- debug / assertion code for Assure library
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef KMP_DEBUG_H
#define KMP_DEBUG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// -----------------------------------------------------------------------------
// Build-time assertion.

// New C++11 style build assert
#define KMP_BUILD_ASSERT(expr) static_assert(expr, "Build condition error")

// -----------------------------------------------------------------------------
// Run-time assertions.

extern void __kmp_dump_debug_buffer(void);

#ifdef KMP_USE_ASSERT
extern int __kmp_debug_assert(char const *expr, char const *file, int line);
#ifdef KMP_DEBUG
#define KMP_ASSERT(cond)                                                       \
  if (!(cond)) {                                                               \
    __kmp_debug_assert(#cond, __FILE__, __LINE__);                             \
  }
#define KMP_ASSERT2(cond, msg)                                                 \
  if (!(cond)) {                                                               \
    __kmp_debug_assert((msg), __FILE__, __LINE__);                             \
  }
#define KMP_DEBUG_ASSERT(cond) KMP_ASSERT(cond)
#define KMP_DEBUG_ASSERT2(cond, msg) KMP_ASSERT2(cond, msg)
#define KMP_DEBUG_USE_VAR(x) /* Nothing (it is used!) */
#else
// Do not expose condition in release build. Use "assertion failure".
#define KMP_ASSERT(cond)                                                       \
  if (!(cond)) {                                                               \
    __kmp_debug_assert("assertion failure", __FILE__, __LINE__);               \
  }
#define KMP_ASSERT2(cond, msg) KMP_ASSERT(cond)
#define KMP_DEBUG_ASSERT(cond) /* Nothing */
#define KMP_DEBUG_ASSERT2(cond, msg) /* Nothing */
#define KMP_DEBUG_USE_VAR(x) ((void)(x))
#endif // KMP_DEBUG
#else
#define KMP_ASSERT(cond) /* Nothing */
#define KMP_ASSERT2(cond, msg) /* Nothing */
#define KMP_DEBUG_ASSERT(cond) /* Nothing */
#define KMP_DEBUG_ASSERT2(cond, msg) /* Nothing */
#define KMP_DEBUG_USE_VAR(x) ((void)(x))
#endif // KMP_USE_ASSERT

#ifdef KMP_DEBUG
extern void __kmp_debug_printf_stdout(char const *format, ...);
#endif
extern void __kmp_debug_printf(char const *format, ...);

#ifdef KMP_DEBUG

extern int kmp_a_debug;
extern int kmp_b_debug;
extern int kmp_c_debug;
extern int kmp_d_debug;
extern int kmp_e_debug;
extern int kmp_f_debug;
extern int kmp_diag;

#define KA_TRACE(d, x)                                                         \
  if (kmp_a_debug >= d) {                                                      \
    __kmp_debug_printf x;                                                      \
  }
#define KB_TRACE(d, x)                                                         \
  if (kmp_b_debug >= d) {                                                      \
    __kmp_debug_printf x;                                                      \
  }
#define KC_TRACE(d, x)                                                         \
  if (kmp_c_debug >= d) {                                                      \
    __kmp_debug_printf x;                                                      \
  }
#define KD_TRACE(d, x)                                                         \
  if (kmp_d_debug >= d) {                                                      \
    __kmp_debug_printf x;                                                      \
  }
#define KE_TRACE(d, x)                                                         \
  if (kmp_e_debug >= d) {                                                      \
    __kmp_debug_printf x;                                                      \
  }
#define KF_TRACE(d, x)                                                         \
  if (kmp_f_debug >= d) {                                                      \
    __kmp_debug_printf x;                                                      \
  }
#define K_DIAG(d, x)                                                           \
  {                                                                            \
    if (kmp_diag == d) {                                                       \
      __kmp_debug_printf_stdout x;                                             \
    }                                                                          \
  }

#define KA_DUMP(d, x)                                                          \
  if (kmp_a_debug >= d) {                                                      \
    int ks;                                                                    \
    __kmp_disable(&ks);                                                        \
    (x);                                                                       \
    __kmp_enable(ks);                                                          \
  }
#define KB_DUMP(d, x)                                                          \
  if (kmp_b_debug >= d) {                                                      \
    int ks;                                                                    \
    __kmp_disable(&ks);                                                        \
    (x);                                                                       \
    __kmp_enable(ks);                                                          \
  }
#define KC_DUMP(d, x)                                                          \
  if (kmp_c_debug >= d) {                                                      \
    int ks;                                                                    \
    __kmp_disable(&ks);                                                        \
    (x);                                                                       \
    __kmp_enable(ks);                                                          \
  }
#define KD_DUMP(d, x)                                                          \
  if (kmp_d_debug >= d) {                                                      \
    int ks;                                                                    \
    __kmp_disable(&ks);                                                        \
    (x);                                                                       \
    __kmp_enable(ks);                                                          \
  }
#define KE_DUMP(d, x)                                                          \
  if (kmp_e_debug >= d) {                                                      \
    int ks;                                                                    \
    __kmp_disable(&ks);                                                        \
    (x);                                                                       \
    __kmp_enable(ks);                                                          \
  }
#define KF_DUMP(d, x)                                                          \
  if (kmp_f_debug >= d) {                                                      \
    int ks;                                                                    \
    __kmp_disable(&ks);                                                        \
    (x);                                                                       \
    __kmp_enable(ks);                                                          \
  }

#else

#define KA_TRACE(d, x) /* nothing to do */
#define KB_TRACE(d, x) /* nothing to do */
#define KC_TRACE(d, x) /* nothing to do */
#define KD_TRACE(d, x) /* nothing to do */
#define KE_TRACE(d, x) /* nothing to do */
#define KF_TRACE(d, x) /* nothing to do */
#define K_DIAG(d, x)                                                           \
  {} /* nothing to do */

#define KA_DUMP(d, x) /* nothing to do */
#define KB_DUMP(d, x) /* nothing to do */
#define KC_DUMP(d, x) /* nothing to do */
#define KD_DUMP(d, x) /* nothing to do */
#define KE_DUMP(d, x) /* nothing to do */
#define KF_DUMP(d, x) /* nothing to do */

#endif // KMP_DEBUG

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif /* KMP_DEBUG_H */

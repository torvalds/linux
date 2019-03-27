/* ===-- int_math.h - internal math inlines ---------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===-----------------------------------------------------------------------===
 *
 * This file is not part of the interface of this library.
 *
 * This file defines substitutes for the libm functions used in some of the
 * compiler-rt implementations, defined in such a way that there is not a direct
 * dependency on libm or math.h. Instead, we use the compiler builtin versions
 * where available. This reduces our dependencies on the system SDK by foisting
 * the responsibility onto the compiler.
 *
 * ===-----------------------------------------------------------------------===
 */

#ifndef INT_MATH_H
#define INT_MATH_H

#ifndef __has_builtin
#  define  __has_builtin(x) 0
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#include <math.h>
#include <stdlib.h>
#include <ymath.h>
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define CRT_INFINITY INFINITY
#else
#define CRT_INFINITY __builtin_huge_valf()
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define crt_isfinite(x) _finite((x))
#define crt_isinf(x) !_finite((x))
#define crt_isnan(x) _isnan((x))
#else
/* Define crt_isfinite in terms of the builtin if available, otherwise provide
 * an alternate version in terms of our other functions. This supports some
 * versions of GCC which didn't have __builtin_isfinite.
 */
#if __has_builtin(__builtin_isfinite)
#  define crt_isfinite(x) __builtin_isfinite((x))
#elif defined(__GNUC__)
#  define crt_isfinite(x) \
  __extension__(({ \
      __typeof((x)) x_ = (x); \
      !crt_isinf(x_) && !crt_isnan(x_); \
    }))
#else
#  error "Do not know how to check for infinity"
#endif /* __has_builtin(__builtin_isfinite) */
#define crt_isinf(x) __builtin_isinf((x))
#define crt_isnan(x) __builtin_isnan((x))
#endif /* _MSC_VER */

#if defined(_MSC_VER) && !defined(__clang__)
#define crt_copysign(x, y) copysign((x), (y))
#define crt_copysignf(x, y) copysignf((x), (y))
#define crt_copysignl(x, y) copysignl((x), (y))
#else
#define crt_copysign(x, y) __builtin_copysign((x), (y))
#define crt_copysignf(x, y) __builtin_copysignf((x), (y))
#define crt_copysignl(x, y) __builtin_copysignl((x), (y))
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define crt_fabs(x) fabs((x))
#define crt_fabsf(x) fabsf((x))
#define crt_fabsl(x) fabs((x))
#else
#define crt_fabs(x) __builtin_fabs((x))
#define crt_fabsf(x) __builtin_fabsf((x))
#define crt_fabsl(x) __builtin_fabsl((x))
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define crt_fmax(x, y) __max((x), (y))
#define crt_fmaxf(x, y) __max((x), (y))
#define crt_fmaxl(x, y) __max((x), (y))
#else
#define crt_fmax(x, y) __builtin_fmax((x), (y))
#define crt_fmaxf(x, y) __builtin_fmaxf((x), (y))
#define crt_fmaxl(x, y) __builtin_fmaxl((x), (y))
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define crt_logbl(x) logbl((x))
#else
#define crt_logbl(x) __builtin_logbl((x))
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define crt_scalbn(x, y) scalbn((x), (y))
#define crt_scalbnf(x, y) scalbnf((x), (y))
#define crt_scalbnl(x, y) scalbnl((x), (y))
#else
#define crt_scalbn(x, y) __builtin_scalbn((x), (y))
#define crt_scalbnf(x, y) __builtin_scalbnf((x), (y))
#define crt_scalbnl(x, y) __builtin_scalbnl((x), (y))
#endif

#endif /* INT_MATH_H */

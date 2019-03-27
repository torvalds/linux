/*===-- floatdidf.c - Implement __floatdidf -------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 *===----------------------------------------------------------------------===
 *
 * This file implements __floatdidf for the compiler_rt library.
 *
 *===----------------------------------------------------------------------===
 */

#include "int_lib.h"

/* Returns: convert a to a double, rounding toward even. */

/* Assumption: double is a IEEE 64 bit floating point type
 *             di_int is a 64 bit integral type
 */

/* seee eeee eeee mmmm mmmm mmmm mmmm mmmm | mmmm mmmm mmmm mmmm mmmm mmmm mmmm mmmm */

#ifndef __SOFT_FP__
/* Support for systems that have hardware floating-point; we'll set the inexact flag
 * as a side-effect of this computation.
 */

COMPILER_RT_ABI double
__floatdidf(di_int a)
{
    static const double twop52 = 4503599627370496.0; // 0x1.0p52
    static const double twop32 = 4294967296.0; // 0x1.0p32

    union { int64_t x; double d; } low = { .d = twop52 };

    const double high = (int32_t)(a >> 32) * twop32;
    low.x |= a & INT64_C(0x00000000ffffffff);

    const double result = (high - twop52) + low.d;
    return result;
}

#else
/* Support for systems that don't have hardware floating-point; there are no flags to
 * set, and we don't want to code-gen to an unknown soft-float implementation.
 */

COMPILER_RT_ABI double
__floatdidf(di_int a)
{
    if (a == 0)
        return 0.0;
    const unsigned N = sizeof(di_int) * CHAR_BIT;
    const di_int s = a >> (N-1);
    a = (a ^ s) - s;
    int sd = N - __builtin_clzll(a);  /* number of significant digits */
    int e = sd - 1;             /* exponent */
    if (sd > DBL_MANT_DIG)
    {
        /*  start:  0000000000000000000001xxxxxxxxxxxxxxxxxxxxxxPQxxxxxxxxxxxxxxxxxx
         *  finish: 000000000000000000000000000000000000001xxxxxxxxxxxxxxxxxxxxxxPQR
         *                                                12345678901234567890123456
         *  1 = msb 1 bit
         *  P = bit DBL_MANT_DIG-1 bits to the right of 1
         * Q = bit DBL_MANT_DIG bits to the right of 1
         *  R = "or" of all bits to the right of Q
        */
        switch (sd)
        {
        case DBL_MANT_DIG + 1:
            a <<= 1;
            break;
        case DBL_MANT_DIG + 2:
            break;
        default:
            a = ((du_int)a >> (sd - (DBL_MANT_DIG+2))) |
                ((a & ((du_int)(-1) >> ((N + DBL_MANT_DIG+2) - sd))) != 0);
        };
        /* finish: */
        a |= (a & 4) != 0;  /* Or P into R */
        ++a;  /* round - this step may add a significant bit */
        a >>= 2;  /* dump Q and R */
        /* a is now rounded to DBL_MANT_DIG or DBL_MANT_DIG+1 bits */
        if (a & ((du_int)1 << DBL_MANT_DIG))
        {
            a >>= 1;
            ++e;
        }
        /* a is now rounded to DBL_MANT_DIG bits */
    }
    else
    {
        a <<= (DBL_MANT_DIG - sd);
        /* a is now rounded to DBL_MANT_DIG bits */
    }
    double_bits fb;
    fb.u.s.high = ((su_int)s & 0x80000000) |        /* sign */
                  ((e + 1023) << 20)       |        /* exponent */
                  ((su_int)(a >> 32) & 0x000FFFFF); /* mantissa-high */
    fb.u.s.low = (su_int)a;                         /* mantissa-low */
    return fb.f;
}
#endif

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI double __aeabi_l2d(di_int a) {
  return __floatdidf(a);
}
#else
AEABI_RTABI double __aeabi_l2d(di_int a) COMPILER_RT_ALIAS(__floatdidf);
#endif
#endif

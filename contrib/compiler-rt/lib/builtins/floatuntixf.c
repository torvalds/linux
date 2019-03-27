/* ===-- floatuntixf.c - Implement __floatuntixf ---------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __floatuntixf for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

/* Returns: convert a to a long double, rounding toward even. */

/* Assumption: long double is a IEEE 80 bit floating point type padded to 128 bits
 *             tu_int is a 128 bit integral type
 */

/* gggg gggg gggg gggg gggg gggg gggg gggg | gggg gggg gggg gggg seee eeee eeee eeee |
 * 1mmm mmmm mmmm mmmm mmmm mmmm mmmm mmmm | mmmm mmmm mmmm mmmm mmmm mmmm mmmm mmmm
 */

COMPILER_RT_ABI long double
__floatuntixf(tu_int a)
{
    if (a == 0)
        return 0.0;
    const unsigned N = sizeof(tu_int) * CHAR_BIT;
    int sd = N - __clzti2(a);  /* number of significant digits */
    int e = sd - 1;             /* exponent */
    if (sd > LDBL_MANT_DIG)
    {
        /*  start:  0000000000000000000001xxxxxxxxxxxxxxxxxxxxxxPQxxxxxxxxxxxxxxxxxx
         *  finish: 000000000000000000000000000000000000001xxxxxxxxxxxxxxxxxxxxxxPQR
         *                                                12345678901234567890123456
         *  1 = msb 1 bit
         *  P = bit LDBL_MANT_DIG-1 bits to the right of 1
         *  Q = bit LDBL_MANT_DIG bits to the right of 1
         *  R = "or" of all bits to the right of Q
	 */
        switch (sd)
        {
        case LDBL_MANT_DIG + 1:
            a <<= 1;
            break;
        case LDBL_MANT_DIG + 2:
            break;
        default:
            a = (a >> (sd - (LDBL_MANT_DIG+2))) |
                ((a & ((tu_int)(-1) >> ((N + LDBL_MANT_DIG+2) - sd))) != 0);
        };
        /* finish: */
        a |= (a & 4) != 0;  /* Or P into R */
        ++a;  /* round - this step may add a significant bit */
        a >>= 2;  /* dump Q and R */
        /* a is now rounded to LDBL_MANT_DIG or LDBL_MANT_DIG+1 bits */
        if (a & ((tu_int)1 << LDBL_MANT_DIG))
        {
            a >>= 1;
            ++e;
        }
        /* a is now rounded to LDBL_MANT_DIG bits */
    }
    else
    {
        a <<= (LDBL_MANT_DIG - sd);
        /* a is now rounded to LDBL_MANT_DIG bits */
    }
    long_double_bits fb;
    fb.u.high.s.low = (e + 16383);                  /* exponent */
    fb.u.low.all = (du_int)a;                     /* mantissa */
    return fb.f;
}

#endif

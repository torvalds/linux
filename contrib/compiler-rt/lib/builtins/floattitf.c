//===-- lib/floattitf.c - int128 -> quad-precision conversion -----*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements ti_int to quad-precision conversion for the
// compiler-rt library in the IEEE-754 default round-to-nearest, ties-to-even
// mode.
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"
#include "int_lib.h"

/* Returns: convert a ti_int to a fp_t, rounding toward even. */

/* Assumption: fp_t is a IEEE 128 bit floating point type
 *             ti_int is a 128 bit integral type
 */

/* seee eeee eeee eeee mmmm mmmm mmmm mmmm | mmmm mmmm mmmm mmmm mmmm mmmm mmmm mmmm |
 * mmmm mmmm mmmm mmmm mmmm mmmm mmmm mmmm | mmmm mmmm mmmm mmmm mmmm mmmm mmmm mmmm
 */

#if defined(CRT_HAS_128BIT) && defined(CRT_LDBL_128BIT)
COMPILER_RT_ABI fp_t
__floattitf(ti_int a) {
    if (a == 0)
        return 0.0;
    const unsigned N = sizeof(ti_int) * CHAR_BIT;
    const ti_int s = a >> (N-1);
    a = (a ^ s) - s;
    int sd = N - __clzti2(a);  /* number of significant digits */
    int e = sd - 1;            /* exponent */
    if (sd > LDBL_MANT_DIG) {
        /*  start:  0000000000000000000001xxxxxxxxxxxxxxxxxxxxxxPQxxxxxxxxxxxxxxxxxx
         *  finish: 000000000000000000000000000000000000001xxxxxxxxxxxxxxxxxxxxxxPQR
         *                                                12345678901234567890123456
         *  1 = msb 1 bit
         *  P = bit LDBL_MANT_DIG-1 bits to the right of 1
         *  Q = bit LDBL_MANT_DIG bits to the right of 1
         *  R = "or" of all bits to the right of Q
         */
        switch (sd) {
        case LDBL_MANT_DIG + 1:
            a <<= 1;
            break;
        case LDBL_MANT_DIG + 2:
            break;
        default:
            a = ((tu_int)a >> (sd - (LDBL_MANT_DIG+2))) |
                ((a & ((tu_int)(-1) >> ((N + LDBL_MANT_DIG+2) - sd))) != 0);
        };
        /* finish: */
        a |= (a & 4) != 0;  /* Or P into R */
        ++a;  /* round - this step may add a significant bit */
        a >>= 2;  /* dump Q and R */
        /* a is now rounded to LDBL_MANT_DIG or LDBL_MANT_DIG+1 bits */
        if (a & ((tu_int)1 << LDBL_MANT_DIG)) {
            a >>= 1;
            ++e;
        }
        /* a is now rounded to LDBL_MANT_DIG bits */
    } else {
        a <<= (LDBL_MANT_DIG - sd);
        /* a is now rounded to LDBL_MANT_DIG bits */
    }

    long_double_bits fb;
    fb.u.high.all = (s & 0x8000000000000000LL)           /* sign */
                  | (du_int)(e + 16383) << 48            /* exponent */
                  | ((a >> 64) & 0x0000ffffffffffffLL);  /* significand */
    fb.u.low.all = (du_int)(a);
    return fb.f;
}

#endif

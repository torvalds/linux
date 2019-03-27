/* ===-- muldi3.c - Implement __muldi3 -------------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __muldi3 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

/* Returns: a * b */

static
di_int
__muldsi3(su_int a, su_int b)
{
    dwords r;
    const int bits_in_word_2 = (int)(sizeof(si_int) * CHAR_BIT) / 2;
    const su_int lower_mask = (su_int)~0 >> bits_in_word_2;
    r.s.low = (a & lower_mask) * (b & lower_mask);
    su_int t = r.s.low >> bits_in_word_2;
    r.s.low &= lower_mask;
    t += (a >> bits_in_word_2) * (b & lower_mask);
    r.s.low += (t & lower_mask) << bits_in_word_2;
    r.s.high = t >> bits_in_word_2;
    t = r.s.low >> bits_in_word_2;
    r.s.low &= lower_mask;
    t += (b >> bits_in_word_2) * (a & lower_mask);
    r.s.low += (t & lower_mask) << bits_in_word_2;
    r.s.high += t >> bits_in_word_2;
    r.s.high += (a >> bits_in_word_2) * (b >> bits_in_word_2);
    return r.all;
}

/* Returns: a * b */

COMPILER_RT_ABI di_int
__muldi3(di_int a, di_int b)
{
    dwords x;
    x.all = a;
    dwords y;
    y.all = b;
    dwords r;
    r.all = __muldsi3(x.s.low, y.s.low);
    r.s.high += x.s.high * y.s.low + x.s.low * y.s.high;
    return r.all;
}

#if defined(__ARM_EABI__)
AEABI_RTABI di_int __aeabi_lmul(di_int a, di_int b) COMPILER_RT_ALIAS(__muldi3);
#endif

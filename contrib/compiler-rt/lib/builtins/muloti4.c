/*===-- muloti4.c - Implement __muloti4 -----------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __muloti4 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

/* Returns: a * b */

/* Effects: sets *overflow to 1  if a * b overflows */

COMPILER_RT_ABI ti_int
__muloti4(ti_int a, ti_int b, int* overflow)
{
    const int N = (int)(sizeof(ti_int) * CHAR_BIT);
    const ti_int MIN = (ti_int)1 << (N-1);
    const ti_int MAX = ~MIN;
    *overflow = 0;
    ti_int result = a * b;
    if (a == MIN)
    {
        if (b != 0 && b != 1)
	    *overflow = 1;
	return result;
    }
    if (b == MIN)
    {
        if (a != 0 && a != 1)
	    *overflow = 1;
        return result;
    }
    ti_int sa = a >> (N - 1);
    ti_int abs_a = (a ^ sa) - sa;
    ti_int sb = b >> (N - 1);
    ti_int abs_b = (b ^ sb) - sb;
    if (abs_a < 2 || abs_b < 2)
        return result;
    if (sa == sb)
    {
        if (abs_a > MAX / abs_b)
            *overflow = 1;
    }
    else
    {
        if (abs_a > MIN / -abs_b)
            *overflow = 1;
    }
    return result;
}

#endif /* CRT_HAS_128BIT */

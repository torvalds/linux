/* ===-- ffsti2.c - Implement __ffsti2 -------------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __ffsti2 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

/* Returns: the index of the least significant 1-bit in a, or
 * the value zero if a is zero. The least significant bit is index one.
 */

COMPILER_RT_ABI si_int
__ffsti2(ti_int a)
{
    twords x;
    x.all = a;
    if (x.s.low == 0)
    {
        if (x.s.high == 0)
            return 0;
        return __builtin_ctzll(x.s.high) + (1 + sizeof(di_int) * CHAR_BIT);
    }
    return __builtin_ctzll(x.s.low) + 1;
}

#endif /* CRT_HAS_128BIT */

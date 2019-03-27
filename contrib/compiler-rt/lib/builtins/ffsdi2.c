/* ===-- ffsdi2.c - Implement __ffsdi2 -------------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __ffsdi2 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

/* Returns: the index of the least significant 1-bit in a, or
 * the value zero if a is zero. The least significant bit is index one.
 */

COMPILER_RT_ABI si_int
__ffsdi2(di_int a)
{
    dwords x;
    x.all = a;
    if (x.s.low == 0)
    {
        if (x.s.high == 0)
            return 0;
        return __builtin_ctz(x.s.high) + (1 + sizeof(si_int) * CHAR_BIT);
    }
    return __builtin_ctz(x.s.low) + 1;
}

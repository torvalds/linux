/* ===-- ffssi2.c - Implement __ffssi2 -------------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __ffssi2 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

/* Returns: the index of the least significant 1-bit in a, or
 * the value zero if a is zero. The least significant bit is index one.
 */

COMPILER_RT_ABI si_int
__ffssi2(si_int a)
{
    if (a == 0)
    {
        return 0;
    }
    return __builtin_ctz(a) + 1;
}

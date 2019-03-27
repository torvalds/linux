/* ===-- absvsi2.c - Implement __absvsi2 -----------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __absvsi2 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

/* Returns: absolute value */

/* Effects: aborts if abs(x) < 0 */

COMPILER_RT_ABI si_int
__absvsi2(si_int a)
{
    const int N = (int)(sizeof(si_int) * CHAR_BIT);
    if (a == (1 << (N-1)))
        compilerrt_abort();
    const si_int t = a >> (N - 1);
    return (a ^ t) - t;
}

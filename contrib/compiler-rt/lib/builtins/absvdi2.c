/*===-- absvdi2.c - Implement __absvdi2 -----------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 *===----------------------------------------------------------------------===
 *
 * This file implements __absvdi2 for the compiler_rt library.
 *
 *===----------------------------------------------------------------------===
 */

#include "int_lib.h"

/* Returns: absolute value */

/* Effects: aborts if abs(x) < 0 */

COMPILER_RT_ABI di_int
__absvdi2(di_int a)
{
    const int N = (int)(sizeof(di_int) * CHAR_BIT);
    if (a == ((di_int)1 << (N-1)))
        compilerrt_abort();
    const di_int t = a >> (N - 1);
    return (a ^ t) - t;
}

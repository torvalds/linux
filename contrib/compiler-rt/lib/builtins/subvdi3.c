/* ===-- subvdi3.c - Implement __subvdi3 -----------------------------------===
 *
 *                The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __subvdi3 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

/* Returns: a - b */

/* Effects: aborts if a - b overflows */

COMPILER_RT_ABI di_int
__subvdi3(di_int a, di_int b)
{
    di_int s = (du_int) a - (du_int) b;
    if (b >= 0)
    {
        if (s > a)
            compilerrt_abort();
    }
    else
    {
        if (s <= a)
            compilerrt_abort();
    }
    return s;
}

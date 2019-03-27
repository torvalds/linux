/* ===-- subvti3.c - Implement __subvti3 -----------------------------------===
 *
 *      	       The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __subvti3 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

/* Returns: a - b */

/* Effects: aborts if a - b overflows */

COMPILER_RT_ABI ti_int
__subvti3(ti_int a, ti_int b)
{
    ti_int s = (tu_int) a - (tu_int) b;
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

#endif /* CRT_HAS_128BIT */

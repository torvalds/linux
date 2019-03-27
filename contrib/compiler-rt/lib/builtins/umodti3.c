/* ===-- umodti3.c - Implement __umodti3 -----------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __umodti3 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

/* Returns: a % b */

COMPILER_RT_ABI tu_int
__umodti3(tu_int a, tu_int b)
{
    tu_int r;
    __udivmodti4(a, b, &r);
    return r;
}

#endif /* CRT_HAS_128BIT */

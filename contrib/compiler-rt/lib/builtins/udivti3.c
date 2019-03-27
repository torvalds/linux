/* ===-- udivti3.c - Implement __udivti3 -----------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __udivti3 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

/* Returns: a / b */

COMPILER_RT_ABI tu_int
__udivti3(tu_int a, tu_int b)
{
    return __udivmodti4(a, b, 0);
}

#endif /* CRT_HAS_128BIT */

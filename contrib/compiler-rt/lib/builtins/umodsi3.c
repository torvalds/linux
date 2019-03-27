/* ===-- umodsi3.c - Implement __umodsi3 -----------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __umodsi3 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

/* Returns: a % b */

COMPILER_RT_ABI su_int
__umodsi3(su_int a, su_int b)
{
    return a - __udivsi3(a, b) * b;
}

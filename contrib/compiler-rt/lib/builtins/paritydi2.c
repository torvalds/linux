/* ===-- paritydi2.c - Implement __paritydi2 -------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __paritydi2 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

/* Returns: 1 if number of bits is odd else returns 0 */

COMPILER_RT_ABI si_int
__paritydi2(di_int a)
{
    dwords x;
    x.all = a;
    return __paritysi2(x.s.high ^ x.s.low);
}

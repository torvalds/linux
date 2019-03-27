/* ===-- parityti2.c - Implement __parityti2 -------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __parityti2 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */ 

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

/* Returns: 1 if number of bits is odd else returns 0 */

COMPILER_RT_ABI si_int
__parityti2(ti_int a)
{
    twords x;
    x.all = a;
    return __paritydi2(x.s.high ^ x.s.low);
}

#endif /* CRT_HAS_128BIT */

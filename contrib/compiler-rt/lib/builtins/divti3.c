/* ===-- divti3.c - Implement __divti3 -------------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __divti3 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

/* Returns: a / b */

COMPILER_RT_ABI ti_int
__divti3(ti_int a, ti_int b)
{
    const int bits_in_tword_m1 = (int)(sizeof(ti_int) * CHAR_BIT) - 1;
    ti_int s_a = a >> bits_in_tword_m1;           /* s_a = a < 0 ? -1 : 0 */
    ti_int s_b = b >> bits_in_tword_m1;           /* s_b = b < 0 ? -1 : 0 */
    a = (a ^ s_a) - s_a;                         /* negate if s_a == -1 */
    b = (b ^ s_b) - s_b;                         /* negate if s_b == -1 */
    s_a ^= s_b;                                  /* sign of quotient */
    return (__udivmodti4(a, b, (tu_int*)0) ^ s_a) - s_a;  /* negate if s_a == -1 */
}

#endif /* CRT_HAS_128BIT */

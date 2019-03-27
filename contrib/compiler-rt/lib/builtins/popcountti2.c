/* ===-- popcountti2.c - Implement __popcountti2 ----------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __popcountti2 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

/* Returns: count of 1 bits */

COMPILER_RT_ABI si_int
__popcountti2(ti_int a)
{
    tu_int x3 = (tu_int)a;
    x3 = x3 - ((x3 >> 1) & (((tu_int)0x5555555555555555uLL << 64) |
                                     0x5555555555555555uLL));
    /* Every 2 bits holds the sum of every pair of bits (64) */
    x3 = ((x3 >> 2) & (((tu_int)0x3333333333333333uLL << 64) | 0x3333333333333333uLL))
       + (x3 & (((tu_int)0x3333333333333333uLL << 64) | 0x3333333333333333uLL));
    /* Every 4 bits holds the sum of every 4-set of bits (3 significant bits) (32) */
    x3 = (x3 + (x3 >> 4))
       & (((tu_int)0x0F0F0F0F0F0F0F0FuLL << 64) | 0x0F0F0F0F0F0F0F0FuLL);
    /* Every 8 bits holds the sum of every 8-set of bits (4 significant bits) (16) */
    du_int x2 = (du_int)(x3 + (x3 >> 64));
    /* Every 8 bits holds the sum of every 8-set of bits (5 significant bits) (8) */
    su_int x = (su_int)(x2 + (x2 >> 32));
    /* Every 8 bits holds the sum of every 8-set of bits (6 significant bits) (4) */
    x = x + (x >> 16);
    /* Every 8 bits holds the sum of every 8-set of bits (7 significant bits) (2) */
    /* Upper 16 bits are garbage */
    return (x + (x >> 8)) & 0xFF;  /* (8 significant bits) */
}

#endif /* CRT_HAS_128BIT */

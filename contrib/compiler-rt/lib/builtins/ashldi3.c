/* ====-- ashldi3.c - Implement __ashldi3 -----------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __ashldi3 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

/* Returns: a << b */

/* Precondition:  0 <= b < bits_in_dword */

COMPILER_RT_ABI di_int
__ashldi3(di_int a, si_int b)
{
    const int bits_in_word = (int)(sizeof(si_int) * CHAR_BIT);
    dwords input;
    dwords result;
    input.all = a;
    if (b & bits_in_word)  /* bits_in_word <= b < bits_in_dword */
    {
        result.s.low = 0;
        result.s.high = input.s.low << (b - bits_in_word);
    }
    else  /* 0 <= b < bits_in_word */
    {
        if (b == 0)
            return a;
        result.s.low  = input.s.low << b;
        result.s.high = (input.s.high << b) | (input.s.low >> (bits_in_word - b));
    }
    return result.all;
}

#if defined(__ARM_EABI__)
AEABI_RTABI di_int __aeabi_llsl(di_int a, si_int b) COMPILER_RT_ALIAS(__ashldi3);
#endif

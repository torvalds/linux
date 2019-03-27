/*===-- negvti2.c - Implement __negvti2 -----------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 *===----------------------------------------------------------------------===
 *
 *This file implements __negvti2 for the compiler_rt library.
 *
 *===----------------------------------------------------------------------===
 */

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

/* Returns: -a */

/* Effects: aborts if -a overflows */

COMPILER_RT_ABI ti_int
__negvti2(ti_int a)
{
    const ti_int MIN = (ti_int)1 << ((int)(sizeof(ti_int) * CHAR_BIT)-1);
    if (a == MIN)
        compilerrt_abort();
    return -a;
}

#endif /* CRT_HAS_128BIT */

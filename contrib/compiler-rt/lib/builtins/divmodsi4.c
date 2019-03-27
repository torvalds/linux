/*===-- divmodsi4.c - Implement __divmodsi4 --------------------------------===
 *
 *                    The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __divmodsi4 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

/* Returns: a / b, *rem = a % b  */

COMPILER_RT_ABI si_int
__divmodsi4(si_int a, si_int b, si_int* rem)
{
  si_int d = __divsi3(a,b);
  *rem = a - (d*b);
  return d; 
}



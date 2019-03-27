/*===-- udivmodsi4.c - Implement __udivmodsi4 ------------------------------===
 *
 *                    The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __udivmodsi4 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

/* Returns: a / b, *rem = a % b  */

COMPILER_RT_ABI su_int
__udivmodsi4(su_int a, su_int b, su_int* rem)
{
  si_int d = __udivsi3(a,b);
  *rem = a - (d*b);
  return d;
}



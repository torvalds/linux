//===----------------------Hexagon builtin routine ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

.macro FUNCTION_BEGIN name
.text
.p2align 5
.globl \name
.type  \name, @function
\name:
.endm

.macro FUNCTION_END name
.size  \name, . - \name
.endm

FUNCTION_BEGIN fabs
  {
    r1 = clrbit(r1, #31)
    jumpr r31
  }
FUNCTION_END fabs

FUNCTION_BEGIN fabsf
  {
    r0 = clrbit(r0, #31)
    jumpr r31
  }
FUNCTION_END fabsf

  .globl fabsl
  .set fabsl, fabs

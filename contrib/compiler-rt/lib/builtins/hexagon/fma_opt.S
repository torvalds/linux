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

FUNCTION_BEGIN fmaf
  r2 += sfmpy(r0, r1)
  {
    r0 = r2
    jumpr r31
  }
FUNCTION_END fmaf

  .globl fmal
  .set fmal, fma

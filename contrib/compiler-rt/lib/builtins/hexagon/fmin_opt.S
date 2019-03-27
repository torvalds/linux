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

FUNCTION_BEGIN fminf
  {
    r0 = sfmin(r0, r1)
    jumpr r31
  }
FUNCTION_END fminf

  .globl fminl
  .set fminl, fmin

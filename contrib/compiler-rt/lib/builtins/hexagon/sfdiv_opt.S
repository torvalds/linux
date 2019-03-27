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

#define Q6_ALIAS(TAG) .global __qdsp_##TAG ; .set __qdsp_##TAG, __hexagon_##TAG
#define FAST_ALIAS(TAG) .global __hexagon_fast_##TAG ; .set __hexagon_fast_##TAG, __hexagon_##TAG
#define FAST2_ALIAS(TAG) .global __hexagon_fast2_##TAG ; .set __hexagon_fast2_##TAG, __hexagon_##TAG

FUNCTION_BEGIN __hexagon_divsf3
  {
    r2,p0 = sfrecipa(r0,r1)
    r4 = sffixupd(r0,r1)
    r3 = ##0x3f800000   // 1.0
  }
  {
    r5 = sffixupn(r0,r1)
    r3 -= sfmpy(r4,r2):lib  // 1-(den/recip) yields error?
    r6 = ##0x80000000
    r7 = r3
  }
  {
    r2 += sfmpy(r3,r2):lib
    r3 = r7
    r6 = r5
    r0 = and(r6,r5)
  }
  {
    r3 -= sfmpy(r4,r2):lib
    r0 += sfmpy(r5,r2):lib
  }
  {
    r2 += sfmpy(r3,r2):lib
    r6 -= sfmpy(r0,r4):lib
  }
  {
    r0 += sfmpy(r6,r2):lib
  }
  {
    r5 -= sfmpy(r0,r4):lib
  }
  {
    r0 += sfmpy(r5,r2,p0):scale
    jumpr r31
  }
FUNCTION_END __hexagon_divsf3

Q6_ALIAS(divsf3)
FAST_ALIAS(divsf3)
FAST2_ALIAS(divsf3)

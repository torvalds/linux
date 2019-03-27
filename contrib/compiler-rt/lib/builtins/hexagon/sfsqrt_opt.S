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

#define RIN r0
#define S r0
#define H r1
#define D r2
#define E r3
#define HALF r4
#define R r5

#define Q6_ALIAS(TAG) .global __qdsp_##TAG ; .set __qdsp_##TAG, __hexagon_##TAG
#define FAST_ALIAS(TAG) .global __hexagon_fast_##TAG ; .set __hexagon_fast_##TAG, __hexagon_##TAG
#define FAST2_ALIAS(TAG) .global __hexagon_fast2_##TAG ; .set __hexagon_fast2_##TAG, __hexagon_##TAG

FUNCTION_BEGIN __hexagon_sqrtf
  {
    E,p0 = sfinvsqrta(RIN)
    R = sffixupr(RIN)
    HALF = ##0x3f000000   // 0.5
    r1:0 = combine(#0,#0)   // clear S/H
  }
  {
    S += sfmpy(E,R):lib   // S0
    H += sfmpy(E,HALF):lib    // H0
    D = HALF
    E = R
  }
  {
    D -= sfmpy(S,H):lib   // d0
    p1 = sfclass(R,#1)    // is zero?
    //E -= sfmpy(S,S):lib   // e0
  }
  {
    S += sfmpy(S,D):lib   // S1
    H += sfmpy(H,D):lib   // H1
    D = HALF
    E = R
  }
  {
    D -= sfmpy(S,H):lib   // d0
    E -= sfmpy(S,S):lib   // e0
  }
  {
    S += sfmpy(H,E):lib   // S2
    H += sfmpy(H,D):lib   // H2
    D = HALF
    E = R
  }
  {
    //D -= sfmpy(S,H):lib   // d2
    E -= sfmpy(S,S):lib   // e2
    if (p1) r0 = or(r0,R)     // sqrt(-0.0) = -0.0
  }
  {
    S += sfmpy(H,E,p0):scale  // S3
    jumpr r31
  }

FUNCTION_END __hexagon_sqrtf

Q6_ALIAS(sqrtf)
FAST_ALIAS(sqrtf)
FAST2_ALIAS(sqrtf)

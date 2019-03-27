//===----------------------Hexagon builtin routine ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define A r1:0
#define B r3:2
#define ATMP r5:4


#define Q6_ALIAS(TAG) .global __qdsp_##TAG ; .set __qdsp_##TAG, __hexagon_##TAG
#define END(TAG) .size TAG,.-TAG

/*
 * Min and Max return A if B is NaN, or B if A is NaN
 * Otherwise, they return the smaller or bigger value
 *
 * If values are equal, we want to favor -0.0 for min and +0.0 for max.
 */

/*
 * Compares always return false for NaN
 * if (isnan(A)) A = B; if (A > B) A = B will only trigger at most one of those options.
 */
	.text
	.global __hexagon_mindf3
	.global __hexagon_maxdf3
	.global fmin
	.type fmin,@function
	.global fmax
	.type fmax,@function
	.type __hexagon_mindf3,@function
	.type __hexagon_maxdf3,@function
	Q6_ALIAS(mindf3)
	Q6_ALIAS(maxdf3)
	.p2align 5
__hexagon_mindf3:
fmin:
	{
		p0 = dfclass(A,#0x10)		// If A is a number
		p1 = dfcmp.gt(A,B)		// AND B > A, don't swap
		ATMP = A
	}
	{
		if (p0) A = B			// if A is NaN use B
		if (p1) A = B			// gt is always false if either is NaN
		p2 = dfcmp.eq(A,B)		// if A == B
		if (!p2.new) jumpr:t r31
	}
	/* A == B, return A|B to select -0.0 over 0.0 */
	{
		A = or(ATMP,B)
		jumpr r31
	}
END(__hexagon_mindf3)
	.falign
__hexagon_maxdf3:
fmax:
	{
		p0 = dfclass(A,#0x10)
		p1 = dfcmp.gt(B,A)
		ATMP = A
	}
	{
		if (p0) A = B
		if (p1) A = B
		p2 = dfcmp.eq(A,B)
		if (!p2.new) jumpr:t r31
	}
	/* A == B, return A&B to select 0.0 over -0.0 */
	{
		A = and(ATMP,B)
		jumpr r31
	}
END(__hexagon_maxdf3)

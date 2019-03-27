/* ===-- clzsi2.c - Implement __clzsi2 -------------------------------------===
 *
 *               The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements count leading zeros for 32bit arguments.
 *
 * ===----------------------------------------------------------------------===
 */
#include "../assembly.h"

	.syntax unified
	.text
	DEFINE_CODE_STATE

	.p2align	2
DEFINE_COMPILERRT_FUNCTION(__clzsi2)
#ifdef __ARM_FEATURE_CLZ
	clz	r0, r0
	JMP(lr)
#else
	/* Assumption: n != 0 */

	/*
	 * r0: n
	 * r1: count of leading zeros in n + 1
	 * r2: scratch register for shifted r0
	 */
	mov	r1, 1

	/*
	 * Basic block:
	 * if ((r0 >> SHIFT) == 0)
	 *   r1 += SHIFT;
	 * else
	 *   r0 >>= SHIFT;
	 * for descending powers of two as SHIFT.
	 */

#define BLOCK(shift) \
	lsrs	r2, r0, shift; \
	movne	r0, r2; \
	addeq	r1, shift \

	BLOCK(16)
	BLOCK(8)
	BLOCK(4)
	BLOCK(2)

	/*
	 * The basic block invariants at this point are (r0 >> 2) == 0 and
	 * r0 != 0. This means 1 <= r0 <= 3 and 0 <= (r0 >> 1) <= 1.
	 *
	 * r0 | (r0 >> 1) == 0 | (r0 >> 1) == 1 | -(r0 >> 1) | 1 - (r0 >> 1)
	 * ---+----------------+----------------+------------+--------------
	 * 1  | 1              | 0              | 0          | 1
	 * 2  | 0              | 1              | -1         | 0
	 * 3  | 0              | 1              | -1         | 0
	 *
	 * The r1's initial value of 1 compensates for the 1 here.
	 */
	sub	r0, r1, r0, lsr #1

	JMP(lr)
#endif // __ARM_FEATURE_CLZ
END_COMPILERRT_FUNCTION(__clzsi2)

NO_EXEC_STACK_DIRECTIVE


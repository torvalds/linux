/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm-generic/export.h>

/* int strncmp(const char *cs, const char *ct, size_t count) */
SYM_FUNC_START(strncmp)
	/*
	 * Returns
	 *   a0 - comparison result, value like strncmp
	 *
	 * Parameters
	 *   a0 - string1
	 *   a1 - string2
	 *   a2 - number of characters to compare
	 *
	 * Clobbers
	 *   t0, t1, t2
	 */
	li	t2, 0
1:
	beq	a2, t2, 2f
	lbu	t0, 0(a0)
	lbu	t1, 0(a1)
	addi	a0, a0, 1
	addi	a1, a1, 1
	bne	t0, t1, 3f
	addi	t2, t2, 1
	bnez	t0, 1b
2:
	li	a0, 0
	ret
3:
	/*
	 * strncmp only needs to return (< 0, 0, > 0) values
	 * not necessarily -1, 0, +1
	 */
	sub	a0, t0, t1
	ret
SYM_FUNC_END(strncmp)

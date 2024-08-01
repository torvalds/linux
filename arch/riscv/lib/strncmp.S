/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/alternative-macros.h>
#include <asm/hwcap.h>

/* int strncmp(const char *cs, const char *ct, size_t count) */
SYM_FUNC_START(strncmp)

	ALTERNATIVE("nop", "j strncmp_zbb", 0, RISCV_ISA_EXT_ZBB, CONFIG_RISCV_ISA_ZBB)

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

/*
 * Variant of strncmp using the ZBB extension if available
 */
#ifdef CONFIG_RISCV_ISA_ZBB
strncmp_zbb:

.option push
.option arch,+zbb

	/*
	 * Returns
	 *   a0 - comparison result, like strncmp
	 *
	 * Parameters
	 *   a0 - string1
	 *   a1 - string2
	 *   a2 - number of characters to compare
	 *
	 * Clobbers
	 *   t0, t1, t2, t3, t4, t5, t6
	 */

	or	t2, a0, a1
	li	t5, -1
	and	t2, t2, SZREG-1
	add	t4, a0, a2
	bnez	t2, 3f

	/* Adjust limit for fast-path.  */
	andi	t6, t4, -SZREG

	/* Main loop for aligned string.  */
	.p2align 3
1:
	bge	a0, t6, 3f
	REG_L	t0, 0(a0)
	REG_L	t1, 0(a1)
	orc.b	t3, t0
	bne	t3, t5, 2f
	orc.b	t3, t1
	bne	t3, t5, 2f
	addi	a0, a0, SZREG
	addi	a1, a1, SZREG
	beq	t0, t1, 1b

	/*
	 * Words don't match, and no null byte in the first
	 * word. Get bytes in big-endian order and compare.
	 */
#ifndef CONFIG_CPU_BIG_ENDIAN
	rev8	t0, t0
	rev8	t1, t1
#endif

	/* Synthesize (t0 >= t1) ? 1 : -1 in a branchless sequence.  */
	sltu	a0, t0, t1
	neg	a0, a0
	ori	a0, a0, 1
	ret

2:
	/*
	 * Found a null byte.
	 * If words don't match, fall back to simple loop.
	 */
	bne	t0, t1, 3f

	/* Otherwise, strings are equal.  */
	li	a0, 0
	ret

	/* Simple loop for misaligned strings.  */
	.p2align 3
3:
	bge	a0, t4, 5f
	lbu	t0, 0(a0)
	lbu	t1, 0(a1)
	addi	a0, a0, 1
	addi	a1, a1, 1
	bne	t0, t1, 4f
	bnez	t0, 3b

4:
	sub	a0, t0, t1
	ret

5:
	li	a0, 0
	ret

.option pop
#endif
SYM_FUNC_END(strncmp)
EXPORT_SYMBOL(strncmp)

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/lib/copy_to_user.S
 *
 *  Author:	Nicolas Pitre
 *  Created:	Sep 29, 2005
 *  Copyright:	MontaVista Software, Inc.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/unwind.h>

/*
 * Prototype:
 *
 *	size_t arm_copy_to_user(void *to, const void *from, size_t n)
 *
 * Purpose:
 *
 *	copy a block to user memory from kernel memory
 *
 * Params:
 *
 *	to = user memory
 *	from = kernel memory
 *	n = number of bytes to copy
 *
 * Return value:
 *
 *	Number of bytes NOT copied.
 */

#define LDR1W_SHIFT	0

	.macro ldr1w ptr reg abort
	W(ldr) \reg, [\ptr], #4
	.endm

	.macro ldr4w ptr reg1 reg2 reg3 reg4 abort
	ldmia \ptr!, {\reg1, \reg2, \reg3, \reg4}
	.endm

	.macro ldr8w ptr reg1 reg2 reg3 reg4 reg5 reg6 reg7 reg8 abort
	ldmia \ptr!, {\reg1, \reg2, \reg3, \reg4, \reg5, \reg6, \reg7, \reg8}
	.endm

	.macro ldr1b ptr reg cond=al abort
	ldrb\cond \reg, [\ptr], #1
	.endm

#ifdef CONFIG_CPU_USE_DOMAINS

#ifndef CONFIG_THUMB2_KERNEL
#define STR1W_SHIFT	0
#else
#define STR1W_SHIFT	1
#endif

	.macro str1w ptr reg abort
	strusr	\reg, \ptr, 4, abort=\abort
	.endm

	.macro str8w ptr reg1 reg2 reg3 reg4 reg5 reg6 reg7 reg8 abort
	str1w \ptr, \reg1, \abort
	str1w \ptr, \reg2, \abort
	str1w \ptr, \reg3, \abort
	str1w \ptr, \reg4, \abort
	str1w \ptr, \reg5, \abort
	str1w \ptr, \reg6, \abort
	str1w \ptr, \reg7, \abort
	str1w \ptr, \reg8, \abort
	.endm

#else

#define STR1W_SHIFT	0

	.macro str1w ptr reg abort
	USERL(\abort, W(str) \reg, [\ptr], #4)
	.endm

	.macro str8w ptr reg1 reg2 reg3 reg4 reg5 reg6 reg7 reg8 abort
	USERL(\abort, stmia \ptr!, {\reg1, \reg2, \reg3, \reg4, \reg5, \reg6, \reg7, \reg8})
	.endm

#endif /* CONFIG_CPU_USE_DOMAINS */

	.macro str1b ptr reg cond=al abort
	strusr	\reg, \ptr, 1, \cond, abort=\abort
	.endm

	.macro enter regs:vararg
	mov	r3, #0
UNWIND( .save	{r0, r2, r3, \regs}		)
	stmdb	sp!, {r0, r2, r3, \regs}
	.endm

	.macro exit regs:vararg
	add	sp, sp, #8
	ldmfd	sp!, {r0, \regs}
	.endm

	.text

ENTRY(__copy_to_user_std)
WEAK(arm_copy_to_user)
#ifdef CONFIG_CPU_SPECTRE
	ldr	r3, =TASK_SIZE
	uaccess_mask_range_ptr r0, r2, r3, ip
#endif

#include "copy_template.S"

ENDPROC(arm_copy_to_user)
ENDPROC(__copy_to_user_std)

	.pushsection .text.fixup,"ax"
	.align 0
	copy_abort_preamble
	ldmfd	sp!, {r1, r2, r3}
	sub	r0, r0, r1
	rsb	r0, r0, r2
	copy_abort_end
	.popsection

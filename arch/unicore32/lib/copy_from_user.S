/*
 * linux/arch/unicore32/lib/copy_from_user.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * Prototype:
 *
 *	size_t raw_copy_from_user(void *to, const void *from, size_t n)
 *
 * Purpose:
 *
 *	copy a block to kernel memory from user memory
 *
 * Params:
 *
 *	to = kernel memory
 *	from = user memory
 *	n = number of bytes to copy
 *
 * Return value:
 *
 *	Number of bytes NOT copied.
 */

	.macro ldr1w ptr reg abort
	ldrusr	\reg, \ptr, 4, abort=\abort
	.endm

	.macro ldr4w ptr reg1 reg2 reg3 reg4 abort
100:	ldm.w	(\reg1, \reg2, \reg3, \reg4), [\ptr]+
	.pushsection __ex_table, "a"
	.align	3
	.long 100b, \abort
	.popsection
	.endm

	.macro ldr8w ptr reg1 reg2 reg3 reg4 reg5 reg6 reg7 reg8 abort
100:	ldm.w (\reg1, \reg2, \reg3, \reg4, \reg5, \reg6, \reg7, \reg8), [\ptr]+
	.pushsection __ex_table, "a"
	.align	3
	.long 100b, \abort
	.popsection
	.endm

	.macro ldr1b ptr reg cond=al abort
	ldrusr	\reg, \ptr, 1, \cond, abort=\abort
	.endm

	.macro str1w ptr reg abort
	stw.w \reg, [\ptr]+, #4
	.endm

	.macro str8w ptr reg1 reg2 reg3 reg4 reg5 reg6 reg7 reg8 abort
	stm.w (\reg1, \reg2, \reg3, \reg4, \reg5, \reg6, \reg7, \reg8), [\ptr]+
	.endm

	.macro str1b ptr reg cond=al abort
	.ifnc	\cond, al
	b\cond	201f
	b	202f
	.endif
201:	stb.w \reg, [\ptr]+, #1
202:
	.endm

	.macro enter
	mov	r3, #0
	stm.w	(r0, r2, r3), [sp-]
	.endm

	.macro exit
	add	sp, sp, #8
	ldm.w	(r0), [sp]+
	mov	pc, lr
	.endm

	.text

ENTRY(raw_copy_from_user)

#include "copy_template.S"

ENDPROC(raw_copy_from_user)

	.pushsection .fixup,"ax"
	.align 0
	copy_abort_preamble
	ldm.w	(r1, r2, r3), [sp]+
	sub	r0, r0, r1
	rsub	r0, r0, r2
	copy_abort_end
	.popsection


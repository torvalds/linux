/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/lib/copy_page.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 *  ASM optimised string functions
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <generated/asm-offsets.h>
#include <asm/cache.h>

#define COPY_COUNT (PAGE_SZ/256)

		.text
		.align	5
/*
 * UniCore optimised copy_page routine
 */
ENTRY(copy_page)
		stm.w	(r17 - r19, lr), [sp-]
		mov	r17, r0
		mov	r18, r1
		mov	r19, #COPY_COUNT
1:
	.rept	4
		ldm.w	(r0 - r15), [r18]+
		stm.w	(r0 - r15), [r17]+
	.endr
		sub.a	r19, r19, #1
		bne	1b
		ldm.w	(r17 - r19, pc), [sp]+
ENDPROC(copy_page)

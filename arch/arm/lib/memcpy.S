/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/lib/memcpy.S
 *
 *  Author:	Nicolas Pitre
 *  Created:	Sep 28, 2005
 *  Copyright:	MontaVista Software, Inc.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/unwind.h>

#define LDR1W_SHIFT	0
#define STR1W_SHIFT	0

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

	.macro str1w ptr reg abort
	W(str) \reg, [\ptr], #4
	.endm

	.macro str8w ptr reg1 reg2 reg3 reg4 reg5 reg6 reg7 reg8 abort
	stmia \ptr!, {\reg1, \reg2, \reg3, \reg4, \reg5, \reg6, \reg7, \reg8}
	.endm

	.macro str1b ptr reg cond=al abort
	strb\cond \reg, [\ptr], #1
	.endm

	.macro enter reg1 reg2
	stmdb sp!, {r0, \reg1, \reg2}
	.endm

	.macro usave reg1 reg2
	UNWIND(	.save	{r0, \reg1, \reg2}	)
	.endm

	.macro exit reg1 reg2
	ldmfd sp!, {r0, \reg1, \reg2}
	.endm

	.text

/* Prototype: void *memcpy(void *dest, const void *src, size_t n); */

ENTRY(__memcpy)
ENTRY(mmiocpy)
WEAK(memcpy)

#include "copy_template.S"

ENDPROC(memcpy)
ENDPROC(mmiocpy)
ENDPROC(__memcpy)

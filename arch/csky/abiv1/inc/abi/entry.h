/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_ENTRY_H
#define __ASM_CSKY_ENTRY_H

#include <asm/setup.h>
#include <abi/regdef.h>

#define LSAVE_PC	8
#define LSAVE_PSR	12
#define LSAVE_A0	24
#define LSAVE_A1	28
#define LSAVE_A2	32
#define LSAVE_A3	36
#define LSAVE_A4	40
#define LSAVE_A5	44

#define usp ss1

.macro USPTOKSP
	mtcr	sp, usp
	mfcr	sp, ss0
.endm

.macro KSPTOUSP
	mtcr	sp, ss0
	mfcr	sp, usp
.endm

.macro	SAVE_ALL epc_inc
	mtcr    r13, ss2
	mfcr    r13, epsr
	btsti   r13, 31
	bt      1f
	USPTOKSP
1:
	subi    sp, 32
	subi    sp, 32
	subi    sp, 16
	stw     r13, (sp, 12)

	stw     lr, (sp, 4)

	mfcr	lr, epc
	movi	r13, \epc_inc
	add	lr, r13
	stw     lr, (sp, 8)

	mov	lr, sp
	addi	lr, 32
	addi	lr, 32
	addi	lr, 16
	bt	2f
	mfcr	lr, ss1
2:
	stw     lr, (sp, 16)

	stw     a0, (sp, 20)
	stw     a0, (sp, 24)
	stw     a1, (sp, 28)
	stw     a2, (sp, 32)
	stw     a3, (sp, 36)

	addi	sp, 32
	addi	sp, 8
	mfcr    r13, ss2
	stw	r6, (sp)
	stw	r7, (sp, 4)
	stw	r8, (sp, 8)
	stw	r9, (sp, 12)
	stw	r10, (sp, 16)
	stw	r11, (sp, 20)
	stw	r12, (sp, 24)
	stw	r13, (sp, 28)
	stw	r14, (sp, 32)
	stw	r1, (sp, 36)
	subi	sp, 32
	subi	sp, 8
.endm

.macro	RESTORE_ALL
	psrclr  ie
	ldw	lr, (sp, 4)
	ldw     a0, (sp, 8)
	mtcr    a0, epc
	ldw     a0, (sp, 12)
	mtcr    a0, epsr
	btsti   a0, 31
	bt      1f
	ldw     a0, (sp, 16)
	mtcr	a0, ss1
1:
	ldw     a0, (sp, 24)
	ldw     a1, (sp, 28)
	ldw     a2, (sp, 32)
	ldw     a3, (sp, 36)

	addi	sp, 32
	addi	sp, 8
	ldw	r6, (sp)
	ldw	r7, (sp, 4)
	ldw	r8, (sp, 8)
	ldw	r9, (sp, 12)
	ldw	r10, (sp, 16)
	ldw	r11, (sp, 20)
	ldw	r12, (sp, 24)
	ldw	r13, (sp, 28)
	ldw	r14, (sp, 32)
	ldw	r1, (sp, 36)
	addi	sp, 32
	addi	sp, 8

	bt      2f
	KSPTOUSP
2:
	rte
.endm

.macro SAVE_SWITCH_STACK
	subi    sp, 32
	stm     r8-r15, (sp)
.endm

.macro RESTORE_SWITCH_STACK
	ldm     r8-r15, (sp)
	addi    sp, 32
.endm

/* MMU registers operators. */
.macro RD_MIR	rx
	cprcr   \rx, cpcr0
.endm

.macro RD_MEH	rx
	cprcr   \rx, cpcr4
.endm

.macro RD_MCIR	rx
	cprcr   \rx, cpcr8
.endm

.macro RD_PGDR  rx
	cprcr   \rx, cpcr29
.endm

.macro WR_MEH	rx
	cpwcr   \rx, cpcr4
.endm

.macro WR_MCIR	rx
	cpwcr   \rx, cpcr8
.endm

.macro SETUP_MMU
	/* Init psr and enable ee */
	lrw	r6, DEFAULT_PSR_VALUE
	mtcr    r6, psr
	psrset  ee

	/* Select MMU as co-processor */
	cpseti	cp15

	/*
	 * cpcr30 format:
	 * 31 - 29 | 28 - 4 | 3 | 2 | 1 | 0
	 *   BA     Reserved  C   D   V
	 */
	cprcr	r6, cpcr30
	lsri	r6, 28
	lsli	r6, 28
	addi	r6, 0xe
	cpwcr	r6, cpcr30

	movi	r6, 0
	cpwcr	r6, cpcr31
.endm

.macro ANDI_R3 rx, imm
	lsri	\rx, 3
	andi	\rx, (\imm >> 3)
.endm
#endif /* __ASM_CSKY_ENTRY_H */

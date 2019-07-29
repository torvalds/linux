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

#define EPC_INCREASE	2
#define EPC_KEEP	0

.macro USPTOKSP
	mtcr	sp, ss1
	mfcr	sp, ss0
.endm

.macro KSPTOUSP
	mtcr	sp, ss0
	mfcr	sp, ss1
.endm

.macro INCTRAP	rx
	addi	\rx, EPC_INCREASE
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

	mfcr	lr, ss1
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
	ldw     a0, (sp, 16)
	mtcr	a0, ss1

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

	bt      1f
	KSPTOUSP
1:
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

.macro SETUP_MMU rx
	lrw	\rx, PHYS_OFFSET | 0xe
	cpwcr	\rx, cpcr30
	lrw	\rx, (PHYS_OFFSET + 0x20000000) | 0xe
	cpwcr	\rx, cpcr31
.endm

#endif /* __ASM_CSKY_ENTRY_H */

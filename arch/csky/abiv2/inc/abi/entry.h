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

#define EPC_INCREASE	4
#define EPC_KEEP	0

#define KSPTOUSP
#define USPTOKSP

#define usp cr<14, 1>

.macro INCTRAP	rx
	addi	\rx, EPC_INCREASE
.endm

.macro SAVE_ALL epc_inc
	subi    sp, 152
	stw	tls, (sp, 0)
	stw	lr, (sp, 4)

	mfcr	lr, epc
	movi	tls, \epc_inc
	add	lr, tls
	stw	lr, (sp, 8)

	mfcr	lr, epsr
	stw	lr, (sp, 12)
	mfcr	lr, usp
	stw	lr, (sp, 16)

	stw     a0, (sp, 20)
	stw     a0, (sp, 24)
	stw     a1, (sp, 28)
	stw     a2, (sp, 32)
	stw     a3, (sp, 36)

	addi	sp, 40
	stm	r4-r13, (sp)

	addi    sp, 40
	stm     r16-r30, (sp)
#ifdef CONFIG_CPU_HAS_HILO
	mfhi	lr
	stw	lr, (sp, 60)
	mflo	lr
	stw	lr, (sp, 64)
	mfcr	lr, cr14
	stw	lr, (sp, 68)
#endif
	subi	sp, 80
.endm

.macro	RESTORE_ALL
	psrclr  ie
	ldw	tls, (sp, 0)
	ldw	lr, (sp, 4)
	ldw	a0, (sp, 8)
	mtcr	a0, epc
	ldw	a0, (sp, 12)
	mtcr	a0, epsr
	ldw	a0, (sp, 16)
	mtcr	a0, usp

#ifdef CONFIG_CPU_HAS_HILO
	ldw	a0, (sp, 140)
	mthi	a0
	ldw	a0, (sp, 144)
	mtlo	a0
	ldw	a0, (sp, 148)
	mtcr	a0, cr14
#endif

	ldw     a0, (sp, 24)
	ldw     a1, (sp, 28)
	ldw     a2, (sp, 32)
	ldw     a3, (sp, 36)

	addi	sp, 40
	ldm	r4-r13, (sp)
	addi    sp, 40
	ldm     r16-r30, (sp)
	addi    sp, 72
	rte
.endm

.macro SAVE_SWITCH_STACK
	subi    sp, 64
	stm	r4-r11, (sp)
	stw	lr,  (sp, 32)
	stw	r16, (sp, 36)
	stw	r17, (sp, 40)
	stw	r26, (sp, 44)
	stw	r27, (sp, 48)
	stw	r28, (sp, 52)
	stw	r29, (sp, 56)
	stw	r30, (sp, 60)
#ifdef CONFIG_CPU_HAS_HILO
	subi	sp, 16
	mfhi	lr
	stw	lr, (sp, 0)
	mflo	lr
	stw	lr, (sp, 4)
	mfcr	lr, cr14
	stw	lr, (sp, 8)
#endif
.endm

.macro RESTORE_SWITCH_STACK
#ifdef CONFIG_CPU_HAS_HILO
	ldw	lr, (sp, 0)
	mthi	lr
	ldw	lr, (sp, 4)
	mtlo	lr
	ldw	lr, (sp, 8)
	mtcr	lr, cr14
	addi	sp, 16
#endif
	ldm	r4-r11, (sp)
	ldw	lr,  (sp, 32)
	ldw	r16, (sp, 36)
	ldw	r17, (sp, 40)
	ldw	r26, (sp, 44)
	ldw	r27, (sp, 48)
	ldw	r28, (sp, 52)
	ldw	r29, (sp, 56)
	ldw	r30, (sp, 60)
	addi	sp, 64
.endm

/* MMU registers operators. */
.macro RD_MIR rx
	mfcr	\rx, cr<0, 15>
.endm

.macro RD_MEH rx
	mfcr	\rx, cr<4, 15>
.endm

.macro RD_MCIR rx
	mfcr	\rx, cr<8, 15>
.endm

.macro RD_PGDR rx
	mfcr	\rx, cr<29, 15>
.endm

.macro RD_PGDR_K rx
	mfcr	\rx, cr<28, 15>
.endm

.macro WR_MEH rx
	mtcr	\rx, cr<4, 15>
.endm

.macro WR_MCIR rx
	mtcr	\rx, cr<8, 15>
.endm

.macro SETUP_MMU rx
	lrw	\rx, PHYS_OFFSET | 0xe
	mtcr	\rx, cr<30, 15>
	lrw	\rx, (PHYS_OFFSET + 0x20000000) | 0xe
	mtcr	\rx, cr<31, 15>
.endm
#endif /* __ASM_CSKY_ENTRY_H */

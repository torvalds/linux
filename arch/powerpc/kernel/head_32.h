/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __HEAD_32_H__
#define __HEAD_32_H__

#include <asm/ptrace.h>	/* for STACK_FRAME_REGS_MARKER */

/*
 * Exception entry code.  This code runs with address translation
 * turned off, i.e. using physical addresses.
 * We assume sprg3 has the physical address of the current
 * task's thread_struct.
 */
.macro EXCEPTION_PROLOG		trapno name handle_dar_dsisr=0
	EXCEPTION_PROLOG_0	handle_dar_dsisr=\handle_dar_dsisr
	EXCEPTION_PROLOG_1
	EXCEPTION_PROLOG_2	\trapno \name handle_dar_dsisr=\handle_dar_dsisr
.endm

.macro EXCEPTION_PROLOG_0 handle_dar_dsisr=0
	mtspr	SPRN_SPRG_SCRATCH0,r10
	mtspr	SPRN_SPRG_SCRATCH1,r11
	mfspr	r10, SPRN_SPRG_THREAD
	.if	\handle_dar_dsisr
#ifdef CONFIG_40x
	mfspr	r11, SPRN_DEAR
#else
	mfspr	r11, SPRN_DAR
#endif
	stw	r11, DAR(r10)
#ifdef CONFIG_40x
	mfspr	r11, SPRN_ESR
#else
	mfspr	r11, SPRN_DSISR
#endif
	stw	r11, DSISR(r10)
	.endif
	mfspr	r11, SPRN_SRR0
	stw	r11, SRR0(r10)
	mfspr	r11, SPRN_SRR1		/* check whether user or kernel */
	stw	r11, SRR1(r10)
	mfcr	r10
	andi.	r11, r11, MSR_PR
.endm

.macro EXCEPTION_PROLOG_1
	mtspr	SPRN_SPRG_SCRATCH2,r1
	subi	r1, r1, INT_FRAME_SIZE		/* use r1 if kernel */
	beq	1f
	mfspr	r1,SPRN_SPRG_THREAD
	lwz	r1,TASK_STACK-THREAD(r1)
	addi	r1, r1, THREAD_SIZE - INT_FRAME_SIZE
1:
#ifdef CONFIG_VMAP_STACK
	mtcrf	0x3f, r1
	bt	32 - THREAD_ALIGN_SHIFT, vmap_stack_overflow
#endif
.endm

.macro EXCEPTION_PROLOG_2 trapno name handle_dar_dsisr=0
#ifdef CONFIG_PPC_8xx
	.if	\handle_dar_dsisr
	li	r11, RPN_PATTERN
	mtspr	SPRN_DAR, r11	/* Tag DAR, to be used in DTLB Error */
	.endif
#endif
	LOAD_REG_IMMEDIATE(r11, MSR_KERNEL & ~MSR_RI) /* re-enable MMU */
	mtspr	SPRN_SRR1, r11
	lis	r11, 1f@h
	ori	r11, r11, 1f@l
	mtspr	SPRN_SRR0, r11
	mfspr	r11, SPRN_SPRG_SCRATCH2
	rfi

	.text
\name\()_virt:
1:
	stw	r11,GPR1(r1)
	stw	r11,0(r1)
	mr	r11, r1
	stw	r10,_CCR(r11)		/* save registers */
	stw	r12,GPR12(r11)
	stw	r9,GPR9(r11)
	mfspr	r10,SPRN_SPRG_SCRATCH0
	mfspr	r12,SPRN_SPRG_SCRATCH1
	stw	r10,GPR10(r11)
	stw	r12,GPR11(r11)
	mflr	r10
	stw	r10,_LINK(r11)
	mfspr	r12, SPRN_SPRG_THREAD
	tovirt(r12, r12)
	.if	\handle_dar_dsisr
	lwz	r10, DAR(r12)
	stw	r10, _DAR(r11)
	lwz	r10, DSISR(r12)
	stw	r10, _DSISR(r11)
	.endif
	lwz	r9, SRR1(r12)
	lwz	r12, SRR0(r12)
#ifdef CONFIG_40x
	rlwinm	r9,r9,0,14,12		/* clear MSR_WE (necessary?) */
#elif defined(CONFIG_PPC_8xx)
	mtspr	SPRN_EID, r2		/* Set MSR_RI */
#else
	li	r10, MSR_KERNEL		/* can take exceptions */
	mtmsr	r10			/* (except for mach check in rtas) */
#endif
	COMMON_EXCEPTION_PROLOG_END \trapno
_ASM_NOKPROBE_SYMBOL(\name\()_virt)
.endm

.macro COMMON_EXCEPTION_PROLOG_END trapno
	stw	r0,GPR0(r1)
	lis	r10,STACK_FRAME_REGS_MARKER@ha /* exception frame marker */
	addi	r10,r10,STACK_FRAME_REGS_MARKER@l
	stw	r10,8(r1)
	li	r10, \trapno
	stw	r10,_TRAP(r1)
	SAVE_4GPRS(3, r1)
	SAVE_2GPRS(7, r1)
	SAVE_NVGPRS(r1)
	stw	r2,GPR2(r1)
	stw	r12,_NIP(r1)
	stw	r9,_MSR(r1)
	mfctr	r10
	mfspr	r2,SPRN_SPRG_THREAD
	stw	r10,_CTR(r1)
	tovirt(r2, r2)
	mfspr	r10,SPRN_XER
	addi	r2, r2, -THREAD
	stw	r10,_XER(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
.endm

.macro prepare_transfer_to_handler
#ifdef CONFIG_PPC_BOOK3S_32
	andi.	r12,r9,MSR_PR
	bne	777f
	bl	prepare_transfer_to_handler
777:
#endif
.endm

.macro SYSCALL_ENTRY trapno
	mfspr	r9, SPRN_SRR1
	mfspr	r12, SPRN_SRR0
	LOAD_REG_IMMEDIATE(r11, MSR_KERNEL)		/* can take exceptions */
	lis	r10, 1f@h
	ori	r10, r10, 1f@l
	mtspr	SPRN_SRR1, r11
	mtspr	SPRN_SRR0, r10
	mfspr	r10,SPRN_SPRG_THREAD
	mr	r11, r1
	lwz	r1,TASK_STACK-THREAD(r10)
	tovirt(r10, r10)
	addi	r1, r1, THREAD_SIZE - INT_FRAME_SIZE
	rfi
1:
	stw	r12,_NIP(r1)
	mfcr	r12
	rlwinm	r12,r12,0,4,2	/* Clear SO bit in CR */
	stw	r12,_CCR(r1)
	b	transfer_to_syscall		/* jump to handler */
.endm

/*
 * Note: code which follows this uses cr0.eq (set if from kernel),
 * r11, r12 (SRR0), and r9 (SRR1).
 *
 * Note2: once we have set r1 we are in a position to take exceptions
 * again, and we could thus set MSR:RI at that point.
 */

/*
 * Exception vectors.
 */
#ifdef CONFIG_PPC_BOOK3S
#define	START_EXCEPTION(n, label)		\
	__HEAD;					\
	. = n;					\
	DO_KVM n;				\
label:

#else
#define	START_EXCEPTION(n, label)		\
	__HEAD;					\
	. = n;					\
label:

#endif

#define EXCEPTION(n, label, hdlr)		\
	START_EXCEPTION(n, label)		\
	EXCEPTION_PROLOG n label;		\
	prepare_transfer_to_handler;		\
	bl	hdlr;				\
	b	interrupt_return

.macro vmap_stack_overflow_exception
	__HEAD
vmap_stack_overflow:
#ifdef CONFIG_SMP
	mfspr	r1, SPRN_SPRG_THREAD
	lwz	r1, TASK_CPU - THREAD(r1)
	slwi	r1, r1, 3
	addis	r1, r1, emergency_ctx-PAGE_OFFSET@ha
#else
	lis	r1, emergency_ctx-PAGE_OFFSET@ha
#endif
	lwz	r1, emergency_ctx-PAGE_OFFSET@l(r1)
	addi	r1, r1, THREAD_SIZE - INT_FRAME_SIZE
	EXCEPTION_PROLOG_2 0 vmap_stack_overflow
	prepare_transfer_to_handler
	bl	stack_overflow_exception
	b	interrupt_return
.endm

#endif /* __HEAD_32_H__ */

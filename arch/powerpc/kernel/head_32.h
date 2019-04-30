/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __HEAD_32_H__
#define __HEAD_32_H__

#include <asm/ptrace.h>	/* for STACK_FRAME_REGS_MARKER */

/*
 * MSR_KERNEL is > 0x8000 on 4xx/Book-E since it include MSR_CE.
 */
.macro __LOAD_MSR_KERNEL r, x
.if \x >= 0x8000
	lis \r, (\x)@h
	ori \r, \r, (\x)@l
.else
	li \r, (\x)
.endif
.endm
#define LOAD_MSR_KERNEL(r, x) __LOAD_MSR_KERNEL r, x

/*
 * Exception entry code.  This code runs with address translation
 * turned off, i.e. using physical addresses.
 * We assume sprg3 has the physical address of the current
 * task's thread_struct.
 */

.macro EXCEPTION_PROLOG
	mtspr	SPRN_SPRG_SCRATCH0,r10
	mtspr	SPRN_SPRG_SCRATCH1,r11
	mfcr	r10
	EXCEPTION_PROLOG_1
	EXCEPTION_PROLOG_2
.endm

.macro EXCEPTION_PROLOG_1
	mfspr	r11,SPRN_SRR1		/* check whether user or kernel */
	andi.	r11,r11,MSR_PR
	tophys(r11,r1)			/* use tophys(r1) if kernel */
	beq	1f
	mfspr	r11,SPRN_SPRG_THREAD
	lwz	r11,TASK_STACK-THREAD(r11)
	addi	r11,r11,THREAD_SIZE
	tophys(r11,r11)
1:	subi	r11,r11,INT_FRAME_SIZE	/* alloc exc. frame */
.endm

.macro EXCEPTION_PROLOG_2
	stw	r10,_CCR(r11)		/* save registers */
	stw	r12,GPR12(r11)
	stw	r9,GPR9(r11)
	mfspr	r10,SPRN_SPRG_SCRATCH0
	stw	r10,GPR10(r11)
	mfspr	r12,SPRN_SPRG_SCRATCH1
	stw	r12,GPR11(r11)
	mflr	r10
	stw	r10,_LINK(r11)
	mfspr	r12,SPRN_SRR0
	mfspr	r9,SPRN_SRR1
	stw	r1,GPR1(r11)
	stw	r1,0(r11)
	tovirt(r1,r11)			/* set new kernel sp */
#ifdef CONFIG_40x
	rlwinm	r9,r9,0,14,12		/* clear MSR_WE (necessary?) */
#else
	li	r10,MSR_KERNEL & ~(MSR_IR|MSR_DR) /* can take exceptions */
	MTMSRD(r10)			/* (except for mach check in rtas) */
#endif
	stw	r0,GPR0(r11)
	lis	r10,STACK_FRAME_REGS_MARKER@ha /* exception frame marker */
	addi	r10,r10,STACK_FRAME_REGS_MARKER@l
	stw	r10,8(r11)
	SAVE_4GPRS(3, r11)
	SAVE_2GPRS(7, r11)
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
	. = n;					\
	DO_KVM n;				\
label:

#else
#define	START_EXCEPTION(n, label)		\
	. = n;					\
label:

#endif

#define EXCEPTION(n, label, hdlr, xfer)		\
	START_EXCEPTION(n, label)		\
	EXCEPTION_PROLOG;			\
	addi	r3,r1,STACK_FRAME_OVERHEAD;	\
	xfer(n, hdlr)

#define EXC_XFER_TEMPLATE(hdlr, trap, msr, tfer, ret)		\
	li	r10,trap;					\
	stw	r10,_TRAP(r11);					\
	LOAD_MSR_KERNEL(r10, msr);				\
	bl	tfer;						\
	.long	hdlr;						\
	.long	ret

#define EXC_XFER_STD(n, hdlr)		\
	EXC_XFER_TEMPLATE(hdlr, n, MSR_KERNEL, transfer_to_handler_full,	\
			  ret_from_except_full)

#define EXC_XFER_LITE(n, hdlr)		\
	EXC_XFER_TEMPLATE(hdlr, n+1, MSR_KERNEL, transfer_to_handler, \
			  ret_from_except)

#define EXC_XFER_SYS(n, hdlr)		\
	EXC_XFER_TEMPLATE(hdlr, n+1, MSR_KERNEL | MSR_EE, transfer_to_handler, \
			  ret_from_except)

#endif /* __HEAD_32_H__ */

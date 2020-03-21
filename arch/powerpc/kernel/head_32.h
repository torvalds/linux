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
.macro EXCEPTION_PROLOG handle_dar_dsisr=0
	EXCEPTION_PROLOG_0	handle_dar_dsisr=\handle_dar_dsisr
	EXCEPTION_PROLOG_1
	EXCEPTION_PROLOG_2	handle_dar_dsisr=\handle_dar_dsisr
.endm

.macro EXCEPTION_PROLOG_0 handle_dar_dsisr=0
	mtspr	SPRN_SPRG_SCRATCH0,r10
	mtspr	SPRN_SPRG_SCRATCH1,r11
#ifdef CONFIG_VMAP_STACK
	mfspr	r10, SPRN_SPRG_THREAD
	.if	\handle_dar_dsisr
	mfspr	r11, SPRN_DAR
	stw	r11, DAR(r10)
	mfspr	r11, SPRN_DSISR
	stw	r11, DSISR(r10)
	.endif
	mfspr	r11, SPRN_SRR0
	stw	r11, SRR0(r10)
#endif
	mfspr	r11, SPRN_SRR1		/* check whether user or kernel */
#ifdef CONFIG_VMAP_STACK
	stw	r11, SRR1(r10)
#endif
	mfcr	r10
	andi.	r11, r11, MSR_PR
.endm

.macro EXCEPTION_PROLOG_1 for_rtas=0
#ifdef CONFIG_VMAP_STACK
	.ifeq	\for_rtas
	li	r11, MSR_KERNEL & ~(MSR_IR | MSR_RI) /* can take DTLB miss */
	mtmsr	r11
	isync
	.endif
	subi	r11, r1, INT_FRAME_SIZE		/* use r1 if kernel */
#else
	tophys(r11,r1)			/* use tophys(r1) if kernel */
	subi	r11, r11, INT_FRAME_SIZE	/* alloc exc. frame */
#endif
	beq	1f
	mfspr	r11,SPRN_SPRG_THREAD
	tovirt_vmstack r11, r11
	lwz	r11,TASK_STACK-THREAD(r11)
	addi	r11, r11, THREAD_SIZE - INT_FRAME_SIZE
	tophys_novmstack r11, r11
1:
#ifdef CONFIG_VMAP_STACK
	mtcrf	0x7f, r11
	bt	32 - THREAD_ALIGN_SHIFT, stack_overflow
#endif
.endm

.macro EXCEPTION_PROLOG_2 handle_dar_dsisr=0
#if defined(CONFIG_VMAP_STACK) && defined(CONFIG_PPC_BOOK3S)
BEGIN_MMU_FTR_SECTION
	mtcr	r10
FTR_SECTION_ELSE
	stw	r10, _CCR(r11)
ALT_MMU_FTR_SECTION_END_IFSET(MMU_FTR_HPTE_TABLE)
#else
	stw	r10,_CCR(r11)		/* save registers */
#endif
	mfspr	r10, SPRN_SPRG_SCRATCH0
	stw	r12,GPR12(r11)
	stw	r9,GPR9(r11)
	stw	r10,GPR10(r11)
#if defined(CONFIG_VMAP_STACK) && defined(CONFIG_PPC_BOOK3S)
BEGIN_MMU_FTR_SECTION
	mfcr	r10
	stw	r10, _CCR(r11)
END_MMU_FTR_SECTION_IFSET(MMU_FTR_HPTE_TABLE)
#endif
	mfspr	r12,SPRN_SPRG_SCRATCH1
	stw	r12,GPR11(r11)
	mflr	r10
	stw	r10,_LINK(r11)
#ifdef CONFIG_VMAP_STACK
	mfspr	r12, SPRN_SPRG_THREAD
	tovirt(r12, r12)
	.if	\handle_dar_dsisr
	lwz	r10, DAR(r12)
	stw	r10, _DAR(r11)
	lwz	r10, DSISR(r12)
	stw	r10, _DSISR(r11)
	.endif
	lwz	r9, SRR1(r12)
#if defined(CONFIG_VMAP_STACK) && defined(CONFIG_PPC_BOOK3S)
BEGIN_MMU_FTR_SECTION
	andi.	r10, r9, MSR_PR
END_MMU_FTR_SECTION_IFSET(MMU_FTR_HPTE_TABLE)
#endif
	lwz	r12, SRR0(r12)
#else
	mfspr	r12,SPRN_SRR0
	mfspr	r9,SPRN_SRR1
#endif
	stw	r1,GPR1(r11)
	stw	r1,0(r11)
	tovirt_novmstack r1, r11	/* set new kernel sp */
#ifdef CONFIG_40x
	rlwinm	r9,r9,0,14,12		/* clear MSR_WE (necessary?) */
#else
#ifdef CONFIG_VMAP_STACK
	li	r10, MSR_KERNEL & ~MSR_IR /* can take exceptions */
#else
	li	r10,MSR_KERNEL & ~(MSR_IR|MSR_DR) /* can take exceptions */
#endif
	mtmsr	r10			/* (except for mach check in rtas) */
#endif
	stw	r0,GPR0(r11)
	lis	r10,STACK_FRAME_REGS_MARKER@ha /* exception frame marker */
	addi	r10,r10,STACK_FRAME_REGS_MARKER@l
	stw	r10,8(r11)
	SAVE_4GPRS(3, r11)
	SAVE_2GPRS(7, r11)
.endm

.macro SYSCALL_ENTRY trapno
	mfspr	r12,SPRN_SPRG_THREAD
#ifdef CONFIG_VMAP_STACK
	mfspr	r9, SPRN_SRR0
	mfspr	r11, SPRN_SRR1
	stw	r9, SRR0(r12)
	stw	r11, SRR1(r12)
#endif
	mfcr	r10
	lwz	r11,TASK_STACK-THREAD(r12)
	rlwinm	r10,r10,0,4,2	/* Clear SO bit in CR */
	addi	r11, r11, THREAD_SIZE - INT_FRAME_SIZE
#ifdef CONFIG_VMAP_STACK
	li	r9, MSR_KERNEL & ~(MSR_IR | MSR_RI) /* can take DTLB miss */
	mtmsr	r9
	isync
#endif
	tovirt_vmstack r12, r12
	tophys_novmstack r11, r11
	mflr	r9
	stw	r10,_CCR(r11)		/* save registers */
	stw	r9, _LINK(r11)
#ifdef CONFIG_VMAP_STACK
	lwz	r10, SRR0(r12)
	lwz	r9, SRR1(r12)
#else
	mfspr	r10,SPRN_SRR0
	mfspr	r9,SPRN_SRR1
#endif
	stw	r1,GPR1(r11)
	stw	r1,0(r11)
	tovirt_novmstack r1, r11	/* set new kernel sp */
	stw	r10,_NIP(r11)
#ifdef CONFIG_40x
	rlwinm	r9,r9,0,14,12		/* clear MSR_WE (necessary?) */
#else
#ifdef CONFIG_VMAP_STACK
	LOAD_REG_IMMEDIATE(r10, MSR_KERNEL & ~MSR_IR) /* can take exceptions */
#else
	LOAD_REG_IMMEDIATE(r10, MSR_KERNEL & ~(MSR_IR|MSR_DR)) /* can take exceptions */
#endif
	mtmsr	r10			/* (except for mach check in rtas) */
#endif
	lis	r10,STACK_FRAME_REGS_MARKER@ha /* exception frame marker */
	stw	r2,GPR2(r11)
	addi	r10,r10,STACK_FRAME_REGS_MARKER@l
	stw	r9,_MSR(r11)
	li	r2, \trapno + 1
	stw	r10,8(r11)
	stw	r2,_TRAP(r11)
	SAVE_GPR(0, r11)
	SAVE_4GPRS(3, r11)
	SAVE_2GPRS(7, r11)
	addi	r11,r1,STACK_FRAME_OVERHEAD
	addi	r2,r12,-THREAD
	stw	r11,PT_REGS(r12)
#if defined(CONFIG_40x)
	/* Check to see if the dbcr0 register is set up to debug.  Use the
	   internal debug mode bit to do this. */
	lwz	r12,THREAD_DBCR0(r12)
	andis.	r12,r12,DBCR0_IDM@h
#endif
	ACCOUNT_CPU_USER_ENTRY(r2, r11, r12)
#if defined(CONFIG_40x)
	beq+	3f
	/* From user and task is ptraced - load up global dbcr0 */
	li	r12,-1			/* clear all pending debug events */
	mtspr	SPRN_DBSR,r12
	lis	r11,global_dbcr0@ha
	tophys(r11,r11)
	addi	r11,r11,global_dbcr0@l
	lwz	r12,0(r11)
	mtspr	SPRN_DBCR0,r12
	lwz	r12,4(r11)
	addi	r12,r12,-1
	stw	r12,4(r11)
#endif

3:
	tovirt_novmstack r2, r2 	/* set r2 to current */
	lis	r11, transfer_to_syscall@h
	ori	r11, r11, transfer_to_syscall@l
#ifdef CONFIG_TRACE_IRQFLAGS
	/*
	 * If MSR is changing we need to keep interrupts disabled at this point
	 * otherwise we might risk taking an interrupt before we tell lockdep
	 * they are enabled.
	 */
	LOAD_REG_IMMEDIATE(r10, MSR_KERNEL)
	rlwimi	r10, r9, 0, MSR_EE
#else
	LOAD_REG_IMMEDIATE(r10, MSR_KERNEL | MSR_EE)
#endif
#if defined(CONFIG_PPC_8xx) && defined(CONFIG_PERF_EVENTS)
	mtspr	SPRN_NRI, r0
#endif
	mtspr	SPRN_SRR1,r10
	mtspr	SPRN_SRR0,r11
	SYNC
	RFI				/* jump to handler, enable MMU */
.endm

.macro save_dar_dsisr_on_stack reg1, reg2, sp
#ifndef CONFIG_VMAP_STACK
	mfspr	\reg1, SPRN_DAR
	mfspr	\reg2, SPRN_DSISR
	stw	\reg1, _DAR(\sp)
	stw	\reg2, _DSISR(\sp)
#endif
.endm

.macro get_and_save_dar_dsisr_on_stack reg1, reg2, sp
#ifdef CONFIG_VMAP_STACK
	lwz	\reg1, _DAR(\sp)
	lwz	\reg2, _DSISR(\sp)
#else
	save_dar_dsisr_on_stack \reg1, \reg2, \sp
#endif
.endm

.macro tovirt_vmstack dst, src
#ifdef CONFIG_VMAP_STACK
	tovirt(\dst, \src)
#else
	.ifnc	\dst, \src
	mr	\dst, \src
	.endif
#endif
.endm

.macro tovirt_novmstack dst, src
#ifndef CONFIG_VMAP_STACK
	tovirt(\dst, \src)
#else
	.ifnc	\dst, \src
	mr	\dst, \src
	.endif
#endif
.endm

.macro tophys_novmstack dst, src
#ifndef CONFIG_VMAP_STACK
	tophys(\dst, \src)
#else
	.ifnc	\dst, \src
	mr	\dst, \src
	.endif
#endif
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
	LOAD_REG_IMMEDIATE(r10, msr);				\
	bl	tfer;						\
	.long	hdlr;						\
	.long	ret

#define EXC_XFER_STD(n, hdlr)		\
	EXC_XFER_TEMPLATE(hdlr, n, MSR_KERNEL, transfer_to_handler_full,	\
			  ret_from_except_full)

#define EXC_XFER_LITE(n, hdlr)		\
	EXC_XFER_TEMPLATE(hdlr, n+1, MSR_KERNEL, transfer_to_handler, \
			  ret_from_except)

.macro vmap_stack_overflow_exception
#ifdef CONFIG_VMAP_STACK
#ifdef CONFIG_SMP
	mfspr	r11, SPRN_SPRG_THREAD
	tovirt(r11, r11)
	lwz	r11, TASK_CPU - THREAD(r11)
	slwi	r11, r11, 3
	addis	r11, r11, emergency_ctx@ha
#else
	lis	r11, emergency_ctx@ha
#endif
	lwz	r11, emergency_ctx@l(r11)
	cmpwi	cr1, r11, 0
	bne	cr1, 1f
	lis	r11, init_thread_union@ha
	addi	r11, r11, init_thread_union@l
1:	addi	r11, r11, THREAD_SIZE - INT_FRAME_SIZE
	EXCEPTION_PROLOG_2
	SAVE_NVGPRS(r11)
	addi	r3, r1, STACK_FRAME_OVERHEAD
	EXC_XFER_STD(0, stack_overflow_exception)
#endif
.endm

#endif /* __HEAD_32_H__ */

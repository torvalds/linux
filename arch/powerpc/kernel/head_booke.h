/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __HEAD_BOOKE_H__
#define __HEAD_BOOKE_H__

#include <asm/ptrace.h>	/* for STACK_FRAME_REGS_MARKER */
#include <asm/kvm_asm.h>
#include <asm/kvm_booke_hv_asm.h>

#ifdef __ASSEMBLY__

/*
 * Macros used for common Book-e exception handling
 */

#define SET_IVOR(vector_number, vector_label)		\
		li	r26,vector_label@l; 		\
		mtspr	SPRN_IVOR##vector_number,r26;	\
		sync

#if (THREAD_SHIFT < 15)
#define ALLOC_STACK_FRAME(reg, val)			\
	addi reg,reg,val
#else
#define ALLOC_STACK_FRAME(reg, val)			\
	addis	reg,reg,val@ha;				\
	addi	reg,reg,val@l
#endif

/*
 * Macro used to get to thread save registers.
 * Note that entries 0-3 are used for the prolog code, and the remaining
 * entries are available for specific exception use in the event a handler
 * requires more than 4 scratch registers.
 */
#define THREAD_NORMSAVE(offset)	(THREAD_NORMSAVES + (offset * 4))

#ifdef CONFIG_PPC_E500
#define BOOKE_CLEAR_BTB(reg)									\
START_BTB_FLUSH_SECTION								\
	BTB_FLUSH(reg)									\
END_BTB_FLUSH_SECTION
#else
#define BOOKE_CLEAR_BTB(reg)
#endif


#define NORMAL_EXCEPTION_PROLOG(trapno, intno)						     \
	mtspr	SPRN_SPRG_WSCRATCH0, r10;	/* save one register */	     \
	mfspr	r10, SPRN_SPRG_THREAD;					     \
	stw	r11, THREAD_NORMSAVE(0)(r10);				     \
	stw	r13, THREAD_NORMSAVE(2)(r10);				     \
	mfcr	r13;			/* save CR in r13 for now	   */\
	mfspr	r11, SPRN_SRR1;		                                     \
	DO_KVM	BOOKE_INTERRUPT_##intno SPRN_SRR1;			     \
	andi.	r11, r11, MSR_PR;	/* check whether user or kernel    */\
	LOAD_REG_IMMEDIATE(r11, MSR_KERNEL);				\
	mtmsr	r11;							\
	mr	r11, r1;						     \
	beq	1f;							     \
	BOOKE_CLEAR_BTB(r11)						\
	/* if from user, start at top of this thread's kernel stack */       \
	lwz	r11, TASK_STACK - THREAD(r10);				     \
	ALLOC_STACK_FRAME(r11, THREAD_SIZE);				     \
1 :	subi	r11, r11, INT_FRAME_SIZE; /* Allocate exception frame */     \
	stw	r13, _CCR(r11);		/* save various registers */	     \
	stw	r12,GPR12(r11);						     \
	stw	r9,GPR9(r11);						     \
	mfspr	r13, SPRN_SPRG_RSCRATCH0;				     \
	stw	r13, GPR10(r11);					     \
	lwz	r12, THREAD_NORMSAVE(0)(r10);				     \
	stw	r12,GPR11(r11);						     \
	lwz	r13, THREAD_NORMSAVE(2)(r10); /* restore r13 */		     \
	mflr	r10;							     \
	stw	r10,_LINK(r11);						     \
	mfspr	r12,SPRN_SRR0;						     \
	stw	r1, GPR1(r11);						     \
	mfspr	r9,SPRN_SRR1;						     \
	stw	r1, 0(r11);						     \
	mr	r1, r11;						     \
	rlwinm	r9,r9,0,14,12;		/* clear MSR_WE (necessary?)	   */\
	COMMON_EXCEPTION_PROLOG_END trapno

.macro COMMON_EXCEPTION_PROLOG_END trapno
	stw	r0,GPR0(r1)
	lis	r10, STACK_FRAME_REGS_MARKER@ha	/* exception frame marker */
	addi	r10, r10, STACK_FRAME_REGS_MARKER@l
	stw	r10, STACK_INT_FRAME_MARKER(r1)
	li	r10, \trapno
	stw	r10,_TRAP(r1)
	SAVE_GPRS(3, 8, r1)
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
	addi	r3,r1,STACK_INT_FRAME_REGS
.endm

.macro prepare_transfer_to_handler
#ifdef CONFIG_PPC_E500
	andi.	r12,r9,MSR_PR
	bne	777f
	bl	prepare_transfer_to_handler
777:
#endif
.endm

.macro SYSCALL_ENTRY trapno intno srr1
	mfspr	r10, SPRN_SPRG_THREAD
#ifdef CONFIG_KVM_BOOKE_HV
BEGIN_FTR_SECTION
	mtspr	SPRN_SPRG_WSCRATCH0, r10
	stw	r11, THREAD_NORMSAVE(0)(r10)
	stw	r13, THREAD_NORMSAVE(2)(r10)
	mfcr	r13			/* save CR in r13 for now	   */
	mfspr	r11, SPRN_SRR1
	mtocrf	0x80, r11	/* check MSR[GS] without clobbering reg */
	bf	3, 1975f
	b	kvmppc_handler_\intno\()_\srr1
1975:
	mr	r12, r13
	lwz	r13, THREAD_NORMSAVE(2)(r10)
FTR_SECTION_ELSE
	mfcr	r12
ALT_FTR_SECTION_END_IFSET(CPU_FTR_EMB_HV)
#else
	mfcr	r12
#endif
	mfspr	r9, SPRN_SRR1
	BOOKE_CLEAR_BTB(r11)
	mr	r11, r1
	lwz	r1, TASK_STACK - THREAD(r10)
	rlwinm	r12,r12,0,4,2	/* Clear SO bit in CR */
	ALLOC_STACK_FRAME(r1, THREAD_SIZE - INT_FRAME_SIZE)
	stw	r12, _CCR(r1)
	mfspr	r12,SPRN_SRR0
	stw	r12,_NIP(r1)
	b	transfer_to_syscall	/* jump to handler */
.endm

/* To handle the additional exception priority levels on 40x and Book-E
 * processors we allocate a stack per additional priority level.
 *
 * On 40x critical is the only additional level
 * On 44x/e500 we have critical and machine check
 *
 * Additionally we reserve a SPRG for each priority level so we can free up a
 * GPR to use as the base for indirect access to the exception stacks.  This
 * is necessary since the MMU is always on, for Book-E parts, and the stacks
 * are offset from KERNELBASE.
 *
 * There is some space optimization to be had here if desired.  However
 * to allow for a common kernel with support for debug exceptions either
 * going to critical or their own debug level we aren't currently
 * providing configurations that micro-optimize space usage.
 */

#define MC_STACK_BASE		mcheckirq_ctx
#define CRIT_STACK_BASE		critirq_ctx

/* only on e500mc */
#define DBG_STACK_BASE		dbgirq_ctx

#ifdef CONFIG_SMP
#define BOOKE_LOAD_EXC_LEVEL_STACK(level)		\
	mfspr	r8,SPRN_PIR;				\
	slwi	r8,r8,2;				\
	addis	r8,r8,level##_STACK_BASE@ha;		\
	lwz	r8,level##_STACK_BASE@l(r8);		\
	addi	r8,r8,THREAD_SIZE - INT_FRAME_SIZE;
#else
#define BOOKE_LOAD_EXC_LEVEL_STACK(level)		\
	lis	r8,level##_STACK_BASE@ha;		\
	lwz	r8,level##_STACK_BASE@l(r8);		\
	addi	r8,r8,THREAD_SIZE - INT_FRAME_SIZE;
#endif

/*
 * Exception prolog for critical/machine check exceptions.  This is a
 * little different from the normal exception prolog above since a
 * critical/machine check exception can potentially occur at any point
 * during normal exception processing. Thus we cannot use the same SPRG
 * registers as the normal prolog above. Instead we use a portion of the
 * critical/machine check exception stack at low physical addresses.
 */
#define EXC_LEVEL_EXCEPTION_PROLOG(exc_level, trapno, intno, exc_level_srr0, exc_level_srr1) \
	mtspr	SPRN_SPRG_WSCRATCH_##exc_level,r8;			     \
	BOOKE_LOAD_EXC_LEVEL_STACK(exc_level);/* r8 points to the exc_level stack*/ \
	stw	r9,GPR9(r8);		/* save various registers	   */\
	mfcr	r9;			/* save CR in r9 for now	   */\
	stw	r10,GPR10(r8);						     \
	stw	r11,GPR11(r8);						     \
	stw	r9,_CCR(r8);		/* save CR on stack		   */\
	mfspr	r11,exc_level_srr1;	/* check whether user or kernel    */\
	DO_KVM	BOOKE_INTERRUPT_##intno exc_level_srr1;		             \
	BOOKE_CLEAR_BTB(r10)						\
	andi.	r11,r11,MSR_PR;						     \
	LOAD_REG_IMMEDIATE(r11, MSR_KERNEL & ~(MSR_ME|MSR_DE|MSR_CE));	\
	mtmsr	r11;							\
	mfspr	r11,SPRN_SPRG_THREAD;	/* if from user, start at top of   */\
	lwz	r11, TASK_STACK - THREAD(r11); /* this thread's kernel stack */\
	addi	r11,r11,THREAD_SIZE - INT_FRAME_SIZE;	/* allocate stack frame    */\
	beq	1f;							     \
	/* COMING FROM USER MODE */					     \
	stw	r9,_CCR(r11);		/* save CR			   */\
	lwz	r10,GPR10(r8);		/* copy regs from exception stack  */\
	lwz	r9,GPR9(r8);						     \
	stw	r10,GPR10(r11);						     \
	lwz	r10,GPR11(r8);						     \
	stw	r9,GPR9(r11);						     \
	stw	r10,GPR11(r11);						     \
	b	2f;							     \
	/* COMING FROM PRIV MODE */					     \
1:	mr	r11, r8;							     \
2:	mfspr	r8,SPRN_SPRG_RSCRATCH_##exc_level;			     \
	stw	r12,GPR12(r11);		/* save various registers	   */\
	mflr	r10;							     \
	stw	r10,_LINK(r11);						     \
	mfspr	r12,SPRN_DEAR;		/* save DEAR and ESR in the frame  */\
	stw	r12,_DEAR(r11);		/* since they may have had stuff   */\
	mfspr	r9,SPRN_ESR;		/* in them at the point where the  */\
	stw	r9,_ESR(r11);		/* exception was taken		   */\
	mfspr	r12,exc_level_srr0;					     \
	stw	r1,GPR1(r11);						     \
	mfspr	r9,exc_level_srr1;					     \
	stw	r1,0(r11);						     \
	mr	r1,r11;							     \
	rlwinm	r9,r9,0,14,12;		/* clear MSR_WE (necessary?)	   */\
	COMMON_EXCEPTION_PROLOG_END trapno

#define SAVE_xSRR(xSRR)			\
	mfspr	r0,SPRN_##xSRR##0;	\
	stw	r0,_##xSRR##0(r1);	\
	mfspr	r0,SPRN_##xSRR##1;	\
	stw	r0,_##xSRR##1(r1)


.macro SAVE_MMU_REGS
#ifdef CONFIG_PPC_E500
	mfspr	r0,SPRN_MAS0
	stw	r0,MAS0(r1)
	mfspr	r0,SPRN_MAS1
	stw	r0,MAS1(r1)
	mfspr	r0,SPRN_MAS2
	stw	r0,MAS2(r1)
	mfspr	r0,SPRN_MAS3
	stw	r0,MAS3(r1)
	mfspr	r0,SPRN_MAS6
	stw	r0,MAS6(r1)
#ifdef CONFIG_PHYS_64BIT
	mfspr	r0,SPRN_MAS7
	stw	r0,MAS7(r1)
#endif /* CONFIG_PHYS_64BIT */
#endif /* CONFIG_PPC_E500 */
#ifdef CONFIG_44x
	mfspr	r0,SPRN_MMUCR
	stw	r0,MMUCR(r1)
#endif
.endm

#define CRITICAL_EXCEPTION_PROLOG(trapno, intno) \
		EXC_LEVEL_EXCEPTION_PROLOG(CRIT, trapno+2, intno, SPRN_CSRR0, SPRN_CSRR1)
#define DEBUG_EXCEPTION_PROLOG(trapno) \
		EXC_LEVEL_EXCEPTION_PROLOG(DBG, trapno+8, DEBUG, SPRN_DSRR0, SPRN_DSRR1)
#define MCHECK_EXCEPTION_PROLOG(trapno) \
		EXC_LEVEL_EXCEPTION_PROLOG(MC, trapno+4, MACHINE_CHECK, \
			SPRN_MCSRR0, SPRN_MCSRR1)

/*
 * Guest Doorbell -- this is a bit odd in that uses GSRR0/1 despite
 * being delivered to the host.  This exception can only happen
 * inside a KVM guest -- so we just handle up to the DO_KVM rather
 * than try to fit this into one of the existing prolog macros.
 */
#define GUEST_DOORBELL_EXCEPTION \
	START_EXCEPTION(GuestDoorbell);					     \
	mtspr	SPRN_SPRG_WSCRATCH0, r10;	/* save one register */	     \
	mfspr	r10, SPRN_SPRG_THREAD;					     \
	stw	r11, THREAD_NORMSAVE(0)(r10);				     \
	mfspr	r11, SPRN_SRR1;		                                     \
	stw	r13, THREAD_NORMSAVE(2)(r10);				     \
	mfcr	r13;			/* save CR in r13 for now	   */\
	DO_KVM	BOOKE_INTERRUPT_GUEST_DBELL SPRN_GSRR1;			     \
	trap

/*
 * Exception vectors.
 */
#define	START_EXCEPTION(label)						     \
        .align 5;              						     \
label:

#define EXCEPTION(n, intno, label, hdlr)			\
	START_EXCEPTION(label);					\
	NORMAL_EXCEPTION_PROLOG(n, intno);			\
	prepare_transfer_to_handler;				\
	bl	hdlr;						\
	b	interrupt_return

#define CRITICAL_EXCEPTION(n, intno, label, hdlr)			\
	START_EXCEPTION(label);						\
	CRITICAL_EXCEPTION_PROLOG(n, intno);				\
	SAVE_MMU_REGS;							\
	SAVE_xSRR(SRR);							\
	prepare_transfer_to_handler;					\
	bl	hdlr;							\
	b	ret_from_crit_exc

#define MCHECK_EXCEPTION(n, label, hdlr)			\
	START_EXCEPTION(label);					\
	MCHECK_EXCEPTION_PROLOG(n);				\
	mfspr	r5,SPRN_ESR;					\
	stw	r5,_ESR(r11);					\
	SAVE_xSRR(DSRR);					\
	SAVE_xSRR(CSRR);					\
	SAVE_MMU_REGS;						\
	SAVE_xSRR(SRR);						\
	prepare_transfer_to_handler;				\
	bl	hdlr;						\
	b	ret_from_mcheck_exc

/* Check for a single step debug exception while in an exception
 * handler before state has been saved.  This is to catch the case
 * where an instruction that we are trying to single step causes
 * an exception (eg ITLB/DTLB miss) and thus the first instruction of
 * the exception handler generates a single step debug exception.
 *
 * If we get a debug trap on the first instruction of an exception handler,
 * we reset the MSR_DE in the _exception handler's_ MSR (the debug trap is
 * a critical exception, so we are using SPRN_CSRR1 to manipulate the MSR).
 * The exception handler was handling a non-critical interrupt, so it will
 * save (and later restore) the MSR via SPRN_CSRR1, which will still have
 * the MSR_DE bit set.
 */
#define DEBUG_DEBUG_EXCEPTION						      \
	START_EXCEPTION(DebugDebug);					      \
	DEBUG_EXCEPTION_PROLOG(2000);						      \
									      \
	/*								      \
	 * If there is a single step or branch-taken exception in an	      \
	 * exception entry sequence, it was probably meant to apply to	      \
	 * the code where the exception occurred (since exception entry	      \
	 * doesn't turn off DE automatically).  We simulate the effect	      \
	 * of turning off DE on entry to an exception handler by turning      \
	 * off DE in the DSRR1 value and clearing the debug status.	      \
	 */								      \
	mfspr	r10,SPRN_DBSR;		/* check single-step/branch taken */  \
	andis.	r10,r10,(DBSR_IC|DBSR_BT)@h;				      \
	beq+	2f;							      \
									      \
	lis	r10,interrupt_base@h;	/* check if exception in vectors */   \
	ori	r10,r10,interrupt_base@l;				      \
	cmplw	r12,r10;						      \
	blt+	2f;			/* addr below exception vectors */    \
									      \
	lis	r10,interrupt_end@h;					      \
	ori	r10,r10,interrupt_end@l;				      \
	cmplw	r12,r10;						      \
	bgt+	2f;			/* addr above exception vectors */    \
									      \
	/* here it looks like we got an inappropriate debug exception. */     \
1:	rlwinm	r9,r9,0,~MSR_DE;	/* clear DE in the CDRR1 value */     \
	lis	r10,(DBSR_IC|DBSR_BT)@h;	/* clear the IC event */      \
	mtspr	SPRN_DBSR,r10;						      \
	/* restore state and get out */					      \
	lwz	r10,_CCR(r11);						      \
	lwz	r0,GPR0(r11);						      \
	lwz	r1,GPR1(r11);						      \
	mtcrf	0x80,r10;						      \
	mtspr	SPRN_DSRR0,r12;						      \
	mtspr	SPRN_DSRR1,r9;						      \
	lwz	r9,GPR9(r11);						      \
	lwz	r12,GPR12(r11);						      \
	mtspr	SPRN_SPRG_WSCRATCH_DBG,r8;				      \
	BOOKE_LOAD_EXC_LEVEL_STACK(DBG); /* r8 points to the debug stack */ \
	lwz	r10,GPR10(r8);						      \
	lwz	r11,GPR11(r8);						      \
	mfspr	r8,SPRN_SPRG_RSCRATCH_DBG;				      \
									      \
	PPC_RFDI;							      \
	b	.;							      \
									      \
	/* continue normal handling for a debug exception... */		      \
2:	mfspr	r4,SPRN_DBSR;						      \
	stw	r4,_ESR(r11);		/* DebugException takes DBSR in _ESR */\
	SAVE_xSRR(CSRR);						      \
	SAVE_MMU_REGS;							      \
	SAVE_xSRR(SRR);							      \
	prepare_transfer_to_handler;				      \
	bl	DebugException;						      \
	b	ret_from_debug_exc

#define DEBUG_CRIT_EXCEPTION						      \
	START_EXCEPTION(DebugCrit);					      \
	CRITICAL_EXCEPTION_PROLOG(2000,DEBUG);				      \
									      \
	/*								      \
	 * If there is a single step or branch-taken exception in an	      \
	 * exception entry sequence, it was probably meant to apply to	      \
	 * the code where the exception occurred (since exception entry	      \
	 * doesn't turn off DE automatically).  We simulate the effect	      \
	 * of turning off DE on entry to an exception handler by turning      \
	 * off DE in the CSRR1 value and clearing the debug status.	      \
	 */								      \
	mfspr	r10,SPRN_DBSR;		/* check single-step/branch taken */  \
	andis.	r10,r10,(DBSR_IC|DBSR_BT)@h;				      \
	beq+	2f;							      \
									      \
	lis	r10,interrupt_base@h;	/* check if exception in vectors */   \
	ori	r10,r10,interrupt_base@l;				      \
	cmplw	r12,r10;						      \
	blt+	2f;			/* addr below exception vectors */    \
									      \
	lis	r10,interrupt_end@h;					      \
	ori	r10,r10,interrupt_end@l;				      \
	cmplw	r12,r10;						      \
	bgt+	2f;			/* addr above exception vectors */    \
									      \
	/* here it looks like we got an inappropriate debug exception. */     \
1:	rlwinm	r9,r9,0,~MSR_DE;	/* clear DE in the CSRR1 value */     \
	lis	r10,(DBSR_IC|DBSR_BT)@h;	/* clear the IC event */      \
	mtspr	SPRN_DBSR,r10;						      \
	/* restore state and get out */					      \
	lwz	r10,_CCR(r11);						      \
	lwz	r0,GPR0(r11);						      \
	lwz	r1,GPR1(r11);						      \
	mtcrf	0x80,r10;						      \
	mtspr	SPRN_CSRR0,r12;						      \
	mtspr	SPRN_CSRR1,r9;						      \
	lwz	r9,GPR9(r11);						      \
	lwz	r12,GPR12(r11);						      \
	mtspr	SPRN_SPRG_WSCRATCH_CRIT,r8;				      \
	BOOKE_LOAD_EXC_LEVEL_STACK(CRIT); /* r8 points to the debug stack */  \
	lwz	r10,GPR10(r8);						      \
	lwz	r11,GPR11(r8);						      \
	mfspr	r8,SPRN_SPRG_RSCRATCH_CRIT;				      \
									      \
	rfci;								      \
	b	.;							      \
									      \
	/* continue normal handling for a critical exception... */	      \
2:	mfspr	r4,SPRN_DBSR;						      \
	stw	r4,_ESR(r11);		/* DebugException takes DBSR in _ESR */\
	SAVE_MMU_REGS;							      \
	SAVE_xSRR(SRR);							      \
	prepare_transfer_to_handler;					      \
	bl	DebugException;						      \
	b	ret_from_crit_exc

#define DATA_STORAGE_EXCEPTION						      \
	START_EXCEPTION(DataStorage)					      \
	NORMAL_EXCEPTION_PROLOG(0x300, DATA_STORAGE);		      \
	mfspr	r5,SPRN_ESR;		/* Grab the ESR and save it */	      \
	stw	r5,_ESR(r11);						      \
	mfspr	r4,SPRN_DEAR;		/* Grab the DEAR */		      \
	stw	r4, _DEAR(r11);						      \
	prepare_transfer_to_handler;					      \
	bl	do_page_fault;						      \
	b	interrupt_return

/*
 * Instruction TLB Error interrupt handlers may call InstructionStorage
 * directly without clearing ESR, so the ESR at this point may be left over
 * from a prior interrupt.
 *
 * In any case, do_page_fault for BOOK3E does not use ESR and always expects
 * dsisr to be 0. ESR_DST from a prior store in particular would confuse fault
 * handling.
 */
#define INSTRUCTION_STORAGE_EXCEPTION					      \
	START_EXCEPTION(InstructionStorage)				      \
	NORMAL_EXCEPTION_PROLOG(0x400, INST_STORAGE);			      \
	li	r5,0;			/* Store 0 in regs->esr (dsisr) */    \
	stw	r5,_ESR(r11);						      \
	stw	r12, _DEAR(r11);	/* Set regs->dear (dar) to SRR0 */    \
	prepare_transfer_to_handler;					      \
	bl	do_page_fault;						      \
	b	interrupt_return

#define ALIGNMENT_EXCEPTION						      \
	START_EXCEPTION(Alignment)					      \
	NORMAL_EXCEPTION_PROLOG(0x600, ALIGNMENT);		      \
	mfspr   r4,SPRN_DEAR;           /* Grab the DEAR and save it */	      \
	stw     r4,_DEAR(r11);						      \
	prepare_transfer_to_handler;					      \
	bl	alignment_exception;					      \
	REST_NVGPRS(r1);						      \
	b	interrupt_return

#define PROGRAM_EXCEPTION						      \
	START_EXCEPTION(Program)					      \
	NORMAL_EXCEPTION_PROLOG(0x700, PROGRAM);		      \
	mfspr	r4,SPRN_ESR;		/* Grab the ESR and save it */	      \
	stw	r4,_ESR(r11);						      \
	prepare_transfer_to_handler;					      \
	bl	program_check_exception;				      \
	REST_NVGPRS(r1);						      \
	b	interrupt_return

#define DECREMENTER_EXCEPTION						      \
	START_EXCEPTION(Decrementer)					      \
	NORMAL_EXCEPTION_PROLOG(0x900, DECREMENTER);		      \
	lis     r0,TSR_DIS@h;           /* Setup the DEC interrupt mask */    \
	mtspr   SPRN_TSR,r0;		/* Clear the DEC interrupt */	      \
	prepare_transfer_to_handler;					      \
	bl	timer_interrupt;					      \
	b	interrupt_return

#define FP_UNAVAILABLE_EXCEPTION					      \
	START_EXCEPTION(FloatingPointUnavailable)			      \
	NORMAL_EXCEPTION_PROLOG(0x800, FP_UNAVAIL);		      \
	beq	1f;							      \
	bl	load_up_fpu;		/* if from user, just load it up */   \
	b	fast_exception_return;					      \
1:	prepare_transfer_to_handler;					      \
	bl	kernel_fp_unavailable_exception;			      \
	b	interrupt_return

#endif /* __ASSEMBLY__ */
#endif /* __HEAD_BOOKE_H__ */

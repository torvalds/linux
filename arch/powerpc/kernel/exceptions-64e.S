/*
 *  Boot code and exception vectors for Book3E processors
 *
 *  Copyright (C) 2007 Ben. Herrenschmidt (benh@kernel.crashing.org), IBM Corp.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/threads.h>
#include <asm/reg.h>
#include <asm/page.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/cputable.h>
#include <asm/setup.h>
#include <asm/thread_info.h>
#include <asm/reg_a2.h>
#include <asm/exception-64e.h>
#include <asm/bug.h>
#include <asm/irqflags.h>
#include <asm/ptrace.h>
#include <asm/ppc-opcode.h>
#include <asm/mmu.h>
#include <asm/hw_irq.h>

/* XXX This will ultimately add space for a special exception save
 *     structure used to save things like SRR0/SRR1, SPRGs, MAS, etc...
 *     when taking special interrupts. For now we don't support that,
 *     special interrupts from within a non-standard level will probably
 *     blow you up
 */
#define	SPECIAL_EXC_FRAME_SIZE	INT_FRAME_SIZE

/* Exception prolog code for all exceptions */
#define EXCEPTION_PROLOG(n, type, addition)				    \
	mtspr	SPRN_SPRG_##type##_SCRATCH,r13;	/* get spare registers */   \
	mfspr	r13,SPRN_SPRG_PACA;	/* get PACA */			    \
	std	r10,PACA_EX##type+EX_R10(r13);				    \
	std	r11,PACA_EX##type+EX_R11(r13);				    \
	mfcr	r10;			/* save CR */			    \
	addition;			/* additional code for that exc. */ \
	std	r1,PACA_EX##type+EX_R1(r13); /* save old r1 in the PACA */  \
	stw	r10,PACA_EX##type+EX_CR(r13); /* save old CR in the PACA */ \
	mfspr	r11,SPRN_##type##_SRR1;/* what are we coming from */	    \
	type##_SET_KSTACK;		/* get special stack if necessary */\
	andi.	r10,r11,MSR_PR;		/* save stack pointer */	    \
	beq	1f;			/* branch around if supervisor */   \
	ld	r1,PACAKSAVE(r13);	/* get kernel stack coming from usr */\
1:	cmpdi	cr1,r1,0;		/* check if SP makes sense */	    \
	bge-	cr1,exc_##n##_bad_stack;/* bad stack (TODO: out of line) */ \
	mfspr	r10,SPRN_##type##_SRR0;	/* read SRR0 before touching stack */

/* Exception type-specific macros */
#define	GEN_SET_KSTACK							    \
	subi	r1,r1,INT_FRAME_SIZE;	/* alloc frame on kernel stack */
#define SPRN_GEN_SRR0	SPRN_SRR0
#define SPRN_GEN_SRR1	SPRN_SRR1

#define CRIT_SET_KSTACK						            \
	ld	r1,PACA_CRIT_STACK(r13);				    \
	subi	r1,r1,SPECIAL_EXC_FRAME_SIZE;
#define SPRN_CRIT_SRR0	SPRN_CSRR0
#define SPRN_CRIT_SRR1	SPRN_CSRR1

#define DBG_SET_KSTACK						            \
	ld	r1,PACA_DBG_STACK(r13);					    \
	subi	r1,r1,SPECIAL_EXC_FRAME_SIZE;
#define SPRN_DBG_SRR0	SPRN_DSRR0
#define SPRN_DBG_SRR1	SPRN_DSRR1

#define MC_SET_KSTACK						            \
	ld	r1,PACA_MC_STACK(r13);					    \
	subi	r1,r1,SPECIAL_EXC_FRAME_SIZE;
#define SPRN_MC_SRR0	SPRN_MCSRR0
#define SPRN_MC_SRR1	SPRN_MCSRR1

#define NORMAL_EXCEPTION_PROLOG(n, addition)				    \
	EXCEPTION_PROLOG(n, GEN, addition##_GEN(n))

#define CRIT_EXCEPTION_PROLOG(n, addition)				    \
	EXCEPTION_PROLOG(n, CRIT, addition##_CRIT(n))

#define DBG_EXCEPTION_PROLOG(n, addition)				    \
	EXCEPTION_PROLOG(n, DBG, addition##_DBG(n))

#define MC_EXCEPTION_PROLOG(n, addition)				    \
	EXCEPTION_PROLOG(n, MC, addition##_MC(n))


/* Variants of the "addition" argument for the prolog
 */
#define PROLOG_ADDITION_NONE_GEN(n)
#define PROLOG_ADDITION_NONE_CRIT(n)
#define PROLOG_ADDITION_NONE_DBG(n)
#define PROLOG_ADDITION_NONE_MC(n)

#define PROLOG_ADDITION_MASKABLE_GEN(n)					    \
	lbz	r11,PACASOFTIRQEN(r13); /* are irqs soft-disabled ? */	    \
	cmpwi	cr0,r11,0;		/* yes -> go out of line */	    \
	beq	masked_interrupt_book3e_##n

#define PROLOG_ADDITION_2REGS_GEN(n)					    \
	std	r14,PACA_EXGEN+EX_R14(r13);				    \
	std	r15,PACA_EXGEN+EX_R15(r13)

#define PROLOG_ADDITION_1REG_GEN(n)					    \
	std	r14,PACA_EXGEN+EX_R14(r13);

#define PROLOG_ADDITION_2REGS_CRIT(n)					    \
	std	r14,PACA_EXCRIT+EX_R14(r13);				    \
	std	r15,PACA_EXCRIT+EX_R15(r13)

#define PROLOG_ADDITION_2REGS_DBG(n)					    \
	std	r14,PACA_EXDBG+EX_R14(r13);				    \
	std	r15,PACA_EXDBG+EX_R15(r13)

#define PROLOG_ADDITION_2REGS_MC(n)					    \
	std	r14,PACA_EXMC+EX_R14(r13);				    \
	std	r15,PACA_EXMC+EX_R15(r13)


/* Core exception code for all exceptions except TLB misses.
 * XXX: Needs to make SPRN_SPRG_GEN depend on exception type
 */
#define EXCEPTION_COMMON(n, excf, ints)					    \
exc_##n##_common:							    \
	std	r0,GPR0(r1);		/* save r0 in stackframe */	    \
	std	r2,GPR2(r1);		/* save r2 in stackframe */	    \
	SAVE_4GPRS(3, r1);		/* save r3 - r6 in stackframe */    \
	SAVE_2GPRS(7, r1);		/* save r7, r8 in stackframe */	    \
	std	r9,GPR9(r1);		/* save r9 in stackframe */	    \
	std	r10,_NIP(r1);		/* save SRR0 to stackframe */	    \
	std	r11,_MSR(r1);		/* save SRR1 to stackframe */	    \
	ACCOUNT_CPU_USER_ENTRY(r10,r11);/* accounting (uses cr0+eq) */	    \
	ld	r3,excf+EX_R10(r13);	/* get back r10 */		    \
	ld	r4,excf+EX_R11(r13);	/* get back r11 */		    \
	mfspr	r5,SPRN_SPRG_GEN_SCRATCH;/* get back r13 */		    \
	std	r12,GPR12(r1);		/* save r12 in stackframe */	    \
	ld	r2,PACATOC(r13);	/* get kernel TOC into r2 */	    \
	mflr	r6;			/* save LR in stackframe */	    \
	mfctr	r7;			/* save CTR in stackframe */	    \
	mfspr	r8,SPRN_XER;		/* save XER in stackframe */	    \
	ld	r9,excf+EX_R1(r13);	/* load orig r1 back from PACA */   \
	lwz	r10,excf+EX_CR(r13);	/* load orig CR back from PACA	*/  \
	lbz	r11,PACASOFTIRQEN(r13);	/* get current IRQ softe */	    \
	ld	r12,exception_marker@toc(r2);				    \
	li	r0,0;							    \
	std	r3,GPR10(r1);		/* save r10 to stackframe */	    \
	std	r4,GPR11(r1);		/* save r11 to stackframe */	    \
	std	r5,GPR13(r1);		/* save it to stackframe */	    \
	std	r6,_LINK(r1);						    \
	std	r7,_CTR(r1);						    \
	std	r8,_XER(r1);						    \
	li	r3,(n)+1;		/* indicate partial regs in trap */ \
	std	r9,0(r1);		/* store stack frame back link */   \
	std	r10,_CCR(r1);		/* store orig CR in stackframe */   \
	std	r9,GPR1(r1);		/* store stack frame back link */   \
	std	r11,SOFTE(r1);		/* and save it to stackframe */     \
	std	r12,STACK_FRAME_OVERHEAD-16(r1); /* mark the frame */	    \
	std	r3,_TRAP(r1);		/* set trap number		*/  \
	std	r0,RESULT(r1);		/* clear regs->result */	    \
	ints;

/* Variants for the "ints" argument. This one does nothing when we want
 * to keep interrupts in their original state
 */
#define INTS_KEEP

/* This second version is meant for exceptions that don't immediately
 * hard-enable. We set a bit in paca->irq_happened to ensure that
 * a subsequent call to arch_local_irq_restore() will properly
 * hard-enable and avoid the fast-path
 */
#define INTS_DISABLE	SOFT_DISABLE_INTS(r3,r4)

/* This is called by exceptions that used INTS_KEEP (that did not touch
 * irq indicators in the PACA). This will restore MSR:EE to it's previous
 * value
 *
 * XXX In the long run, we may want to open-code it in order to separate the
 *     load from the wrtee, thus limiting the latency caused by the dependency
 *     but at this point, I'll favor code clarity until we have a near to final
 *     implementation
 */
#define INTS_RESTORE_HARD						    \
	ld	r11,_MSR(r1);						    \
	wrtee	r11;

/* XXX FIXME: Restore r14/r15 when necessary */
#define BAD_STACK_TRAMPOLINE(n)						    \
exc_##n##_bad_stack:							    \
	li	r1,(n);			/* get exception number */	    \
	sth	r1,PACA_TRAP_SAVE(r13);	/* store trap */		    \
	b	bad_stack_book3e;	/* bad stack error */

/* WARNING: If you change the layout of this stub, make sure you chcek
	*   the debug exception handler which handles single stepping
	*   into exceptions from userspace, and the MM code in
	*   arch/powerpc/mm/tlb_nohash.c which patches the branch here
	*   and would need to be updated if that branch is moved
	*/
#define	EXCEPTION_STUB(loc, label)					\
	. = interrupt_base_book3e + loc;				\
	nop;	/* To make debug interrupts happy */			\
	b	exc_##label##_book3e;

#define ACK_NONE(r)
#define ACK_DEC(r)							\
	lis	r,TSR_DIS@h;						\
	mtspr	SPRN_TSR,r
#define ACK_FIT(r)							\
	lis	r,TSR_FIS@h;						\
	mtspr	SPRN_TSR,r

/* Used by asynchronous interrupt that may happen in the idle loop.
 *
 * This check if the thread was in the idle loop, and if yes, returns
 * to the caller rather than the PC. This is to avoid a race if
 * interrupts happen before the wait instruction.
 */
#define CHECK_NAPPING()							\
	clrrdi	r11,r1,THREAD_SHIFT;					\
	ld	r10,TI_LOCAL_FLAGS(r11);				\
	andi.	r9,r10,_TLF_NAPPING;					\
	beq+	1f;							\
	ld	r8,_LINK(r1);						\
	rlwinm	r7,r10,0,~_TLF_NAPPING;					\
	std	r8,_NIP(r1);						\
	std	r7,TI_LOCAL_FLAGS(r11);					\
1:


#define MASKABLE_EXCEPTION(trapnum, label, hdlr, ack)			\
	START_EXCEPTION(label);						\
	NORMAL_EXCEPTION_PROLOG(trapnum, PROLOG_ADDITION_MASKABLE)	\
	EXCEPTION_COMMON(trapnum, PACA_EXGEN, INTS_DISABLE)		\
	ack(r8);							\
	CHECK_NAPPING();						\
	addi	r3,r1,STACK_FRAME_OVERHEAD;				\
	bl	hdlr;							\
	b	.ret_from_except_lite;

/* This value is used to mark exception frames on the stack. */
	.section	".toc","aw"
exception_marker:
	.tc	ID_EXC_MARKER[TC],STACK_FRAME_REGS_MARKER


/*
 * And here we have the exception vectors !
 */

	.text
	.balign	0x1000
	.globl interrupt_base_book3e
interrupt_base_book3e:					/* fake trap */
	EXCEPTION_STUB(0x000, machine_check)		/* 0x0200 */
	EXCEPTION_STUB(0x020, critical_input)		/* 0x0580 */
	EXCEPTION_STUB(0x040, debug_crit)		/* 0x0d00 */
	EXCEPTION_STUB(0x060, data_storage)		/* 0x0300 */
	EXCEPTION_STUB(0x080, instruction_storage)	/* 0x0400 */
	EXCEPTION_STUB(0x0a0, external_input)		/* 0x0500 */
	EXCEPTION_STUB(0x0c0, alignment)		/* 0x0600 */
	EXCEPTION_STUB(0x0e0, program)			/* 0x0700 */
	EXCEPTION_STUB(0x100, fp_unavailable)		/* 0x0800 */
	EXCEPTION_STUB(0x120, system_call)		/* 0x0c00 */
	EXCEPTION_STUB(0x140, ap_unavailable)		/* 0x0f20 */
	EXCEPTION_STUB(0x160, decrementer)		/* 0x0900 */
	EXCEPTION_STUB(0x180, fixed_interval)		/* 0x0980 */
	EXCEPTION_STUB(0x1a0, watchdog)			/* 0x09f0 */
	EXCEPTION_STUB(0x1c0, data_tlb_miss)
	EXCEPTION_STUB(0x1e0, instruction_tlb_miss)
	EXCEPTION_STUB(0x260, perfmon)
	EXCEPTION_STUB(0x280, doorbell)
	EXCEPTION_STUB(0x2a0, doorbell_crit)
	EXCEPTION_STUB(0x2c0, guest_doorbell)
	EXCEPTION_STUB(0x2e0, guest_doorbell_crit)
	EXCEPTION_STUB(0x300, hypercall)
	EXCEPTION_STUB(0x320, ehpriv)

	.globl interrupt_end_book3e
interrupt_end_book3e:

/* Critical Input Interrupt */
	START_EXCEPTION(critical_input);
	CRIT_EXCEPTION_PROLOG(0x100, PROLOG_ADDITION_NONE)
//	EXCEPTION_COMMON(0x100, PACA_EXCRIT, INTS_DISABLE)
//	bl	special_reg_save_crit
//	CHECK_NAPPING();
//	addi	r3,r1,STACK_FRAME_OVERHEAD
//	bl	.critical_exception
//	b	ret_from_crit_except
	b	.

/* Machine Check Interrupt */
	START_EXCEPTION(machine_check);
	CRIT_EXCEPTION_PROLOG(0x200, PROLOG_ADDITION_NONE)
//	EXCEPTION_COMMON(0x200, PACA_EXMC, INTS_DISABLE)
//	bl	special_reg_save_mc
//	addi	r3,r1,STACK_FRAME_OVERHEAD
//	CHECK_NAPPING();
//	bl	.machine_check_exception
//	b	ret_from_mc_except
	b	.

/* Data Storage Interrupt */
	START_EXCEPTION(data_storage)
	NORMAL_EXCEPTION_PROLOG(0x300, PROLOG_ADDITION_2REGS)
	mfspr	r14,SPRN_DEAR
	mfspr	r15,SPRN_ESR
	EXCEPTION_COMMON(0x300, PACA_EXGEN, INTS_DISABLE)
	b	storage_fault_common

/* Instruction Storage Interrupt */
	START_EXCEPTION(instruction_storage);
	NORMAL_EXCEPTION_PROLOG(0x400, PROLOG_ADDITION_2REGS)
	li	r15,0
	mr	r14,r10
	EXCEPTION_COMMON(0x400, PACA_EXGEN, INTS_DISABLE)
	b	storage_fault_common

/* External Input Interrupt */
	MASKABLE_EXCEPTION(0x500, external_input, .do_IRQ, ACK_NONE)

/* Alignment */
	START_EXCEPTION(alignment);
	NORMAL_EXCEPTION_PROLOG(0x600, PROLOG_ADDITION_2REGS)
	mfspr	r14,SPRN_DEAR
	mfspr	r15,SPRN_ESR
	EXCEPTION_COMMON(0x600, PACA_EXGEN, INTS_KEEP)
	b	alignment_more	/* no room, go out of line */

/* Program Interrupt */
	START_EXCEPTION(program);
	NORMAL_EXCEPTION_PROLOG(0x700, PROLOG_ADDITION_1REG)
	mfspr	r14,SPRN_ESR
	EXCEPTION_COMMON(0x700, PACA_EXGEN, INTS_DISABLE)
	std	r14,_DSISR(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	ld	r14,PACA_EXGEN+EX_R14(r13)
	bl	.save_nvgprs
	bl	.program_check_exception
	b	.ret_from_except

/* Floating Point Unavailable Interrupt */
	START_EXCEPTION(fp_unavailable);
	NORMAL_EXCEPTION_PROLOG(0x800, PROLOG_ADDITION_NONE)
	/* we can probably do a shorter exception entry for that one... */
	EXCEPTION_COMMON(0x800, PACA_EXGEN, INTS_KEEP)
	ld	r12,_MSR(r1)
	andi.	r0,r12,MSR_PR;
	beq-	1f
	bl	.load_up_fpu
	b	fast_exception_return
1:	INTS_DISABLE
	bl	.save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.kernel_fp_unavailable_exception
	b	.ret_from_except

/* Decrementer Interrupt */
	MASKABLE_EXCEPTION(0x900, decrementer, .timer_interrupt, ACK_DEC)

/* Fixed Interval Timer Interrupt */
	MASKABLE_EXCEPTION(0x980, fixed_interval, .unknown_exception, ACK_FIT)

/* Watchdog Timer Interrupt */
	START_EXCEPTION(watchdog);
	CRIT_EXCEPTION_PROLOG(0x9f0, PROLOG_ADDITION_NONE)
//	EXCEPTION_COMMON(0x9f0, PACA_EXCRIT, INTS_DISABLE)
//	bl	special_reg_save_crit
//	CHECK_NAPPING();
//	addi	r3,r1,STACK_FRAME_OVERHEAD
//	bl	.unknown_exception
//	b	ret_from_crit_except
	b	.

/* System Call Interrupt */
	START_EXCEPTION(system_call)
	mr	r9,r13			/* keep a copy of userland r13 */
	mfspr	r11,SPRN_SRR0		/* get return address */
	mfspr	r12,SPRN_SRR1		/* get previous MSR */
	mfspr	r13,SPRN_SPRG_PACA	/* get our PACA */
	b	system_call_common

/* Auxiliary Processor Unavailable Interrupt */
	START_EXCEPTION(ap_unavailable);
	NORMAL_EXCEPTION_PROLOG(0xf20, PROLOG_ADDITION_NONE)
	EXCEPTION_COMMON(0xf20, PACA_EXGEN, INTS_DISABLE)
	bl	.save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.unknown_exception
	b	.ret_from_except

/* Debug exception as a critical interrupt*/
	START_EXCEPTION(debug_crit);
	CRIT_EXCEPTION_PROLOG(0xd00, PROLOG_ADDITION_2REGS)

	/*
	 * If there is a single step or branch-taken exception in an
	 * exception entry sequence, it was probably meant to apply to
	 * the code where the exception occurred (since exception entry
	 * doesn't turn off DE automatically).  We simulate the effect
	 * of turning off DE on entry to an exception handler by turning
	 * off DE in the CSRR1 value and clearing the debug status.
	 */

	mfspr	r14,SPRN_DBSR		/* check single-step/branch taken */
	andis.	r15,r14,DBSR_IC@h
	beq+	1f

	LOAD_REG_IMMEDIATE(r14,interrupt_base_book3e)
	LOAD_REG_IMMEDIATE(r15,interrupt_end_book3e)
	cmpld	cr0,r10,r14
	cmpld	cr1,r10,r15
	blt+	cr0,1f
	bge+	cr1,1f

	/* here it looks like we got an inappropriate debug exception. */
	lis	r14,DBSR_IC@h		/* clear the IC event */
	rlwinm	r11,r11,0,~MSR_DE	/* clear DE in the CSRR1 value */
	mtspr	SPRN_DBSR,r14
	mtspr	SPRN_CSRR1,r11
	lwz	r10,PACA_EXCRIT+EX_CR(r13)	/* restore registers */
	ld	r1,PACA_EXCRIT+EX_R1(r13)
	ld	r14,PACA_EXCRIT+EX_R14(r13)
	ld	r15,PACA_EXCRIT+EX_R15(r13)
	mtcr	r10
	ld	r10,PACA_EXCRIT+EX_R10(r13)	/* restore registers */
	ld	r11,PACA_EXCRIT+EX_R11(r13)
	mfspr	r13,SPRN_SPRG_CRIT_SCRATCH
	rfci

	/* Normal debug exception */
	/* XXX We only handle coming from userspace for now since we can't
	 *     quite save properly an interrupted kernel state yet
	 */
1:	andi.	r14,r11,MSR_PR;		/* check for userspace again */
	beq	kernel_dbg_exc;		/* if from kernel mode */

	/* Now we mash up things to make it look like we are coming on a
	 * normal exception
	 */
	mfspr	r15,SPRN_SPRG_CRIT_SCRATCH
	mtspr	SPRN_SPRG_GEN_SCRATCH,r15
	mfspr	r14,SPRN_DBSR
	EXCEPTION_COMMON(0xd00, PACA_EXCRIT, INTS_DISABLE)
	std	r14,_DSISR(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	mr	r4,r14
	ld	r14,PACA_EXCRIT+EX_R14(r13)
	ld	r15,PACA_EXCRIT+EX_R15(r13)
	bl	.save_nvgprs
	bl	.DebugException
	b	.ret_from_except

kernel_dbg_exc:
	b	.	/* NYI */

/* Debug exception as a debug interrupt*/
	START_EXCEPTION(debug_debug);
	DBG_EXCEPTION_PROLOG(0xd08, PROLOG_ADDITION_2REGS)

	/*
	 * If there is a single step or branch-taken exception in an
	 * exception entry sequence, it was probably meant to apply to
	 * the code where the exception occurred (since exception entry
	 * doesn't turn off DE automatically).  We simulate the effect
	 * of turning off DE on entry to an exception handler by turning
	 * off DE in the DSRR1 value and clearing the debug status.
	 */

	mfspr	r14,SPRN_DBSR		/* check single-step/branch taken */
	andis.	r15,r14,DBSR_IC@h
	beq+	1f

	LOAD_REG_IMMEDIATE(r14,interrupt_base_book3e)
	LOAD_REG_IMMEDIATE(r15,interrupt_end_book3e)
	cmpld	cr0,r10,r14
	cmpld	cr1,r10,r15
	blt+	cr0,1f
	bge+	cr1,1f

	/* here it looks like we got an inappropriate debug exception. */
	lis	r14,DBSR_IC@h		/* clear the IC event */
	rlwinm	r11,r11,0,~MSR_DE	/* clear DE in the DSRR1 value */
	mtspr	SPRN_DBSR,r14
	mtspr	SPRN_DSRR1,r11
	lwz	r10,PACA_EXDBG+EX_CR(r13)	/* restore registers */
	ld	r1,PACA_EXDBG+EX_R1(r13)
	ld	r14,PACA_EXDBG+EX_R14(r13)
	ld	r15,PACA_EXDBG+EX_R15(r13)
	mtcr	r10
	ld	r10,PACA_EXDBG+EX_R10(r13)	/* restore registers */
	ld	r11,PACA_EXDBG+EX_R11(r13)
	mfspr	r13,SPRN_SPRG_DBG_SCRATCH
	rfdi

	/* Normal debug exception */
	/* XXX We only handle coming from userspace for now since we can't
	 *     quite save properly an interrupted kernel state yet
	 */
1:	andi.	r14,r11,MSR_PR;		/* check for userspace again */
	beq	kernel_dbg_exc;		/* if from kernel mode */

	/* Now we mash up things to make it look like we are coming on a
	 * normal exception
	 */
	mfspr	r15,SPRN_SPRG_DBG_SCRATCH
	mtspr	SPRN_SPRG_GEN_SCRATCH,r15
	mfspr	r14,SPRN_DBSR
	EXCEPTION_COMMON(0xd08, PACA_EXDBG, INTS_DISABLE)
	std	r14,_DSISR(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	mr	r4,r14
	ld	r14,PACA_EXDBG+EX_R14(r13)
	ld	r15,PACA_EXDBG+EX_R15(r13)
	bl	.save_nvgprs
	bl	.DebugException
	b	.ret_from_except

	START_EXCEPTION(perfmon);
	NORMAL_EXCEPTION_PROLOG(0x260, PROLOG_ADDITION_NONE)
	EXCEPTION_COMMON(0x260, PACA_EXGEN, INTS_DISABLE)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.performance_monitor_exception
	b	.ret_from_except_lite

/* Doorbell interrupt */
	MASKABLE_EXCEPTION(0x280, doorbell, .doorbell_exception, ACK_NONE)

/* Doorbell critical Interrupt */
	START_EXCEPTION(doorbell_crit);
	CRIT_EXCEPTION_PROLOG(0x2a0, PROLOG_ADDITION_NONE)
//	EXCEPTION_COMMON(0x2a0, PACA_EXCRIT, INTS_DISABLE)
//	bl	special_reg_save_crit
//	CHECK_NAPPING();
//	addi	r3,r1,STACK_FRAME_OVERHEAD
//	bl	.doorbell_critical_exception
//	b	ret_from_crit_except
	b	.

/* Guest Doorbell */
	MASKABLE_EXCEPTION(0x2c0, guest_doorbell, .unknown_exception, ACK_NONE)

/* Guest Doorbell critical Interrupt */
	START_EXCEPTION(guest_doorbell_crit);
	CRIT_EXCEPTION_PROLOG(0x2e0, PROLOG_ADDITION_NONE)
//	EXCEPTION_COMMON(0x2e0, PACA_EXCRIT, INTS_DISABLE)
//	bl	special_reg_save_crit
//	CHECK_NAPPING();
//	addi	r3,r1,STACK_FRAME_OVERHEAD
//	bl	.guest_doorbell_critical_exception
//	b	ret_from_crit_except
	b	.

/* Hypervisor call */
	START_EXCEPTION(hypercall);
	NORMAL_EXCEPTION_PROLOG(0x310, PROLOG_ADDITION_NONE)
	EXCEPTION_COMMON(0x310, PACA_EXGEN, INTS_KEEP)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.save_nvgprs
	INTS_RESTORE_HARD
	bl	.unknown_exception
	b	.ret_from_except

/* Embedded Hypervisor priviledged  */
	START_EXCEPTION(ehpriv);
	NORMAL_EXCEPTION_PROLOG(0x320, PROLOG_ADDITION_NONE)
	EXCEPTION_COMMON(0x320, PACA_EXGEN, INTS_KEEP)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.save_nvgprs
	INTS_RESTORE_HARD
	bl	.unknown_exception
	b	.ret_from_except

/*
 * An interrupt came in while soft-disabled; We mark paca->irq_happened
 * accordingly and if the interrupt is level sensitive, we hard disable
 */

masked_interrupt_book3e_0x500:
	/* XXX When adding support for EPR, use PACA_IRQ_EE_EDGE */
	li	r11,PACA_IRQ_EE
	b	masked_interrupt_book3e_full_mask

masked_interrupt_book3e_0x900:
	ACK_DEC(r11);
	li	r11,PACA_IRQ_DEC
	b	masked_interrupt_book3e_no_mask
masked_interrupt_book3e_0x980:
	ACK_FIT(r11);
	li	r11,PACA_IRQ_DEC
	b	masked_interrupt_book3e_no_mask
masked_interrupt_book3e_0x280:
masked_interrupt_book3e_0x2c0:
	li	r11,PACA_IRQ_DBELL
	b	masked_interrupt_book3e_no_mask

masked_interrupt_book3e_no_mask:
	mtcr	r10
	lbz	r10,PACAIRQHAPPENED(r13)
	or	r10,r10,r11
	stb	r10,PACAIRQHAPPENED(r13)
	b	1f
masked_interrupt_book3e_full_mask:
	mtcr	r10
	lbz	r10,PACAIRQHAPPENED(r13)
	or	r10,r10,r11
	stb	r10,PACAIRQHAPPENED(r13)
	mfspr	r10,SPRN_SRR1
	rldicl	r11,r10,48,1		/* clear MSR_EE */
	rotldi	r10,r11,16
	mtspr	SPRN_SRR1,r10
1:	ld	r10,PACA_EXGEN+EX_R10(r13);
	ld	r11,PACA_EXGEN+EX_R11(r13);
	mfspr	r13,SPRN_SPRG_GEN_SCRATCH;
	rfi
	b	.
/*
 * Called from arch_local_irq_enable when an interrupt needs
 * to be resent. r3 contains either 0x500,0x900,0x260 or 0x280
 * to indicate the kind of interrupt. MSR:EE is already off.
 * We generate a stackframe like if a real interrupt had happened.
 *
 * Note: While MSR:EE is off, we need to make sure that _MSR
 * in the generated frame has EE set to 1 or the exception
 * handler will not properly re-enable them.
 */
_GLOBAL(__replay_interrupt)
	/* We are going to jump to the exception common code which
	 * will retrieve various register values from the PACA which
	 * we don't give a damn about.
	 */
	mflr	r10
	mfmsr	r11
	mfcr	r4
	mtspr	SPRN_SPRG_GEN_SCRATCH,r13;
	std	r1,PACA_EXGEN+EX_R1(r13);
	stw	r4,PACA_EXGEN+EX_CR(r13);
	ori	r11,r11,MSR_EE
	subi	r1,r1,INT_FRAME_SIZE;
	cmpwi	cr0,r3,0x500
	beq	exc_0x500_common
	cmpwi	cr0,r3,0x900
	beq	exc_0x900_common
	cmpwi	cr0,r3,0x280
	beq	exc_0x280_common
	blr


/*
 * This is called from 0x300 and 0x400 handlers after the prologs with
 * r14 and r15 containing the fault address and error code, with the
 * original values stashed away in the PACA
 */
storage_fault_common:
	std	r14,_DAR(r1)
	std	r15,_DSISR(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	mr	r4,r14
	mr	r5,r15
	ld	r14,PACA_EXGEN+EX_R14(r13)
	ld	r15,PACA_EXGEN+EX_R15(r13)
	bl	.do_page_fault
	cmpdi	r3,0
	bne-	1f
	b	.ret_from_except_lite
1:	bl	.save_nvgprs
	mr	r5,r3
	addi	r3,r1,STACK_FRAME_OVERHEAD
	ld	r4,_DAR(r1)
	bl	.bad_page_fault
	b	.ret_from_except

/*
 * Alignment exception doesn't fit entirely in the 0x100 bytes so it
 * continues here.
 */
alignment_more:
	std	r14,_DAR(r1)
	std	r15,_DSISR(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	ld	r14,PACA_EXGEN+EX_R14(r13)
	ld	r15,PACA_EXGEN+EX_R15(r13)
	bl	.save_nvgprs
	INTS_RESTORE_HARD
	bl	.alignment_exception
	b	.ret_from_except

/*
 * We branch here from entry_64.S for the last stage of the exception
 * return code path. MSR:EE is expected to be off at that point
 */
_GLOBAL(exception_return_book3e)
	b	1f

/* This is the return from load_up_fpu fast path which could do with
 * less GPR restores in fact, but for now we have a single return path
 */
	.globl fast_exception_return
fast_exception_return:
	wrteei	0
1:	mr	r0,r13
	ld	r10,_MSR(r1)
	REST_4GPRS(2, r1)
	andi.	r6,r10,MSR_PR
	REST_2GPRS(6, r1)
	beq	1f
	ACCOUNT_CPU_USER_EXIT(r10, r11)
	ld	r0,GPR13(r1)

1:	stdcx.	r0,0,r1		/* to clear the reservation */

	ld	r8,_CCR(r1)
	ld	r9,_LINK(r1)
	ld	r10,_CTR(r1)
	ld	r11,_XER(r1)
	mtcr	r8
	mtlr	r9
	mtctr	r10
	mtxer	r11
	REST_2GPRS(8, r1)
	ld	r10,GPR10(r1)
	ld	r11,GPR11(r1)
	ld	r12,GPR12(r1)
	mtspr	SPRN_SPRG_GEN_SCRATCH,r0

	std	r10,PACA_EXGEN+EX_R10(r13);
	std	r11,PACA_EXGEN+EX_R11(r13);
	ld	r10,_NIP(r1)
	ld	r11,_MSR(r1)
	ld	r0,GPR0(r1)
	ld	r1,GPR1(r1)
	mtspr	SPRN_SRR0,r10
	mtspr	SPRN_SRR1,r11
	ld	r10,PACA_EXGEN+EX_R10(r13)
	ld	r11,PACA_EXGEN+EX_R11(r13)
	mfspr	r13,SPRN_SPRG_GEN_SCRATCH
	rfi

/*
 * Trampolines used when spotting a bad kernel stack pointer in
 * the exception entry code.
 *
 * TODO: move some bits like SRR0 read to trampoline, pass PACA
 * index around, etc... to handle crit & mcheck
 */
BAD_STACK_TRAMPOLINE(0x000)
BAD_STACK_TRAMPOLINE(0x100)
BAD_STACK_TRAMPOLINE(0x200)
BAD_STACK_TRAMPOLINE(0x260)
BAD_STACK_TRAMPOLINE(0x280)
BAD_STACK_TRAMPOLINE(0x2a0)
BAD_STACK_TRAMPOLINE(0x2c0)
BAD_STACK_TRAMPOLINE(0x2e0)
BAD_STACK_TRAMPOLINE(0x300)
BAD_STACK_TRAMPOLINE(0x310)
BAD_STACK_TRAMPOLINE(0x320)
BAD_STACK_TRAMPOLINE(0x400)
BAD_STACK_TRAMPOLINE(0x500)
BAD_STACK_TRAMPOLINE(0x600)
BAD_STACK_TRAMPOLINE(0x700)
BAD_STACK_TRAMPOLINE(0x800)
BAD_STACK_TRAMPOLINE(0x900)
BAD_STACK_TRAMPOLINE(0x980)
BAD_STACK_TRAMPOLINE(0x9f0)
BAD_STACK_TRAMPOLINE(0xa00)
BAD_STACK_TRAMPOLINE(0xb00)
BAD_STACK_TRAMPOLINE(0xc00)
BAD_STACK_TRAMPOLINE(0xd00)
BAD_STACK_TRAMPOLINE(0xd08)
BAD_STACK_TRAMPOLINE(0xe00)
BAD_STACK_TRAMPOLINE(0xf00)
BAD_STACK_TRAMPOLINE(0xf20)

	.globl	bad_stack_book3e
bad_stack_book3e:
	/* XXX: Needs to make SPRN_SPRG_GEN depend on exception type */
	mfspr	r10,SPRN_SRR0;		  /* read SRR0 before touching stack */
	ld	r1,PACAEMERGSP(r13)
	subi	r1,r1,64+INT_FRAME_SIZE
	std	r10,_NIP(r1)
	std	r11,_MSR(r1)
	ld	r10,PACA_EXGEN+EX_R1(r13) /* FIXME for crit & mcheck */
	lwz	r11,PACA_EXGEN+EX_CR(r13) /* FIXME for crit & mcheck */
	std	r10,GPR1(r1)
	std	r11,_CCR(r1)
	mfspr	r10,SPRN_DEAR
	mfspr	r11,SPRN_ESR
	std	r10,_DAR(r1)
	std	r11,_DSISR(r1)
	std	r0,GPR0(r1);		/* save r0 in stackframe */	    \
	std	r2,GPR2(r1);		/* save r2 in stackframe */	    \
	SAVE_4GPRS(3, r1);		/* save r3 - r6 in stackframe */    \
	SAVE_2GPRS(7, r1);		/* save r7, r8 in stackframe */	    \
	std	r9,GPR9(r1);		/* save r9 in stackframe */	    \
	ld	r3,PACA_EXGEN+EX_R10(r13);/* get back r10 */		    \
	ld	r4,PACA_EXGEN+EX_R11(r13);/* get back r11 */		    \
	mfspr	r5,SPRN_SPRG_GEN_SCRATCH;/* get back r13 XXX can be wrong */ \
	std	r3,GPR10(r1);		/* save r10 to stackframe */	    \
	std	r4,GPR11(r1);		/* save r11 to stackframe */	    \
	std	r12,GPR12(r1);		/* save r12 in stackframe */	    \
	std	r5,GPR13(r1);		/* save it to stackframe */	    \
	mflr	r10
	mfctr	r11
	mfxer	r12
	std	r10,_LINK(r1)
	std	r11,_CTR(r1)
	std	r12,_XER(r1)
	SAVE_10GPRS(14,r1)
	SAVE_8GPRS(24,r1)
	lhz	r12,PACA_TRAP_SAVE(r13)
	std	r12,_TRAP(r1)
	addi	r11,r1,INT_FRAME_SIZE
	std	r11,0(r1)
	li	r12,0
	std	r12,0(r11)
	ld	r2,PACATOC(r13)
1:	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.kernel_bad_stack
	b	1b

/*
 * Setup the initial TLB for a core. This current implementation
 * assume that whatever we are running off will not conflict with
 * the new mapping at PAGE_OFFSET.
 */
_GLOBAL(initial_tlb_book3e)

	/* Look for the first TLB with IPROT set */
	mfspr	r4,SPRN_TLB0CFG
	andi.	r3,r4,TLBnCFG_IPROT
	lis	r3,MAS0_TLBSEL(0)@h
	bne	found_iprot

	mfspr	r4,SPRN_TLB1CFG
	andi.	r3,r4,TLBnCFG_IPROT
	lis	r3,MAS0_TLBSEL(1)@h
	bne	found_iprot

	mfspr	r4,SPRN_TLB2CFG
	andi.	r3,r4,TLBnCFG_IPROT
	lis	r3,MAS0_TLBSEL(2)@h
	bne	found_iprot

	lis	r3,MAS0_TLBSEL(3)@h
	mfspr	r4,SPRN_TLB3CFG
	/* fall through */

found_iprot:
	andi.	r5,r4,TLBnCFG_HES
	bne	have_hes

	mflr	r8				/* save LR */
/* 1. Find the index of the entry we're executing in
 *
 * r3 = MAS0_TLBSEL (for the iprot array)
 * r4 = SPRN_TLBnCFG
 */
	bl	invstr				/* Find our address */
invstr:	mflr	r6				/* Make it accessible */
	mfmsr	r7
	rlwinm	r5,r7,27,31,31			/* extract MSR[IS] */
	mfspr	r7,SPRN_PID
	slwi	r7,r7,16
	or	r7,r7,r5
	mtspr	SPRN_MAS6,r7
	tlbsx	0,r6				/* search MSR[IS], SPID=PID */

	mfspr	r3,SPRN_MAS0
	rlwinm	r5,r3,16,20,31			/* Extract MAS0(Entry) */

	mfspr	r7,SPRN_MAS1			/* Insure IPROT set */
	oris	r7,r7,MAS1_IPROT@h
	mtspr	SPRN_MAS1,r7
	tlbwe

/* 2. Invalidate all entries except the entry we're executing in
 *
 * r3 = MAS0 w/TLBSEL & ESEL for the entry we are running in
 * r4 = SPRN_TLBnCFG
 * r5 = ESEL of entry we are running in
 */
	andi.	r4,r4,TLBnCFG_N_ENTRY		/* Extract # entries */
	li	r6,0				/* Set Entry counter to 0 */
1:	mr	r7,r3				/* Set MAS0(TLBSEL) */
	rlwimi	r7,r6,16,4,15			/* Setup MAS0 = TLBSEL | ESEL(r6) */
	mtspr	SPRN_MAS0,r7
	tlbre
	mfspr	r7,SPRN_MAS1
	rlwinm	r7,r7,0,2,31			/* Clear MAS1 Valid and IPROT */
	cmpw	r5,r6
	beq	skpinv				/* Dont update the current execution TLB */
	mtspr	SPRN_MAS1,r7
	tlbwe
	isync
skpinv:	addi	r6,r6,1				/* Increment */
	cmpw	r6,r4				/* Are we done? */
	bne	1b				/* If not, repeat */

	/* Invalidate all TLBs */
	PPC_TLBILX_ALL(R0,R0)
	sync
	isync

/* 3. Setup a temp mapping and jump to it
 *
 * r3 = MAS0 w/TLBSEL & ESEL for the entry we are running in
 * r5 = ESEL of entry we are running in
 */
	andi.	r7,r5,0x1	/* Find an entry not used and is non-zero */
	addi	r7,r7,0x1
	mr	r4,r3		/* Set MAS0(TLBSEL) = 1 */
	mtspr	SPRN_MAS0,r4
	tlbre

	rlwimi	r4,r7,16,4,15	/* Setup MAS0 = TLBSEL | ESEL(r7) */
	mtspr	SPRN_MAS0,r4

	mfspr	r7,SPRN_MAS1
	xori	r6,r7,MAS1_TS		/* Setup TMP mapping in the other Address space */
	mtspr	SPRN_MAS1,r6

	tlbwe

	mfmsr	r6
	xori	r6,r6,MSR_IS
	mtspr	SPRN_SRR1,r6
	bl	1f		/* Find our address */
1:	mflr	r6
	addi	r6,r6,(2f - 1b)
	mtspr	SPRN_SRR0,r6
	rfi
2:

/* 4. Clear out PIDs & Search info
 *
 * r3 = MAS0 w/TLBSEL & ESEL for the entry we started in
 * r4 = MAS0 w/TLBSEL & ESEL for the temp mapping
 * r5 = MAS3
 */
	li	r6,0
	mtspr   SPRN_MAS6,r6
	mtspr	SPRN_PID,r6

/* 5. Invalidate mapping we started in
 *
 * r3 = MAS0 w/TLBSEL & ESEL for the entry we started in
 * r4 = MAS0 w/TLBSEL & ESEL for the temp mapping
 * r5 = MAS3
 */
	mtspr	SPRN_MAS0,r3
	tlbre
	mfspr	r6,SPRN_MAS1
	rlwinm	r6,r6,0,2,0	/* clear IPROT */
	mtspr	SPRN_MAS1,r6
	tlbwe

	/* Invalidate TLB1 */
	PPC_TLBILX_ALL(R0,R0)
	sync
	isync

/* The mapping only needs to be cache-coherent on SMP */
#ifdef CONFIG_SMP
#define M_IF_SMP	MAS2_M
#else
#define M_IF_SMP	0
#endif

/* 6. Setup KERNELBASE mapping in TLB[0]
 *
 * r3 = MAS0 w/TLBSEL & ESEL for the entry we started in
 * r4 = MAS0 w/TLBSEL & ESEL for the temp mapping
 * r5 = MAS3
 */
	rlwinm	r3,r3,0,16,3	/* clear ESEL */
	mtspr	SPRN_MAS0,r3
	lis	r6,(MAS1_VALID|MAS1_IPROT)@h
	ori	r6,r6,(MAS1_TSIZE(BOOK3E_PAGESZ_1GB))@l
	mtspr	SPRN_MAS1,r6

	LOAD_REG_IMMEDIATE(r6, PAGE_OFFSET | M_IF_SMP)
	mtspr	SPRN_MAS2,r6

	rlwinm	r5,r5,0,0,25
	ori	r5,r5,MAS3_SR | MAS3_SW | MAS3_SX
	mtspr	SPRN_MAS3,r5
	li	r5,-1
	rlwinm	r5,r5,0,0,25

	tlbwe

/* 7. Jump to KERNELBASE mapping
 *
 * r4 = MAS0 w/TLBSEL & ESEL for the temp mapping
 */
	/* Now we branch the new virtual address mapped by this entry */
	LOAD_REG_IMMEDIATE(r6,2f)
	lis	r7,MSR_KERNEL@h
	ori	r7,r7,MSR_KERNEL@l
	mtspr	SPRN_SRR0,r6
	mtspr	SPRN_SRR1,r7
	rfi				/* start execution out of TLB1[0] entry */
2:

/* 8. Clear out the temp mapping
 *
 * r4 = MAS0 w/TLBSEL & ESEL for the entry we are running in
 */
	mtspr	SPRN_MAS0,r4
	tlbre
	mfspr	r5,SPRN_MAS1
	rlwinm	r5,r5,0,2,0	/* clear IPROT */
	mtspr	SPRN_MAS1,r5
	tlbwe

	/* Invalidate TLB1 */
	PPC_TLBILX_ALL(R0,R0)
	sync
	isync

	/* We translate LR and return */
	tovirt(r8,r8)
	mtlr	r8
	blr

have_hes:
	/* Setup MAS 0,1,2,3 and 7 for tlbwe of a 1G entry that maps the
	 * kernel linear mapping. We also set MAS8 once for all here though
	 * that will have to be made dependent on whether we are running under
	 * a hypervisor I suppose.
	 */

	/* BEWARE, MAGIC
	 * This code is called as an ordinary function on the boot CPU. But to
	 * avoid duplication, this code is also used in SCOM bringup of
	 * secondary CPUs. We read the code between the initial_tlb_code_start
	 * and initial_tlb_code_end labels one instruction at a time and RAM it
	 * into the new core via SCOM. That doesn't process branches, so there
	 * must be none between those two labels. It also means if this code
	 * ever takes any parameters, the SCOM code must also be updated to
	 * provide them.
	 */
	.globl a2_tlbinit_code_start
a2_tlbinit_code_start:

	ori	r11,r3,MAS0_WQ_ALLWAYS
	oris	r11,r11,MAS0_ESEL(3)@h /* Use way 3: workaround A2 erratum 376 */
	mtspr	SPRN_MAS0,r11
	lis	r3,(MAS1_VALID | MAS1_IPROT)@h
	ori	r3,r3,BOOK3E_PAGESZ_1GB << MAS1_TSIZE_SHIFT
	mtspr	SPRN_MAS1,r3
	LOAD_REG_IMMEDIATE(r3, PAGE_OFFSET | MAS2_M)
	mtspr	SPRN_MAS2,r3
	li	r3,MAS3_SR | MAS3_SW | MAS3_SX
	mtspr	SPRN_MAS7_MAS3,r3
	li	r3,0
	mtspr	SPRN_MAS8,r3

	/* Write the TLB entry */
	tlbwe

	.globl a2_tlbinit_after_linear_map
a2_tlbinit_after_linear_map:

	/* Now we branch the new virtual address mapped by this entry */
	LOAD_REG_IMMEDIATE(r3,1f)
	mtctr	r3
	bctr

1:	/* We are now running at PAGE_OFFSET, clean the TLB of everything
	 * else (including IPROTed things left by firmware)
	 * r4 = TLBnCFG
	 * r3 = current address (more or less)
	 */

	li	r5,0
	mtspr	SPRN_MAS6,r5
	tlbsx	0,r3

	rlwinm	r9,r4,0,TLBnCFG_N_ENTRY
	rlwinm	r10,r4,8,0xff
	addi	r10,r10,-1	/* Get inner loop mask */

	li	r3,1

	mfspr	r5,SPRN_MAS1
	rlwinm	r5,r5,0,(~(MAS1_VALID|MAS1_IPROT))

	mfspr	r6,SPRN_MAS2
	rldicr	r6,r6,0,51		/* Extract EPN */

	mfspr	r7,SPRN_MAS0
	rlwinm	r7,r7,0,0xffff0fff	/* Clear HES and WQ */

	rlwinm	r8,r7,16,0xfff		/* Extract ESEL */

2:	add	r4,r3,r8
	and	r4,r4,r10

	rlwimi	r7,r4,16,MAS0_ESEL_MASK

	mtspr	SPRN_MAS0,r7
	mtspr	SPRN_MAS1,r5
	mtspr	SPRN_MAS2,r6
	tlbwe

	addi	r3,r3,1
	and.	r4,r3,r10

	bne	3f
	addis	r6,r6,(1<<30)@h
3:
	cmpw	r3,r9
	blt	2b

	.globl  a2_tlbinit_after_iprot_flush
a2_tlbinit_after_iprot_flush:

#ifdef CONFIG_PPC_EARLY_DEBUG_WSP
	/* Now establish early debug mappings if applicable */
	/* Restore the MAS0 we used for linear mapping load */
	mtspr	SPRN_MAS0,r11

	lis	r3,(MAS1_VALID | MAS1_IPROT)@h
	ori	r3,r3,(BOOK3E_PAGESZ_4K << MAS1_TSIZE_SHIFT)
	mtspr	SPRN_MAS1,r3
	LOAD_REG_IMMEDIATE(r3, WSP_UART_VIRT | MAS2_I | MAS2_G)
	mtspr	SPRN_MAS2,r3
	LOAD_REG_IMMEDIATE(r3, WSP_UART_PHYS | MAS3_SR | MAS3_SW)
	mtspr	SPRN_MAS7_MAS3,r3
	/* re-use the MAS8 value from the linear mapping */
	tlbwe
#endif /* CONFIG_PPC_EARLY_DEBUG_WSP */

	PPC_TLBILX(0,R0,R0)
	sync
	isync

	.globl a2_tlbinit_code_end
a2_tlbinit_code_end:

	/* We translate LR and return */
	mflr	r3
	tovirt(r3,r3)
	mtlr	r3
	blr

/*
 * Main entry (boot CPU, thread 0)
 *
 * We enter here from head_64.S, possibly after the prom_init trampoline
 * with r3 and r4 already saved to r31 and 30 respectively and in 64 bits
 * mode. Anything else is as it was left by the bootloader
 *
 * Initial requirements of this port:
 *
 * - Kernel loaded at 0 physical
 * - A good lump of memory mapped 0:0 by UTLB entry 0
 * - MSR:IS & MSR:DS set to 0
 *
 * Note that some of the above requirements will be relaxed in the future
 * as the kernel becomes smarter at dealing with different initial conditions
 * but for now you have to be careful
 */
_GLOBAL(start_initialization_book3e)
	mflr	r28

	/* First, we need to setup some initial TLBs to map the kernel
	 * text, data and bss at PAGE_OFFSET. We don't have a real mode
	 * and always use AS 0, so we just set it up to match our link
	 * address and never use 0 based addresses.
	 */
	bl	.initial_tlb_book3e

	/* Init global core bits */
	bl	.init_core_book3e

	/* Init per-thread bits */
	bl	.init_thread_book3e

	/* Return to common init code */
	tovirt(r28,r28)
	mtlr	r28
	blr


/*
 * Secondary core/processor entry
 *
 * This is entered for thread 0 of a secondary core, all other threads
 * are expected to be stopped. It's similar to start_initialization_book3e
 * except that it's generally entered from the holding loop in head_64.S
 * after CPUs have been gathered by Open Firmware.
 *
 * We assume we are in 32 bits mode running with whatever TLB entry was
 * set for us by the firmware or POR engine.
 */
_GLOBAL(book3e_secondary_core_init_tlb_set)
	li	r4,1
	b	.generic_secondary_smp_init

_GLOBAL(book3e_secondary_core_init)
	mflr	r28

	/* Do we need to setup initial TLB entry ? */
	cmplwi	r4,0
	bne	2f

	/* Setup TLB for this core */
	bl	.initial_tlb_book3e

	/* We can return from the above running at a different
	 * address, so recalculate r2 (TOC)
	 */
	bl	.relative_toc

	/* Init global core bits */
2:	bl	.init_core_book3e

	/* Init per-thread bits */
3:	bl	.init_thread_book3e

	/* Return to common init code at proper virtual address.
	 *
	 * Due to various previous assumptions, we know we entered this
	 * function at either the final PAGE_OFFSET mapping or using a
	 * 1:1 mapping at 0, so we don't bother doing a complicated check
	 * here, we just ensure the return address has the right top bits.
	 *
	 * Note that if we ever want to be smarter about where we can be
	 * started from, we have to be careful that by the time we reach
	 * the code below we may already be running at a different location
	 * than the one we were called from since initial_tlb_book3e can
	 * have moved us already.
	 */
	cmpdi	cr0,r28,0
	blt	1f
	lis	r3,PAGE_OFFSET@highest
	sldi	r3,r3,32
	or	r28,r28,r3
1:	mtlr	r28
	blr

_GLOBAL(book3e_secondary_thread_init)
	mflr	r28
	b	3b

_STATIC(init_core_book3e)
	/* Establish the interrupt vector base */
	LOAD_REG_IMMEDIATE(r3, interrupt_base_book3e)
	mtspr	SPRN_IVPR,r3
	sync
	blr

_STATIC(init_thread_book3e)
	lis	r3,(SPRN_EPCR_ICM | SPRN_EPCR_GICM)@h
	mtspr	SPRN_EPCR,r3

	/* Make sure interrupts are off */
	wrteei	0

	/* disable all timers and clear out status */
	li	r3,0
	mtspr	SPRN_TCR,r3
	mfspr	r3,SPRN_TSR
	mtspr	SPRN_TSR,r3

	blr

_GLOBAL(__setup_base_ivors)
	SET_IVOR(0, 0x020) /* Critical Input */
	SET_IVOR(1, 0x000) /* Machine Check */
	SET_IVOR(2, 0x060) /* Data Storage */ 
	SET_IVOR(3, 0x080) /* Instruction Storage */
	SET_IVOR(4, 0x0a0) /* External Input */ 
	SET_IVOR(5, 0x0c0) /* Alignment */ 
	SET_IVOR(6, 0x0e0) /* Program */ 
	SET_IVOR(7, 0x100) /* FP Unavailable */ 
	SET_IVOR(8, 0x120) /* System Call */ 
	SET_IVOR(9, 0x140) /* Auxiliary Processor Unavailable */ 
	SET_IVOR(10, 0x160) /* Decrementer */ 
	SET_IVOR(11, 0x180) /* Fixed Interval Timer */ 
	SET_IVOR(12, 0x1a0) /* Watchdog Timer */ 
	SET_IVOR(13, 0x1c0) /* Data TLB Error */ 
	SET_IVOR(14, 0x1e0) /* Instruction TLB Error */
	SET_IVOR(15, 0x040) /* Debug */

	sync

	blr

_GLOBAL(setup_perfmon_ivor)
	SET_IVOR(35, 0x260) /* Performance Monitor */
	blr

_GLOBAL(setup_doorbell_ivors)
	SET_IVOR(36, 0x280) /* Processor Doorbell */
	SET_IVOR(37, 0x2a0) /* Processor Doorbell Crit */

	/* Check MMUCFG[LPIDSIZE] to determine if we have category E.HV */
	mfspr	r10,SPRN_MMUCFG
	rlwinm.	r10,r10,0,MMUCFG_LPIDSIZE
	beqlr

	SET_IVOR(38, 0x2c0) /* Guest Processor Doorbell */
	SET_IVOR(39, 0x2e0) /* Guest Processor Doorbell Crit/MC */
	blr

_GLOBAL(setup_ehv_ivors)
	/*
	 * We may be running as a guest and lack E.HV even on a chip
	 * that normally has it.
	 */
	mfspr	r10,SPRN_MMUCFG
	rlwinm.	r10,r10,0,MMUCFG_LPIDSIZE
	beqlr

	SET_IVOR(40, 0x300) /* Embedded Hypervisor System Call */
	SET_IVOR(41, 0x320) /* Embedded Hypervisor Privilege */
	blr

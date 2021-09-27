/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *    Copyright (c) 1995-1996 Gary Thomas <gdt@linuxppc.org>
 *      Initial PowerPC version.
 *    Copyright (c) 1996 Cort Dougan <cort@cs.nmt.edu>
 *      Rewritten for PReP
 *    Copyright (c) 1996 Paul Mackerras <paulus@cs.anu.edu.au>
 *      Low-level exception handers, MMU support, and rewrite.
 *    Copyright (c) 1997 Dan Malek <dmalek@jlc.net>
 *      PowerPC 8xx modifications.
 *    Copyright (c) 1998-1999 TiVo, Inc.
 *      PowerPC 403GCX modifications.
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *      PowerPC 403GCX/405GP modifications.
 *    Copyright 2000 MontaVista Software Inc.
 *	PPC405 modifications
 *      PowerPC 403GCX/405GP modifications.
 * 	Author: MontaVista Software, Inc.
 *         	frank_rowand@mvista.com or source@mvista.com
 * 	   	debbie_chu@mvista.com
 *
 *    Module name: head_4xx.S
 *
 *    Description:
 *      Kernel execution entry point code.
 */

#include <linux/init.h>
#include <linux/pgtable.h>
#include <linux/sizes.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ptrace.h>
#include <asm/export.h>

#include "head_32.h"

/* As with the other PowerPC ports, it is expected that when code
 * execution begins here, the following registers contain valid, yet
 * optional, information:
 *
 *   r3 - Board info structure pointer (DRAM, frequency, MAC address, etc.)
 *   r4 - Starting address of the init RAM disk
 *   r5 - Ending address of the init RAM disk
 *   r6 - Start of kernel command line string (e.g. "mem=96m")
 *   r7 - End of kernel command line string
 *
 * This is all going to change RSN when we add bi_recs.......  -- Dan
 */
	__HEAD
_ENTRY(_stext);
_ENTRY(_start);

	mr	r31,r3			/* save device tree ptr */

	/* We have to turn on the MMU right away so we get cache modes
	 * set correctly.
	 */
	bl	initial_mmu

/* We now have the lower 16 Meg mapped into TLB entries, and the caches
 * ready to work.
 */
turn_on_mmu:
	lis	r0,MSR_KERNEL@h
	ori	r0,r0,MSR_KERNEL@l
	mtspr	SPRN_SRR1,r0
	lis	r0,start_here@h
	ori	r0,r0,start_here@l
	mtspr	SPRN_SRR0,r0
	rfi				/* enables MMU */
	b	.			/* prevent prefetch past rfi */

/*
 * This area is used for temporarily saving registers during the
 * critical exception prolog.
 */
	. = 0xc0
crit_save:
_ENTRY(crit_r10)
	.space	4
_ENTRY(crit_r11)
	.space	4
_ENTRY(crit_srr0)
	.space	4
_ENTRY(crit_srr1)
	.space	4
_ENTRY(crit_r1)
	.space	4
_ENTRY(crit_dear)
	.space	4
_ENTRY(crit_esr)
	.space	4

/*
 * Exception prolog for critical exceptions.  This is a little different
 * from the normal exception prolog above since a critical exception
 * can potentially occur at any point during normal exception processing.
 * Thus we cannot use the same SPRG registers as the normal prolog above.
 * Instead we use a couple of words of memory at low physical addresses.
 * This is OK since we don't support SMP on these processors.
 */
.macro CRITICAL_EXCEPTION_PROLOG trapno name
	stw	r10,crit_r10@l(0)	/* save two registers to work with */
	stw	r11,crit_r11@l(0)
	mfspr	r10,SPRN_SRR0
	mfspr	r11,SPRN_SRR1
	stw	r10,crit_srr0@l(0)
	stw	r11,crit_srr1@l(0)
	mfspr	r10,SPRN_DEAR
	mfspr	r11,SPRN_ESR
	stw	r10,crit_dear@l(0)
	stw	r11,crit_esr@l(0)
	mfcr	r10			/* save CR in r10 for now	   */
	mfspr	r11,SPRN_SRR3		/* check whether user or kernel    */
	andi.	r11,r11,MSR_PR
	lis	r11,(critirq_ctx-PAGE_OFFSET)@ha
	lwz	r11,(critirq_ctx-PAGE_OFFSET)@l(r11)
	beq	1f
	/* COMING FROM USER MODE */
	mfspr	r11,SPRN_SPRG_THREAD	/* if from user, start at top of   */
	lwz	r11,TASK_STACK-THREAD(r11) /* this thread's kernel stack */
1:	stw	r1,crit_r1@l(0)
	addi	r1,r11,THREAD_SIZE-INT_FRAME_SIZE /* Alloc an excpt frm  */
	LOAD_REG_IMMEDIATE(r11, MSR_KERNEL & ~(MSR_ME|MSR_DE|MSR_CE)) /* re-enable MMU */
	mtspr	SPRN_SRR1, r11
	lis	r11, 1f@h
	ori	r11, r11, 1f@l
	mtspr	SPRN_SRR0, r11
	rfi

	.text
1:
\name\()_virt:
	lwz	r11,crit_r1@l(0)
	stw	r11,GPR1(r1)
	stw	r11,0(r1)
	mr	r11,r1
	stw	r10,_CCR(r11)		/* save various registers	   */
	stw	r12,GPR12(r11)
	stw	r9,GPR9(r11)
	mflr	r10
	stw	r10,_LINK(r11)
	lis	r9,PAGE_OFFSET@ha
	lwz	r10,crit_r10@l(r9)
	lwz	r12,crit_r11@l(r9)
	stw	r10,GPR10(r11)
	stw	r12,GPR11(r11)
	lwz	r12,crit_dear@l(r9)
	lwz	r9,crit_esr@l(r9)
	stw	r12,_DEAR(r11)		/* since they may have had stuff   */
	stw	r9,_ESR(r11)		/* exception was taken		   */
	mfspr	r12,SPRN_SRR2
	mfspr	r9,SPRN_SRR3
	rlwinm	r9,r9,0,14,12		/* clear MSR_WE (necessary?)	   */
	COMMON_EXCEPTION_PROLOG_END \trapno + 2
_ASM_NOKPROBE_SYMBOL(\name\()_virt)
.endm

	/*
	 * State at this point:
	 * r9 saved in stack frame, now saved SRR3 & ~MSR_WE
	 * r10 saved in crit_r10 and in stack frame, trashed
	 * r11 saved in crit_r11 and in stack frame,
	 *	now phys stack/exception frame pointer
	 * r12 saved in stack frame, now saved SRR2
	 * CR saved in stack frame, CR0.EQ = !SRR3.PR
	 * LR, DEAR, ESR in stack frame
	 * r1 saved in stack frame, now virt stack/excframe pointer
	 * r0, r3-r8 saved in stack frame
	 */

/*
 * Exception vectors.
 */
#define CRITICAL_EXCEPTION(n, label, hdlr)			\
	START_EXCEPTION(n, label);				\
	CRITICAL_EXCEPTION_PROLOG n label;				\
	prepare_transfer_to_handler;				\
	bl	hdlr;						\
	b	ret_from_crit_exc

/*
 * 0x0100 - Critical Interrupt Exception
 */
	CRITICAL_EXCEPTION(0x0100, CriticalInterrupt, unknown_exception)

/*
 * 0x0200 - Machine Check Exception
 */
	CRITICAL_EXCEPTION(0x0200, MachineCheck, machine_check_exception)

/*
 * 0x0300 - Data Storage Exception
 * This happens for just a few reasons.  U0 set (but we don't do that),
 * or zone protection fault (user violation, write to protected page).
 * The other Data TLB exceptions bail out to this point
 * if they can't resolve the lightweight TLB fault.
 */
	START_EXCEPTION(0x0300,	DataStorage)
	EXCEPTION_PROLOG 0x300 DataStorage handle_dar_dsisr=1
	prepare_transfer_to_handler
	bl	do_page_fault
	b	interrupt_return

/*
 * 0x0400 - Instruction Storage Exception
 * This is caused by a fetch from non-execute or guarded pages.
 */
	START_EXCEPTION(0x0400, InstructionAccess)
	EXCEPTION_PROLOG 0x400 InstructionAccess
	li	r5,0
	stw	r5, _ESR(r11)		/* Zero ESR */
	stw	r12, _DEAR(r11)		/* SRR0 as DEAR */
	prepare_transfer_to_handler
	bl	do_page_fault
	b	interrupt_return

/* 0x0500 - External Interrupt Exception */
	EXCEPTION(0x0500, HardwareInterrupt, do_IRQ)

/* 0x0600 - Alignment Exception */
	START_EXCEPTION(0x0600, Alignment)
	EXCEPTION_PROLOG 0x600 Alignment handle_dar_dsisr=1
	prepare_transfer_to_handler
	bl	alignment_exception
	REST_NVGPRS(r1)
	b	interrupt_return

/* 0x0700 - Program Exception */
	START_EXCEPTION(0x0700, ProgramCheck)
	EXCEPTION_PROLOG 0x700 ProgramCheck handle_dar_dsisr=1
	prepare_transfer_to_handler
	bl	program_check_exception
	REST_NVGPRS(r1)
	b	interrupt_return

	EXCEPTION(0x0800, Trap_08, unknown_exception)
	EXCEPTION(0x0900, Trap_09, unknown_exception)
	EXCEPTION(0x0A00, Trap_0A, unknown_exception)
	EXCEPTION(0x0B00, Trap_0B, unknown_exception)

/* 0x0C00 - System Call Exception */
	START_EXCEPTION(0x0C00,	SystemCall)
	SYSCALL_ENTRY	0xc00
/*	Trap_0D is commented out to get more space for system call exception */

/*	EXCEPTION(0x0D00, Trap_0D, unknown_exception) */
	EXCEPTION(0x0E00, Trap_0E, unknown_exception)
	EXCEPTION(0x0F00, Trap_0F, unknown_exception)

/* 0x1000 - Programmable Interval Timer (PIT) Exception */
	START_EXCEPTION(0x1000, DecrementerTrap)
	b Decrementer

/* 0x1010 - Fixed Interval Timer (FIT) Exception */
	START_EXCEPTION(0x1010, FITExceptionTrap)
	b FITException

/* 0x1020 - Watchdog Timer (WDT) Exception */
	START_EXCEPTION(0x1020, WDTExceptionTrap)
	b WDTException

/* 0x1100 - Data TLB Miss Exception
 * As the name implies, translation is not in the MMU, so search the
 * page tables and fix it.  The only purpose of this function is to
 * load TLB entries from the page table if they exist.
 */
	START_EXCEPTION(0x1100,	DTLBMiss)
	mtspr	SPRN_SPRG_SCRATCH5, r10 /* Save some working registers */
	mtspr	SPRN_SPRG_SCRATCH6, r11
	mtspr	SPRN_SPRG_SCRATCH3, r12
	mtspr	SPRN_SPRG_SCRATCH4, r9
	mfcr	r12
	mfspr	r9, SPRN_PID
	rlwimi	r12, r9, 0, 0xff
	mfspr	r10, SPRN_DEAR		/* Get faulting address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	lis	r11, PAGE_OFFSET@h
	cmplw	r10, r11
	blt+	3f
	lis	r11, swapper_pg_dir@h
	ori	r11, r11, swapper_pg_dir@l
	li	r9, 0
	mtspr	SPRN_PID, r9		/* TLB will have 0 TID */
	b	4f

	/* Get the PGD for the current thread.
	 */
3:
	mfspr	r11,SPRN_SPRG_THREAD
	lwz	r11,PGDIR(r11)
4:
	tophys(r11, r11)
	rlwimi	r11, r10, 12, 20, 29	/* Create L1 (pgdir/pmd) address */
	lwz	r11, 0(r11)		/* Get L1 entry */
	andi.	r9, r11, _PMD_PRESENT	/* Check if it points to a PTE page */
	beq	2f			/* Bail if no table */

	rlwimi	r11, r10, 22, 20, 29	/* Compute PTE address */
	lwz	r11, 0(r11)		/* Get Linux PTE */
	li	r9, _PAGE_PRESENT | _PAGE_ACCESSED
	andc.	r9, r9, r11		/* Check permission */
	bne	5f

	rlwinm	r9, r11, 1, _PAGE_RW	/* dirty => rw */
	and	r9, r9, r11		/* hwwrite = dirty & rw */
	rlwimi	r11, r9, 0, _PAGE_RW	/* replace rw by hwwrite */

	/* Create TLB tag.  This is the faulting address plus a static
	 * set of bits.  These are size, valid, E, U0.
	*/
	li	r9, 0x00c0
	rlwimi	r10, r9, 0, 20, 31

	b	finish_tlb_load

2:	/* Check for possible large-page pmd entry */
	rlwinm.	r9, r11, 2, 22, 24
	beq	5f

	/* Create TLB tag.  This is the faulting address, plus a static
	 * set of bits (valid, E, U0) plus the size from the PMD.
	 */
	ori	r9, r9, 0x40
	rlwimi	r10, r9, 0, 20, 31

	b	finish_tlb_load

5:
	/* The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
	mtspr	SPRN_PID, r12
	mtcrf	0x80, r12
	mfspr	r9, SPRN_SPRG_SCRATCH4
	mfspr	r12, SPRN_SPRG_SCRATCH3
	mfspr	r11, SPRN_SPRG_SCRATCH6
	mfspr	r10, SPRN_SPRG_SCRATCH5
	b	DataStorage

/* 0x1200 - Instruction TLB Miss Exception
 * Nearly the same as above, except we get our information from different
 * registers and bailout to a different point.
 */
	START_EXCEPTION(0x1200,	ITLBMiss)
	mtspr	SPRN_SPRG_SCRATCH5, r10	 /* Save some working registers */
	mtspr	SPRN_SPRG_SCRATCH6, r11
	mtspr	SPRN_SPRG_SCRATCH3, r12
	mtspr	SPRN_SPRG_SCRATCH4, r9
	mfcr	r12
	mfspr	r9, SPRN_PID
	rlwimi	r12, r9, 0, 0xff
	mfspr	r10, SPRN_SRR0		/* Get faulting address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	lis	r11, PAGE_OFFSET@h
	cmplw	r10, r11
	blt+	3f
	lis	r11, swapper_pg_dir@h
	ori	r11, r11, swapper_pg_dir@l
	li	r9, 0
	mtspr	SPRN_PID, r9		/* TLB will have 0 TID */
	b	4f

	/* Get the PGD for the current thread.
	 */
3:
	mfspr	r11,SPRN_SPRG_THREAD
	lwz	r11,PGDIR(r11)
4:
	tophys(r11, r11)
	rlwimi	r11, r10, 12, 20, 29	/* Create L1 (pgdir/pmd) address */
	lwz	r11, 0(r11)		/* Get L1 entry */
	andi.	r9, r11, _PMD_PRESENT	/* Check if it points to a PTE page */
	beq	2f			/* Bail if no table */

	rlwimi	r11, r10, 22, 20, 29	/* Compute PTE address */
	lwz	r11, 0(r11)		/* Get Linux PTE */
	li	r9, _PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_EXEC
	andc.	r9, r9, r11		/* Check permission */
	bne	5f

	rlwinm	r9, r11, 1, _PAGE_RW	/* dirty => rw */
	and	r9, r9, r11		/* hwwrite = dirty & rw */
	rlwimi	r11, r9, 0, _PAGE_RW	/* replace rw by hwwrite */

	/* Create TLB tag.  This is the faulting address plus a static
	 * set of bits.  These are size, valid, E, U0.
	*/
	li	r9, 0x00c0
	rlwimi	r10, r9, 0, 20, 31

	b	finish_tlb_load

2:	/* Check for possible large-page pmd entry */
	rlwinm.	r9, r11, 2, 22, 24
	beq	5f

	/* Create TLB tag.  This is the faulting address, plus a static
	 * set of bits (valid, E, U0) plus the size from the PMD.
	 */
	ori	r9, r9, 0x40
	rlwimi	r10, r9, 0, 20, 31

	b	finish_tlb_load

5:
	/* The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
	mtspr	SPRN_PID, r12
	mtcrf	0x80, r12
	mfspr	r9, SPRN_SPRG_SCRATCH4
	mfspr	r12, SPRN_SPRG_SCRATCH3
	mfspr	r11, SPRN_SPRG_SCRATCH6
	mfspr	r10, SPRN_SPRG_SCRATCH5
	b	InstructionAccess

	EXCEPTION(0x1300, Trap_13, unknown_exception)
	EXCEPTION(0x1400, Trap_14, unknown_exception)
	EXCEPTION(0x1500, Trap_15, unknown_exception)
	EXCEPTION(0x1600, Trap_16, unknown_exception)
	EXCEPTION(0x1700, Trap_17, unknown_exception)
	EXCEPTION(0x1800, Trap_18, unknown_exception)
	EXCEPTION(0x1900, Trap_19, unknown_exception)
	EXCEPTION(0x1A00, Trap_1A, unknown_exception)
	EXCEPTION(0x1B00, Trap_1B, unknown_exception)
	EXCEPTION(0x1C00, Trap_1C, unknown_exception)
	EXCEPTION(0x1D00, Trap_1D, unknown_exception)
	EXCEPTION(0x1E00, Trap_1E, unknown_exception)
	EXCEPTION(0x1F00, Trap_1F, unknown_exception)

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
 * save (and later restore) the MSR via SPRN_SRR1, which will still have
 * the MSR_DE bit set.
 */
	/* 0x2000 - Debug Exception */
	START_EXCEPTION(0x2000, DebugTrap)
	CRITICAL_EXCEPTION_PROLOG 0x2000 DebugTrap

	/*
	 * If this is a single step or branch-taken exception in an
	 * exception entry sequence, it was probably meant to apply to
	 * the code where the exception occurred (since exception entry
	 * doesn't turn off DE automatically).  We simulate the effect
	 * of turning off DE on entry to an exception handler by turning
	 * off DE in the SRR3 value and clearing the debug status.
	 */
	mfspr	r10,SPRN_DBSR		/* check single-step/branch taken */
	andis.	r10,r10,DBSR_IC@h
	beq+	2f

	andi.	r10,r9,MSR_IR|MSR_PR	/* check supervisor + MMU off */
	beq	1f			/* branch and fix it up */

	mfspr   r10,SPRN_SRR2		/* Faulting instruction address */
	cmplwi  r10,0x2100
	bgt+    2f			/* address above exception vectors */

	/* here it looks like we got an inappropriate debug exception. */
1:	rlwinm	r9,r9,0,~MSR_DE		/* clear DE in the SRR3 value */
	lis	r10,DBSR_IC@h		/* clear the IC event */
	mtspr	SPRN_DBSR,r10
	/* restore state and get out */
	lwz	r10,_CCR(r11)
	lwz	r0,GPR0(r11)
	lwz	r1,GPR1(r11)
	mtcrf	0x80,r10
	mtspr	SPRN_SRR2,r12
	mtspr	SPRN_SRR3,r9
	lwz	r9,GPR9(r11)
	lwz	r12,GPR12(r11)
	lwz	r10,crit_r10@l(0)
	lwz	r11,crit_r11@l(0)
	rfci
	b	.

	/* continue normal handling for a critical exception... */
2:	mfspr	r4,SPRN_DBSR
	stw	r4,_ESR(r11)		/* DebugException takes DBSR in _ESR */
	prepare_transfer_to_handler
	bl	DebugException
	b	ret_from_crit_exc

	/* Programmable Interval Timer (PIT) Exception. (from 0x1000) */
	__HEAD
Decrementer:
	EXCEPTION_PROLOG 0x1000 Decrementer
	lis	r0,TSR_PIS@h
	mtspr	SPRN_TSR,r0		/* Clear the PIT exception */
	prepare_transfer_to_handler
	bl	timer_interrupt
	b	interrupt_return

	/* Fixed Interval Timer (FIT) Exception. (from 0x1010) */
	__HEAD
FITException:
	EXCEPTION_PROLOG 0x1010 FITException
	prepare_transfer_to_handler
	bl	unknown_exception
	b	interrupt_return

	/* Watchdog Timer (WDT) Exception. (from 0x1020) */
	__HEAD
WDTException:
	CRITICAL_EXCEPTION_PROLOG 0x1020 WDTException
	prepare_transfer_to_handler
	bl	WatchdogException
	b	ret_from_crit_exc

/* Other PowerPC processors, namely those derived from the 6xx-series
 * have vectors from 0x2100 through 0x2F00 defined, but marked as reserved.
 * However, for the 4xx-series processors these are neither defined nor
 * reserved.
 */

	__HEAD
	/* Damn, I came up one instruction too many to fit into the
	 * exception space :-).  Both the instruction and data TLB
	 * miss get to this point to load the TLB.
	 * 	r10 - TLB_TAG value
	 * 	r11 - Linux PTE
	 *	r9 - available to use
	 *	PID - loaded with proper value when we get here
	 *	Upon exit, we reload everything and RFI.
	 * Actually, it will fit now, but oh well.....a common place
	 * to load the TLB.
	 */
tlb_4xx_index:
	.long	0
finish_tlb_load:
	/*
	 * Clear out the software-only bits in the PTE to generate the
	 * TLB_DATA value.  These are the bottom 2 bits of the RPM, the
	 * top 3 bits of the zone field, and M.
	 */
	li	r9, 0x0ce2
	andc	r11, r11, r9

	/* load the next available TLB index. */
	lwz	r9, tlb_4xx_index@l(0)
	addi	r9, r9, 1
	andi.	r9, r9, PPC40X_TLB_SIZE - 1
	stw	r9, tlb_4xx_index@l(0)

	tlbwe	r11, r9, TLB_DATA		/* Load TLB LO */
	tlbwe	r10, r9, TLB_TAG		/* Load TLB HI */

	/* Done...restore registers and get out of here.
	*/
	mtspr	SPRN_PID, r12
	mtcrf	0x80, r12
	mfspr	r9, SPRN_SPRG_SCRATCH4
	mfspr	r12, SPRN_SPRG_SCRATCH3
	mfspr	r11, SPRN_SPRG_SCRATCH6
	mfspr	r10, SPRN_SPRG_SCRATCH5
	rfi			/* Should sync shadow TLBs */
	b	.		/* prevent prefetch past rfi */

/* This is where the main kernel code starts.
 */
start_here:

	/* ptr to current */
	lis	r2,init_task@h
	ori	r2,r2,init_task@l

	/* ptr to phys current thread */
	tophys(r4,r2)
	addi	r4,r4,THREAD	/* init task's THREAD */
	mtspr	SPRN_SPRG_THREAD,r4

	/* stack */
	lis	r1,init_thread_union@ha
	addi	r1,r1,init_thread_union@l
	li	r0,0
	stwu	r0,THREAD_SIZE-STACK_FRAME_OVERHEAD(r1)

	bl	early_init	/* We have to do this with MMU on */

/*
 * Decide what sort of machine this is and initialize the MMU.
 */
#ifdef CONFIG_KASAN
	bl	kasan_early_init
#endif
	li	r3,0
	mr	r4,r31
	bl	machine_init
	bl	MMU_init

/* Go back to running unmapped so we can load up new values
 * and change to using our exception vectors.
 * On the 4xx, all we have to do is invalidate the TLB to clear
 * the old 16M byte TLB mappings.
 */
	lis	r4,2f@h
	ori	r4,r4,2f@l
	tophys(r4,r4)
	lis	r3,(MSR_KERNEL & ~(MSR_IR|MSR_DR))@h
	ori	r3,r3,(MSR_KERNEL & ~(MSR_IR|MSR_DR))@l
	mtspr	SPRN_SRR0,r4
	mtspr	SPRN_SRR1,r3
	rfi
	b	.		/* prevent prefetch past rfi */

/* Load up the kernel context */
2:
	sync			/* Flush to memory before changing TLB */
	tlbia
	isync			/* Flush shadow TLBs */

	/* set up the PTE pointers for the Abatron bdiGDB.
	*/
	lis	r6, swapper_pg_dir@h
	ori	r6, r6, swapper_pg_dir@l
	lis	r5, abatron_pteptrs@h
	ori	r5, r5, abatron_pteptrs@l
	stw	r5, 0xf0(0)	/* Must match your Abatron config file */
	tophys(r5,r5)
	stw	r6, 0(r5)

/* Now turn on the MMU for real! */
	lis	r4,MSR_KERNEL@h
	ori	r4,r4,MSR_KERNEL@l
	lis	r3,start_kernel@h
	ori	r3,r3,start_kernel@l
	mtspr	SPRN_SRR0,r3
	mtspr	SPRN_SRR1,r4
	rfi			/* enable MMU and jump to start_kernel */
	b	.		/* prevent prefetch past rfi */

/* Set up the initial MMU state so we can do the first level of
 * kernel initialization.  This maps the first 32 MBytes of memory 1:1
 * virtual to physical and more importantly sets the cache mode.
 */
initial_mmu:
	tlbia			/* Invalidate all TLB entries */
	isync

	/* We should still be executing code at physical address 0x0000xxxx
	 * at this point. However, start_here is at virtual address
	 * 0xC000xxxx. So, set up a TLB mapping to cover this once
	 * translation is enabled.
	 */

	lis	r3,KERNELBASE@h		/* Load the kernel virtual address */
	ori	r3,r3,KERNELBASE@l
	tophys(r4,r3)			/* Load the kernel physical address */

	iccci	r0,r3			/* Invalidate the i-cache before use */

	/* Load the kernel PID.
	*/
	li	r0,0
	mtspr	SPRN_PID,r0
	sync

	/* Configure and load one entry into TLB slots 63 */
	clrrwi	r4,r4,10		/* Mask off the real page number */
	ori	r4,r4,(TLB_WR | TLB_EX)	/* Set the write and execute bits */

	clrrwi	r3,r3,10		/* Mask off the effective page number */
	ori	r3,r3,(TLB_VALID | TLB_PAGESZ(PAGESZ_16M))

        li      r0,63                    /* TLB slot 63 */

	tlbwe	r4,r0,TLB_DATA		/* Load the data portion of the entry */
	tlbwe	r3,r0,TLB_TAG		/* Load the tag portion of the entry */

	li	r0,62			/* TLB slot 62 */
	addis	r4,r4,SZ_16M@h
	addis	r3,r3,SZ_16M@h
	tlbwe	r4,r0,TLB_DATA		/* Load the data portion of the entry */
	tlbwe	r3,r0,TLB_TAG		/* Load the tag portion of the entry */

	isync

	/* Establish the exception vector base
	*/
	lis	r4,KERNELBASE@h		/* EVPR only uses the high 16-bits */
	tophys(r0,r4)			/* Use the physical address */
	mtspr	SPRN_EVPR,r0

	blr

_GLOBAL(abort)
        mfspr   r13,SPRN_DBCR0
        oris    r13,r13,DBCR0_RST_SYSTEM@h
        mtspr   SPRN_DBCR0,r13

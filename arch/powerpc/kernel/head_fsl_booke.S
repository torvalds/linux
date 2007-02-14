/*
 * Kernel execution entry point code.
 *
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
 *    Copyright 2002-2004 MontaVista Software, Inc.
 *      PowerPC 44x support, Matt Porter <mporter@kernel.crashing.org>
 *    Copyright 2004 Freescale Semiconductor, Inc
 *      PowerPC e500 modifications, Kumar Gala <galak@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/threads.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include "head_booke.h"

/* As with the other PowerPC ports, it is expected that when code
 * execution begins here, the following registers contain valid, yet
 * optional, information:
 *
 *   r3 - Board info structure pointer (DRAM, frequency, MAC address, etc.)
 *   r4 - Starting address of the init RAM disk
 *   r5 - Ending address of the init RAM disk
 *   r6 - Start of kernel command line string (e.g. "mem=128")
 *   r7 - End of kernel command line string
 *
 */
	.text
_GLOBAL(_stext)
_GLOBAL(_start)
	/*
	 * Reserve a word at a fixed location to store the address
	 * of abatron_pteptrs
	 */
	nop
/*
 * Save parameters we are passed
 */
	mr	r31,r3
	mr	r30,r4
	mr	r29,r5
	mr	r28,r6
	mr	r27,r7
	li	r24,0		/* CPU number */

/* We try to not make any assumptions about how the boot loader
 * setup or used the TLBs.  We invalidate all mappings from the
 * boot loader and load a single entry in TLB1[0] to map the
 * first 16M of kernel memory.  Any boot info passed from the
 * bootloader needs to live in this first 16M.
 *
 * Requirement on bootloader:
 *  - The page we're executing in needs to reside in TLB1 and
 *    have IPROT=1.  If not an invalidate broadcast could
 *    evict the entry we're currently executing in.
 *
 *  r3 = Index of TLB1 were executing in
 *  r4 = Current MSR[IS]
 *  r5 = Index of TLB1 temp mapping
 *
 * Later in mapin_ram we will correctly map lowmem, and resize TLB1[0]
 * if needed
 */

/* 1. Find the index of the entry we're executing in */
	bl	invstr				/* Find our address */
invstr:	mflr	r6				/* Make it accessible */
	mfmsr	r7
	rlwinm	r4,r7,27,31,31			/* extract MSR[IS] */
	mfspr	r7, SPRN_PID0
	slwi	r7,r7,16
	or	r7,r7,r4
	mtspr	SPRN_MAS6,r7
	tlbsx	0,r6				/* search MSR[IS], SPID=PID0 */
#ifndef CONFIG_E200
	mfspr	r7,SPRN_MAS1
	andis.	r7,r7,MAS1_VALID@h
	bne	match_TLB
	mfspr	r7,SPRN_PID1
	slwi	r7,r7,16
	or	r7,r7,r4
	mtspr	SPRN_MAS6,r7
	tlbsx	0,r6				/* search MSR[IS], SPID=PID1 */
	mfspr	r7,SPRN_MAS1
	andis.	r7,r7,MAS1_VALID@h
	bne	match_TLB
	mfspr	r7, SPRN_PID2
	slwi	r7,r7,16
	or	r7,r7,r4
	mtspr	SPRN_MAS6,r7
	tlbsx	0,r6				/* Fall through, we had to match */
#endif
match_TLB:
	mfspr	r7,SPRN_MAS0
	rlwinm	r3,r7,16,20,31			/* Extract MAS0(Entry) */

	mfspr	r7,SPRN_MAS1			/* Insure IPROT set */
	oris	r7,r7,MAS1_IPROT@h
	mtspr	SPRN_MAS1,r7
	tlbwe

/* 2. Invalidate all entries except the entry we're executing in */
	mfspr	r9,SPRN_TLB1CFG
	andi.	r9,r9,0xfff
	li	r6,0				/* Set Entry counter to 0 */
1:	lis	r7,0x1000			/* Set MAS0(TLBSEL) = 1 */
	rlwimi	r7,r6,16,4,15			/* Setup MAS0 = TLBSEL | ESEL(r6) */
	mtspr	SPRN_MAS0,r7
	tlbre
	mfspr	r7,SPRN_MAS1
	rlwinm	r7,r7,0,2,31			/* Clear MAS1 Valid and IPROT */
	cmpw	r3,r6
	beq	skpinv				/* Dont update the current execution TLB */
	mtspr	SPRN_MAS1,r7
	tlbwe
	isync
skpinv:	addi	r6,r6,1				/* Increment */
	cmpw	r6,r9				/* Are we done? */
	bne	1b				/* If not, repeat */

	/* Invalidate TLB0 */
	li      r6,0x04
	tlbivax 0,r6
#ifdef CONFIG_SMP
	tlbsync
#endif
	/* Invalidate TLB1 */
	li      r6,0x0c
	tlbivax 0,r6
#ifdef CONFIG_SMP
	tlbsync
#endif
	msync

/* 3. Setup a temp mapping and jump to it */
	andi.	r5, r3, 0x1	/* Find an entry not used and is non-zero */
	addi	r5, r5, 0x1
	lis	r7,0x1000	/* Set MAS0(TLBSEL) = 1 */
	rlwimi	r7,r3,16,4,15	/* Setup MAS0 = TLBSEL | ESEL(r3) */
	mtspr	SPRN_MAS0,r7
	tlbre

	/* Just modify the entry ID and EPN for the temp mapping */
	lis	r7,0x1000	/* Set MAS0(TLBSEL) = 1 */
	rlwimi	r7,r5,16,4,15	/* Setup MAS0 = TLBSEL | ESEL(r5) */
	mtspr	SPRN_MAS0,r7
	xori	r6,r4,1		/* Setup TMP mapping in the other Address space */
	slwi	r6,r6,12
	oris	r6,r6,(MAS1_VALID|MAS1_IPROT)@h
	ori	r6,r6,(MAS1_TSIZE(BOOKE_PAGESZ_4K))@l
	mtspr	SPRN_MAS1,r6
	mfspr	r6,SPRN_MAS2
	li	r7,0		/* temp EPN = 0 */
	rlwimi	r7,r6,0,20,31
	mtspr	SPRN_MAS2,r7
	tlbwe

	xori	r6,r4,1
	slwi	r6,r6,5		/* setup new context with other address space */
	bl	1f		/* Find our address */
1:	mflr	r9
	rlwimi	r7,r9,0,20,31
	addi	r7,r7,24
	mtspr	SPRN_SRR0,r7
	mtspr	SPRN_SRR1,r6
	rfi

/* 4. Clear out PIDs & Search info */
	li	r6,0
	mtspr	SPRN_PID0,r6
#ifndef CONFIG_E200
	mtspr	SPRN_PID1,r6
	mtspr	SPRN_PID2,r6
#endif
	mtspr	SPRN_MAS6,r6

/* 5. Invalidate mapping we started in */
	lis	r7,0x1000	/* Set MAS0(TLBSEL) = 1 */
	rlwimi	r7,r3,16,4,15	/* Setup MAS0 = TLBSEL | ESEL(r3) */
	mtspr	SPRN_MAS0,r7
	tlbre
	mfspr	r6,SPRN_MAS1
	rlwinm	r6,r6,0,2,0	/* clear IPROT */
	mtspr	SPRN_MAS1,r6
	tlbwe
	/* Invalidate TLB1 */
	li      r9,0x0c
	tlbivax 0,r9
#ifdef CONFIG_SMP
	tlbsync
#endif
	msync

/* 6. Setup KERNELBASE mapping in TLB1[0] */
	lis	r6,0x1000		/* Set MAS0(TLBSEL) = TLB1(1), ESEL = 0 */
	mtspr	SPRN_MAS0,r6
	lis	r6,(MAS1_VALID|MAS1_IPROT)@h
	ori	r6,r6,(MAS1_TSIZE(BOOKE_PAGESZ_16M))@l
	mtspr	SPRN_MAS1,r6
	li	r7,0
	lis	r6,KERNELBASE@h
	ori	r6,r6,KERNELBASE@l
	rlwimi	r6,r7,0,20,31
	mtspr	SPRN_MAS2,r6
	li	r7,(MAS3_SX|MAS3_SW|MAS3_SR)
	mtspr	SPRN_MAS3,r7
	tlbwe

/* 7. Jump to KERNELBASE mapping */
	lis	r7,MSR_KERNEL@h
	ori	r7,r7,MSR_KERNEL@l
	bl	1f			/* Find our address */
1:	mflr	r9
	rlwimi	r6,r9,0,20,31
	addi	r6,r6,24
	mtspr	SPRN_SRR0,r6
	mtspr	SPRN_SRR1,r7
	rfi				/* start execution out of TLB1[0] entry */

/* 8. Clear out the temp mapping */
	lis	r7,0x1000	/* Set MAS0(TLBSEL) = 1 */
	rlwimi	r7,r5,16,4,15	/* Setup MAS0 = TLBSEL | ESEL(r5) */
	mtspr	SPRN_MAS0,r7
	tlbre
	mfspr	r8,SPRN_MAS1
	rlwinm	r8,r8,0,2,0	/* clear IPROT */
	mtspr	SPRN_MAS1,r8
	tlbwe
	/* Invalidate TLB1 */
	li      r9,0x0c
	tlbivax 0,r9
#ifdef CONFIG_SMP
	tlbsync
#endif
	msync

	/* Establish the interrupt vector offsets */
	SET_IVOR(0,  CriticalInput);
	SET_IVOR(1,  MachineCheck);
	SET_IVOR(2,  DataStorage);
	SET_IVOR(3,  InstructionStorage);
	SET_IVOR(4,  ExternalInput);
	SET_IVOR(5,  Alignment);
	SET_IVOR(6,  Program);
	SET_IVOR(7,  FloatingPointUnavailable);
	SET_IVOR(8,  SystemCall);
	SET_IVOR(9,  AuxillaryProcessorUnavailable);
	SET_IVOR(10, Decrementer);
	SET_IVOR(11, FixedIntervalTimer);
	SET_IVOR(12, WatchdogTimer);
	SET_IVOR(13, DataTLBError);
	SET_IVOR(14, InstructionTLBError);
	SET_IVOR(15, Debug);
	SET_IVOR(32, SPEUnavailable);
	SET_IVOR(33, SPEFloatingPointData);
	SET_IVOR(34, SPEFloatingPointRound);
#ifndef CONFIG_E200
	SET_IVOR(35, PerformanceMonitor);
#endif

	/* Establish the interrupt vector base */
	lis	r4,interrupt_base@h	/* IVPR only uses the high 16-bits */
	mtspr	SPRN_IVPR,r4

	/* Setup the defaults for TLB entries */
	li	r2,(MAS4_TSIZED(BOOKE_PAGESZ_4K))@l
#ifdef CONFIG_E200
	oris	r2,r2,MAS4_TLBSELD(1)@h
#endif
   	mtspr	SPRN_MAS4, r2

#if 0
	/* Enable DOZE */
	mfspr	r2,SPRN_HID0
	oris	r2,r2,HID0_DOZE@h
	mtspr	SPRN_HID0, r2
#endif
#ifdef CONFIG_E200
	/* enable dedicated debug exception handling resources (Debug APU) */
	mfspr	r2,SPRN_HID0
	ori 	r2,r2,HID0_DAPUEN@l
	mtspr	SPRN_HID0,r2
#endif

#if !defined(CONFIG_BDI_SWITCH)
	/*
	 * The Abatron BDI JTAG debugger does not tolerate others
	 * mucking with the debug registers.
	 */
	lis	r2,DBCR0_IDM@h
	mtspr	SPRN_DBCR0,r2
	isync
	/* clear any residual debug events */
	li	r2,-1
	mtspr	SPRN_DBSR,r2
#endif

	/*
	 * This is where the main kernel code starts.
	 */

	/* ptr to current */
	lis	r2,init_task@h
	ori	r2,r2,init_task@l

	/* ptr to current thread */
	addi	r4,r2,THREAD	/* init task's THREAD */
	mtspr	SPRN_SPRG3,r4

	/* stack */
	lis	r1,init_thread_union@h
	ori	r1,r1,init_thread_union@l
	li	r0,0
	stwu	r0,THREAD_SIZE-STACK_FRAME_OVERHEAD(r1)

	bl	early_init

	mfspr	r3,SPRN_TLB1CFG
	andi.	r3,r3,0xfff
	lis	r4,num_tlbcam_entries@ha
	stw	r3,num_tlbcam_entries@l(r4)
/*
 * Decide what sort of machine this is and initialize the MMU.
 */
	mr	r3,r31
	mr	r4,r30
	mr	r5,r29
	mr	r6,r28
	mr	r7,r27
	bl	machine_init
	bl	MMU_init

	/* Setup PTE pointers for the Abatron bdiGDB */
	lis	r6, swapper_pg_dir@h
	ori	r6, r6, swapper_pg_dir@l
	lis	r5, abatron_pteptrs@h
	ori	r5, r5, abatron_pteptrs@l
	lis	r4, KERNELBASE@h
	ori	r4, r4, KERNELBASE@l
	stw	r5, 0(r4)	/* Save abatron_pteptrs at a fixed location */
	stw	r6, 0(r5)

	/* Let's move on */
	lis	r4,start_kernel@h
	ori	r4,r4,start_kernel@l
	lis	r3,MSR_KERNEL@h
	ori	r3,r3,MSR_KERNEL@l
	mtspr	SPRN_SRR0,r4
	mtspr	SPRN_SRR1,r3
	rfi			/* change context and jump to start_kernel */

/* Macros to hide the PTE size differences
 *
 * FIND_PTE -- walks the page tables given EA & pgdir pointer
 *   r10 -- EA of fault
 *   r11 -- PGDIR pointer
 *   r12 -- free
 *   label 2: is the bailout case
 *
 * if we find the pte (fall through):
 *   r11 is low pte word
 *   r12 is pointer to the pte
 */
#ifdef CONFIG_PTE_64BIT
#define PTE_FLAGS_OFFSET	4
#define FIND_PTE	\
	rlwinm 	r12, r10, 13, 19, 29;	/* Compute pgdir/pmd offset */	\
	lwzx	r11, r12, r11;		/* Get pgd/pmd entry */		\
	rlwinm.	r12, r11, 0, 0, 20;	/* Extract pt base address */	\
	beq	2f;			/* Bail if no table */		\
	rlwimi	r12, r10, 23, 20, 28;	/* Compute pte address */	\
	lwz	r11, 4(r12);		/* Get pte entry */
#else
#define PTE_FLAGS_OFFSET	0
#define FIND_PTE	\
	rlwimi	r11, r10, 12, 20, 29;	/* Create L1 (pgdir/pmd) address */	\
	lwz	r11, 0(r11);		/* Get L1 entry */			\
	rlwinm.	r12, r11, 0, 0, 19;	/* Extract L2 (pte) base address */	\
	beq	2f;			/* Bail if no table */			\
	rlwimi	r12, r10, 22, 20, 29;	/* Compute PTE address */		\
	lwz	r11, 0(r12);		/* Get Linux PTE */
#endif

/*
 * Interrupt vector entry code
 *
 * The Book E MMUs are always on so we don't need to handle
 * interrupts in real mode as with previous PPC processors. In
 * this case we handle interrupts in the kernel virtual address
 * space.
 *
 * Interrupt vectors are dynamically placed relative to the
 * interrupt prefix as determined by the address of interrupt_base.
 * The interrupt vectors offsets are programmed using the labels
 * for each interrupt vector entry.
 *
 * Interrupt vectors must be aligned on a 16 byte boundary.
 * We align on a 32 byte cache line boundary for good measure.
 */

interrupt_base:
	/* Critical Input Interrupt */
	CRITICAL_EXCEPTION(0x0100, CriticalInput, unknown_exception)

	/* Machine Check Interrupt */
#ifdef CONFIG_E200
	/* no RFMCI, MCSRRs on E200 */
	CRITICAL_EXCEPTION(0x0200, MachineCheck, machine_check_exception)
#else
	MCHECK_EXCEPTION(0x0200, MachineCheck, machine_check_exception)
#endif

	/* Data Storage Interrupt */
	START_EXCEPTION(DataStorage)
	mtspr	SPRN_SPRG0, r10		/* Save some working registers */
	mtspr	SPRN_SPRG1, r11
	mtspr	SPRN_SPRG4W, r12
	mtspr	SPRN_SPRG5W, r13
	mfcr	r11
	mtspr	SPRN_SPRG7W, r11

	/*
	 * Check if it was a store fault, if not then bail
	 * because a user tried to access a kernel or
	 * read-protected page.  Otherwise, get the
	 * offending address and handle it.
	 */
	mfspr	r10, SPRN_ESR
	andis.	r10, r10, ESR_ST@h
	beq	2f

	mfspr	r10, SPRN_DEAR		/* Get faulting address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	lis	r11, TASK_SIZE@h
	ori	r11, r11, TASK_SIZE@l
	cmplw	0, r10, r11
	bge	2f

	/* Get the PGD for the current thread */
3:
	mfspr	r11,SPRN_SPRG3
	lwz	r11,PGDIR(r11)
4:
	FIND_PTE

	/* Are _PAGE_USER & _PAGE_RW set & _PAGE_HWWRITE not? */
	andi.	r13, r11, _PAGE_RW|_PAGE_USER|_PAGE_HWWRITE
	cmpwi	0, r13, _PAGE_RW|_PAGE_USER
	bne	2f			/* Bail if not */

	/* Update 'changed'. */
	ori	r11, r11, _PAGE_DIRTY|_PAGE_ACCESSED|_PAGE_HWWRITE
	stw	r11, PTE_FLAGS_OFFSET(r12) /* Update Linux page table */

	/* MAS2 not updated as the entry does exist in the tlb, this
	   fault taken to detect state transition (eg: COW -> DIRTY)
	 */
	andi.	r11, r11, _PAGE_HWEXEC
	rlwimi	r11, r11, 31, 27, 27	/* SX <- _PAGE_HWEXEC */
	ori     r11, r11, (MAS3_UW|MAS3_SW|MAS3_UR|MAS3_SR)@l /* set static perms */

	/* update search PID in MAS6, AS = 0 */
	mfspr	r12, SPRN_PID0
	slwi	r12, r12, 16
	mtspr	SPRN_MAS6, r12

	/* find the TLB index that caused the fault.  It has to be here. */
	tlbsx	0, r10

	/* only update the perm bits, assume the RPN is fine */
	mfspr	r12, SPRN_MAS3
	rlwimi	r12, r11, 0, 20, 31
	mtspr	SPRN_MAS3,r12
	tlbwe

	/* Done...restore registers and get out of here.  */
	mfspr	r11, SPRN_SPRG7R
	mtcr	r11
	mfspr	r13, SPRN_SPRG5R
	mfspr	r12, SPRN_SPRG4R
	mfspr	r11, SPRN_SPRG1
	mfspr	r10, SPRN_SPRG0
	rfi			/* Force context change */

2:
	/*
	 * The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
	mfspr	r11, SPRN_SPRG7R
	mtcr	r11
	mfspr	r13, SPRN_SPRG5R
	mfspr	r12, SPRN_SPRG4R
	mfspr	r11, SPRN_SPRG1
	mfspr	r10, SPRN_SPRG0
	b	data_access

	/* Instruction Storage Interrupt */
	INSTRUCTION_STORAGE_EXCEPTION

	/* External Input Interrupt */
	EXCEPTION(0x0500, ExternalInput, do_IRQ, EXC_XFER_LITE)

	/* Alignment Interrupt */
	ALIGNMENT_EXCEPTION

	/* Program Interrupt */
	PROGRAM_EXCEPTION

	/* Floating Point Unavailable Interrupt */
#ifdef CONFIG_PPC_FPU
	FP_UNAVAILABLE_EXCEPTION
#else
#ifdef CONFIG_E200
	/* E200 treats 'normal' floating point instructions as FP Unavail exception */
	EXCEPTION(0x0800, FloatingPointUnavailable, program_check_exception, EXC_XFER_EE)
#else
	EXCEPTION(0x0800, FloatingPointUnavailable, unknown_exception, EXC_XFER_EE)
#endif
#endif

	/* System Call Interrupt */
	START_EXCEPTION(SystemCall)
	NORMAL_EXCEPTION_PROLOG
	EXC_XFER_EE_LITE(0x0c00, DoSyscall)

	/* Auxillary Processor Unavailable Interrupt */
	EXCEPTION(0x2900, AuxillaryProcessorUnavailable, unknown_exception, EXC_XFER_EE)

	/* Decrementer Interrupt */
	DECREMENTER_EXCEPTION

	/* Fixed Internal Timer Interrupt */
	/* TODO: Add FIT support */
	EXCEPTION(0x3100, FixedIntervalTimer, unknown_exception, EXC_XFER_EE)

	/* Watchdog Timer Interrupt */
#ifdef CONFIG_BOOKE_WDT
	CRITICAL_EXCEPTION(0x3200, WatchdogTimer, WatchdogException)
#else
	CRITICAL_EXCEPTION(0x3200, WatchdogTimer, unknown_exception)
#endif

	/* Data TLB Error Interrupt */
	START_EXCEPTION(DataTLBError)
	mtspr	SPRN_SPRG0, r10		/* Save some working registers */
	mtspr	SPRN_SPRG1, r11
	mtspr	SPRN_SPRG4W, r12
	mtspr	SPRN_SPRG5W, r13
	mfcr	r11
	mtspr	SPRN_SPRG7W, r11
	mfspr	r10, SPRN_DEAR		/* Get faulting address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	lis	r11, TASK_SIZE@h
	ori	r11, r11, TASK_SIZE@l
	cmplw	5, r10, r11
	blt	5, 3f
	lis	r11, swapper_pg_dir@h
	ori	r11, r11, swapper_pg_dir@l

	mfspr	r12,SPRN_MAS1		/* Set TID to 0 */
	rlwinm	r12,r12,0,16,1
	mtspr	SPRN_MAS1,r12

	b	4f

	/* Get the PGD for the current thread */
3:
	mfspr	r11,SPRN_SPRG3
	lwz	r11,PGDIR(r11)

4:
	FIND_PTE
	andi.	r13, r11, _PAGE_PRESENT	/* Is the page present? */
	beq	2f			/* Bail if not present */

#ifdef CONFIG_PTE_64BIT
	lwz	r13, 0(r12)
#endif
	ori	r11, r11, _PAGE_ACCESSED
	stw	r11, PTE_FLAGS_OFFSET(r12)

	 /* Jump to common tlb load */
	b	finish_tlb_load
2:
	/* The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
	mfspr	r11, SPRN_SPRG7R
	mtcr	r11
	mfspr	r13, SPRN_SPRG5R
	mfspr	r12, SPRN_SPRG4R
	mfspr	r11, SPRN_SPRG1
	mfspr	r10, SPRN_SPRG0
	b	data_access

	/* Instruction TLB Error Interrupt */
	/*
	 * Nearly the same as above, except we get our
	 * information from different registers and bailout
	 * to a different point.
	 */
	START_EXCEPTION(InstructionTLBError)
	mtspr	SPRN_SPRG0, r10		/* Save some working registers */
	mtspr	SPRN_SPRG1, r11
	mtspr	SPRN_SPRG4W, r12
	mtspr	SPRN_SPRG5W, r13
	mfcr	r11
	mtspr	SPRN_SPRG7W, r11
	mfspr	r10, SPRN_SRR0		/* Get faulting address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	lis	r11, TASK_SIZE@h
	ori	r11, r11, TASK_SIZE@l
	cmplw	5, r10, r11
	blt	5, 3f
	lis	r11, swapper_pg_dir@h
	ori	r11, r11, swapper_pg_dir@l

	mfspr	r12,SPRN_MAS1		/* Set TID to 0 */
	rlwinm	r12,r12,0,16,1
	mtspr	SPRN_MAS1,r12

	b	4f

	/* Get the PGD for the current thread */
3:
	mfspr	r11,SPRN_SPRG3
	lwz	r11,PGDIR(r11)

4:
	FIND_PTE
	andi.	r13, r11, _PAGE_PRESENT	/* Is the page present? */
	beq	2f			/* Bail if not present */

#ifdef CONFIG_PTE_64BIT
	lwz	r13, 0(r12)
#endif
	ori	r11, r11, _PAGE_ACCESSED
	stw	r11, PTE_FLAGS_OFFSET(r12)

	/* Jump to common TLB load point */
	b	finish_tlb_load

2:
	/* The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
	mfspr	r11, SPRN_SPRG7R
	mtcr	r11
	mfspr	r13, SPRN_SPRG5R
	mfspr	r12, SPRN_SPRG4R
	mfspr	r11, SPRN_SPRG1
	mfspr	r10, SPRN_SPRG0
	b	InstructionStorage

#ifdef CONFIG_SPE
	/* SPE Unavailable */
	START_EXCEPTION(SPEUnavailable)
	NORMAL_EXCEPTION_PROLOG
	bne	load_up_spe
	addi    r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_EE_LITE(0x2010, KernelSPE)
#else
	EXCEPTION(0x2020, SPEUnavailable, unknown_exception, EXC_XFER_EE)
#endif /* CONFIG_SPE */

	/* SPE Floating Point Data */
#ifdef CONFIG_SPE
	EXCEPTION(0x2030, SPEFloatingPointData, SPEFloatingPointException, EXC_XFER_EE);
#else
	EXCEPTION(0x2040, SPEFloatingPointData, unknown_exception, EXC_XFER_EE)
#endif /* CONFIG_SPE */

	/* SPE Floating Point Round */
	EXCEPTION(0x2050, SPEFloatingPointRound, unknown_exception, EXC_XFER_EE)

	/* Performance Monitor */
	EXCEPTION(0x2060, PerformanceMonitor, performance_monitor_exception, EXC_XFER_STD)


	/* Debug Interrupt */
	DEBUG_EXCEPTION

/*
 * Local functions
 */

	/*
	 * Data TLB exceptions will bail out to this point
	 * if they can't resolve the lightweight TLB fault.
	 */
data_access:
	NORMAL_EXCEPTION_PROLOG
	mfspr	r5,SPRN_ESR		/* Grab the ESR, save it, pass arg3 */
	stw	r5,_ESR(r11)
	mfspr	r4,SPRN_DEAR		/* Grab the DEAR, save it, pass arg2 */
	andis.	r10,r5,(ESR_ILK|ESR_DLK)@h
	bne	1f
	EXC_XFER_EE_LITE(0x0300, handle_page_fault)
1:
	addi	r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_EE_LITE(0x0300, CacheLockingException)

/*

 * Both the instruction and data TLB miss get to this
 * point to load the TLB.
 * 	r10 - EA of fault
 * 	r11 - TLB (info from Linux PTE)
 * 	r12, r13 - available to use
 * 	CR5 - results of addr < TASK_SIZE
 *	MAS0, MAS1 - loaded with proper value when we get here
 *	MAS2, MAS3 - will need additional info from Linux PTE
 *	Upon exit, we reload everything and RFI.
 */
finish_tlb_load:
	/*
	 * We set execute, because we don't have the granularity to
	 * properly set this at the page level (Linux problem).
	 * Many of these bits are software only.  Bits we don't set
	 * here we (properly should) assume have the appropriate value.
	 */

	mfspr	r12, SPRN_MAS2
#ifdef CONFIG_PTE_64BIT
	rlwimi	r12, r11, 26, 24, 31	/* extract ...WIMGE from pte */
#else
	rlwimi	r12, r11, 26, 27, 31	/* extract WIMGE from pte */
#endif
	mtspr	SPRN_MAS2, r12

	bge	5, 1f

	/* is user addr */
	andi.	r12, r11, (_PAGE_USER | _PAGE_HWWRITE | _PAGE_HWEXEC)
	andi.	r10, r11, _PAGE_USER	/* Test for _PAGE_USER */
	srwi	r10, r12, 1
	or	r12, r12, r10	/* Copy user perms into supervisor */
	iseleq	r12, 0, r12
	b	2f

	/* is kernel addr */
1:	rlwinm	r12, r11, 31, 29, 29	/* Extract _PAGE_HWWRITE into SW */
	ori	r12, r12, (MAS3_SX | MAS3_SR)

#ifdef CONFIG_PTE_64BIT
2:	rlwimi	r12, r13, 24, 0, 7	/* grab RPN[32:39] */
	rlwimi	r12, r11, 24, 8, 19	/* grab RPN[40:51] */
	mtspr	SPRN_MAS3, r12
BEGIN_FTR_SECTION
	srwi	r10, r13, 8		/* grab RPN[8:31] */
	mtspr	SPRN_MAS7, r10
END_FTR_SECTION_IFSET(CPU_FTR_BIG_PHYS)
#else
2:	rlwimi	r11, r12, 0, 20, 31	/* Extract RPN from PTE and merge with perms */
	mtspr	SPRN_MAS3, r11
#endif
#ifdef CONFIG_E200
	/* Round robin TLB1 entries assignment */
	mfspr	r12, SPRN_MAS0

	/* Extract TLB1CFG(NENTRY) */
	mfspr	r11, SPRN_TLB1CFG
	andi.	r11, r11, 0xfff

	/* Extract MAS0(NV) */
	andi.	r13, r12, 0xfff
	addi	r13, r13, 1
	cmpw	0, r13, r11
	addi	r12, r12, 1

	/* check if we need to wrap */
	blt	7f

	/* wrap back to first free tlbcam entry */
	lis	r13, tlbcam_index@ha
	lwz	r13, tlbcam_index@l(r13)
	rlwimi	r12, r13, 0, 20, 31
7:
	mtspr   SPRN_MAS0,r12
#endif /* CONFIG_E200 */

	tlbwe

	/* Done...restore registers and get out of here.  */
	mfspr	r11, SPRN_SPRG7R
	mtcr	r11
	mfspr	r13, SPRN_SPRG5R
	mfspr	r12, SPRN_SPRG4R
	mfspr	r11, SPRN_SPRG1
	mfspr	r10, SPRN_SPRG0
	rfi					/* Force context change */

#ifdef CONFIG_SPE
/* Note that the SPE support is closely modeled after the AltiVec
 * support.  Changes to one are likely to be applicable to the
 * other!  */
load_up_spe:
/*
 * Disable SPE for the task which had SPE previously,
 * and save its SPE registers in its thread_struct.
 * Enables SPE for use in the kernel on return.
 * On SMP we know the SPE units are free, since we give it up every
 * switch.  -- Kumar
 */
	mfmsr	r5
	oris	r5,r5,MSR_SPE@h
	mtmsr	r5			/* enable use of SPE now */
	isync
/*
 * For SMP, we don't do lazy SPE switching because it just gets too
 * horrendously complex, especially when a task switches from one CPU
 * to another.  Instead we call giveup_spe in switch_to.
 */
#ifndef CONFIG_SMP
	lis	r3,last_task_used_spe@ha
	lwz	r4,last_task_used_spe@l(r3)
	cmpi	0,r4,0
	beq	1f
	addi	r4,r4,THREAD	/* want THREAD of last_task_used_spe */
	SAVE_32EVRS(0,r10,r4)
   	evxor	evr10, evr10, evr10	/* clear out evr10 */
	evmwumiaa evr10, evr10, evr10	/* evr10 <- ACC = 0 * 0 + ACC */
	li	r5,THREAD_ACC
   	evstddx	evr10, r4, r5		/* save off accumulator */
	lwz	r5,PT_REGS(r4)
	lwz	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	lis	r10,MSR_SPE@h
	andc	r4,r4,r10	/* disable SPE for previous task */
	stw	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#endif /* CONFIG_SMP */
	/* enable use of SPE after return */
	oris	r9,r9,MSR_SPE@h
	mfspr	r5,SPRN_SPRG3		/* current task's THREAD (phys) */
	li	r4,1
	li	r10,THREAD_ACC
	stw	r4,THREAD_USED_SPE(r5)
	evlddx	evr4,r10,r5
	evmra	evr4,evr4
	REST_32EVRS(0,r10,r5)
#ifndef CONFIG_SMP
	subi	r4,r5,THREAD
	stw	r4,last_task_used_spe@l(r3)
#endif /* CONFIG_SMP */
	/* restore registers and return */
2:	REST_4GPRS(3, r11)
	lwz	r10,_CCR(r11)
	REST_GPR(1, r11)
	mtcr	r10
	lwz	r10,_LINK(r11)
	mtlr	r10
	REST_GPR(10, r11)
	mtspr	SPRN_SRR1,r9
	mtspr	SPRN_SRR0,r12
	REST_GPR(9, r11)
	REST_GPR(12, r11)
	lwz	r11,GPR11(r11)
	rfi

/*
 * SPE unavailable trap from kernel - print a message, but let
 * the task use SPE in the kernel until it returns to user mode.
 */
KernelSPE:
	lwz	r3,_MSR(r1)
	oris	r3,r3,MSR_SPE@h
	stw	r3,_MSR(r1)	/* enable use of SPE after return */
	lis	r3,87f@h
	ori	r3,r3,87f@l
	mr	r4,r2		/* current */
	lwz	r5,_NIP(r1)
	bl	printk
	b	ret_from_except
87:	.string	"SPE used in kernel  (task=%p, pc=%x)  \n"
	.align	4,0

#endif /* CONFIG_SPE */

/*
 * Global functions
 */

/*
 * extern void loadcam_entry(unsigned int index)
 *
 * Load TLBCAM[index] entry in to the L2 CAM MMU
 */
_GLOBAL(loadcam_entry)
	lis	r4,TLBCAM@ha
	addi	r4,r4,TLBCAM@l
	mulli	r5,r3,20
	add	r3,r5,r4
	lwz	r4,0(r3)
	mtspr	SPRN_MAS0,r4
	lwz	r4,4(r3)
	mtspr	SPRN_MAS1,r4
	lwz	r4,8(r3)
	mtspr	SPRN_MAS2,r4
	lwz	r4,12(r3)
	mtspr	SPRN_MAS3,r4
	tlbwe
	isync
	blr

/*
 * extern void giveup_altivec(struct task_struct *prev)
 *
 * The e500 core does not have an AltiVec unit.
 */
_GLOBAL(giveup_altivec)
	blr

#ifdef CONFIG_SPE
/*
 * extern void giveup_spe(struct task_struct *prev)
 *
 */
_GLOBAL(giveup_spe)
	mfmsr	r5
	oris	r5,r5,MSR_SPE@h
	mtmsr	r5			/* enable use of SPE now */
	isync
	cmpi	0,r3,0
	beqlr-				/* if no previous owner, done */
	addi	r3,r3,THREAD		/* want THREAD of task */
	lwz	r5,PT_REGS(r3)
	cmpi	0,r5,0
	SAVE_32EVRS(0, r4, r3)
   	evxor	evr6, evr6, evr6	/* clear out evr6 */
	evmwumiaa evr6, evr6, evr6	/* evr6 <- ACC = 0 * 0 + ACC */
	li	r4,THREAD_ACC
   	evstddx	evr6, r4, r3		/* save off accumulator */
	mfspr	r6,SPRN_SPEFSCR
	stw	r6,THREAD_SPEFSCR(r3)	/* save spefscr register value */
	beq	1f
	lwz	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	lis	r3,MSR_SPE@h
	andc	r4,r4,r3		/* disable SPE for previous task */
	stw	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#ifndef CONFIG_SMP
	li	r5,0
	lis	r4,last_task_used_spe@ha
	stw	r5,last_task_used_spe@l(r4)
#endif /* CONFIG_SMP */
	blr
#endif /* CONFIG_SPE */

/*
 * extern void giveup_fpu(struct task_struct *prev)
 *
 * Not all FSL Book-E cores have an FPU
 */
#ifndef CONFIG_PPC_FPU
_GLOBAL(giveup_fpu)
	blr
#endif

/*
 * extern void abort(void)
 *
 * At present, this routine just applies a system reset.
 */
_GLOBAL(abort)
	li	r13,0
        mtspr   SPRN_DBCR0,r13		/* disable all debug events */
	isync
	mfmsr	r13
	ori	r13,r13,MSR_DE@l	/* Enable Debug Events */
	mtmsr	r13
	isync
        mfspr   r13,SPRN_DBCR0
        lis	r13,(DBCR0_IDM|DBCR0_RST_CHIP)@h
        mtspr   SPRN_DBCR0,r13
	isync

_GLOBAL(set_context)

#ifdef CONFIG_BDI_SWITCH
	/* Context switch the PTE pointer for the Abatron BDI2000.
	 * The PGDIR is the second parameter.
	 */
	lis	r5, abatron_pteptrs@h
	ori	r5, r5, abatron_pteptrs@l
	stw	r4, 0x4(r5)
#endif
	mtspr	SPRN_PID,r3
	isync			/* Force context change */
	blr

/*
 * We put a few things here that have to be page-aligned. This stuff
 * goes at the beginning of the data segment, which is page-aligned.
 */
	.data
	.align	12
	.globl	sdata
sdata:
	.globl	empty_zero_page
empty_zero_page:
	.space	4096
	.globl	swapper_pg_dir
swapper_pg_dir:
	.space	4096

/* Reserved 4k for the critical exception stack & 4k for the machine
 * check stack per CPU for kernel mode exceptions */
	.section .bss
        .align 12
exception_stack_bottom:
	.space	BOOKE_EXCEPTION_STACK_SIZE * NR_CPUS
	.globl	exception_stack_top
exception_stack_top:

/*
 * This space gets a copy of optional info passed to us by the bootstrap
 * which is used to pass parameters into the kernel like root=/dev/sda1, etc.
 */
	.globl	cmd_line
cmd_line:
	.space	512

/*
 * Room for two PTE pointers, usually the kernel and current user pointers
 * to their respective root page table.
 */
abatron_pteptrs:
	.space	8

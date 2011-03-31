/*
 * Kernel execution entry point code.
 *
 *    Copyright (c) 1995-1996 Gary Thomas <gdt@linuxppc.org>
 *	Initial PowerPC version.
 *    Copyright (c) 1996 Cort Dougan <cort@cs.nmt.edu>
 *	Rewritten for PReP
 *    Copyright (c) 1996 Paul Mackerras <paulus@cs.anu.edu.au>
 *	Low-level exception handers, MMU support, and rewrite.
 *    Copyright (c) 1997 Dan Malek <dmalek@jlc.net>
 *	PowerPC 8xx modifications.
 *    Copyright (c) 1998-1999 TiVo, Inc.
 *	PowerPC 403GCX modifications.
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *	PowerPC 403GCX/405GP modifications.
 *    Copyright 2000 MontaVista Software Inc.
 *	PPC405 modifications
 *	PowerPC 403GCX/405GP modifications.
 *	Author: MontaVista Software, Inc.
 *		frank_rowand@mvista.com or source@mvista.com
 *		debbie_chu@mvista.com
 *    Copyright 2002-2004 MontaVista Software, Inc.
 *	PowerPC 44x support, Matt Porter <mporter@kernel.crashing.org>
 *    Copyright 2004 Freescale Semiconductor, Inc
 *	PowerPC e500 modifications, Kumar Gala <galak@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/threads.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/cache.h>
#include <asm/ptrace.h>
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
	__HEAD
_ENTRY(_stext);
_ENTRY(_start);
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
	li	r25,0		/* phys kernel start (low) */
	li	r24,0		/* CPU number */
	li	r23,0		/* phys kernel start (high) */

/* We try to not make any assumptions about how the boot loader
 * setup or used the TLBs.  We invalidate all mappings from the
 * boot loader and load a single entry in TLB1[0] to map the
 * first 64M of kernel memory.  Any boot info passed from the
 * bootloader needs to live in this first 64M.
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

_ENTRY(__early_start)

#define ENTRY_MAPPING_BOOT_SETUP
#include "fsl_booke_entry_mapping.S"
#undef ENTRY_MAPPING_BOOT_SETUP

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
	SET_IVOR(15, DebugCrit);

	/* Establish the interrupt vector base */
	lis	r4,interrupt_base@h	/* IVPR only uses the high 16-bits */
	mtspr	SPRN_IVPR,r4

	/* Setup the defaults for TLB entries */
	li	r2,(MAS4_TSIZED(BOOK3E_PAGESZ_4K))@l
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

#ifdef CONFIG_SMP
	/* Check to see if we're the second processor, and jump
	 * to the secondary_start code if so
	 */
	lis	r24, boot_cpuid@h
	ori	r24, r24, boot_cpuid@l
	lwz	r24, 0(r24)
	cmpwi	r24, -1
	mfspr   r24,SPRN_PIR
	bne	__secondary_start
#endif

	/*
	 * This is where the main kernel code starts.
	 */

	/* ptr to current */
	lis	r2,init_task@h
	ori	r2,r2,init_task@l

	/* ptr to current thread */
	addi	r4,r2,THREAD	/* init task's THREAD */
	mtspr	SPRN_SPRG_THREAD,r4

	/* stack */
	lis	r1,init_thread_union@h
	ori	r1,r1,init_thread_union@l
	li	r0,0
	stwu	r0,THREAD_SIZE-STACK_FRAME_OVERHEAD(r1)

	rlwinm  r22,r1,0,0,31-THREAD_SHIFT      /* current thread_info */
	stw	r24, TI_CPU(r22)

	bl	early_init

#ifdef CONFIG_RELOCATABLE
	lis	r3,kernstart_addr@ha
	la	r3,kernstart_addr@l(r3)
#ifdef CONFIG_PHYS_64BIT
	stw	r23,0(r3)
	stw	r25,4(r3)
#else
	stw	r25,0(r3)
#endif
#endif

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
#define FIND_PTE	\
	rlwinm	r12, r10, 13, 19, 29;	/* Compute pgdir/pmd offset */	\
	lwzx	r11, r12, r11;		/* Get pgd/pmd entry */		\
	rlwinm.	r12, r11, 0, 0, 20;	/* Extract pt base address */	\
	beq	2f;			/* Bail if no table */		\
	rlwimi	r12, r10, 23, 20, 28;	/* Compute pte address */	\
	lwz	r11, 4(r12);		/* Get pte entry */
#else
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

	/* Auxiliary Processor Unavailable Interrupt */
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
	mtspr	SPRN_SPRG_WSCRATCH0, r10 /* Save some working registers */
	mtspr	SPRN_SPRG_WSCRATCH1, r11
	mtspr	SPRN_SPRG_WSCRATCH2, r12
	mtspr	SPRN_SPRG_WSCRATCH3, r13
	mfcr	r11
	mtspr	SPRN_SPRG_WSCRATCH4, r11
	mfspr	r10, SPRN_DEAR		/* Get faulting address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	lis	r11, PAGE_OFFSET@h
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
	mfspr	r11,SPRN_SPRG_THREAD
	lwz	r11,PGDIR(r11)

4:
	/* Mask of required permission bits. Note that while we
	 * do copy ESR:ST to _PAGE_RW position as trying to write
	 * to an RO page is pretty common, we don't do it with
	 * _PAGE_DIRTY. We could do it, but it's a fairly rare
	 * event so I'd rather take the overhead when it happens
	 * rather than adding an instruction here. We should measure
	 * whether the whole thing is worth it in the first place
	 * as we could avoid loading SPRN_ESR completely in the first
	 * place...
	 *
	 * TODO: Is it worth doing that mfspr & rlwimi in the first
	 *       place or can we save a couple of instructions here ?
	 */
	mfspr	r12,SPRN_ESR
#ifdef CONFIG_PTE_64BIT
	li	r13,_PAGE_PRESENT
	oris	r13,r13,_PAGE_ACCESSED@h
#else
	li	r13,_PAGE_PRESENT|_PAGE_ACCESSED
#endif
	rlwimi	r13,r12,11,29,29

	FIND_PTE
	andc.	r13,r13,r11		/* Check permission */

#ifdef CONFIG_PTE_64BIT
#ifdef CONFIG_SMP
	subf	r10,r11,r12		/* create false data dep */
	lwzx	r13,r11,r10		/* Get upper pte bits */
#else
	lwz	r13,0(r12)		/* Get upper pte bits */
#endif
#endif

	bne	2f			/* Bail if permission/valid mismach */

	/* Jump to common tlb load */
	b	finish_tlb_load
2:
	/* The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
	mfspr	r11, SPRN_SPRG_RSCRATCH4
	mtcr	r11
	mfspr	r13, SPRN_SPRG_RSCRATCH3
	mfspr	r12, SPRN_SPRG_RSCRATCH2
	mfspr	r11, SPRN_SPRG_RSCRATCH1
	mfspr	r10, SPRN_SPRG_RSCRATCH0
	b	DataStorage

	/* Instruction TLB Error Interrupt */
	/*
	 * Nearly the same as above, except we get our
	 * information from different registers and bailout
	 * to a different point.
	 */
	START_EXCEPTION(InstructionTLBError)
	mtspr	SPRN_SPRG_WSCRATCH0, r10 /* Save some working registers */
	mtspr	SPRN_SPRG_WSCRATCH1, r11
	mtspr	SPRN_SPRG_WSCRATCH2, r12
	mtspr	SPRN_SPRG_WSCRATCH3, r13
	mfcr	r11
	mtspr	SPRN_SPRG_WSCRATCH4, r11
	mfspr	r10, SPRN_SRR0		/* Get faulting address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	lis	r11, PAGE_OFFSET@h
	cmplw	5, r10, r11
	blt	5, 3f
	lis	r11, swapper_pg_dir@h
	ori	r11, r11, swapper_pg_dir@l

	mfspr	r12,SPRN_MAS1		/* Set TID to 0 */
	rlwinm	r12,r12,0,16,1
	mtspr	SPRN_MAS1,r12

	/* Make up the required permissions for kernel code */
#ifdef CONFIG_PTE_64BIT
	li	r13,_PAGE_PRESENT | _PAGE_BAP_SX
	oris	r13,r13,_PAGE_ACCESSED@h
#else
	li	r13,_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_EXEC
#endif
	b	4f

	/* Get the PGD for the current thread */
3:
	mfspr	r11,SPRN_SPRG_THREAD
	lwz	r11,PGDIR(r11)

	/* Make up the required permissions for user code */
#ifdef CONFIG_PTE_64BIT
	li	r13,_PAGE_PRESENT | _PAGE_BAP_UX
	oris	r13,r13,_PAGE_ACCESSED@h
#else
	li	r13,_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_EXEC
#endif

4:
	FIND_PTE
	andc.	r13,r13,r11		/* Check permission */

#ifdef CONFIG_PTE_64BIT
#ifdef CONFIG_SMP
	subf	r10,r11,r12		/* create false data dep */
	lwzx	r13,r11,r10		/* Get upper pte bits */
#else
	lwz	r13,0(r12)		/* Get upper pte bits */
#endif
#endif

	bne	2f			/* Bail if permission mismach */

	/* Jump to common TLB load point */
	b	finish_tlb_load

2:
	/* The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
	mfspr	r11, SPRN_SPRG_RSCRATCH4
	mtcr	r11
	mfspr	r13, SPRN_SPRG_RSCRATCH3
	mfspr	r12, SPRN_SPRG_RSCRATCH2
	mfspr	r11, SPRN_SPRG_RSCRATCH1
	mfspr	r10, SPRN_SPRG_RSCRATCH0
	b	InstructionStorage

#ifdef CONFIG_SPE
	/* SPE Unavailable */
	START_EXCEPTION(SPEUnavailable)
	NORMAL_EXCEPTION_PROLOG
	bne	load_up_spe
	addi	r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_EE_LITE(0x2010, KernelSPE)
#else
	EXCEPTION(0x2020, SPEUnavailable, unknown_exception, EXC_XFER_EE)
#endif /* CONFIG_SPE */

	/* SPE Floating Point Data */
#ifdef CONFIG_SPE
	EXCEPTION(0x2030, SPEFloatingPointData, SPEFloatingPointException, EXC_XFER_EE);

	/* SPE Floating Point Round */
	EXCEPTION(0x2050, SPEFloatingPointRound, SPEFloatingPointRoundException, EXC_XFER_EE)
#else
	EXCEPTION(0x2040, SPEFloatingPointData, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x2050, SPEFloatingPointRound, unknown_exception, EXC_XFER_EE)
#endif /* CONFIG_SPE */

	/* Performance Monitor */
	EXCEPTION(0x2060, PerformanceMonitor, performance_monitor_exception, EXC_XFER_STD)

	EXCEPTION(0x2070, Doorbell, doorbell_exception, EXC_XFER_STD)

	CRITICAL_EXCEPTION(0x2080, CriticalDoorbell, unknown_exception)

	/* Debug Interrupt */
	DEBUG_DEBUG_EXCEPTION
	DEBUG_CRIT_EXCEPTION

/*
 * Local functions
 */

/*
 * Both the instruction and data TLB miss get to this
 * point to load the TLB.
 *	r10 - available to use
 *	r11 - TLB (info from Linux PTE)
 *	r12 - available to use
 *	r13 - upper bits of PTE (if PTE_64BIT) or available to use
 *	CR5 - results of addr >= PAGE_OFFSET
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
	rlwimi	r12, r11, 32-19, 27, 31	/* extract WIMGE from pte */
#else
	rlwimi	r12, r11, 26, 27, 31	/* extract WIMGE from pte */
#endif
	mtspr	SPRN_MAS2, r12

#ifdef CONFIG_PTE_64BIT
	rlwinm	r12, r11, 32-2, 26, 31	/* Move in perm bits */
	andi.	r10, r11, _PAGE_DIRTY
	bne	1f
	li	r10, MAS3_SW | MAS3_UW
	andc	r12, r12, r10
1:	rlwimi	r12, r13, 20, 0, 11	/* grab RPN[32:43] */
	rlwimi	r12, r11, 20, 12, 19	/* grab RPN[44:51] */
	mtspr	SPRN_MAS3, r12
BEGIN_MMU_FTR_SECTION
	srwi	r10, r13, 12		/* grab RPN[12:31] */
	mtspr	SPRN_MAS7, r10
END_MMU_FTR_SECTION_IFSET(MMU_FTR_BIG_PHYS)
#else
	li	r10, (_PAGE_EXEC | _PAGE_PRESENT)
	rlwimi	r10, r11, 31, 29, 29	/* extract _PAGE_DIRTY into SW */
	and	r12, r11, r10
	andi.	r10, r11, _PAGE_USER	/* Test for _PAGE_USER */
	slwi	r10, r12, 1
	or	r10, r10, r12
	iseleq	r12, r12, r10
	rlwimi	r11, r12, 0, 20, 31	/* Extract RPN from PTE and merge with perms */
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
	mtspr	SPRN_MAS0,r12
#endif /* CONFIG_E200 */

	tlbwe

	/* Done...restore registers and get out of here.  */
	mfspr	r11, SPRN_SPRG_RSCRATCH4
	mtcr	r11
	mfspr	r13, SPRN_SPRG_RSCRATCH3
	mfspr	r12, SPRN_SPRG_RSCRATCH2
	mfspr	r11, SPRN_SPRG_RSCRATCH1
	mfspr	r10, SPRN_SPRG_RSCRATCH0
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
#endif /* !CONFIG_SMP */
	/* enable use of SPE after return */
	oris	r9,r9,MSR_SPE@h
	mfspr	r5,SPRN_SPRG_THREAD	/* current task's THREAD (phys) */
	li	r4,1
	li	r10,THREAD_ACC
	stw	r4,THREAD_USED_SPE(r5)
	evlddx	evr4,r10,r5
	evmra	evr4,evr4
	REST_32EVRS(0,r10,r5)
#ifndef CONFIG_SMP
	subi	r4,r5,THREAD
	stw	r4,last_task_used_spe@l(r3)
#endif /* !CONFIG_SMP */
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
#ifdef CONFIG_PRINTK
	lis	r3,87f@h
	ori	r3,r3,87f@l
	mr	r4,r2		/* current */
	lwz	r5,_NIP(r1)
	bl	printk
#endif
	b	ret_from_except
#ifdef CONFIG_PRINTK
87:	.string	"SPE used in kernel  (task=%p, pc=%x)  \n"
#endif
	.align	4,0

#endif /* CONFIG_SPE */

/*
 * Global functions
 */

/* Adjust or setup IVORs for e200 */
_GLOBAL(__setup_e200_ivors)
	li	r3,DebugDebug@l
	mtspr	SPRN_IVOR15,r3
	li	r3,SPEUnavailable@l
	mtspr	SPRN_IVOR32,r3
	li	r3,SPEFloatingPointData@l
	mtspr	SPRN_IVOR33,r3
	li	r3,SPEFloatingPointRound@l
	mtspr	SPRN_IVOR34,r3
	sync
	blr

/* Adjust or setup IVORs for e500v1/v2 */
_GLOBAL(__setup_e500_ivors)
	li	r3,DebugCrit@l
	mtspr	SPRN_IVOR15,r3
	li	r3,SPEUnavailable@l
	mtspr	SPRN_IVOR32,r3
	li	r3,SPEFloatingPointData@l
	mtspr	SPRN_IVOR33,r3
	li	r3,SPEFloatingPointRound@l
	mtspr	SPRN_IVOR34,r3
	li	r3,PerformanceMonitor@l
	mtspr	SPRN_IVOR35,r3
	sync
	blr

/* Adjust or setup IVORs for e500mc */
_GLOBAL(__setup_e500mc_ivors)
	li	r3,DebugDebug@l
	mtspr	SPRN_IVOR15,r3
	li	r3,PerformanceMonitor@l
	mtspr	SPRN_IVOR35,r3
	li	r3,Doorbell@l
	mtspr	SPRN_IVOR36,r3
	li	r3,CriticalDoorbell@l
	mtspr	SPRN_IVOR37,r3
	sync
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
#endif /* !CONFIG_SMP */
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
	mtspr	SPRN_DBCR0,r13		/* disable all debug events */
	isync
	mfmsr	r13
	ori	r13,r13,MSR_DE@l	/* Enable Debug Events */
	mtmsr	r13
	isync
	mfspr	r13,SPRN_DBCR0
	lis	r13,(DBCR0_IDM|DBCR0_RST_CHIP)@h
	mtspr	SPRN_DBCR0,r13
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

_GLOBAL(flush_dcache_L1)
	mfspr	r3,SPRN_L1CFG0

	rlwinm	r5,r3,9,3	/* Extract cache block size */
	twlgti	r5,1		/* Only 32 and 64 byte cache blocks
				 * are currently defined.
				 */
	li	r4,32
	subfic	r6,r5,2		/* r6 = log2(1KiB / cache block size) -
				 *      log2(number of ways)
				 */
	slw	r5,r4,r5	/* r5 = cache block size */

	rlwinm	r7,r3,0,0xff	/* Extract number of KiB in the cache */
	mulli	r7,r7,13	/* An 8-way cache will require 13
				 * loads per set.
				 */
	slw	r7,r7,r6

	/* save off HID0 and set DCFA */
	mfspr	r8,SPRN_HID0
	ori	r9,r8,HID0_DCFA@l
	mtspr	SPRN_HID0,r9
	isync

	lis	r4,KERNELBASE@h
	mtctr	r7

1:	lwz	r3,0(r4)	/* Load... */
	add	r4,r4,r5
	bdnz	1b

	msync
	lis	r4,KERNELBASE@h
	mtctr	r7

1:	dcbf	0,r4		/* ...and flush. */
	add	r4,r4,r5
	bdnz	1b
	
	/* restore HID0 */
	mtspr	SPRN_HID0,r8
	isync

	blr

#ifdef CONFIG_SMP
/* When we get here, r24 needs to hold the CPU # */
	.globl __secondary_start
__secondary_start:
	lis	r3,__secondary_hold_acknowledge@h
	ori	r3,r3,__secondary_hold_acknowledge@l
	stw	r24,0(r3)

	li	r3,0
	mr	r4,r24		/* Why? */
	bl	call_setup_cpu

	lis	r3,tlbcam_index@ha
	lwz	r3,tlbcam_index@l(r3)
	mtctr	r3
	li	r26,0		/* r26 safe? */

	/* Load each CAM entry */
1:	mr	r3,r26
	bl	loadcam_entry
	addi	r26,r26,1
	bdnz	1b

	/* get current_thread_info and current */
	lis	r1,secondary_ti@ha
	lwz	r1,secondary_ti@l(r1)
	lwz	r2,TI_TASK(r1)

	/* stack */
	addi	r1,r1,THREAD_SIZE-STACK_FRAME_OVERHEAD
	li	r0,0
	stw	r0,0(r1)

	/* ptr to current thread */
	addi	r4,r2,THREAD	/* address of our thread_struct */
	mtspr	SPRN_SPRG_THREAD,r4

	/* Setup the defaults for TLB entries */
	li	r4,(MAS4_TSIZED(BOOK3E_PAGESZ_4K))@l
	mtspr	SPRN_MAS4,r4

	/* Jump to start_secondary */
	lis	r4,MSR_KERNEL@h
	ori	r4,r4,MSR_KERNEL@l
	lis	r3,start_secondary@h
	ori	r3,r3,start_secondary@l
	mtspr	SPRN_SRR0,r3
	mtspr	SPRN_SRR1,r4
	sync
	rfi
	sync

	.globl __secondary_hold_acknowledge
__secondary_hold_acknowledge:
	.long	-1
#endif

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
	.space	PGD_TABLE_SIZE

/*
 * Room for two PTE pointers, usually the kernel and current user pointers
 * to their respective root page table.
 */
abatron_pteptrs:
	.space	8

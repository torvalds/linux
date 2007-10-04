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
 *    Copyright 2002-2005 MontaVista Software, Inc.
 *      PowerPC 44x support, Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/ibm4xx.h>
#include <asm/ibm44x.h>
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

/*
 * Set up the initial MMU state
 *
 * We are still executing code at the virtual address
 * mappings set by the firmware for the base of RAM.
 *
 * We first invalidate all TLB entries but the one
 * we are running from.  We then load the KERNELBASE
 * mappings so we can begin to use kernel addresses
 * natively and so the interrupt vector locations are
 * permanently pinned (necessary since Book E
 * implementations always have translation enabled).
 *
 * TODO: Use the known TLB entry we are running from to
 *	 determine which physical region we are located
 *	 in.  This can be used to determine where in RAM
 *	 (on a shared CPU system) or PCI memory space
 *	 (on a DRAMless system) we are located.
 *       For now, we assume a perfect world which means
 *	 we are located at the base of DRAM (physical 0).
 */

/*
 * Search TLB for entry that we are currently using.
 * Invalidate all entries but the one we are using.
 */
	/* Load our current PID->MMUCR TID and MSR IS->MMUCR STS */
	mfspr	r3,SPRN_PID			/* Get PID */
	mfmsr	r4				/* Get MSR */
	andi.	r4,r4,MSR_IS@l			/* TS=1? */
	beq	wmmucr				/* If not, leave STS=0 */
	oris	r3,r3,PPC44x_MMUCR_STS@h	/* Set STS=1 */
wmmucr:	mtspr	SPRN_MMUCR,r3			/* Put MMUCR */
	sync

	bl	invstr				/* Find our address */
invstr:	mflr	r5				/* Make it accessible */
	tlbsx	r23,0,r5			/* Find entry we are in */
	li	r4,0				/* Start at TLB entry 0 */
	li	r3,0				/* Set PAGEID inval value */
1:	cmpw	r23,r4				/* Is this our entry? */
	beq	skpinv				/* If so, skip the inval */
	tlbwe	r3,r4,PPC44x_TLB_PAGEID		/* If not, inval the entry */
skpinv:	addi	r4,r4,1				/* Increment */
	cmpwi	r4,64				/* Are we done? */
	bne	1b				/* If not, repeat */
	isync					/* If so, context change */

/*
 * Configure and load pinned entry into TLB slot 63.
 */

	lis	r3,KERNELBASE@h		/* Load the kernel virtual address */
	ori	r3,r3,KERNELBASE@l

	/* Kernel is at the base of RAM */
	li r4, 0			/* Load the kernel physical address */

	/* Load the kernel PID = 0 */
	li	r0,0
	mtspr	SPRN_PID,r0
	sync

	/* Initialize MMUCR */
	li	r5,0
	mtspr	SPRN_MMUCR,r5
	sync

 	/* pageid fields */
	clrrwi	r3,r3,10		/* Mask off the effective page number */
	ori	r3,r3,PPC44x_TLB_VALID | PPC44x_TLB_256M

	/* xlat fields */
	clrrwi	r4,r4,10		/* Mask off the real page number */
					/* ERPN is 0 for first 4GB page */

	/* attrib fields */
	/* Added guarded bit to protect against speculative loads/stores */
	li	r5,0
	ori	r5,r5,(PPC44x_TLB_SW | PPC44x_TLB_SR | PPC44x_TLB_SX | PPC44x_TLB_G)

        li      r0,63                    /* TLB slot 63 */

	tlbwe	r3,r0,PPC44x_TLB_PAGEID	/* Load the pageid fields */
	tlbwe	r4,r0,PPC44x_TLB_XLAT	/* Load the translation fields */
	tlbwe	r5,r0,PPC44x_TLB_ATTRIB	/* Load the attrib/access fields */

	/* Force context change */
	mfmsr	r0
	mtspr	SPRN_SRR1, r0
	lis	r0,3f@h
	ori	r0,r0,3f@l
	mtspr	SPRN_SRR0,r0
	sync
	rfi

	/* If necessary, invalidate original entry we used */
3:	cmpwi	r23,63
	beq	4f
	li	r6,0
	tlbwe   r6,r23,PPC44x_TLB_PAGEID
	isync

4:
#ifdef CONFIG_SERIAL_TEXT_DEBUG
	/*
	 * Add temporary UART mapping for early debug.
	 * We can map UART registers wherever we want as long as they don't
	 * interfere with other system mappings (e.g. with pinned entries).
	 * For an example of how we handle this - see ocotea.h.       --ebs
	 */
 	/* pageid fields */
	lis	r3,UART0_IO_BASE@h
	ori	r3,r3,PPC44x_TLB_VALID | PPC44x_TLB_4K

	/* xlat fields */
	lis	r4,UART0_PHYS_IO_BASE@h		/* RPN depends on SoC */
#ifdef UART0_PHYS_ERPN
	ori	r4,r4,UART0_PHYS_ERPN		/* Add ERPN if above 4GB */
#endif

	/* attrib fields */
	li	r5,0
	ori	r5,r5,(PPC44x_TLB_SW | PPC44x_TLB_SR | PPC44x_TLB_I | PPC44x_TLB_G)

        li      r0,0                    /* TLB slot 0 */

	tlbwe	r3,r0,PPC44x_TLB_PAGEID	/* Load the pageid fields */
	tlbwe	r4,r0,PPC44x_TLB_XLAT	/* Load the translation fields */
	tlbwe	r5,r0,PPC44x_TLB_ATTRIB	/* Load the attrib/access fields */

	/* Force context change */
	isync
#endif /* CONFIG_SERIAL_TEXT_DEBUG */

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

	/* Establish the interrupt vector base */
	lis	r4,interrupt_base@h	/* IVPR only uses the high 16-bits */
	mtspr	SPRN_IVPR,r4

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
#ifdef CONFIG_440A
	MCHECK_EXCEPTION(0x0200, MachineCheck, machine_check_exception)
#else
	CRITICAL_EXCEPTION(0x0200, MachineCheck, machine_check_exception)
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
	cmplw	r10, r11
	blt+	3f
	lis	r11, swapper_pg_dir@h
	ori	r11, r11, swapper_pg_dir@l

	mfspr   r12,SPRN_MMUCR
	rlwinm	r12,r12,0,0,23		/* Clear TID */

	b	4f

	/* Get the PGD for the current thread */
3:
	mfspr	r11,SPRN_SPRG3
	lwz	r11,PGDIR(r11)

	/* Load PID into MMUCR TID */
	mfspr	r12,SPRN_MMUCR		/* Get MMUCR */
	mfspr   r13,SPRN_PID		/* Get PID */
	rlwimi	r12,r13,0,24,31		/* Set TID */

4:
	mtspr   SPRN_MMUCR,r12

	rlwinm  r12, r10, 13, 19, 29    /* Compute pgdir/pmd offset */
	lwzx    r11, r12, r11           /* Get pgd/pmd entry */
	rlwinm. r12, r11, 0, 0, 20      /* Extract pt base address */
	beq     2f                      /* Bail if no table */

	rlwimi  r12, r10, 23, 20, 28    /* Compute pte address */
	lwz     r11, 4(r12)             /* Get pte entry */

	andi.	r13, r11, _PAGE_RW	/* Is it writeable? */
	beq	2f			/* Bail if not */

	/* Update 'changed'.
	*/
	ori	r11, r11, _PAGE_DIRTY|_PAGE_ACCESSED|_PAGE_HWWRITE
	stw	r11, 4(r12)		/* Update Linux page table */

	li	r13, PPC44x_TLB_SR@l	/* Set SR */
	rlwimi	r13, r11, 29, 29, 29	/* SX = _PAGE_HWEXEC */
	rlwimi	r13, r11, 0, 30, 30	/* SW = _PAGE_RW */
	rlwimi	r13, r11, 29, 28, 28	/* UR = _PAGE_USER */
	rlwimi	r12, r11, 31, 26, 26	/* (_PAGE_USER>>1)->r12 */
	rlwimi	r12, r11, 29, 30, 30	/* (_PAGE_USER>>3)->r12 */
	and	r12, r12, r11		/* HWEXEC/RW & USER */
	rlwimi	r13, r12, 0, 26, 26	/* UX = HWEXEC & USER */
	rlwimi	r13, r12, 3, 27, 27	/* UW = RW & USER */

	rlwimi	r11,r13,0,26,31		/* Insert static perms */

	rlwinm	r11,r11,0,20,15		/* Clear U0-U3 */

	/* find the TLB index that caused the fault.  It has to be here. */
	tlbsx	r10, 0, r10

	tlbwe	r11, r10, PPC44x_TLB_ATTRIB	/* Write ATTRIB */

	/* Done...restore registers and get out of here.
	*/
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
	EXCEPTION(0x2010, FloatingPointUnavailable, unknown_exception, EXC_XFER_EE)
#endif

	/* System Call Interrupt */
	START_EXCEPTION(SystemCall)
	NORMAL_EXCEPTION_PROLOG
	EXC_XFER_EE_LITE(0x0c00, DoSyscall)

	/* Auxillary Processor Unavailable Interrupt */
	EXCEPTION(0x2020, AuxillaryProcessorUnavailable, unknown_exception, EXC_XFER_EE)

	/* Decrementer Interrupt */
	DECREMENTER_EXCEPTION

	/* Fixed Internal Timer Interrupt */
	/* TODO: Add FIT support */
	EXCEPTION(0x1010, FixedIntervalTimer, unknown_exception, EXC_XFER_EE)

	/* Watchdog Timer Interrupt */
	/* TODO: Add watchdog support */
#ifdef CONFIG_BOOKE_WDT
	CRITICAL_EXCEPTION(0x1020, WatchdogTimer, WatchdogException)
#else
	CRITICAL_EXCEPTION(0x1020, WatchdogTimer, unknown_exception)
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
	cmplw	r10, r11
	blt+	3f
	lis	r11, swapper_pg_dir@h
	ori	r11, r11, swapper_pg_dir@l

	mfspr	r12,SPRN_MMUCR
	rlwinm	r12,r12,0,0,23		/* Clear TID */

	b	4f

	/* Get the PGD for the current thread */
3:
	mfspr	r11,SPRN_SPRG3
	lwz	r11,PGDIR(r11)

	/* Load PID into MMUCR TID */
	mfspr	r12,SPRN_MMUCR
	mfspr   r13,SPRN_PID		/* Get PID */
	rlwimi	r12,r13,0,24,31		/* Set TID */

4:
	mtspr	SPRN_MMUCR,r12

	rlwinm 	r12, r10, 13, 19, 29	/* Compute pgdir/pmd offset */
	lwzx	r11, r12, r11		/* Get pgd/pmd entry */
	rlwinm.	r12, r11, 0, 0, 20	/* Extract pt base address */
	beq	2f			/* Bail if no table */

	rlwimi	r12, r10, 23, 20, 28	/* Compute pte address */
	lwz	r11, 4(r12)		/* Get pte entry */
	andi.	r13, r11, _PAGE_PRESENT	/* Is the page present? */
	beq	2f			/* Bail if not present */

	ori	r11, r11, _PAGE_ACCESSED
	stw	r11, 4(r12)

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
	cmplw	r10, r11
	blt+	3f
	lis	r11, swapper_pg_dir@h
	ori	r11, r11, swapper_pg_dir@l

	mfspr	r12,SPRN_MMUCR
	rlwinm	r12,r12,0,0,23		/* Clear TID */

	b	4f

	/* Get the PGD for the current thread */
3:
	mfspr	r11,SPRN_SPRG3
	lwz	r11,PGDIR(r11)

	/* Load PID into MMUCR TID */
	mfspr	r12,SPRN_MMUCR
	mfspr   r13,SPRN_PID		/* Get PID */
	rlwimi	r12,r13,0,24,31		/* Set TID */

4:
	mtspr	SPRN_MMUCR,r12

	rlwinm	r12, r10, 13, 19, 29	/* Compute pgdir/pmd offset */
	lwzx	r11, r12, r11		/* Get pgd/pmd entry */
	rlwinm.	r12, r11, 0, 0, 20	/* Extract pt base address */
	beq	2f			/* Bail if no table */

	rlwimi	r12, r10, 23, 20, 28	/* Compute pte address */
	lwz	r11, 4(r12)		/* Get pte entry */
	andi.	r13, r11, _PAGE_PRESENT	/* Is the page present? */
	beq	2f			/* Bail if not present */

	ori	r11, r11, _PAGE_ACCESSED
	stw	r11, 4(r12)

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
	EXC_XFER_EE_LITE(0x0300, handle_page_fault)

/*

 * Both the instruction and data TLB miss get to this
 * point to load the TLB.
 * 	r10 - EA of fault
 * 	r11 - available to use
 *	r12 - Pointer to the 64-bit PTE
 *	r13 - available to use
 *	MMUCR - loaded with proper value when we get here
 *	Upon exit, we reload everything and RFI.
 */
finish_tlb_load:
	/*
	 * We set execute, because we don't have the granularity to
	 * properly set this at the page level (Linux problem).
	 * If shared is set, we cause a zero PID->TID load.
	 * Many of these bits are software only.  Bits we don't set
	 * here we (properly should) assume have the appropriate value.
	 */

	/* Load the next available TLB index */
	lis	r13, tlb_44x_index@ha
	lwz	r13, tlb_44x_index@l(r13)
	/* Load the TLB high watermark */
	lis	r11, tlb_44x_hwater@ha
	lwz	r11, tlb_44x_hwater@l(r11)

	/* Increment, rollover, and store TLB index */
	addi	r13, r13, 1
	cmpw	0, r13, r11			/* reserve entries */
	ble	7f
	li	r13, 0
7:
	/* Store the next available TLB index */
	lis	r11, tlb_44x_index@ha
	stw	r13, tlb_44x_index@l(r11)

	lwz	r11, 0(r12)			/* Get MS word of PTE */
	lwz	r12, 4(r12)			/* Get LS word of PTE */
	rlwimi	r11, r12, 0, 0 , 19		/* Insert RPN */
	tlbwe	r11, r13, PPC44x_TLB_XLAT	/* Write XLAT */

	/*
	 * Create PAGEID. This is the faulting address,
	 * page size, and valid flag.
	 */
	li	r11, PPC44x_TLB_VALID | PPC44x_TLB_4K
	rlwimi	r10, r11, 0, 20, 31		/* Insert valid and page size */
	tlbwe	r10, r13, PPC44x_TLB_PAGEID	/* Write PAGEID */

	li	r10, PPC44x_TLB_SR@l		/* Set SR */
	rlwimi	r10, r12, 0, 30, 30		/* Set SW = _PAGE_RW */
	rlwimi	r10, r12, 29, 29, 29		/* SX = _PAGE_HWEXEC */
	rlwimi	r10, r12, 29, 28, 28		/* UR = _PAGE_USER */
	rlwimi	r11, r12, 31, 26, 26		/* (_PAGE_USER>>1)->r12 */
	and	r11, r12, r11			/* HWEXEC & USER */
	rlwimi	r10, r11, 0, 26, 26		/* UX = HWEXEC & USER */

	rlwimi	r12, r10, 0, 26, 31		/* Insert static perms */
	rlwinm	r12, r12, 0, 20, 15		/* Clear U0-U3 */
	tlbwe	r12, r13, PPC44x_TLB_ATTRIB	/* Write ATTRIB */

	/* Done...restore registers and get out of here.
	*/
	mfspr	r11, SPRN_SPRG7R
	mtcr	r11
	mfspr	r13, SPRN_SPRG5R
	mfspr	r12, SPRN_SPRG4R
	mfspr	r11, SPRN_SPRG1
	mfspr	r10, SPRN_SPRG0
	rfi					/* Force context change */

/*
 * Global functions
 */

/*
 * extern void giveup_altivec(struct task_struct *prev)
 *
 * The 44x core does not have an AltiVec unit.
 */
_GLOBAL(giveup_altivec)
	blr

/*
 * extern void giveup_fpu(struct task_struct *prev)
 *
 * The 44x core does not have an FPU.
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
        mfspr   r13,SPRN_DBCR0
        oris    r13,r13,DBCR0_RST_SYSTEM@h
        mtspr   SPRN_DBCR0,r13

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

/*
 * To support >32-bit physical addresses, we use an 8KB pgdir.
 */
	.globl	swapper_pg_dir
swapper_pg_dir:
	.space	8192

/* Reserved 4k for the critical exception stack & 4k for the machine
 * check stack per CPU for kernel mode exceptions */
	.section .bss
        .align 12
exception_stack_bottom:
	.space	BOOKE_EXCEPTION_STACK_SIZE
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

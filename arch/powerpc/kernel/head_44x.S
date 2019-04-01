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

#include <linux/init.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ptrace.h>
#include <asm/synch.h>
#include <asm/export.h>
#include <asm/code-patching-asm.h>
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
	mr	r31,r3		/* save device tree ptr */
	li	r24,0		/* CPU number */

#ifdef CONFIG_RELOCATABLE
/*
 * Relocate ourselves to the current runtime address.
 * This is called only by the Boot CPU.
 * "relocate" is called with our current runtime virutal
 * address.
 * r21 will be loaded with the physical runtime address of _stext
 */
	bl	0f				/* Get our runtime address */
0:	mflr	r21				/* Make it accessible */
	addis	r21,r21,(_stext - 0b)@ha
	addi	r21,r21,(_stext - 0b)@l 	/* Get our current runtime base */

	/*
	 * We have the runtime (virutal) address of our base.
	 * We calculate our shift of offset from a 256M page.
	 * We could map the 256M page we belong to at PAGE_OFFSET and
	 * get going from there.
	 */
	lis	r4,KERNELBASE@h
	ori	r4,r4,KERNELBASE@l
	rlwinm	r6,r21,0,4,31			/* r6 = PHYS_START % 256M */
	rlwinm	r5,r4,0,4,31			/* r5 = KERNELBASE % 256M */
	subf	r3,r5,r6			/* r3 = r6 - r5 */
	add	r3,r4,r3			/* Required Virutal Address */

	bl	relocate
#endif

	bl	init_cpu_state

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

	bl	early_init

#ifdef CONFIG_RELOCATABLE
	/*
	 * Relocatable kernel support based on processing of dynamic
	 * relocation entries.
	 *
	 * r25 will contain RPN/ERPN for the start address of memory
	 * r21 will contain the current offset of _stext
	 */
	lis	r3,kernstart_addr@ha
	la	r3,kernstart_addr@l(r3)

	/*
	 * Compute the kernstart_addr.
	 * kernstart_addr => (r6,r8)
	 * kernstart_addr & ~0xfffffff => (r6,r7)
	 */
	rlwinm	r6,r25,0,28,31	/* ERPN. Bits 32-35 of Address */
	rlwinm	r7,r25,0,0,3	/* RPN - assuming 256 MB page size */
	rlwinm	r8,r21,0,4,31	/* r8 = (_stext & 0xfffffff) */
	or	r8,r7,r8	/* Compute the lower 32bit of kernstart_addr */

	/* Store kernstart_addr */
	stw	r6,0(r3)	/* higher 32bit */
	stw	r8,4(r3)	/* lower 32bit  */

	/*
	 * Compute the virt_phys_offset :
	 * virt_phys_offset = stext.run - kernstart_addr
	 *
	 * stext.run = (KERNELBASE & ~0xfffffff) + (kernstart_addr & 0xfffffff)
	 * When we relocate, we have :
	 *
	 *	(kernstart_addr & 0xfffffff) = (stext.run & 0xfffffff)
	 *
	 * hence:
	 *  virt_phys_offset = (KERNELBASE & ~0xfffffff) - (kernstart_addr & ~0xfffffff)
	 *
	 */

	/* KERNELBASE&~0xfffffff => (r4,r5) */
	li	r4, 0		/* higer 32bit */
	lis	r5,KERNELBASE@h
	rlwinm	r5,r5,0,0,3	/* Align to 256M, lower 32bit */

	/*
	 * 64bit subtraction.
	 */
	subfc	r5,r7,r5
	subfe	r4,r6,r4

	/* Store virt_phys_offset */
	lis	r3,virt_phys_offset@ha
	la	r3,virt_phys_offset@l(r3)

	stw	r4,0(r3)
	stw	r5,4(r3)

#elif defined(CONFIG_DYNAMIC_MEMSTART)
	/*
	 * Mapping based, page aligned dynamic kernel loading.
	 *
	 * r25 will contain RPN/ERPN for the start address of memory
	 *
	 * Add the difference between KERNELBASE and PAGE_OFFSET to the
	 * start of physical memory to get kernstart_addr.
	 */
	lis	r3,kernstart_addr@ha
	la	r3,kernstart_addr@l(r3)

	lis	r4,KERNELBASE@h
	ori	r4,r4,KERNELBASE@l
	lis	r5,PAGE_OFFSET@h
	ori	r5,r5,PAGE_OFFSET@l
	subf	r4,r5,r4

	rlwinm	r6,r25,0,28,31	/* ERPN */
	rlwinm	r7,r25,0,0,3	/* RPN - assuming 256 MB page size */
	add	r7,r7,r4

	stw	r6,0(r3)
	stw	r7,4(r3)
#endif

/*
 * Decide what sort of machine this is and initialize the MMU.
 */
	li	r3,0
	mr	r4,r31
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

	/* Clear the Machine Check Syndrome Register */
	li	r0,0
	mtspr	SPRN_MCSR,r0

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
	CRITICAL_EXCEPTION(0x0100, CRITICAL, CriticalInput, unknown_exception)

	/* Machine Check Interrupt */
	CRITICAL_EXCEPTION(0x0200, MACHINE_CHECK, MachineCheck, \
			   machine_check_exception)
	MCHECK_EXCEPTION(0x0210, MachineCheckA, machine_check_exception)

	/* Data Storage Interrupt */
	DATA_STORAGE_EXCEPTION

		/* Instruction Storage Interrupt */
	INSTRUCTION_STORAGE_EXCEPTION

	/* External Input Interrupt */
	EXCEPTION(0x0500, BOOKE_INTERRUPT_EXTERNAL, ExternalInput, \
		  do_IRQ, EXC_XFER_LITE)

	/* Alignment Interrupt */
	ALIGNMENT_EXCEPTION

	/* Program Interrupt */
	PROGRAM_EXCEPTION

	/* Floating Point Unavailable Interrupt */
#ifdef CONFIG_PPC_FPU
	FP_UNAVAILABLE_EXCEPTION
#else
	EXCEPTION(0x2010, BOOKE_INTERRUPT_FP_UNAVAIL, \
		  FloatingPointUnavailable, unknown_exception, EXC_XFER_EE)
#endif
	/* System Call Interrupt */
	START_EXCEPTION(SystemCall)
	NORMAL_EXCEPTION_PROLOG(BOOKE_INTERRUPT_SYSCALL)
	EXC_XFER_EE_LITE(0x0c00, DoSyscall)

	/* Auxiliary Processor Unavailable Interrupt */
	EXCEPTION(0x2020, BOOKE_INTERRUPT_AP_UNAVAIL, \
		  AuxillaryProcessorUnavailable, unknown_exception, EXC_XFER_EE)

	/* Decrementer Interrupt */
	DECREMENTER_EXCEPTION

	/* Fixed Internal Timer Interrupt */
	/* TODO: Add FIT support */
	EXCEPTION(0x1010, BOOKE_INTERRUPT_FIT, FixedIntervalTimer, \
		  unknown_exception, EXC_XFER_EE)

	/* Watchdog Timer Interrupt */
	/* TODO: Add watchdog support */
#ifdef CONFIG_BOOKE_WDT
	CRITICAL_EXCEPTION(0x1020, WATCHDOG, WatchdogTimer, WatchdogException)
#else
	CRITICAL_EXCEPTION(0x1020, WATCHDOG, WatchdogTimer, unknown_exception)
#endif

	/* Data TLB Error Interrupt */
	START_EXCEPTION(DataTLBError44x)
	mtspr	SPRN_SPRG_WSCRATCH0, r10		/* Save some working registers */
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
	cmplw	r10, r11
	blt+	3f
	lis	r11, swapper_pg_dir@h
	ori	r11, r11, swapper_pg_dir@l

	mfspr	r12,SPRN_MMUCR
	rlwinm	r12,r12,0,0,23		/* Clear TID */

	b	4f

	/* Get the PGD for the current thread */
3:
	mfspr	r11,SPRN_SPRG_THREAD
	lwz	r11,PGDIR(r11)

	/* Load PID into MMUCR TID */
	mfspr	r12,SPRN_MMUCR
	mfspr   r13,SPRN_PID		/* Get PID */
	rlwimi	r12,r13,0,24,31		/* Set TID */

4:
	mtspr	SPRN_MMUCR,r12

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
	li	r13,_PAGE_PRESENT|_PAGE_ACCESSED
	rlwimi	r13,r12,10,30,30

	/* Load the PTE */
	/* Compute pgdir/pmd offset */
	rlwinm  r12, r10, PPC44x_PGD_OFF_SHIFT, PPC44x_PGD_OFF_MASK_BIT, 29
	lwzx	r11, r12, r11		/* Get pgd/pmd entry */
	rlwinm.	r12, r11, 0, 0, 20	/* Extract pt base address */
	beq	2f			/* Bail if no table */

	/* Compute pte address */
	rlwimi  r12, r10, PPC44x_PTE_ADD_SHIFT, PPC44x_PTE_ADD_MASK_BIT, 28
	lwz	r11, 0(r12)		/* Get high word of pte entry */
	lwz	r12, 4(r12)		/* Get low word of pte entry */

	lis	r10,tlb_44x_index@ha

	andc.	r13,r13,r12		/* Check permission */

	/* Load the next available TLB index */
	lwz	r13,tlb_44x_index@l(r10)

	bne	2f			/* Bail if permission mismach */

	/* Increment, rollover, and store TLB index */
	addi	r13,r13,1

	patch_site 0f, patch__tlb_44x_hwater_D
	/* Compare with watermark (instruction gets patched) */
0:	cmpwi	0,r13,1			/* reserve entries */
	ble	5f
	li	r13,0
5:
	/* Store the next available TLB index */
	stw	r13,tlb_44x_index@l(r10)

	/* Re-load the faulting address */
	mfspr	r10,SPRN_DEAR

	 /* Jump to common tlb load */
	b	finish_tlb_load_44x

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
	START_EXCEPTION(InstructionTLBError44x)
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
	cmplw	r10, r11
	blt+	3f
	lis	r11, swapper_pg_dir@h
	ori	r11, r11, swapper_pg_dir@l

	mfspr	r12,SPRN_MMUCR
	rlwinm	r12,r12,0,0,23		/* Clear TID */

	b	4f

	/* Get the PGD for the current thread */
3:
	mfspr	r11,SPRN_SPRG_THREAD
	lwz	r11,PGDIR(r11)

	/* Load PID into MMUCR TID */
	mfspr	r12,SPRN_MMUCR
	mfspr   r13,SPRN_PID		/* Get PID */
	rlwimi	r12,r13,0,24,31		/* Set TID */

4:
	mtspr	SPRN_MMUCR,r12

	/* Make up the required permissions */
	li	r13,_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_EXEC

	/* Compute pgdir/pmd offset */
	rlwinm 	r12, r10, PPC44x_PGD_OFF_SHIFT, PPC44x_PGD_OFF_MASK_BIT, 29
	lwzx	r11, r12, r11		/* Get pgd/pmd entry */
	rlwinm.	r12, r11, 0, 0, 20	/* Extract pt base address */
	beq	2f			/* Bail if no table */

	/* Compute pte address */
	rlwimi	r12, r10, PPC44x_PTE_ADD_SHIFT, PPC44x_PTE_ADD_MASK_BIT, 28
	lwz	r11, 0(r12)		/* Get high word of pte entry */
	lwz	r12, 4(r12)		/* Get low word of pte entry */

	lis	r10,tlb_44x_index@ha

	andc.	r13,r13,r12		/* Check permission */

	/* Load the next available TLB index */
	lwz	r13,tlb_44x_index@l(r10)

	bne	2f			/* Bail if permission mismach */

	/* Increment, rollover, and store TLB index */
	addi	r13,r13,1

	patch_site 0f, patch__tlb_44x_hwater_I
	/* Compare with watermark (instruction gets patched) */
0:	cmpwi	0,r13,1			/* reserve entries */
	ble	5f
	li	r13,0
5:
	/* Store the next available TLB index */
	stw	r13,tlb_44x_index@l(r10)

	/* Re-load the faulting address */
	mfspr	r10,SPRN_SRR0

	/* Jump to common TLB load point */
	b	finish_tlb_load_44x

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

/*
 * Both the instruction and data TLB miss get to this
 * point to load the TLB.
 * 	r10 - EA of fault
 * 	r11 - PTE high word value
 *	r12 - PTE low word value
 *	r13 - TLB index
 *	MMUCR - loaded with proper value when we get here
 *	Upon exit, we reload everything and RFI.
 */
finish_tlb_load_44x:
	/* Combine RPN & ERPN an write WS 0 */
	rlwimi	r11,r12,0,0,31-PAGE_SHIFT
	tlbwe	r11,r13,PPC44x_TLB_XLAT

	/*
	 * Create WS1. This is the faulting address (EPN),
	 * page size, and valid flag.
	 */
	li	r11,PPC44x_TLB_VALID | PPC44x_TLBE_SIZE
	/* Insert valid and page size */
	rlwimi	r10,r11,0,PPC44x_PTE_ADD_MASK_BIT,31
	tlbwe	r10,r13,PPC44x_TLB_PAGEID	/* Write PAGEID */

	/* And WS 2 */
	li	r10,0xf85			/* Mask to apply from PTE */
	rlwimi	r10,r12,29,30,30		/* DIRTY -> SW position */
	and	r11,r12,r10			/* Mask PTE bits to keep */
	andi.	r10,r12,_PAGE_USER		/* User page ? */
	beq	1f				/* nope, leave U bits empty */
	rlwimi	r11,r11,3,26,28			/* yes, copy S bits to U */
1:	tlbwe	r11,r13,PPC44x_TLB_ATTRIB	/* Write ATTRIB */

	/* Done...restore registers and get out of here.
	*/
	mfspr	r11, SPRN_SPRG_RSCRATCH4
	mtcr	r11
	mfspr	r13, SPRN_SPRG_RSCRATCH3
	mfspr	r12, SPRN_SPRG_RSCRATCH2
	mfspr	r11, SPRN_SPRG_RSCRATCH1
	mfspr	r10, SPRN_SPRG_RSCRATCH0
	rfi					/* Force context change */

/* TLB error interrupts for 476
 */
#ifdef CONFIG_PPC_47x
	START_EXCEPTION(DataTLBError47x)
	mtspr	SPRN_SPRG_WSCRATCH0,r10	/* Save some working registers */
	mtspr	SPRN_SPRG_WSCRATCH1,r11
	mtspr	SPRN_SPRG_WSCRATCH2,r12
	mtspr	SPRN_SPRG_WSCRATCH3,r13
	mfcr	r11
	mtspr	SPRN_SPRG_WSCRATCH4,r11
	mfspr	r10,SPRN_DEAR		/* Get faulting address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	lis	r11,PAGE_OFFSET@h
	cmplw	cr0,r10,r11
	blt+	3f
	lis	r11,swapper_pg_dir@h
	ori	r11,r11, swapper_pg_dir@l
	li	r12,0			/* MMUCR = 0 */
	b	4f

	/* Get the PGD for the current thread and setup MMUCR */
3:	mfspr	r11,SPRN_SPRG3
	lwz	r11,PGDIR(r11)
	mfspr   r12,SPRN_PID		/* Get PID */
4:	mtspr	SPRN_MMUCR,r12		/* Set MMUCR */

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
	li	r13,_PAGE_PRESENT|_PAGE_ACCESSED
	rlwimi	r13,r12,10,30,30

	/* Load the PTE */
	/* Compute pgdir/pmd offset */
	rlwinm  r12,r10,PPC44x_PGD_OFF_SHIFT,PPC44x_PGD_OFF_MASK_BIT,29
	lwzx	r11,r12,r11		/* Get pgd/pmd entry */

	/* Word 0 is EPN,V,TS,DSIZ */
	li	r12,PPC47x_TLB0_VALID | PPC47x_TLBE_SIZE
	rlwimi	r10,r12,0,32-PAGE_SHIFT,31	/* Insert valid and page size*/
	li	r12,0
	tlbwe	r10,r12,0

	/* XXX can we do better ? Need to make sure tlbwe has established
	 * latch V bit in MMUCR0 before the PTE is loaded further down */
#ifdef CONFIG_SMP
	isync
#endif

	rlwinm.	r12,r11,0,0,20		/* Extract pt base address */
	/* Compute pte address */
	rlwimi  r12,r10,PPC44x_PTE_ADD_SHIFT,PPC44x_PTE_ADD_MASK_BIT,28
	beq	2f			/* Bail if no table */
	lwz	r11,0(r12)		/* Get high word of pte entry */

	/* XXX can we do better ? maybe insert a known 0 bit from r11 into the
	 * bottom of r12 to create a data dependency... We can also use r10
	 * as destination nowadays
	 */
#ifdef CONFIG_SMP
	lwsync
#endif
	lwz	r12,4(r12)		/* Get low word of pte entry */

	andc.	r13,r13,r12		/* Check permission */

	 /* Jump to common tlb load */
	beq	finish_tlb_load_47x

2:	/* The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
	mfspr	r11,SPRN_SPRG_RSCRATCH4
	mtcr	r11
	mfspr	r13,SPRN_SPRG_RSCRATCH3
	mfspr	r12,SPRN_SPRG_RSCRATCH2
	mfspr	r11,SPRN_SPRG_RSCRATCH1
	mfspr	r10,SPRN_SPRG_RSCRATCH0
	b	DataStorage

	/* Instruction TLB Error Interrupt */
	/*
	 * Nearly the same as above, except we get our
	 * information from different registers and bailout
	 * to a different point.
	 */
	START_EXCEPTION(InstructionTLBError47x)
	mtspr	SPRN_SPRG_WSCRATCH0,r10	/* Save some working registers */
	mtspr	SPRN_SPRG_WSCRATCH1,r11
	mtspr	SPRN_SPRG_WSCRATCH2,r12
	mtspr	SPRN_SPRG_WSCRATCH3,r13
	mfcr	r11
	mtspr	SPRN_SPRG_WSCRATCH4,r11
	mfspr	r10,SPRN_SRR0		/* Get faulting address */

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	lis	r11,PAGE_OFFSET@h
	cmplw	cr0,r10,r11
	blt+	3f
	lis	r11,swapper_pg_dir@h
	ori	r11,r11, swapper_pg_dir@l
	li	r12,0			/* MMUCR = 0 */
	b	4f

	/* Get the PGD for the current thread and setup MMUCR */
3:	mfspr	r11,SPRN_SPRG_THREAD
	lwz	r11,PGDIR(r11)
	mfspr   r12,SPRN_PID		/* Get PID */
4:	mtspr	SPRN_MMUCR,r12		/* Set MMUCR */

	/* Make up the required permissions */
	li	r13,_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_EXEC

	/* Load PTE */
	/* Compute pgdir/pmd offset */
	rlwinm  r12,r10,PPC44x_PGD_OFF_SHIFT,PPC44x_PGD_OFF_MASK_BIT,29
	lwzx	r11,r12,r11		/* Get pgd/pmd entry */

	/* Word 0 is EPN,V,TS,DSIZ */
	li	r12,PPC47x_TLB0_VALID | PPC47x_TLBE_SIZE
	rlwimi	r10,r12,0,32-PAGE_SHIFT,31	/* Insert valid and page size*/
	li	r12,0
	tlbwe	r10,r12,0

	/* XXX can we do better ? Need to make sure tlbwe has established
	 * latch V bit in MMUCR0 before the PTE is loaded further down */
#ifdef CONFIG_SMP
	isync
#endif

	rlwinm.	r12,r11,0,0,20		/* Extract pt base address */
	/* Compute pte address */
	rlwimi  r12,r10,PPC44x_PTE_ADD_SHIFT,PPC44x_PTE_ADD_MASK_BIT,28
	beq	2f			/* Bail if no table */

	lwz	r11,0(r12)		/* Get high word of pte entry */
	/* XXX can we do better ? maybe insert a known 0 bit from r11 into the
	 * bottom of r12 to create a data dependency... We can also use r10
	 * as destination nowadays
	 */
#ifdef CONFIG_SMP
	lwsync
#endif
	lwz	r12,4(r12)		/* Get low word of pte entry */

	andc.	r13,r13,r12		/* Check permission */

	/* Jump to common TLB load point */
	beq	finish_tlb_load_47x

2:	/* The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
	mfspr	r11, SPRN_SPRG_RSCRATCH4
	mtcr	r11
	mfspr	r13, SPRN_SPRG_RSCRATCH3
	mfspr	r12, SPRN_SPRG_RSCRATCH2
	mfspr	r11, SPRN_SPRG_RSCRATCH1
	mfspr	r10, SPRN_SPRG_RSCRATCH0
	b	InstructionStorage

/*
 * Both the instruction and data TLB miss get to this
 * point to load the TLB.
 * 	r10 - free to use
 * 	r11 - PTE high word value
 *	r12 - PTE low word value
 *      r13 - free to use
 *	MMUCR - loaded with proper value when we get here
 *	Upon exit, we reload everything and RFI.
 */
finish_tlb_load_47x:
	/* Combine RPN & ERPN an write WS 1 */
	rlwimi	r11,r12,0,0,31-PAGE_SHIFT
	tlbwe	r11,r13,1

	/* And make up word 2 */
	li	r10,0xf85			/* Mask to apply from PTE */
	rlwimi	r10,r12,29,30,30		/* DIRTY -> SW position */
	and	r11,r12,r10			/* Mask PTE bits to keep */
	andi.	r10,r12,_PAGE_USER		/* User page ? */
	beq	1f				/* nope, leave U bits empty */
	rlwimi	r11,r11,3,26,28			/* yes, copy S bits to U */
1:	tlbwe	r11,r13,2

	/* Done...restore registers and get out of here.
	*/
	mfspr	r11, SPRN_SPRG_RSCRATCH4
	mtcr	r11
	mfspr	r13, SPRN_SPRG_RSCRATCH3
	mfspr	r12, SPRN_SPRG_RSCRATCH2
	mfspr	r11, SPRN_SPRG_RSCRATCH1
	mfspr	r10, SPRN_SPRG_RSCRATCH0
	rfi

#endif /* CONFIG_PPC_47x */

	/* Debug Interrupt */
	/*
	 * This statement needs to exist at the end of the IVPR
	 * definition just in case you end up taking a debug
	 * exception within another exception.
	 */
	DEBUG_CRIT_EXCEPTION

interrupt_end:

/*
 * Global functions
 */

/*
 * Adjust the machine check IVOR on 440A cores
 */
_GLOBAL(__fixup_440A_mcheck)
	li	r3,MachineCheckA@l
	mtspr	SPRN_IVOR1,r3
	sync
	blr

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
 * Init CPU state. This is called at boot time or for secondary CPUs
 * to setup initial TLB entries, setup IVORs, etc...
 *
 */
_GLOBAL(init_cpu_state)
	mflr	r22
#ifdef CONFIG_PPC_47x
	/* We use the PVR to differentiate 44x cores from 476 */
	mfspr	r3,SPRN_PVR
	srwi	r3,r3,16
	cmplwi	cr0,r3,PVR_476FPE@h
	beq	head_start_47x
	cmplwi	cr0,r3,PVR_476@h
	beq	head_start_47x
	cmplwi	cr0,r3,PVR_476_ISS@h
	beq	head_start_47x
#endif /* CONFIG_PPC_47x */

/*
 * In case the firmware didn't do it, we apply some workarounds
 * that are good for all 440 core variants here
 */
	mfspr	r3,SPRN_CCR0
	rlwinm	r3,r3,0,0,27	/* disable icache prefetch */
	isync
	mtspr	SPRN_CCR0,r3
	isync
	sync

/*
 * Set up the initial MMU state for 44x
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
#ifdef CONFIG_NONSTATIC_KERNEL
	/*
	 * In case of a NONSTATIC_KERNEL we reuse the TLB XLAT
	 * entries of the initial mapping set by the boot loader.
	 * The XLAT entry is stored in r25
	 */

	/* Read the XLAT entry for our current mapping */
	tlbre	r25,r23,PPC44x_TLB_XLAT

	lis	r3,KERNELBASE@h
	ori	r3,r3,KERNELBASE@l

	/* Use our current RPN entry */
	mr	r4,r25
#else

	lis	r3,PAGE_OFFSET@h
	ori	r3,r3,PAGE_OFFSET@l

	/* Kernel is at the base of RAM */
	li r4, 0			/* Load the kernel physical address */
#endif

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
#ifdef CONFIG_PPC_EARLY_DEBUG_44x
	/* Add UART mapping for early debug. */

	/* pageid fields */
	lis	r3,PPC44x_EARLY_DEBUG_VIRTADDR@h
	ori	r3,r3,PPC44x_TLB_VALID|PPC44x_TLB_TS|PPC44x_TLB_64K

	/* xlat fields */
	lis	r4,CONFIG_PPC_EARLY_DEBUG_44x_PHYSLOW@h
	ori	r4,r4,CONFIG_PPC_EARLY_DEBUG_44x_PHYSHIGH

	/* attrib fields */
	li	r5,(PPC44x_TLB_SW|PPC44x_TLB_SR|PPC44x_TLB_I|PPC44x_TLB_G)
        li      r0,62                    /* TLB slot 0 */

	tlbwe	r3,r0,PPC44x_TLB_PAGEID
	tlbwe	r4,r0,PPC44x_TLB_XLAT
	tlbwe	r5,r0,PPC44x_TLB_ATTRIB

	/* Force context change */
	isync
#endif /* CONFIG_PPC_EARLY_DEBUG_44x */

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
	SET_IVOR(13, DataTLBError44x);
	SET_IVOR(14, InstructionTLBError44x);
	SET_IVOR(15, DebugCrit);

	b	head_start_common


#ifdef CONFIG_PPC_47x

#ifdef CONFIG_SMP

/* Entry point for secondary 47x processors */
_GLOBAL(start_secondary_47x)
        mr      r24,r3          /* CPU number */

	bl	init_cpu_state

	/* Now we need to bolt the rest of kernel memory which
	 * is done in C code. We must be careful because our task
	 * struct or our stack can (and will probably) be out
	 * of reach of the initial 256M TLB entry, so we use a
	 * small temporary stack in .bss for that. This works
	 * because only one CPU at a time can be in this code
	 */
	lis	r1,temp_boot_stack@h
	ori	r1,r1,temp_boot_stack@l
	addi	r1,r1,1024-STACK_FRAME_OVERHEAD
	li	r0,0
	stw	r0,0(r1)
	bl	mmu_init_secondary

	/* Now we can get our task struct and real stack pointer */

	/* Get current's stack and current */
	lis	r2,secondary_current@ha
	lwz	r2,secondary_current@l(r2)
	lwz	r1,TASK_STACK(r2)

	/* Current stack pointer */
	addi	r1,r1,THREAD_SIZE-STACK_FRAME_OVERHEAD
	li	r0,0
	stw	r0,0(r1)

	/* Kernel stack for exception entry in SPRG3 */
	addi	r4,r2,THREAD	/* init task's THREAD */
	mtspr	SPRN_SPRG3,r4

	b	start_secondary

#endif /* CONFIG_SMP */

/*
 * Set up the initial MMU state for 44x
 *
 * We are still executing code at the virtual address
 * mappings set by the firmware for the base of RAM.
 */

head_start_47x:
	/* Load our current PID->MMUCR TID and MSR IS->MMUCR STS */
	mfspr	r3,SPRN_PID			/* Get PID */
	mfmsr	r4				/* Get MSR */
	andi.	r4,r4,MSR_IS@l			/* TS=1? */
	beq	1f				/* If not, leave STS=0 */
	oris	r3,r3,PPC47x_MMUCR_STS@h	/* Set STS=1 */
1:	mtspr	SPRN_MMUCR,r3			/* Put MMUCR */
	sync

	/* Find the entry we are running from */
	bl	1f
1:	mflr	r23
	tlbsx	r23,0,r23
	tlbre	r24,r23,0
	tlbre	r25,r23,1
	tlbre	r26,r23,2

/*
 * Cleanup time
 */

	/* Initialize MMUCR */
	li	r5,0
	mtspr	SPRN_MMUCR,r5
	sync

clear_all_utlb_entries:

	#; Set initial values.

	addis		r3,0,0x8000
	addi		r4,0,0
	addi		r5,0,0
	b		clear_utlb_entry

	#; Align the loop to speed things up.

	.align		6

clear_utlb_entry:

	tlbwe		r4,r3,0
	tlbwe		r5,r3,1
	tlbwe		r5,r3,2
	addis		r3,r3,0x2000
	cmpwi		r3,0
	bne		clear_utlb_entry
	addis		r3,0,0x8000
	addis		r4,r4,0x100
	cmpwi		r4,0
	bne		clear_utlb_entry

	#; Restore original entry.

	oris	r23,r23,0x8000  /* specify the way */
	tlbwe		r24,r23,0
	tlbwe		r25,r23,1
	tlbwe		r26,r23,2

/*
 * Configure and load pinned entry into TLB for the kernel core
 */

	lis	r3,PAGE_OFFSET@h
	ori	r3,r3,PAGE_OFFSET@l

	/* Load the kernel PID = 0 */
	li	r0,0
	mtspr	SPRN_PID,r0
	sync

	/* Word 0 */
	clrrwi	r3,r3,12		/* Mask off the effective page number */
	ori	r3,r3,PPC47x_TLB0_VALID | PPC47x_TLB0_256M

	/* Word 1 - use r25.  RPN is the same as the original entry */

	/* Word 2 */
	li	r5,0
	ori	r5,r5,PPC47x_TLB2_S_RWX
#ifdef CONFIG_SMP
	ori	r5,r5,PPC47x_TLB2_M
#endif

	/* We write to way 0 and bolted 0 */
	lis	r0,0x8800
	tlbwe	r3,r0,0
	tlbwe	r25,r0,1
	tlbwe	r5,r0,2

/*
 * Configure SSPCR, ISPCR and USPCR for now to search everything, we can fix
 * them up later
 */
	LOAD_REG_IMMEDIATE(r3, 0x9abcdef0)
	mtspr	SPRN_SSPCR,r3
	mtspr	SPRN_USPCR,r3
	LOAD_REG_IMMEDIATE(r3, 0x12345670)
	mtspr	SPRN_ISPCR,r3

	/* Force context change */
	mfmsr	r0
	mtspr	SPRN_SRR1, r0
	lis	r0,3f@h
	ori	r0,r0,3f@l
	mtspr	SPRN_SRR0,r0
	sync
	rfi

	/* Invalidate original entry we used */
3:
	rlwinm	r24,r24,0,21,19 /* clear the "valid" bit */
	tlbwe	r24,r23,0
	addi	r24,0,0
	tlbwe	r24,r23,1
	tlbwe	r24,r23,2
	isync                   /* Clear out the shadow TLB entries */

#ifdef CONFIG_PPC_EARLY_DEBUG_44x
	/* Add UART mapping for early debug. */

	/* Word 0 */
	lis	r3,PPC44x_EARLY_DEBUG_VIRTADDR@h
	ori	r3,r3,PPC47x_TLB0_VALID | PPC47x_TLB0_TS | PPC47x_TLB0_1M

	/* Word 1 */
	lis	r4,CONFIG_PPC_EARLY_DEBUG_44x_PHYSLOW@h
	ori	r4,r4,CONFIG_PPC_EARLY_DEBUG_44x_PHYSHIGH

	/* Word 2 */
	li	r5,(PPC47x_TLB2_S_RW | PPC47x_TLB2_IMG)

	/* Bolted in way 0, bolt slot 5, we -hope- we don't hit the same
	 * congruence class as the kernel, we need to make sure of it at
	 * some point
	 */
        lis	r0,0x8d00
	tlbwe	r3,r0,0
	tlbwe	r4,r0,1
	tlbwe	r5,r0,2

	/* Force context change */
	isync
#endif /* CONFIG_PPC_EARLY_DEBUG_44x */

	/* Establish the interrupt vector offsets */
	SET_IVOR(0,  CriticalInput);
	SET_IVOR(1,  MachineCheckA);
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
	SET_IVOR(13, DataTLBError47x);
	SET_IVOR(14, InstructionTLBError47x);
	SET_IVOR(15, DebugCrit);

	/* We configure icbi to invalidate 128 bytes at a time since the
	 * current 32-bit kernel code isn't too happy with icache != dcache
	 * block size. We also disable the BTAC as this can cause errors
	 * in some circumstances (see IBM Erratum 47).
	 */
	mfspr	r3,SPRN_CCR0
	oris	r3,r3,0x0020
	ori	r3,r3,0x0040
	mtspr	SPRN_CCR0,r3
	isync

#endif /* CONFIG_PPC_47x */

/*
 * Here we are back to code that is common between 44x and 47x
 *
 * We proceed to further kernel initialization and return to the
 * main kernel entry
 */
head_start_common:
	/* Establish the interrupt vector base */
	lis	r4,interrupt_base@h	/* IVPR only uses the high 16-bits */
	mtspr	SPRN_IVPR,r4

	/*
	 * If the kernel was loaded at a non-zero 256 MB page, we need to
	 * mask off the most significant 4 bits to get the relative address
	 * from the start of physical memory
	 */
	rlwinm	r22,r22,0,4,31
	addis	r22,r22,PAGE_OFFSET@h
	mtlr	r22
	isync
	blr

/*
 * We put a few things here that have to be page-aligned. This stuff
 * goes at the beginning of the data segment, which is page-aligned.
 */
	.data
	.align	PAGE_SHIFT
	.globl	sdata
sdata:
	.globl	empty_zero_page
empty_zero_page:
	.space	PAGE_SIZE
EXPORT_SYMBOL(empty_zero_page)

/*
 * To support >32-bit physical addresses, we use an 8KB pgdir.
 */
	.globl	swapper_pg_dir
swapper_pg_dir:
	.space	PGD_TABLE_SIZE

/*
 * Room for two PTE pointers, usually the kernel and current user pointers
 * to their respective root page table.
 */
abatron_pteptrs:
	.space	8

#ifdef CONFIG_SMP
	.align	12
temp_boot_stack:
	.space	1024
#endif /* CONFIG_SMP */

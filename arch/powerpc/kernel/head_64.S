/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Rewritten by Cort Dougan (cort@cs.nmt.edu) for PReP
 *    Copyright (C) 1996 Cort Dougan <cort@cs.nmt.edu>
 *  Adapted for Power Macintosh by Paul Mackerras.
 *  Low-level exception handlers and MMU support
 *  rewritten by Paul Mackerras.
 *    Copyright (C) 1996 Paul Mackerras.
 *
 *  Adapted for 64bit PowerPC by Dave Engebretsen, Peter Bergner, and
 *    Mike Corrigan {engebret|bergner|mikejc}@us.ibm.com
 *
 *  This file contains the entry point for the 64-bit kernel along
 *  with some early initialization code common to all 64-bit powerpc
 *  variants.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/threads.h>
#include <asm/reg.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/bug.h>
#include <asm/cputable.h>
#include <asm/setup.h>
#include <asm/hvcall.h>
#include <asm/thread_info.h>
#include <asm/firmware.h>
#include <asm/page_64.h>
#include <asm/irqflags.h>
#include <asm/kvm_book3s_asm.h>
#include <asm/ptrace.h>
#include <asm/hw_irq.h>

/* The physical memory is laid out such that the secondary processor
 * spin code sits at 0x0000...0x00ff. On server, the vectors follow
 * using the layout described in exceptions-64s.S
 */

/*
 * Entering into this code we make the following assumptions:
 *
 *  For pSeries or server processors:
 *   1. The MMU is off & open firmware is running in real mode.
 *   2. The kernel is entered at __start
 * -or- For OPAL entry:
 *   1. The MMU is off, processor in HV mode, primary CPU enters at 0
 *      with device-tree in gpr3. We also get OPAL base in r8 and
 *	entry in r9 for debugging purposes
 *   2. Secondary processors enter at 0x60 with PIR in gpr3
 *
 *  For Book3E processors:
 *   1. The MMU is on running in AS0 in a state defined in ePAPR
 *   2. The kernel is entered at __start
 */

	.text
	.globl  _stext
_stext:
_GLOBAL(__start)
	/* NOP this out unconditionally */
BEGIN_FTR_SECTION
	FIXUP_ENDIAN
	b	.__start_initialization_multiplatform
END_FTR_SECTION(0, 1)

	/* Catch branch to 0 in real mode */
	trap

	/* Secondary processors spin on this value until it becomes nonzero.
	 * When it does it contains the real address of the descriptor
	 * of the function that the cpu should jump to to continue
	 * initialization.
	 */
	.balign 8
	.globl  __secondary_hold_spinloop
__secondary_hold_spinloop:
	.llong	0x0

	/* Secondary processors write this value with their cpu # */
	/* after they enter the spin loop immediately below.	  */
	.globl	__secondary_hold_acknowledge
__secondary_hold_acknowledge:
	.llong	0x0

#ifdef CONFIG_RELOCATABLE
	/* This flag is set to 1 by a loader if the kernel should run
	 * at the loaded address instead of the linked address.  This
	 * is used by kexec-tools to keep the the kdump kernel in the
	 * crash_kernel region.  The loader is responsible for
	 * observing the alignment requirement.
	 */
	/* Do not move this variable as kexec-tools knows about it. */
	. = 0x5c
	.globl	__run_at_load
__run_at_load:
	.long	0x72756e30	/* "run0" -- relocate to 0 by default */
#endif

	. = 0x60
/*
 * The following code is used to hold secondary processors
 * in a spin loop after they have entered the kernel, but
 * before the bulk of the kernel has been relocated.  This code
 * is relocated to physical address 0x60 before prom_init is run.
 * All of it must fit below the first exception vector at 0x100.
 * Use .globl here not _GLOBAL because we want __secondary_hold
 * to be the actual text address, not a descriptor.
 */
	.globl	__secondary_hold
__secondary_hold:
	FIXUP_ENDIAN
#ifndef CONFIG_PPC_BOOK3E
	mfmsr	r24
	ori	r24,r24,MSR_RI
	mtmsrd	r24			/* RI on */
#endif
	/* Grab our physical cpu number */
	mr	r24,r3
	/* stash r4 for book3e */
	mr	r25,r4

	/* Tell the master cpu we're here */
	/* Relocation is off & we are located at an address less */
	/* than 0x100, so only need to grab low order offset.    */
	std	r24,__secondary_hold_acknowledge-_stext(0)
	sync

	li	r26,0
#ifdef CONFIG_PPC_BOOK3E
	tovirt(r26,r26)
#endif
	/* All secondary cpus wait here until told to start. */
100:	ld	r4,__secondary_hold_spinloop-_stext(r26)
	cmpdi	0,r4,0
	beq	100b

#if defined(CONFIG_SMP) || defined(CONFIG_KEXEC)
#ifdef CONFIG_PPC_BOOK3E
	tovirt(r4,r4)
#endif
	ld	r4,0(r4)		/* deref function descriptor */
	mtctr	r4
	mr	r3,r24
	/*
	 * it may be the case that other platforms have r4 right to
	 * begin with, this gives us some safety in case it is not
	 */
#ifdef CONFIG_PPC_BOOK3E
	mr	r4,r25
#else
	li	r4,0
#endif
	/* Make sure that patched code is visible */
	isync
	bctr
#else
	BUG_OPCODE
#endif

/* This value is used to mark exception frames on the stack. */
	.section ".toc","aw"
exception_marker:
	.tc	ID_72656773_68657265[TC],0x7265677368657265
	.text

/*
 * On server, we include the exception vectors code here as it
 * relies on absolute addressing which is only possible within
 * this compilation unit
 */
#ifdef CONFIG_PPC_BOOK3S
#include "exceptions-64s.S"
#endif

_GLOBAL(generic_secondary_thread_init)
	mr	r24,r3

	/* turn on 64-bit mode */
	bl	.enable_64b_mode

	/* get a valid TOC pointer, wherever we're mapped at */
	bl	.relative_toc
	tovirt(r2,r2)

#ifdef CONFIG_PPC_BOOK3E
	/* Book3E initialization */
	mr	r3,r24
	bl	.book3e_secondary_thread_init
#endif
	b	generic_secondary_common_init

/*
 * On pSeries and most other platforms, secondary processors spin
 * in the following code.
 * At entry, r3 = this processor's number (physical cpu id)
 *
 * On Book3E, r4 = 1 to indicate that the initial TLB entry for
 * this core already exists (setup via some other mechanism such
 * as SCOM before entry).
 */
_GLOBAL(generic_secondary_smp_init)
	FIXUP_ENDIAN
	mr	r24,r3
	mr	r25,r4

	/* turn on 64-bit mode */
	bl	.enable_64b_mode

	/* get a valid TOC pointer, wherever we're mapped at */
	bl	.relative_toc
	tovirt(r2,r2)

#ifdef CONFIG_PPC_BOOK3E
	/* Book3E initialization */
	mr	r3,r24
	mr	r4,r25
	bl	.book3e_secondary_core_init
#endif

generic_secondary_common_init:
	/* Set up a paca value for this processor. Since we have the
	 * physical cpu id in r24, we need to search the pacas to find
	 * which logical id maps to our physical one.
	 */
	LOAD_REG_ADDR(r13, paca)	/* Load paca pointer		 */
	ld	r13,0(r13)		/* Get base vaddr of paca array	 */
#ifndef CONFIG_SMP
	addi	r13,r13,PACA_SIZE	/* know r13 if used accidentally */
	b	.kexec_wait		/* wait for next kernel if !SMP	 */
#else
	LOAD_REG_ADDR(r7, nr_cpu_ids)	/* Load nr_cpu_ids address       */
	lwz	r7,0(r7)		/* also the max paca allocated 	 */
	li	r5,0			/* logical cpu id                */
1:	lhz	r6,PACAHWCPUID(r13)	/* Load HW procid from paca      */
	cmpw	r6,r24			/* Compare to our id             */
	beq	2f
	addi	r13,r13,PACA_SIZE	/* Loop to next PACA on miss     */
	addi	r5,r5,1
	cmpw	r5,r7			/* Check if more pacas exist     */
	blt	1b

	mr	r3,r24			/* not found, copy phys to r3	 */
	b	.kexec_wait		/* next kernel might do better	 */

2:	SET_PACA(r13)
#ifdef CONFIG_PPC_BOOK3E
	addi	r12,r13,PACA_EXTLB	/* and TLB exc frame in another  */
	mtspr	SPRN_SPRG_TLB_EXFRAME,r12
#endif

	/* From now on, r24 is expected to be logical cpuid */
	mr	r24,r5

	/* See if we need to call a cpu state restore handler */
	LOAD_REG_ADDR(r23, cur_cpu_spec)
	ld	r23,0(r23)
	ld	r23,CPU_SPEC_RESTORE(r23)
	cmpdi	0,r23,0
	beq	3f
	ld	r23,0(r23)
	mtctr	r23
	bctrl

3:	LOAD_REG_ADDR(r3, spinning_secondaries) /* Decrement spinning_secondaries */
	lwarx	r4,0,r3
	subi	r4,r4,1
	stwcx.	r4,0,r3
	bne	3b
	isync

4:	HMT_LOW
	lbz	r23,PACAPROCSTART(r13)	/* Test if this processor should */
					/* start.			 */
	cmpwi	0,r23,0
	beq	4b			/* Loop until told to go	 */

	sync				/* order paca.run and cur_cpu_spec */
	isync				/* In case code patching happened */

	/* Create a temp kernel stack for use before relocation is on.	*/
	ld	r1,PACAEMERGSP(r13)
	subi	r1,r1,STACK_FRAME_OVERHEAD

	b	__secondary_start
#endif /* SMP */

/*
 * Turn the MMU off.
 * Assumes we're mapped EA == RA if the MMU is on.
 */
#ifdef CONFIG_PPC_BOOK3S
_STATIC(__mmu_off)
	mfmsr	r3
	andi.	r0,r3,MSR_IR|MSR_DR
	beqlr
	mflr	r4
	andc	r3,r3,r0
	mtspr	SPRN_SRR0,r4
	mtspr	SPRN_SRR1,r3
	sync
	rfid
	b	.	/* prevent speculative execution */
#endif


/*
 * Here is our main kernel entry point. We support currently 2 kind of entries
 * depending on the value of r5.
 *
 *   r5 != NULL -> OF entry, we go to prom_init, "legacy" parameter content
 *                 in r3...r7
 *   
 *   r5 == NULL -> kexec style entry. r3 is a physical pointer to the
 *                 DT block, r4 is a physical pointer to the kernel itself
 *
 */
_GLOBAL(__start_initialization_multiplatform)
	/* Make sure we are running in 64 bits mode */
	bl	.enable_64b_mode

	/* Get TOC pointer (current runtime address) */
	bl	.relative_toc

	/* find out where we are now */
	bcl	20,31,$+4
0:	mflr	r26			/* r26 = runtime addr here */
	addis	r26,r26,(_stext - 0b)@ha
	addi	r26,r26,(_stext - 0b)@l	/* current runtime base addr */

	/*
	 * Are we booted from a PROM Of-type client-interface ?
	 */
	cmpldi	cr0,r5,0
	beq	1f
	b	.__boot_from_prom		/* yes -> prom */
1:
	/* Save parameters */
	mr	r31,r3
	mr	r30,r4
#ifdef CONFIG_PPC_EARLY_DEBUG_OPAL
	/* Save OPAL entry */
	mr	r28,r8
	mr	r29,r9
#endif

#ifdef CONFIG_PPC_BOOK3E
	bl	.start_initialization_book3e
	b	.__after_prom_start
#else
	/* Setup some critical 970 SPRs before switching MMU off */
	mfspr	r0,SPRN_PVR
	srwi	r0,r0,16
	cmpwi	r0,0x39		/* 970 */
	beq	1f
	cmpwi	r0,0x3c		/* 970FX */
	beq	1f
	cmpwi	r0,0x44		/* 970MP */
	beq	1f
	cmpwi	r0,0x45		/* 970GX */
	bne	2f
1:	bl	.__cpu_preinit_ppc970
2:

	/* Switch off MMU if not already off */
	bl	.__mmu_off
	b	.__after_prom_start
#endif /* CONFIG_PPC_BOOK3E */

_INIT_STATIC(__boot_from_prom)
#ifdef CONFIG_PPC_OF_BOOT_TRAMPOLINE
	/* Save parameters */
	mr	r31,r3
	mr	r30,r4
	mr	r29,r5
	mr	r28,r6
	mr	r27,r7

	/*
	 * Align the stack to 16-byte boundary
	 * Depending on the size and layout of the ELF sections in the initial
	 * boot binary, the stack pointer may be unaligned on PowerMac
	 */
	rldicr	r1,r1,0,59

#ifdef CONFIG_RELOCATABLE
	/* Relocate code for where we are now */
	mr	r3,r26
	bl	.relocate
#endif

	/* Restore parameters */
	mr	r3,r31
	mr	r4,r30
	mr	r5,r29
	mr	r6,r28
	mr	r7,r27

	/* Do all of the interaction with OF client interface */
	mr	r8,r26
	bl	.prom_init
#endif /* #CONFIG_PPC_OF_BOOT_TRAMPOLINE */

	/* We never return. We also hit that trap if trying to boot
	 * from OF while CONFIG_PPC_OF_BOOT_TRAMPOLINE isn't selected */
	trap

_STATIC(__after_prom_start)
#ifdef CONFIG_RELOCATABLE
	/* process relocations for the final address of the kernel */
	lis	r25,PAGE_OFFSET@highest	/* compute virtual base of kernel */
	sldi	r25,r25,32
	lwz	r7,__run_at_load-_stext(r26)
	cmplwi	cr0,r7,1	/* flagged to stay where we are ? */
	bne	1f
	add	r25,r25,r26
1:	mr	r3,r25
	bl	.relocate
#endif

/*
 * We need to run with _stext at physical address PHYSICAL_START.
 * This will leave some code in the first 256B of
 * real memory, which are reserved for software use.
 *
 * Note: This process overwrites the OF exception vectors.
 */
	li	r3,0			/* target addr */
#ifdef CONFIG_PPC_BOOK3E
	tovirt(r3,r3)			/* on booke, we already run at PAGE_OFFSET */
#endif
	mr.	r4,r26			/* In some cases the loader may  */
	beq	9f			/* have already put us at zero */
	li	r6,0x100		/* Start offset, the first 0x100 */
					/* bytes were copied earlier.	 */
#ifdef CONFIG_PPC_BOOK3E
	tovirt(r6,r6)			/* on booke, we already run at PAGE_OFFSET */
#endif

#ifdef CONFIG_RELOCATABLE
/*
 * Check if the kernel has to be running as relocatable kernel based on the
 * variable __run_at_load, if it is set the kernel is treated as relocatable
 * kernel, otherwise it will be moved to PHYSICAL_START
 */
	lwz	r7,__run_at_load-_stext(r26)
	cmplwi	cr0,r7,1
	bne	3f

	/* just copy interrupts */
	LOAD_REG_IMMEDIATE(r5, __end_interrupts - _stext)
	b	5f
3:
#endif
	lis	r5,(copy_to_here - _stext)@ha
	addi	r5,r5,(copy_to_here - _stext)@l /* # bytes of memory to copy */

	bl	.copy_and_flush		/* copy the first n bytes	 */
					/* this includes the code being	 */
					/* executed here.		 */
	addis	r8,r3,(4f - _stext)@ha	/* Jump to the copy of this code */
	addi	r8,r8,(4f - _stext)@l	/* that we just made */
	mtctr	r8
	bctr

.balign 8
p_end:	.llong	_end - _stext

4:	/* Now copy the rest of the kernel up to _end */
	addis	r5,r26,(p_end - _stext)@ha
	ld	r5,(p_end - _stext)@l(r5)	/* get _end */
5:	bl	.copy_and_flush		/* copy the rest */

9:	b	.start_here_multiplatform

/*
 * Copy routine used to copy the kernel to start at physical address 0
 * and flush and invalidate the caches as needed.
 * r3 = dest addr, r4 = source addr, r5 = copy limit, r6 = start offset
 * on exit, r3, r4, r5 are unchanged, r6 is updated to be >= r5.
 *
 * Note: this routine *only* clobbers r0, r6 and lr
 */
_GLOBAL(copy_and_flush)
	addi	r5,r5,-8
	addi	r6,r6,-8
4:	li	r0,8			/* Use the smallest common	*/
					/* denominator cache line	*/
					/* size.  This results in	*/
					/* extra cache line flushes	*/
					/* but operation is correct.	*/
					/* Can't get cache line size	*/
					/* from NACA as it is being	*/
					/* moved too.			*/

	mtctr	r0			/* put # words/line in ctr	*/
3:	addi	r6,r6,8			/* copy a cache line		*/
	ldx	r0,r6,r4
	stdx	r0,r6,r3
	bdnz	3b
	dcbst	r6,r3			/* write it to memory		*/
	sync
	icbi	r6,r3			/* flush the icache line	*/
	cmpld	0,r6,r5
	blt	4b
	sync
	addi	r5,r5,8
	addi	r6,r6,8
	isync
	blr

.align 8
copy_to_here:

#ifdef CONFIG_SMP
#ifdef CONFIG_PPC_PMAC
/*
 * On PowerMac, secondary processors starts from the reset vector, which
 * is temporarily turned into a call to one of the functions below.
 */
	.section ".text";
	.align 2 ;

	.globl	__secondary_start_pmac_0
__secondary_start_pmac_0:
	/* NB the entries for cpus 0, 1, 2 must each occupy 8 bytes. */
	li	r24,0
	b	1f
	li	r24,1
	b	1f
	li	r24,2
	b	1f
	li	r24,3
1:
	
_GLOBAL(pmac_secondary_start)
	/* turn on 64-bit mode */
	bl	.enable_64b_mode

	li	r0,0
	mfspr	r3,SPRN_HID4
	rldimi	r3,r0,40,23	/* clear bit 23 (rm_ci) */
	sync
	mtspr	SPRN_HID4,r3
	isync
	sync
	slbia

	/* get TOC pointer (real address) */
	bl	.relative_toc
	tovirt(r2,r2)

	/* Copy some CPU settings from CPU 0 */
	bl	.__restore_cpu_ppc970

	/* pSeries do that early though I don't think we really need it */
	mfmsr	r3
	ori	r3,r3,MSR_RI
	mtmsrd	r3			/* RI on */

	/* Set up a paca value for this processor. */
	LOAD_REG_ADDR(r4,paca)		/* Load paca pointer		*/
	ld	r4,0(r4)		/* Get base vaddr of paca array	*/
	mulli	r13,r24,PACA_SIZE	/* Calculate vaddr of right paca */
	add	r13,r13,r4		/* for this processor.		*/
	SET_PACA(r13)			/* Save vaddr of paca in an SPRG*/

	/* Mark interrupts soft and hard disabled (they might be enabled
	 * in the PACA when doing hotplug)
	 */
	li	r0,0
	stb	r0,PACASOFTIRQEN(r13)
	li	r0,PACA_IRQ_HARD_DIS
	stb	r0,PACAIRQHAPPENED(r13)

	/* Create a temp kernel stack for use before relocation is on.	*/
	ld	r1,PACAEMERGSP(r13)
	subi	r1,r1,STACK_FRAME_OVERHEAD

	b	__secondary_start

#endif /* CONFIG_PPC_PMAC */

/*
 * This function is called after the master CPU has released the
 * secondary processors.  The execution environment is relocation off.
 * The paca for this processor has the following fields initialized at
 * this point:
 *   1. Processor number
 *   2. Segment table pointer (virtual address)
 * On entry the following are set:
 *   r1	       = stack pointer (real addr of temp stack)
 *   r24       = cpu# (in Linux terms)
 *   r13       = paca virtual address
 *   SPRG_PACA = paca virtual address
 */
	.section ".text";
	.align 2 ;

	.globl	__secondary_start
__secondary_start:
	/* Set thread priority to MEDIUM */
	HMT_MEDIUM

	/* Initialize the kernel stack */
	LOAD_REG_ADDR(r3, current_set)
	sldi	r28,r24,3		/* get current_set[cpu#]	 */
	ldx	r14,r3,r28
	addi	r14,r14,THREAD_SIZE-STACK_FRAME_OVERHEAD
	std	r14,PACAKSAVE(r13)

	/* Do early setup for that CPU (stab, slb, hash table pointer) */
	bl	.early_setup_secondary

	/*
	 * setup the new stack pointer, but *don't* use this until
	 * translation is on.
	 */
	mr	r1, r14

	/* Clear backchain so we get nice backtraces */
	li	r7,0
	mtlr	r7

	/* Mark interrupts soft and hard disabled (they might be enabled
	 * in the PACA when doing hotplug)
	 */
	stb	r7,PACASOFTIRQEN(r13)
	li	r0,PACA_IRQ_HARD_DIS
	stb	r0,PACAIRQHAPPENED(r13)

	/* enable MMU and jump to start_secondary */
	LOAD_REG_ADDR(r3, .start_secondary_prolog)
	LOAD_REG_IMMEDIATE(r4, MSR_KERNEL)

	mtspr	SPRN_SRR0,r3
	mtspr	SPRN_SRR1,r4
	RFI
	b	.	/* prevent speculative execution */

/* 
 * Running with relocation on at this point.  All we want to do is
 * zero the stack back-chain pointer and get the TOC virtual address
 * before going into C code.
 */
_GLOBAL(start_secondary_prolog)
	ld	r2,PACATOC(r13)
	li	r3,0
	std	r3,0(r1)		/* Zero the stack frame pointer	*/
	bl	.start_secondary
	b	.
/*
 * Reset stack pointer and call start_secondary
 * to continue with online operation when woken up
 * from cede in cpu offline.
 */
_GLOBAL(start_secondary_resume)
	ld	r1,PACAKSAVE(r13)	/* Reload kernel stack pointer */
	li	r3,0
	std	r3,0(r1)		/* Zero the stack frame pointer	*/
	bl	.start_secondary
	b	.
#endif

/*
 * This subroutine clobbers r11 and r12
 */
_GLOBAL(enable_64b_mode)
	mfmsr	r11			/* grab the current MSR */
#ifdef CONFIG_PPC_BOOK3E
	oris	r11,r11,0x8000		/* CM bit set, we'll set ICM later */
	mtmsr	r11
#else /* CONFIG_PPC_BOOK3E */
	li	r12,(MSR_64BIT | MSR_ISF)@highest
	sldi	r12,r12,48
	or	r11,r11,r12
	mtmsrd	r11
	isync
#endif
	blr

/*
 * This puts the TOC pointer into r2, offset by 0x8000 (as expected
 * by the toolchain).  It computes the correct value for wherever we
 * are running at the moment, using position-independent code.
 *
 * Note: The compiler constructs pointers using offsets from the
 * TOC in -mcmodel=medium mode. After we relocate to 0 but before
 * the MMU is on we need our TOC to be a virtual address otherwise
 * these pointers will be real addresses which may get stored and
 * accessed later with the MMU on. We use tovirt() at the call
 * sites to handle this.
 */
_GLOBAL(relative_toc)
	mflr	r0
	bcl	20,31,$+4
0:	mflr	r11
	ld	r2,(p_toc - 0b)(r11)
	add	r2,r2,r11
	mtlr	r0
	blr

.balign 8
p_toc:	.llong	__toc_start + 0x8000 - 0b

/*
 * This is where the main kernel code starts.
 */
_INIT_STATIC(start_here_multiplatform)
	/* set up the TOC */
	bl      .relative_toc
	tovirt(r2,r2)

	/* Clear out the BSS. It may have been done in prom_init,
	 * already but that's irrelevant since prom_init will soon
	 * be detached from the kernel completely. Besides, we need
	 * to clear it now for kexec-style entry.
	 */
	LOAD_REG_ADDR(r11,__bss_stop)
	LOAD_REG_ADDR(r8,__bss_start)
	sub	r11,r11,r8		/* bss size			*/
	addi	r11,r11,7		/* round up to an even double word */
	srdi.	r11,r11,3		/* shift right by 3		*/
	beq	4f
	addi	r8,r8,-8
	li	r0,0
	mtctr	r11			/* zero this many doublewords	*/
3:	stdu	r0,8(r8)
	bdnz	3b
4:

#ifdef CONFIG_PPC_EARLY_DEBUG_OPAL
	/* Setup OPAL entry */
	LOAD_REG_ADDR(r11, opal)
	std	r28,0(r11);
	std	r29,8(r11);
#endif

#ifndef CONFIG_PPC_BOOK3E
	mfmsr	r6
	ori	r6,r6,MSR_RI
	mtmsrd	r6			/* RI on */
#endif

#ifdef CONFIG_RELOCATABLE
	/* Save the physical address we're running at in kernstart_addr */
	LOAD_REG_ADDR(r4, kernstart_addr)
	clrldi	r0,r25,2
	std	r0,0(r4)
#endif

	/* The following gets the stack set up with the regs */
	/* pointing to the real addr of the kernel stack.  This is   */
	/* all done to support the C function call below which sets  */
	/* up the htab.  This is done because we have relocated the  */
	/* kernel but are still running in real mode. */

	LOAD_REG_ADDR(r3,init_thread_union)

	/* set up a stack pointer */
	addi	r1,r3,THREAD_SIZE
	li	r0,0
	stdu	r0,-STACK_FRAME_OVERHEAD(r1)

	/* Do very early kernel initializations, including initial hash table,
	 * stab and slb setup before we turn on relocation.	*/

	/* Restore parameters passed from prom_init/kexec */
	mr	r3,r31
	bl	.early_setup		/* also sets r13 and SPRG_PACA */

	LOAD_REG_ADDR(r3, .start_here_common)
	ld	r4,PACAKMSR(r13)
	mtspr	SPRN_SRR0,r3
	mtspr	SPRN_SRR1,r4
	RFI
	b	.	/* prevent speculative execution */
	
	/* This is where all platforms converge execution */
_INIT_GLOBAL(start_here_common)
	/* relocation is on at this point */
	std	r1,PACAKSAVE(r13)

	/* Load the TOC (virtual address) */
	ld	r2,PACATOC(r13)

	/* Do more system initializations in virtual mode */
	bl	.setup_system

	/* Mark interrupts soft and hard disabled (they might be enabled
	 * in the PACA when doing hotplug)
	 */
	li	r0,0
	stb	r0,PACASOFTIRQEN(r13)
	li	r0,PACA_IRQ_HARD_DIS
	stb	r0,PACAIRQHAPPENED(r13)

	/* Generic kernel entry */
	bl	.start_kernel

	/* Not reached */
	BUG_OPCODE

/*
 * We put a few things here that have to be page-aligned.
 * This stuff goes at the beginning of the bss, which is page-aligned.
 */
	.section ".bss"

	.align	PAGE_SHIFT

	.globl	empty_zero_page
empty_zero_page:
	.space	PAGE_SIZE

	.globl	swapper_pg_dir
swapper_pg_dir:
	.space	PGD_TABLE_SIZE

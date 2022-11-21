/* SPDX-License-Identifier: GPL-2.0-or-later */
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
 */

#include <linux/init.h>
#include <linux/threads.h>
#include <linux/pgtable.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/cache.h>
#include <asm/ptrace.h>
#include <asm/export.h>
#include <asm/feature-fixups.h>
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
_GLOBAL(_stext);
_GLOBAL(_start);
	/*
	 * Reserve a word at a fixed location to store the address
	 * of abatron_pteptrs
	 */
	nop

	/* Translate device tree address to physical, save in r30/r31 */
	bl	get_phys_addr
	mr	r30,r3
	mr	r31,r4

	li	r25,0			/* phys kernel start (low) */
	li	r24,0			/* CPU number */
	li	r23,0			/* phys kernel start (high) */

#ifdef CONFIG_RELOCATABLE
	LOAD_REG_ADDR_PIC(r3, _stext)	/* Get our current runtime base */

	/* Translate _stext address to physical, save in r23/r25 */
	bl	get_phys_addr
	mr	r23,r3
	mr	r25,r4

	bcl	20,31,$+4
0:	mflr	r8
	addis	r3,r8,(is_second_reloc - 0b)@ha
	lwz	r19,(is_second_reloc - 0b)@l(r3)

	/* Check if this is the second relocation. */
	cmpwi	r19,1
	bne	1f

	/*
	 * For the second relocation, we already get the real memstart_addr
	 * from device tree. So we will map PAGE_OFFSET to memstart_addr,
	 * then the virtual address of start kernel should be:
	 *          PAGE_OFFSET + (kernstart_addr - memstart_addr)
	 * Since the offset between kernstart_addr and memstart_addr should
	 * never be beyond 1G, so we can just use the lower 32bit of them
	 * for the calculation.
	 */
	lis	r3,PAGE_OFFSET@h

	addis	r4,r8,(kernstart_addr - 0b)@ha
	addi	r4,r4,(kernstart_addr - 0b)@l
	lwz	r5,4(r4)

	addis	r6,r8,(memstart_addr - 0b)@ha
	addi	r6,r6,(memstart_addr - 0b)@l
	lwz	r7,4(r6)

	subf	r5,r7,r5
	add	r3,r3,r5
	b	2f

1:
	/*
	 * We have the runtime (virtual) address of our base.
	 * We calculate our shift of offset from a 64M page.
	 * We could map the 64M page we belong to at PAGE_OFFSET and
	 * get going from there.
	 */
	lis	r4,KERNELBASE@h
	ori	r4,r4,KERNELBASE@l
	rlwinm	r6,r25,0,0x3ffffff		/* r6 = PHYS_START % 64M */
	rlwinm	r5,r4,0,0x3ffffff		/* r5 = KERNELBASE % 64M */
	subf	r3,r5,r6			/* r3 = r6 - r5 */
	add	r3,r4,r3			/* Required Virtual Address */

2:	bl	relocate

	/*
	 * For the second relocation, we already set the right tlb entries
	 * for the kernel space, so skip the code in fsl_booke_entry_mapping.S
	*/
	cmpwi	r19,1
	beq	set_ivor
#endif

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

_GLOBAL(__early_start)
	LOAD_REG_ADDR_PIC(r20, kernstart_virt_addr)
	lwz     r20,0(r20)

#define ENTRY_MAPPING_BOOT_SETUP
#include "fsl_booke_entry_mapping.S"
#undef ENTRY_MAPPING_BOOT_SETUP

set_ivor:
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
	mtspr	SPRN_MAS4, r2

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
	LOAD_REG_ADDR_PIC(r24, boot_cpuid)
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

#ifdef CONFIG_SMP
	stw	r24, TASK_CPU(r2)
#endif

	bl	early_init

#ifdef CONFIG_KASAN
	bl	kasan_early_init
#endif
#ifdef CONFIG_RELOCATABLE
	mr	r3,r30
	mr	r4,r31
#ifdef CONFIG_PHYS_64BIT
	mr	r5,r23
	mr	r6,r25
#else
	mr	r5,r25
#endif
	bl	relocate_init
#endif

#ifdef CONFIG_DYNAMIC_MEMSTART
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
	mr	r3,r30
	mr	r4,r31
	bl	machine_init
	bl	MMU_init

	/* Setup PTE pointers for the Abatron bdiGDB */
	lis	r6, swapper_pg_dir@h
	ori	r6, r6, swapper_pg_dir@l
	lis	r5, abatron_pteptrs@h
	ori	r5, r5, abatron_pteptrs@l
	lis     r3, kernstart_virt_addr@ha
	lwz     r4, kernstart_virt_addr@l(r3)
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
 *   r10 is the pshift from the PGD, if we're a hugepage
 */
#ifdef CONFIG_PTE_64BIT
#ifdef CONFIG_HUGETLB_PAGE
#define FIND_PTE	\
	rlwinm	r12, r10, 13, 19, 29;	/* Compute pgdir/pmd offset */	\
	lwzx	r11, r12, r11;		/* Get pgd/pmd entry */		\
	rlwinm.	r12, r11, 0, 0, 20;	/* Extract pt base address */	\
	blt	1000f;			/* Normal non-huge page */	\
	beq	2f;			/* Bail if no table */		\
	oris	r11, r11, PD_HUGE@h;	/* Put back address bit */	\
	andi.	r10, r11, HUGEPD_SHIFT_MASK@l; /* extract size field */	\
	xor	r12, r10, r11;		/* drop size bits from pointer */ \
	b	1001f;							\
1000:	rlwimi	r12, r10, 23, 20, 28;	/* Compute pte address */	\
	li	r10, 0;			/* clear r10 */			\
1001:	lwz	r11, 4(r12);		/* Get pte entry */
#else
#define FIND_PTE	\
	rlwinm	r12, r10, 13, 19, 29;	/* Compute pgdir/pmd offset */	\
	lwzx	r11, r12, r11;		/* Get pgd/pmd entry */		\
	rlwinm.	r12, r11, 0, 0, 20;	/* Extract pt base address */	\
	beq	2f;			/* Bail if no table */		\
	rlwimi	r12, r10, 23, 20, 28;	/* Compute pte address */	\
	lwz	r11, 4(r12);		/* Get pte entry */
#endif /* HUGEPAGE */
#else /* !PTE_64BIT */
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
	CRITICAL_EXCEPTION(0x0100, CRITICAL, CriticalInput, unknown_exception)

	/* Machine Check Interrupt */
	MCHECK_EXCEPTION(0x0200, MachineCheck, machine_check_exception)

	/* Data Storage Interrupt */
	START_EXCEPTION(DataStorage)
	NORMAL_EXCEPTION_PROLOG(0x300, DATA_STORAGE)
	mfspr	r5,SPRN_ESR		/* Grab the ESR, save it */
	stw	r5,_ESR(r11)
	mfspr	r4,SPRN_DEAR		/* Grab the DEAR, save it */
	stw	r4, _DEAR(r11)
	andis.	r10,r5,(ESR_ILK|ESR_DLK)@h
	bne	1f
	prepare_transfer_to_handler
	bl	do_page_fault
	b	interrupt_return
1:
	prepare_transfer_to_handler
	bl	CacheLockingException
	b	interrupt_return

	/* Instruction Storage Interrupt */
	INSTRUCTION_STORAGE_EXCEPTION

	/* External Input Interrupt */
	EXCEPTION(0x0500, EXTERNAL, ExternalInput, do_IRQ)

	/* Alignment Interrupt */
	ALIGNMENT_EXCEPTION

	/* Program Interrupt */
	PROGRAM_EXCEPTION

	/* Floating Point Unavailable Interrupt */
#ifdef CONFIG_PPC_FPU
	FP_UNAVAILABLE_EXCEPTION
#else
	EXCEPTION(0x0800, FP_UNAVAIL, FloatingPointUnavailable, unknown_exception)
#endif

	/* System Call Interrupt */
	START_EXCEPTION(SystemCall)
	SYSCALL_ENTRY   0xc00 BOOKE_INTERRUPT_SYSCALL SPRN_SRR1

	/* Auxiliary Processor Unavailable Interrupt */
	EXCEPTION(0x2900, AP_UNAVAIL, AuxillaryProcessorUnavailable, unknown_exception)

	/* Decrementer Interrupt */
	DECREMENTER_EXCEPTION

	/* Fixed Internal Timer Interrupt */
	/* TODO: Add FIT support */
	EXCEPTION(0x3100, FIT, FixedIntervalTimer, unknown_exception)

	/* Watchdog Timer Interrupt */
#ifdef CONFIG_BOOKE_WDT
	CRITICAL_EXCEPTION(0x3200, WATCHDOG, WatchdogTimer, WatchdogException)
#else
	CRITICAL_EXCEPTION(0x3200, WATCHDOG, WatchdogTimer, unknown_exception)
#endif

	/* Data TLB Error Interrupt */
	START_EXCEPTION(DataTLBError)
	mtspr	SPRN_SPRG_WSCRATCH0, r10 /* Save some working registers */
	mfspr	r10, SPRN_SPRG_THREAD
	stw	r11, THREAD_NORMSAVE(0)(r10)
#ifdef CONFIG_KVM_BOOKE_HV
BEGIN_FTR_SECTION
	mfspr	r11, SPRN_SRR1
END_FTR_SECTION_IFSET(CPU_FTR_EMB_HV)
#endif
	stw	r12, THREAD_NORMSAVE(1)(r10)
	stw	r13, THREAD_NORMSAVE(2)(r10)
	mfcr	r13
	stw	r13, THREAD_NORMSAVE(3)(r10)
	DO_KVM	BOOKE_INTERRUPT_DTLB_MISS SPRN_SRR1
START_BTB_FLUSH_SECTION
	mfspr r11, SPRN_SRR1
	andi. r10,r11,MSR_PR
	beq 1f
	BTB_FLUSH(r10)
1:
END_BTB_FLUSH_SECTION
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

#ifdef CONFIG_PPC_KUAP
	mfspr	r12, SPRN_MAS1
	rlwinm.	r12,r12,0,0x3fff0000
	beq	2f			/* KUAP fault */
#endif

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
	subf	r13,r11,r12		/* create false data dep */
	lwzx	r13,r11,r13		/* Get upper pte bits */
#else
	lwz	r13,0(r12)		/* Get upper pte bits */
#endif
#endif

	bne	2f			/* Bail if permission/valid mismatch */

	/* Jump to common tlb load */
	b	finish_tlb_load
2:
	/* The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
	mfspr	r10, SPRN_SPRG_THREAD
	lwz	r11, THREAD_NORMSAVE(3)(r10)
	mtcr	r11
	lwz	r13, THREAD_NORMSAVE(2)(r10)
	lwz	r12, THREAD_NORMSAVE(1)(r10)
	lwz	r11, THREAD_NORMSAVE(0)(r10)
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
	mfspr	r10, SPRN_SPRG_THREAD
	stw	r11, THREAD_NORMSAVE(0)(r10)
#ifdef CONFIG_KVM_BOOKE_HV
BEGIN_FTR_SECTION
	mfspr	r11, SPRN_SRR1
END_FTR_SECTION_IFSET(CPU_FTR_EMB_HV)
#endif
	stw	r12, THREAD_NORMSAVE(1)(r10)
	stw	r13, THREAD_NORMSAVE(2)(r10)
	mfcr	r13
	stw	r13, THREAD_NORMSAVE(3)(r10)
	DO_KVM	BOOKE_INTERRUPT_ITLB_MISS SPRN_SRR1
START_BTB_FLUSH_SECTION
	mfspr r11, SPRN_SRR1
	andi. r10,r11,MSR_PR
	beq 1f
	BTB_FLUSH(r10)
1:
END_BTB_FLUSH_SECTION

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

#ifdef CONFIG_PPC_KUAP
	mfspr	r12, SPRN_MAS1
	rlwinm.	r12,r12,0,0x3fff0000
	beq	2f			/* KUAP fault */
#endif

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
	subf	r13,r11,r12		/* create false data dep */
	lwzx	r13,r11,r13		/* Get upper pte bits */
#else
	lwz	r13,0(r12)		/* Get upper pte bits */
#endif
#endif

	bne	2f			/* Bail if permission mismatch */

	/* Jump to common TLB load point */
	b	finish_tlb_load

2:
	/* The bailout.  Restore registers to pre-exception conditions
	 * and call the heavyweights to help us out.
	 */
	mfspr	r10, SPRN_SPRG_THREAD
	lwz	r11, THREAD_NORMSAVE(3)(r10)
	mtcr	r11
	lwz	r13, THREAD_NORMSAVE(2)(r10)
	lwz	r12, THREAD_NORMSAVE(1)(r10)
	lwz	r11, THREAD_NORMSAVE(0)(r10)
	mfspr	r10, SPRN_SPRG_RSCRATCH0
	b	InstructionStorage

/* Define SPE handlers for e500v2 */
#ifdef CONFIG_SPE
	/* SPE Unavailable */
	START_EXCEPTION(SPEUnavailable)
	NORMAL_EXCEPTION_PROLOG(0x2010, SPE_UNAVAIL)
	beq	1f
	bl	load_up_spe
	b	fast_exception_return
1:	prepare_transfer_to_handler
	bl	KernelSPE
	b	interrupt_return
#elif defined(CONFIG_SPE_POSSIBLE)
	EXCEPTION(0x2020, SPE_UNAVAIL, SPEUnavailable, unknown_exception)
#endif /* CONFIG_SPE_POSSIBLE */

	/* SPE Floating Point Data */
#ifdef CONFIG_SPE
	START_EXCEPTION(SPEFloatingPointData)
	NORMAL_EXCEPTION_PROLOG(0x2030, SPE_FP_DATA)
	prepare_transfer_to_handler
	bl	SPEFloatingPointException
	REST_NVGPRS(r1)
	b	interrupt_return

	/* SPE Floating Point Round */
	START_EXCEPTION(SPEFloatingPointRound)
	NORMAL_EXCEPTION_PROLOG(0x2050, SPE_FP_ROUND)
	prepare_transfer_to_handler
	bl	SPEFloatingPointRoundException
	REST_NVGPRS(r1)
	b	interrupt_return
#elif defined(CONFIG_SPE_POSSIBLE)
	EXCEPTION(0x2040, SPE_FP_DATA, SPEFloatingPointData, unknown_exception)
	EXCEPTION(0x2050, SPE_FP_ROUND, SPEFloatingPointRound, unknown_exception)
#endif /* CONFIG_SPE_POSSIBLE */


	/* Performance Monitor */
	EXCEPTION(0x2060, PERFORMANCE_MONITOR, PerformanceMonitor, \
		  performance_monitor_exception)

	EXCEPTION(0x2070, DOORBELL, Doorbell, doorbell_exception)

	CRITICAL_EXCEPTION(0x2080, DOORBELL_CRITICAL, \
			   CriticalDoorbell, unknown_exception)

	/* Debug Interrupt */
	DEBUG_DEBUG_EXCEPTION
	DEBUG_CRIT_EXCEPTION

	GUEST_DOORBELL_EXCEPTION

	CRITICAL_EXCEPTION(0, GUEST_DBELL_CRIT, CriticalGuestDoorbell, \
			   unknown_exception)

	/* Hypercall */
	EXCEPTION(0, HV_SYSCALL, Hypercall, unknown_exception)

	/* Embedded Hypervisor Privilege */
	EXCEPTION(0, HV_PRIV, Ehvpriv, unknown_exception)

interrupt_end:

/*
 * Local functions
 */

/*
 * Both the instruction and data TLB miss get to this
 * point to load the TLB.
 *	r10 - tsize encoding (if HUGETLB_PAGE) or available to use
 *	r11 - TLB (info from Linux PTE)
 *	r12 - available to use
 *	r13 - upper bits of PTE (if PTE_64BIT) or available to use
 *	CR5 - results of addr >= PAGE_OFFSET
 *	MAS0, MAS1 - loaded with proper value when we get here
 *	MAS2, MAS3 - will need additional info from Linux PTE
 *	Upon exit, we reload everything and RFI.
 */
finish_tlb_load:
#ifdef CONFIG_HUGETLB_PAGE
	cmpwi	6, r10, 0			/* check for huge page */
	beq	6, finish_tlb_load_cont    	/* !huge */

	/* Alas, we need more scratch registers for hugepages */
	mfspr	r12, SPRN_SPRG_THREAD
	stw	r14, THREAD_NORMSAVE(4)(r12)
	stw	r15, THREAD_NORMSAVE(5)(r12)
	stw	r16, THREAD_NORMSAVE(6)(r12)
	stw	r17, THREAD_NORMSAVE(7)(r12)

	/* Get the next_tlbcam_idx percpu var */
#ifdef CONFIG_SMP
	lwz	r15, TASK_CPU-THREAD(r12)
	lis     r14, __per_cpu_offset@h
	ori     r14, r14, __per_cpu_offset@l
	rlwinm  r15, r15, 2, 0, 29
	lwzx    r16, r14, r15
#else
	li	r16, 0
#endif
	lis     r17, next_tlbcam_idx@h
	ori	r17, r17, next_tlbcam_idx@l
	add	r17, r17, r16			/* r17 = *next_tlbcam_idx */
	lwz     r15, 0(r17)			/* r15 = next_tlbcam_idx */

	lis	r14, MAS0_TLBSEL(1)@h		/* select TLB1 (TLBCAM) */
	rlwimi	r14, r15, 16, 4, 15		/* next_tlbcam_idx entry */
	mtspr	SPRN_MAS0, r14

	/* Extract TLB1CFG(NENTRY) */
	mfspr	r16, SPRN_TLB1CFG
	andi.	r16, r16, 0xfff

	/* Update next_tlbcam_idx, wrapping when necessary */
	addi	r15, r15, 1
	cmpw	r15, r16
	blt 	100f
	lis	r14, tlbcam_index@h
	ori	r14, r14, tlbcam_index@l
	lwz	r15, 0(r14)
100:	stw	r15, 0(r17)

	/*
	 * Calc MAS1_TSIZE from r10 (which has pshift encoded)
	 * tlb_enc = (pshift - 10).
	 */
	subi	r15, r10, 10
	mfspr	r16, SPRN_MAS1
	rlwimi	r16, r15, 7, 20, 24
	mtspr	SPRN_MAS1, r16

	/* copy the pshift for use later */
	mr	r14, r10

	/* fall through */

#endif /* CONFIG_HUGETLB_PAGE */

	/*
	 * We set execute, because we don't have the granularity to
	 * properly set this at the page level (Linux problem).
	 * Many of these bits are software only.  Bits we don't set
	 * here we (properly should) assume have the appropriate value.
	 */
finish_tlb_load_cont:
#ifdef CONFIG_PTE_64BIT
	rlwinm	r12, r11, 32-2, 26, 31	/* Move in perm bits */
	andi.	r10, r11, _PAGE_DIRTY
	bne	1f
	li	r10, MAS3_SW | MAS3_UW
	andc	r12, r12, r10
1:	rlwimi	r12, r13, 20, 0, 11	/* grab RPN[32:43] */
	rlwimi	r12, r11, 20, 12, 19	/* grab RPN[44:51] */
2:	mtspr	SPRN_MAS3, r12
BEGIN_MMU_FTR_SECTION
	srwi	r10, r13, 12		/* grab RPN[12:31] */
	mtspr	SPRN_MAS7, r10
END_MMU_FTR_SECTION_IFSET(MMU_FTR_BIG_PHYS)
#else
	li	r10, (_PAGE_EXEC | _PAGE_PRESENT)
	mr	r13, r11
	rlwimi	r10, r11, 31, 29, 29	/* extract _PAGE_DIRTY into SW */
	and	r12, r11, r10
	andi.	r10, r11, _PAGE_USER	/* Test for _PAGE_USER */
	slwi	r10, r12, 1
	or	r10, r10, r12
	rlwinm	r10, r10, 0, ~_PAGE_EXEC	/* Clear SX on user pages */
	iseleq	r12, r12, r10
	rlwimi	r13, r12, 0, 20, 31	/* Get RPN from PTE, merge w/ perms */
	mtspr	SPRN_MAS3, r13
#endif

	mfspr	r12, SPRN_MAS2
#ifdef CONFIG_PTE_64BIT
	rlwimi	r12, r11, 32-19, 27, 31	/* extract WIMGE from pte */
#else
	rlwimi	r12, r11, 26, 27, 31	/* extract WIMGE from pte */
#endif
#ifdef CONFIG_HUGETLB_PAGE
	beq	6, 3f			/* don't mask if page isn't huge */
	li	r13, 1
	slw	r13, r13, r14
	subi	r13, r13, 1
	rlwinm	r13, r13, 0, 0, 19	/* bottom bits used for WIMGE/etc */
	andc	r12, r12, r13		/* mask off ea bits within the page */
#endif
3:	mtspr	SPRN_MAS2, r12

tlb_write_entry:
	tlbwe

	/* Done...restore registers and get out of here.  */
	mfspr	r10, SPRN_SPRG_THREAD
#ifdef CONFIG_HUGETLB_PAGE
	beq	6, 8f /* skip restore for 4k page faults */
	lwz	r14, THREAD_NORMSAVE(4)(r10)
	lwz	r15, THREAD_NORMSAVE(5)(r10)
	lwz	r16, THREAD_NORMSAVE(6)(r10)
	lwz	r17, THREAD_NORMSAVE(7)(r10)
#endif
8:	lwz	r11, THREAD_NORMSAVE(3)(r10)
	mtcr	r11
	lwz	r13, THREAD_NORMSAVE(2)(r10)
	lwz	r12, THREAD_NORMSAVE(1)(r10)
	lwz	r11, THREAD_NORMSAVE(0)(r10)
	mfspr	r10, SPRN_SPRG_RSCRATCH0
	rfi					/* Force context change */

#ifdef CONFIG_SPE
/* Note that the SPE support is closely modeled after the AltiVec
 * support.  Changes to one are likely to be applicable to the
 * other!  */
_GLOBAL(load_up_spe)
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
	/* enable use of SPE after return */
	oris	r9,r9,MSR_SPE@h
	mfspr	r5,SPRN_SPRG_THREAD	/* current task's THREAD (phys) */
	li	r4,1
	li	r10,THREAD_ACC
	stw	r4,THREAD_USED_SPE(r5)
	evlddx	evr4,r10,r5
	evmra	evr4,evr4
	REST_32EVRS(0,r10,r5,THREAD_EVR0)
	blr

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
	bl	_printk
#endif
	b	interrupt_return
#ifdef CONFIG_PRINTK
87:	.string	"SPE used in kernel  (task=%p, pc=%x)  \n"
#endif
	.align	4,0

#endif /* CONFIG_SPE */

/*
 * Translate the effec addr in r3 to phys addr. The phys addr will be put
 * into r3(higher 32bit) and r4(lower 32bit)
 */
get_phys_addr:
	mfmsr	r8
	mfspr	r9,SPRN_PID
	rlwinm	r9,r9,16,0x3fff0000	/* turn PID into MAS6[SPID] */
	rlwimi	r9,r8,28,0x00000001	/* turn MSR[DS] into MAS6[SAS] */
	mtspr	SPRN_MAS6,r9

	tlbsx	0,r3			/* must succeed */

	mfspr	r8,SPRN_MAS1
	mfspr	r12,SPRN_MAS3
	rlwinm	r9,r8,25,0x1f		/* r9 = log2(page size) */
	li	r10,1024
	slw	r10,r10,r9		/* r10 = page size */
	addi	r10,r10,-1
	and	r11,r3,r10		/* r11 = page offset */
	andc	r4,r12,r10		/* r4 = page base */
	or	r4,r4,r11		/* r4 = devtree phys addr */
#ifdef CONFIG_PHYS_64BIT
	mfspr	r3,SPRN_MAS7
#endif
	blr

/*
 * Global functions
 */

#ifdef CONFIG_E500
#ifndef CONFIG_PPC_E500MC
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
#else
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

/* setup ehv ivors for */
_GLOBAL(__setup_ehv_ivors)
	li	r3,GuestDoorbell@l
	mtspr	SPRN_IVOR38,r3
	li	r3,CriticalGuestDoorbell@l
	mtspr	SPRN_IVOR39,r3
	li	r3,Hypercall@l
	mtspr	SPRN_IVOR40,r3
	li	r3,Ehvpriv@l
	mtspr	SPRN_IVOR41,r3
	sync
	blr
#endif /* CONFIG_PPC_E500MC */
#endif /* CONFIG_E500 */

#ifdef CONFIG_SPE
/*
 * extern void __giveup_spe(struct task_struct *prev)
 *
 */
_GLOBAL(__giveup_spe)
	addi	r3,r3,THREAD		/* want THREAD of task */
	lwz	r5,PT_REGS(r3)
	cmpi	0,r5,0
	SAVE_32EVRS(0, r4, r3, THREAD_EVR0)
	evxor	evr6, evr6, evr6	/* clear out evr6 */
	evmwumiaa evr6, evr6, evr6	/* evr6 <- ACC = 0 * 0 + ACC */
	li	r4,THREAD_ACC
	evstddx	evr6, r4, r3		/* save off accumulator */
	beq	1f
	lwz	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	lis	r3,MSR_SPE@h
	andc	r4,r4,r3		/* disable SPE for previous task */
	stw	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
	blr
#endif /* CONFIG_SPE */

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

#ifdef CONFIG_SMP
/* When we get here, r24 needs to hold the CPU # */
	.globl __secondary_start
__secondary_start:
	LOAD_REG_ADDR_PIC(r3, tlbcam_index)
	lwz	r3,0(r3)
	mtctr	r3
	li	r26,0		/* r26 safe? */

	bl	switch_to_as1
	mr	r27,r3		/* tlb entry */
	/* Load each CAM entry */
1:	mr	r3,r26
	bl	loadcam_entry
	addi	r26,r26,1
	bdnz	1b
	mr	r3,r27		/* tlb entry */
	LOAD_REG_ADDR_PIC(r4, memstart_addr)
	lwz	r4,0(r4)
	mr	r5,r25		/* phys kernel start */
	rlwinm	r5,r5,0,~0x3ffffff	/* aligned 64M */
	subf	r4,r5,r4	/* memstart_addr - phys kernel start */
	lis	r7,KERNELBASE@h
	ori	r7,r7,KERNELBASE@l
	cmpw	r20,r7		/* if kernstart_virt_addr != KERNELBASE, randomized */
	beq	2f
	li	r4,0
2:	li	r5,0		/* no device tree */
	li	r6,0		/* not boot cpu */
	bl	restore_to_as0


	lis	r3,__secondary_hold_acknowledge@h
	ori	r3,r3,__secondary_hold_acknowledge@l
	stw	r24,0(r3)

	li	r3,0
	mr	r4,r24		/* Why? */
	bl	call_setup_cpu

	/* get current's stack and current */
	lis	r2,secondary_current@ha
	lwz	r2,secondary_current@l(r2)
	lwz	r1,TASK_STACK(r2)

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
 * Create a 64M tlb by address and entry
 * r3 - entry
 * r4 - virtual address
 * r5/r6 - physical address
 */
_GLOBAL(create_kaslr_tlb_entry)
	lis     r7,0x1000               /* Set MAS0(TLBSEL) = 1 */
	rlwimi  r7,r3,16,4,15           /* Setup MAS0 = TLBSEL | ESEL(r6) */
	mtspr   SPRN_MAS0,r7            /* Write MAS0 */

	lis     r3,(MAS1_VALID|MAS1_IPROT)@h
	ori     r3,r3,(MAS1_TSIZE(BOOK3E_PAGESZ_64M))@l
	mtspr   SPRN_MAS1,r3            /* Write MAS1 */

	lis     r3,MAS2_EPN_MASK(BOOK3E_PAGESZ_64M)@h
	ori     r3,r3,MAS2_EPN_MASK(BOOK3E_PAGESZ_64M)@l
	and     r3,r3,r4
	ori	r3,r3,MAS2_M_IF_NEEDED@l
	mtspr   SPRN_MAS2,r3            /* Write MAS2(EPN) */

#ifdef CONFIG_PHYS_64BIT
	ori     r8,r6,(MAS3_SW|MAS3_SR|MAS3_SX)
	mtspr   SPRN_MAS3,r8            /* Write MAS3(RPN) */
	mtspr	SPRN_MAS7,r5
#else
	ori     r8,r5,(MAS3_SW|MAS3_SR|MAS3_SX)
	mtspr   SPRN_MAS3,r8            /* Write MAS3(RPN) */
#endif

	tlbwe                           /* Write TLB */
	isync
	sync
	blr

/*
 * Return to the start of the relocated kernel and run again
 * r3 - virtual address of fdt
 * r4 - entry of the kernel
 */
_GLOBAL(reloc_kernel_entry)
	mfmsr	r7
	rlwinm	r7, r7, 0, ~(MSR_IS | MSR_DS)

	mtspr	SPRN_SRR0,r4
	mtspr	SPRN_SRR1,r7
	rfi

/*
 * Create a tlb entry with the same effective and physical address as
 * the tlb entry used by the current running code. But set the TS to 1.
 * Then switch to the address space 1. It will return with the r3 set to
 * the ESEL of the new created tlb.
 */
_GLOBAL(switch_to_as1)
	mflr	r5

	/* Find a entry not used */
	mfspr	r3,SPRN_TLB1CFG
	andi.	r3,r3,0xfff
	mfspr	r4,SPRN_PID
	rlwinm	r4,r4,16,0x3fff0000	/* turn PID into MAS6[SPID] */
	mtspr	SPRN_MAS6,r4
1:	lis	r4,0x1000		/* Set MAS0(TLBSEL) = 1 */
	addi	r3,r3,-1
	rlwimi	r4,r3,16,4,15		/* Setup MAS0 = TLBSEL | ESEL(r3) */
	mtspr	SPRN_MAS0,r4
	tlbre
	mfspr	r4,SPRN_MAS1
	andis.	r4,r4,MAS1_VALID@h
	bne	1b

	/* Get the tlb entry used by the current running code */
	bcl	20,31,$+4
0:	mflr	r4
	tlbsx	0,r4

	mfspr	r4,SPRN_MAS1
	ori	r4,r4,MAS1_TS		/* Set the TS = 1 */
	mtspr	SPRN_MAS1,r4

	mfspr	r4,SPRN_MAS0
	rlwinm	r4,r4,0,~MAS0_ESEL_MASK
	rlwimi	r4,r3,16,4,15		/* Setup MAS0 = TLBSEL | ESEL(r3) */
	mtspr	SPRN_MAS0,r4
	tlbwe
	isync
	sync

	mfmsr	r4
	ori	r4,r4,MSR_IS | MSR_DS
	mtspr	SPRN_SRR0,r5
	mtspr	SPRN_SRR1,r4
	sync
	rfi

/*
 * Restore to the address space 0 and also invalidate the tlb entry created
 * by switch_to_as1.
 * r3 - the tlb entry which should be invalidated
 * r4 - __pa(PAGE_OFFSET in AS1) - __pa(PAGE_OFFSET in AS0)
 * r5 - device tree virtual address. If r4 is 0, r5 is ignored.
 * r6 - boot cpu
*/
_GLOBAL(restore_to_as0)
	mflr	r0

	bcl	20,31,$+4
0:	mflr	r9
	addi	r9,r9,1f - 0b

	/*
	 * We may map the PAGE_OFFSET in AS0 to a different physical address,
	 * so we need calculate the right jump and device tree address based
	 * on the offset passed by r4.
	 */
	add	r9,r9,r4
	add	r5,r5,r4
	add	r0,r0,r4

2:	mfmsr	r7
	li	r8,(MSR_IS | MSR_DS)
	andc	r7,r7,r8

	mtspr	SPRN_SRR0,r9
	mtspr	SPRN_SRR1,r7
	sync
	rfi

	/* Invalidate the temporary tlb entry for AS1 */
1:	lis	r9,0x1000		/* Set MAS0(TLBSEL) = 1 */
	rlwimi	r9,r3,16,4,15		/* Setup MAS0 = TLBSEL | ESEL(r3) */
	mtspr	SPRN_MAS0,r9
	tlbre
	mfspr	r9,SPRN_MAS1
	rlwinm	r9,r9,0,2,31		/* Clear MAS1 Valid and IPPROT */
	mtspr	SPRN_MAS1,r9
	tlbwe
	isync

	cmpwi	r4,0
	cmpwi	cr1,r6,0
	cror	eq,4*cr1+eq,eq
	bne	3f			/* offset != 0 && is_boot_cpu */
	mtlr	r0
	blr

	/*
	 * The PAGE_OFFSET will map to a different physical address,
	 * jump to _start to do another relocation again.
	*/
3:	mr	r3,r5
	bl	_start

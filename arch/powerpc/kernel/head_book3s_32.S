/* SPDX-License-Identifier: GPL-2.0-or-later */
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
 *  MPC8xx modifications Copyright (C) 1997 Dan Malek (dmalek@jlc.net).
 *
 *  This file contains the low-level support and setup for the
 *  PowerPC platform, including trap and interrupt dispatch.
 *  (The PPC 8xx embedded CPUs use head_8xx.S instead.)
 */

#include <linux/init.h>
#include <linux/pgtable.h>
#include <asm/reg.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/cputable.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ptrace.h>
#include <asm/bug.h>
#include <asm/kvm_book3s_asm.h>
#include <asm/export.h>
#include <asm/feature-fixups.h>
#include <asm/interrupt.h>

#include "head_32.h"

#define LOAD_BAT(n, reg, RA, RB)	\
	/* see the comment for clear_bats() -- Cort */ \
	li	RA,0;			\
	mtspr	SPRN_IBAT##n##U,RA;	\
	mtspr	SPRN_DBAT##n##U,RA;	\
	lwz	RA,(n*16)+0(reg);	\
	lwz	RB,(n*16)+4(reg);	\
	mtspr	SPRN_IBAT##n##U,RA;	\
	mtspr	SPRN_IBAT##n##L,RB;	\
	lwz	RA,(n*16)+8(reg);	\
	lwz	RB,(n*16)+12(reg);	\
	mtspr	SPRN_DBAT##n##U,RA;	\
	mtspr	SPRN_DBAT##n##L,RB

	__HEAD
_ENTRY(_stext);

/*
 * _start is defined this way because the XCOFF loader in the OpenFirmware
 * on the powermac expects the entry point to be a procedure descriptor.
 */
_ENTRY(_start);
	/*
	 * These are here for legacy reasons, the kernel used to
	 * need to look like a coff function entry for the pmac
	 * but we're always started by some kind of bootloader now.
	 *  -- Cort
	 */
	nop	/* used by __secondary_hold on prep (mtx) and chrp smp */
	nop	/* used by __secondary_hold on prep (mtx) and chrp smp */
	nop

/* PMAC
 * Enter here with the kernel text, data and bss loaded starting at
 * 0, running with virtual == physical mapping.
 * r5 points to the prom entry point (the client interface handler
 * address).  Address translation is turned on, with the prom
 * managing the hash table.  Interrupts are disabled.  The stack
 * pointer (r1) points to just below the end of the half-meg region
 * from 0x380000 - 0x400000, which is mapped in already.
 *
 * If we are booted from MacOS via BootX, we enter with the kernel
 * image loaded somewhere, and the following values in registers:
 *  r3: 'BooX' (0x426f6f58)
 *  r4: virtual address of boot_infos_t
 *  r5: 0
 *
 * PREP
 * This is jumped to on prep systems right after the kernel is relocated
 * to its proper place in memory by the boot loader.  The expected layout
 * of the regs is:
 *   r3: ptr to residual data
 *   r4: initrd_start or if no initrd then 0
 *   r5: initrd_end - unused if r4 is 0
 *   r6: Start of command line string
 *   r7: End of command line string
 *
 * This just gets a minimal mmu environment setup so we can call
 * start_here() to do the real work.
 * -- Cort
 */

	.globl	__start
__start:
/*
 * We have to do any OF calls before we map ourselves to KERNELBASE,
 * because OF may have I/O devices mapped into that area
 * (particularly on CHRP).
 */
	cmpwi	0,r5,0
	beq	1f

#ifdef CONFIG_PPC_OF_BOOT_TRAMPOLINE
	/* find out where we are now */
	bcl	20,31,$+4
0:	mflr	r8			/* r8 = runtime addr here */
	addis	r8,r8,(_stext - 0b)@ha
	addi	r8,r8,(_stext - 0b)@l	/* current runtime base addr */
	bl	prom_init
#endif /* CONFIG_PPC_OF_BOOT_TRAMPOLINE */

	/* We never return. We also hit that trap if trying to boot
	 * from OF while CONFIG_PPC_OF_BOOT_TRAMPOLINE isn't selected */
	trap

/*
 * Check for BootX signature when supporting PowerMac and branch to
 * appropriate trampoline if it's present
 */
#ifdef CONFIG_PPC_PMAC
1:	lis	r31,0x426f
	ori	r31,r31,0x6f58
	cmpw	0,r3,r31
	bne	1f
	bl	bootx_init
	trap
#endif /* CONFIG_PPC_PMAC */

1:	mr	r31,r3			/* save device tree ptr */
	li	r24,0			/* cpu # */

/*
 * early_init() does the early machine identification and does
 * the necessary low-level setup and clears the BSS
 *  -- Cort <cort@fsmlabs.com>
 */
	bl	early_init

/* Switch MMU off, clear BATs and flush TLB. At this point, r3 contains
 * the physical address we are running at, returned by early_init()
 */
 	bl	mmu_off
__after_mmu_off:
	bl	clear_bats
	bl	flush_tlbs

	bl	initial_bats
	bl	load_segment_registers
	bl	reloc_offset
	bl	early_hash_table
#if defined(CONFIG_BOOTX_TEXT)
	bl	setup_disp_bat
#endif
#ifdef CONFIG_PPC_EARLY_DEBUG_CPM
	bl	setup_cpm_bat
#endif
#ifdef CONFIG_PPC_EARLY_DEBUG_USBGECKO
	bl	setup_usbgecko_bat
#endif

/*
 * Call setup_cpu for CPU 0 and initialize 6xx Idle
 */
	bl	reloc_offset
	li	r24,0			/* cpu# */
	bl	call_setup_cpu		/* Call setup_cpu for this CPU */
	bl	reloc_offset
	bl	init_idle_6xx


/*
 * We need to run with _start at physical address 0.
 * On CHRP, we are loaded at 0x10000 since OF on CHRP uses
 * the exception vectors at 0 (and therefore this copy
 * overwrites OF's exception vectors with our own).
 * The MMU is off at this point.
 */
	bl	reloc_offset
	mr	r26,r3
	addis	r4,r3,KERNELBASE@h	/* current address of _start */
	lis	r5,PHYSICAL_START@h
	cmplw	0,r4,r5			/* already running at PHYSICAL_START? */
	bne	relocate_kernel
/*
 * we now have the 1st 16M of ram mapped with the bats.
 * prep needs the mmu to be turned on here, but pmac already has it on.
 * this shouldn't bother the pmac since it just gets turned on again
 * as we jump to our code at KERNELBASE. -- Cort
 * Actually no, pmac doesn't have it on any more. BootX enters with MMU
 * off, and in other cases, we now turn it off before changing BATs above.
 */
turn_on_mmu:
	mfmsr	r0
	ori	r0,r0,MSR_DR|MSR_IR|MSR_RI
	mtspr	SPRN_SRR1,r0
	lis	r0,start_here@h
	ori	r0,r0,start_here@l
	mtspr	SPRN_SRR0,r0
	rfi				/* enables MMU */

/*
 * We need __secondary_hold as a place to hold the other cpus on
 * an SMP machine, even when we are running a UP kernel.
 */
	. = 0xc0			/* for prep bootloader */
	li	r3,1			/* MTX only has 1 cpu */
	.globl	__secondary_hold
__secondary_hold:
	/* tell the master we're here */
	stw	r3,__secondary_hold_acknowledge@l(0)
#ifdef CONFIG_SMP
100:	lwz	r4,0(0)
	/* wait until we're told to start */
	cmpw	0,r4,r3
	bne	100b
	/* our cpu # was at addr 0 - go */
	mr	r24,r3			/* cpu # */
	b	__secondary_start
#else
	b	.
#endif /* CONFIG_SMP */

	.globl	__secondary_hold_spinloop
__secondary_hold_spinloop:
	.long	0
	.globl	__secondary_hold_acknowledge
__secondary_hold_acknowledge:
	.long	-1

/* System reset */
/* core99 pmac starts the seconary here by changing the vector, and
   putting it back to what it was (unknown_async_exception) when done.  */
	EXCEPTION(INTERRUPT_SYSTEM_RESET, Reset, unknown_async_exception)

/* Machine check */
/*
 * On CHRP, this is complicated by the fact that we could get a
 * machine check inside RTAS, and we have no guarantee that certain
 * critical registers will have the values we expect.  The set of
 * registers that might have bad values includes all the GPRs
 * and all the BATs.  We indicate that we are in RTAS by putting
 * a non-zero value, the address of the exception frame to use,
 * in thread.rtas_sp.  The machine check handler checks thread.rtas_sp
 * and uses its value if it is non-zero.
 * (Other exception handlers assume that r1 is a valid kernel stack
 * pointer when we take an exception from supervisor mode.)
 *	-- paulus.
 */
	START_EXCEPTION(INTERRUPT_MACHINE_CHECK, MachineCheck)
	EXCEPTION_PROLOG_0
#ifdef CONFIG_PPC_CHRP
	mtspr	SPRN_SPRG_SCRATCH2,r1
	mfspr	r1, SPRN_SPRG_THREAD
	lwz	r1, RTAS_SP(r1)
	cmpwi	cr1, r1, 0
	bne	cr1, 7f
	mfspr	r1, SPRN_SPRG_SCRATCH2
#endif /* CONFIG_PPC_CHRP */
	EXCEPTION_PROLOG_1
7:	EXCEPTION_PROLOG_2 0x200 MachineCheck
#ifdef CONFIG_PPC_CHRP
	beq	cr1, 1f
	twi	31, 0, 0
#endif
1:	prepare_transfer_to_handler
	bl	machine_check_exception
	b	interrupt_return

/* Data access exception. */
	START_EXCEPTION(INTERRUPT_DATA_STORAGE, DataAccess)
#ifdef CONFIG_PPC_BOOK3S_604
BEGIN_MMU_FTR_SECTION
	mtspr	SPRN_SPRG_SCRATCH2,r10
	mfspr	r10, SPRN_SPRG_THREAD
	stw	r11, THR11(r10)
	mfspr	r10, SPRN_DSISR
	mfcr	r11
	andis.	r10, r10, (DSISR_BAD_FAULT_32S | DSISR_DABRMATCH)@h
	mfspr	r10, SPRN_SPRG_THREAD
	beq	hash_page_dsi
.Lhash_page_dsi_cont:
	mtcr	r11
	lwz	r11, THR11(r10)
	mfspr	r10, SPRN_SPRG_SCRATCH2
MMU_FTR_SECTION_ELSE
	b	1f
ALT_MMU_FTR_SECTION_END_IFSET(MMU_FTR_HPTE_TABLE)
#endif
1:	EXCEPTION_PROLOG_0 handle_dar_dsisr=1
	EXCEPTION_PROLOG_1
	EXCEPTION_PROLOG_2 INTERRUPT_DATA_STORAGE DataAccess handle_dar_dsisr=1
	prepare_transfer_to_handler
	lwz	r5, _DSISR(r1)
	andis.	r0, r5, DSISR_DABRMATCH@h
	bne-	1f
	bl	do_page_fault
	b	interrupt_return
1:	bl	do_break
	REST_NVGPRS(r1)
	b	interrupt_return


/* Instruction access exception. */
	START_EXCEPTION(INTERRUPT_INST_STORAGE, InstructionAccess)
	mtspr	SPRN_SPRG_SCRATCH0,r10
	mtspr	SPRN_SPRG_SCRATCH1,r11
	mfspr	r10, SPRN_SPRG_THREAD
	mfspr	r11, SPRN_SRR0
	stw	r11, SRR0(r10)
	mfspr	r11, SPRN_SRR1		/* check whether user or kernel */
	stw	r11, SRR1(r10)
	mfcr	r10
#ifdef CONFIG_PPC_BOOK3S_604
BEGIN_MMU_FTR_SECTION
	andis.	r11, r11, SRR1_ISI_NOPT@h	/* no pte found? */
	bne	hash_page_isi
.Lhash_page_isi_cont:
	mfspr	r11, SPRN_SRR1		/* check whether user or kernel */
END_MMU_FTR_SECTION_IFSET(MMU_FTR_HPTE_TABLE)
#endif
	andi.	r11, r11, MSR_PR

	EXCEPTION_PROLOG_1
	EXCEPTION_PROLOG_2 INTERRUPT_INST_STORAGE InstructionAccess
	andis.	r5,r9,DSISR_SRR1_MATCH_32S@h /* Filter relevant SRR1 bits */
	stw	r5, _DSISR(r11)
	stw	r12, _DAR(r11)
	prepare_transfer_to_handler
	bl	do_page_fault
	b	interrupt_return

/* External interrupt */
	EXCEPTION(INTERRUPT_EXTERNAL, HardwareInterrupt, do_IRQ)

/* Alignment exception */
	START_EXCEPTION(INTERRUPT_ALIGNMENT, Alignment)
	EXCEPTION_PROLOG INTERRUPT_ALIGNMENT Alignment handle_dar_dsisr=1
	prepare_transfer_to_handler
	bl	alignment_exception
	REST_NVGPRS(r1)
	b	interrupt_return

/* Program check exception */
	START_EXCEPTION(INTERRUPT_PROGRAM, ProgramCheck)
	EXCEPTION_PROLOG INTERRUPT_PROGRAM ProgramCheck
	prepare_transfer_to_handler
	bl	program_check_exception
	REST_NVGPRS(r1)
	b	interrupt_return

/* Floating-point unavailable */
	START_EXCEPTION(0x800, FPUnavailable)
#ifdef CONFIG_PPC_FPU
BEGIN_FTR_SECTION
/*
 * Certain Freescale cores don't have a FPU and treat fp instructions
 * as a FP Unavailable exception.  Redirect to illegal/emulation handling.
 */
	b 	ProgramCheck
END_FTR_SECTION_IFSET(CPU_FTR_FPU_UNAVAILABLE)
	EXCEPTION_PROLOG INTERRUPT_FP_UNAVAIL FPUnavailable
	beq	1f
	bl	load_up_fpu		/* if from user, just load it up */
	b	fast_exception_return
1:	prepare_transfer_to_handler
	bl	kernel_fp_unavailable_exception
	b	interrupt_return
#else
	b 	ProgramCheck
#endif

/* Decrementer */
	EXCEPTION(INTERRUPT_DECREMENTER, Decrementer, timer_interrupt)

	EXCEPTION(0xa00, Trap_0a, unknown_exception)
	EXCEPTION(0xb00, Trap_0b, unknown_exception)

/* System call */
	START_EXCEPTION(INTERRUPT_SYSCALL, SystemCall)
	SYSCALL_ENTRY	INTERRUPT_SYSCALL

	EXCEPTION(INTERRUPT_TRACE, SingleStep, single_step_exception)
	EXCEPTION(0xe00, Trap_0e, unknown_exception)

/*
 * The Altivec unavailable trap is at 0x0f20.  Foo.
 * We effectively remap it to 0x3000.
 * We include an altivec unavailable exception vector even if
 * not configured for Altivec, so that you can't panic a
 * non-altivec kernel running on a machine with altivec just
 * by executing an altivec instruction.
 */
	START_EXCEPTION(INTERRUPT_PERFMON, PerformanceMonitorTrap)
	b	PerformanceMonitor

	START_EXCEPTION(INTERRUPT_ALTIVEC_UNAVAIL, AltiVecUnavailableTrap)
	b	AltiVecUnavailable

	__HEAD
/*
 * Handle TLB miss for instruction on 603/603e.
 * Note: we get an alternate set of r0 - r3 to use automatically.
 */
	. = INTERRUPT_INST_TLB_MISS_603
InstructionTLBMiss:
/*
 * r0:	scratch
 * r1:	linux style pte ( later becomes ppc hardware pte )
 * r2:	ptr to linux-style pte
 * r3:	scratch
 */
	/* Get PTE (linux-style) and check access */
	mfspr	r3,SPRN_IMISS
#ifdef CONFIG_MODULES
	lis	r1, TASK_SIZE@h		/* check if kernel address */
	cmplw	0,r1,r3
#endif
	mfspr	r2, SPRN_SDR1
	li	r1,_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_EXEC | _PAGE_USER
	rlwinm	r2, r2, 28, 0xfffff000
#ifdef CONFIG_MODULES
	bgt-	112f
	lis	r2, (swapper_pg_dir - PAGE_OFFSET)@ha	/* if kernel address, use */
	li	r1,_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_EXEC
	addi	r2, r2, (swapper_pg_dir - PAGE_OFFSET)@l	/* kernel page table */
#endif
112:	rlwimi	r2,r3,12,20,29		/* insert top 10 bits of address */
	lwz	r2,0(r2)		/* get pmd entry */
	rlwinm.	r2,r2,0,0,19		/* extract address of pte page */
	beq-	InstructionAddressInvalid	/* return if no mapping */
	rlwimi	r2,r3,22,20,29		/* insert next 10 bits of address */
	lwz	r0,0(r2)		/* get linux-style pte */
	andc.	r1,r1,r0		/* check access & ~permission */
	bne-	InstructionAddressInvalid /* return if access not permitted */
	/* Convert linux-style PTE to low word of PPC-style PTE */
	rlwimi	r0,r0,32-2,31,31	/* _PAGE_USER -> PP lsb */
	ori	r1, r1, 0xe06		/* clear out reserved bits */
	andc	r1, r0, r1		/* PP = user? 1 : 0 */
BEGIN_FTR_SECTION
	rlwinm	r1,r1,0,~_PAGE_COHERENT	/* clear M (coherence not required) */
END_FTR_SECTION_IFCLR(CPU_FTR_NEED_COHERENT)
	mtspr	SPRN_RPA,r1
	tlbli	r3
	mfspr	r3,SPRN_SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r3
	rfi
InstructionAddressInvalid:
	mfspr	r3,SPRN_SRR1
	rlwinm	r1,r3,9,6,6	/* Get load/store bit */

	addis	r1,r1,0x2000
	mtspr	SPRN_DSISR,r1	/* (shouldn't be needed) */
	andi.	r2,r3,0xFFFF	/* Clear upper bits of SRR1 */
	or	r2,r2,r1
	mtspr	SPRN_SRR1,r2
	mfspr	r1,SPRN_IMISS	/* Get failing address */
	rlwinm.	r2,r2,0,31,31	/* Check for little endian access */
	rlwimi	r2,r2,1,30,30	/* change 1 -> 3 */
	xor	r1,r1,r2
	mtspr	SPRN_DAR,r1	/* Set fault address */
	mfmsr	r0		/* Restore "normal" registers */
	xoris	r0,r0,MSR_TGPR>>16
	mtcrf	0x80,r3		/* Restore CR0 */
	mtmsr	r0
	b	InstructionAccess

/*
 * Handle TLB miss for DATA Load operation on 603/603e
 */
	. = INTERRUPT_DATA_LOAD_TLB_MISS_603
DataLoadTLBMiss:
/*
 * r0:	scratch
 * r1:	linux style pte ( later becomes ppc hardware pte )
 * r2:	ptr to linux-style pte
 * r3:	scratch
 */
	/* Get PTE (linux-style) and check access */
	mfspr	r3,SPRN_DMISS
	lis	r1, TASK_SIZE@h		/* check if kernel address */
	cmplw	0,r1,r3
	mfspr	r2, SPRN_SDR1
	li	r1, _PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_USER
	rlwinm	r2, r2, 28, 0xfffff000
	bgt-	112f
	lis	r2, (swapper_pg_dir - PAGE_OFFSET)@ha	/* if kernel address, use */
	li	r1, _PAGE_PRESENT | _PAGE_ACCESSED
	addi	r2, r2, (swapper_pg_dir - PAGE_OFFSET)@l	/* kernel page table */
112:	rlwimi	r2,r3,12,20,29		/* insert top 10 bits of address */
	lwz	r2,0(r2)		/* get pmd entry */
	rlwinm.	r2,r2,0,0,19		/* extract address of pte page */
	beq-	DataAddressInvalid	/* return if no mapping */
	rlwimi	r2,r3,22,20,29		/* insert next 10 bits of address */
	lwz	r0,0(r2)		/* get linux-style pte */
	andc.	r1,r1,r0		/* check access & ~permission */
	bne-	DataAddressInvalid	/* return if access not permitted */
	/* Convert linux-style PTE to low word of PPC-style PTE */
	rlwinm	r1,r0,32-9,30,30	/* _PAGE_RW -> PP msb */
	rlwimi	r0,r0,32-1,30,30	/* _PAGE_USER -> PP msb */
	rlwimi	r1,r0,32-3,24,24	/* _PAGE_RW -> _PAGE_DIRTY */
	rlwimi	r0,r0,32-1,31,31	/* _PAGE_USER -> PP lsb */
	xori	r1,r1,_PAGE_DIRTY	/* clear dirty when not rw */
	ori	r1,r1,0xe04		/* clear out reserved bits */
	andc	r1,r0,r1		/* PP = user? rw? 1: 3: 0 */
BEGIN_FTR_SECTION
	rlwinm	r1,r1,0,~_PAGE_COHERENT	/* clear M (coherence not required) */
END_FTR_SECTION_IFCLR(CPU_FTR_NEED_COHERENT)
	mtspr	SPRN_RPA,r1
BEGIN_MMU_FTR_SECTION
	li	r0,1
	mfspr	r1,SPRN_SPRG_603_LRU
	rlwinm	r2,r3,20,27,31		/* Get Address bits 15:19 */
	slw	r0,r0,r2
	xor	r1,r0,r1
	srw	r0,r1,r2
	mtspr   SPRN_SPRG_603_LRU,r1
	mfspr	r2,SPRN_SRR1
	rlwimi	r2,r0,31-14,14,14
	mtspr   SPRN_SRR1,r2
	mtcrf	0x80,r2
	tlbld	r3
	rfi
MMU_FTR_SECTION_ELSE
	mfspr	r2,SPRN_SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r2
	tlbld	r3
	rfi
ALT_MMU_FTR_SECTION_END_IFSET(MMU_FTR_NEED_DTLB_SW_LRU)
DataAddressInvalid:
	mfspr	r3,SPRN_SRR1
	rlwinm	r1,r3,9,6,6	/* Get load/store bit */
	addis	r1,r1,0x2000
	mtspr	SPRN_DSISR,r1
	andi.	r2,r3,0xFFFF	/* Clear upper bits of SRR1 */
	mtspr	SPRN_SRR1,r2
	mfspr	r1,SPRN_DMISS	/* Get failing address */
	rlwinm.	r2,r2,0,31,31	/* Check for little endian access */
	beq	20f		/* Jump if big endian */
	xori	r1,r1,3
20:	mtspr	SPRN_DAR,r1	/* Set fault address */
	mfmsr	r0		/* Restore "normal" registers */
	xoris	r0,r0,MSR_TGPR>>16
	mtcrf	0x80,r3		/* Restore CR0 */
	mtmsr	r0
	b	DataAccess

/*
 * Handle TLB miss for DATA Store on 603/603e
 */
	. = INTERRUPT_DATA_STORE_TLB_MISS_603
DataStoreTLBMiss:
/*
 * r0:	scratch
 * r1:	linux style pte ( later becomes ppc hardware pte )
 * r2:	ptr to linux-style pte
 * r3:	scratch
 */
	/* Get PTE (linux-style) and check access */
	mfspr	r3,SPRN_DMISS
	lis	r1, TASK_SIZE@h		/* check if kernel address */
	cmplw	0,r1,r3
	mfspr	r2, SPRN_SDR1
	li	r1, _PAGE_RW | _PAGE_DIRTY | _PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_USER
	rlwinm	r2, r2, 28, 0xfffff000
	bgt-	112f
	lis	r2, (swapper_pg_dir - PAGE_OFFSET)@ha	/* if kernel address, use */
	li	r1, _PAGE_RW | _PAGE_DIRTY | _PAGE_PRESENT | _PAGE_ACCESSED
	addi	r2, r2, (swapper_pg_dir - PAGE_OFFSET)@l	/* kernel page table */
112:	rlwimi	r2,r3,12,20,29		/* insert top 10 bits of address */
	lwz	r2,0(r2)		/* get pmd entry */
	rlwinm.	r2,r2,0,0,19		/* extract address of pte page */
	beq-	DataAddressInvalid	/* return if no mapping */
	rlwimi	r2,r3,22,20,29		/* insert next 10 bits of address */
	lwz	r0,0(r2)		/* get linux-style pte */
	andc.	r1,r1,r0		/* check access & ~permission */
	bne-	DataAddressInvalid	/* return if access not permitted */
	/* Convert linux-style PTE to low word of PPC-style PTE */
	rlwimi	r0,r0,32-2,31,31	/* _PAGE_USER -> PP lsb */
	li	r1,0xe06		/* clear out reserved bits & PP msb */
	andc	r1,r0,r1		/* PP = user? 1: 0 */
BEGIN_FTR_SECTION
	rlwinm	r1,r1,0,~_PAGE_COHERENT	/* clear M (coherence not required) */
END_FTR_SECTION_IFCLR(CPU_FTR_NEED_COHERENT)
	mtspr	SPRN_RPA,r1
	mfspr	r2,SPRN_SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r2
BEGIN_MMU_FTR_SECTION
	li	r0,1
	mfspr	r1,SPRN_SPRG_603_LRU
	rlwinm	r2,r3,20,27,31		/* Get Address bits 15:19 */
	slw	r0,r0,r2
	xor	r1,r0,r1
	srw	r0,r1,r2
	mtspr   SPRN_SPRG_603_LRU,r1
	mfspr	r2,SPRN_SRR1
	rlwimi	r2,r0,31-14,14,14
	mtspr   SPRN_SRR1,r2
	mtcrf	0x80,r2
	tlbld	r3
	rfi
MMU_FTR_SECTION_ELSE
	mfspr	r2,SPRN_SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r2
	tlbld	r3
	rfi
ALT_MMU_FTR_SECTION_END_IFSET(MMU_FTR_NEED_DTLB_SW_LRU)

#ifndef CONFIG_ALTIVEC
#define altivec_assist_exception	unknown_exception
#endif

#ifndef CONFIG_TAU_INT
#define TAUException	unknown_async_exception
#endif

	EXCEPTION(0x1300, Trap_13, instruction_breakpoint_exception)
	EXCEPTION(0x1400, SMI, SMIException)
	EXCEPTION(0x1500, Trap_15, unknown_exception)
	EXCEPTION(0x1600, Trap_16, altivec_assist_exception)
	EXCEPTION(0x1700, Trap_17, TAUException)
	EXCEPTION(0x1800, Trap_18, unknown_exception)
	EXCEPTION(0x1900, Trap_19, unknown_exception)
	EXCEPTION(0x1a00, Trap_1a, unknown_exception)
	EXCEPTION(0x1b00, Trap_1b, unknown_exception)
	EXCEPTION(0x1c00, Trap_1c, unknown_exception)
	EXCEPTION(0x1d00, Trap_1d, unknown_exception)
	EXCEPTION(0x1e00, Trap_1e, unknown_exception)
	EXCEPTION(0x1f00, Trap_1f, unknown_exception)
	EXCEPTION(0x2000, RunMode, RunModeException)
	EXCEPTION(0x2100, Trap_21, unknown_exception)
	EXCEPTION(0x2200, Trap_22, unknown_exception)
	EXCEPTION(0x2300, Trap_23, unknown_exception)
	EXCEPTION(0x2400, Trap_24, unknown_exception)
	EXCEPTION(0x2500, Trap_25, unknown_exception)
	EXCEPTION(0x2600, Trap_26, unknown_exception)
	EXCEPTION(0x2700, Trap_27, unknown_exception)
	EXCEPTION(0x2800, Trap_28, unknown_exception)
	EXCEPTION(0x2900, Trap_29, unknown_exception)
	EXCEPTION(0x2a00, Trap_2a, unknown_exception)
	EXCEPTION(0x2b00, Trap_2b, unknown_exception)
	EXCEPTION(0x2c00, Trap_2c, unknown_exception)
	EXCEPTION(0x2d00, Trap_2d, unknown_exception)
	EXCEPTION(0x2e00, Trap_2e, unknown_exception)
	EXCEPTION(0x2f00, Trap_2f, unknown_exception)

	__HEAD
	. = 0x3000

#ifdef CONFIG_PPC_BOOK3S_604
.macro save_regs_thread		thread
	stw	r0, THR0(\thread)
	stw	r3, THR3(\thread)
	stw	r4, THR4(\thread)
	stw	r5, THR5(\thread)
	stw	r6, THR6(\thread)
	stw	r8, THR8(\thread)
	stw	r9, THR9(\thread)
	mflr	r0
	stw	r0, THLR(\thread)
	mfctr	r0
	stw	r0, THCTR(\thread)
.endm

.macro restore_regs_thread	thread
	lwz	r0, THLR(\thread)
	mtlr	r0
	lwz	r0, THCTR(\thread)
	mtctr	r0
	lwz	r0, THR0(\thread)
	lwz	r3, THR3(\thread)
	lwz	r4, THR4(\thread)
	lwz	r5, THR5(\thread)
	lwz	r6, THR6(\thread)
	lwz	r8, THR8(\thread)
	lwz	r9, THR9(\thread)
.endm

hash_page_dsi:
	save_regs_thread	r10
	mfdsisr	r3
	mfdar	r4
	mfsrr0	r5
	mfsrr1	r9
	rlwinm	r3, r3, 32 - 15, _PAGE_RW	/* DSISR_STORE -> _PAGE_RW */
	bl	hash_page
	mfspr	r10, SPRN_SPRG_THREAD
	restore_regs_thread r10
	b	.Lhash_page_dsi_cont

hash_page_isi:
	mr	r11, r10
	mfspr	r10, SPRN_SPRG_THREAD
	save_regs_thread	r10
	li	r3, 0
	lwz	r4, SRR0(r10)
	lwz	r9, SRR1(r10)
	bl	hash_page
	mfspr	r10, SPRN_SPRG_THREAD
	restore_regs_thread r10
	mr	r10, r11
	b	.Lhash_page_isi_cont

	.globl fast_hash_page_return
fast_hash_page_return:
	andis.	r10, r9, SRR1_ISI_NOPT@h	/* Set on ISI, cleared on DSI */
	mfspr	r10, SPRN_SPRG_THREAD
	restore_regs_thread r10
	bne	1f

	/* DSI */
	mtcr	r11
	lwz	r11, THR11(r10)
	mfspr	r10, SPRN_SPRG_SCRATCH2
	rfi

1:	/* ISI */
	mtcr	r11
	mfspr	r11, SPRN_SPRG_SCRATCH1
	mfspr	r10, SPRN_SPRG_SCRATCH0
	rfi
#endif /* CONFIG_PPC_BOOK3S_604 */

#ifdef CONFIG_VMAP_STACK
	vmap_stack_overflow_exception
#endif

	__HEAD
AltiVecUnavailable:
	EXCEPTION_PROLOG 0xf20 AltiVecUnavailable
#ifdef CONFIG_ALTIVEC
	beq	1f
	bl	load_up_altivec		/* if from user, just load it up */
	b	fast_exception_return
#endif /* CONFIG_ALTIVEC */
1:	prepare_transfer_to_handler
	bl	altivec_unavailable_exception
	b	interrupt_return

	__HEAD
PerformanceMonitor:
	EXCEPTION_PROLOG 0xf00 PerformanceMonitor
	prepare_transfer_to_handler
	bl	performance_monitor_exception
	b	interrupt_return


	__HEAD
/*
 * This code is jumped to from the startup code to copy
 * the kernel image to physical address PHYSICAL_START.
 */
relocate_kernel:
	lis	r3,PHYSICAL_START@h	/* Destination base address */
	li	r6,0			/* Destination offset */
	li	r5,0x4000		/* # bytes of memory to copy */
	bl	copy_and_flush		/* copy the first 0x4000 bytes */
	addi	r0,r3,4f@l		/* jump to the address of 4f */
	mtctr	r0			/* in copy and do the rest. */
	bctr				/* jump to the copy */
4:	lis	r5,_end-KERNELBASE@h
	ori	r5,r5,_end-KERNELBASE@l
	bl	copy_and_flush		/* copy the rest */
	b	turn_on_mmu

/*
 * Copy routine used to copy the kernel to start at physical address 0
 * and flush and invalidate the caches as needed.
 * r3 = dest addr, r4 = source addr, r5 = copy limit, r6 = start offset
 * on exit, r3, r4, r5 are unchanged, r6 is updated to be >= r5.
 */
_ENTRY(copy_and_flush)
	addi	r5,r5,-4
	addi	r6,r6,-4
4:	li	r0,L1_CACHE_BYTES/4
	mtctr	r0
3:	addi	r6,r6,4			/* copy a cache line */
	lwzx	r0,r6,r4
	stwx	r0,r6,r3
	bdnz	3b
	dcbst	r6,r3			/* write it to memory */
	sync
	icbi	r6,r3			/* flush the icache line */
	cmplw	0,r6,r5
	blt	4b
	sync				/* additional sync needed on g4 */
	isync
	addi	r5,r5,4
	addi	r6,r6,4
	blr

#ifdef CONFIG_SMP
	.globl __secondary_start_mpc86xx
__secondary_start_mpc86xx:
	mfspr	r3, SPRN_PIR
	stw	r3, __secondary_hold_acknowledge@l(0)
	mr	r24, r3			/* cpu # */
	b	__secondary_start

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
	/* on powersurge, we come in here with IR=0 and DR=1, and DBAT 0
	   set to map the 0xf0000000 - 0xffffffff region */
	mfmsr	r0
	rlwinm	r0,r0,0,28,26		/* clear DR (0x10) */
	mtmsr	r0
	isync

	.globl	__secondary_start
__secondary_start:
	/* Copy some CPU settings from CPU 0 */
	bl	__restore_cpu_setup

	lis	r3,-KERNELBASE@h
	mr	r4,r24
	bl	call_setup_cpu		/* Call setup_cpu for this CPU */
	lis	r3,-KERNELBASE@h
	bl	init_idle_6xx

	/* get current's stack and current */
	lis	r2,secondary_current@ha
	tophys(r2,r2)
	lwz	r2,secondary_current@l(r2)
	tophys(r1,r2)
	lwz	r1,TASK_STACK(r1)

	/* stack */
	addi	r1,r1,THREAD_SIZE-STACK_FRAME_OVERHEAD
	li	r0,0
	tophys(r3,r1)
	stw	r0,0(r3)

	/* load up the MMU */
	bl	load_segment_registers
	bl	load_up_mmu

	/* ptr to phys current thread */
	tophys(r4,r2)
	addi	r4,r4,THREAD	/* phys address of our thread_struct */
	mtspr	SPRN_SPRG_THREAD,r4
BEGIN_MMU_FTR_SECTION
	lis	r4, (swapper_pg_dir - PAGE_OFFSET)@h
	ori	r4, r4, (swapper_pg_dir - PAGE_OFFSET)@l
	rlwinm	r4, r4, 4, 0xffff01ff
	mtspr	SPRN_SDR1, r4
END_MMU_FTR_SECTION_IFCLR(MMU_FTR_HPTE_TABLE)

	/* enable MMU and jump to start_secondary */
	li	r4,MSR_KERNEL
	lis	r3,start_secondary@h
	ori	r3,r3,start_secondary@l
	mtspr	SPRN_SRR0,r3
	mtspr	SPRN_SRR1,r4
	rfi
#endif /* CONFIG_SMP */

#ifdef CONFIG_KVM_BOOK3S_HANDLER
#include "../kvm/book3s_rmhandlers.S"
#endif

/*
 * Load stuff into the MMU.  Intended to be called with
 * IR=0 and DR=0.
 */
early_hash_table:
	sync			/* Force all PTE updates to finish */
	isync
	tlbia			/* Clear all TLB entries */
	sync			/* wait for tlbia/tlbie to finish */
	TLBSYNC			/* ... on all CPUs */
	/* Load the SDR1 register (hash table base & size) */
	lis	r6, early_hash - PAGE_OFFSET@h
	ori	r6, r6, 3	/* 256kB table */
	mtspr	SPRN_SDR1, r6
	blr

load_up_mmu:
	sync			/* Force all PTE updates to finish */
	isync
	tlbia			/* Clear all TLB entries */
	sync			/* wait for tlbia/tlbie to finish */
	TLBSYNC			/* ... on all CPUs */
BEGIN_MMU_FTR_SECTION
	/* Load the SDR1 register (hash table base & size) */
	lis	r6,_SDR1@ha
	tophys(r6,r6)
	lwz	r6,_SDR1@l(r6)
	mtspr	SPRN_SDR1,r6
END_MMU_FTR_SECTION_IFSET(MMU_FTR_HPTE_TABLE)

/* Load the BAT registers with the values set up by MMU_init. */
	lis	r3,BATS@ha
	addi	r3,r3,BATS@l
	tophys(r3,r3)
	LOAD_BAT(0,r3,r4,r5)
	LOAD_BAT(1,r3,r4,r5)
	LOAD_BAT(2,r3,r4,r5)
	LOAD_BAT(3,r3,r4,r5)
BEGIN_MMU_FTR_SECTION
	LOAD_BAT(4,r3,r4,r5)
	LOAD_BAT(5,r3,r4,r5)
	LOAD_BAT(6,r3,r4,r5)
	LOAD_BAT(7,r3,r4,r5)
END_MMU_FTR_SECTION_IFSET(MMU_FTR_USE_HIGH_BATS)
	blr

_GLOBAL(load_segment_registers)
	li	r0, NUM_USER_SEGMENTS /* load up user segment register values */
	mtctr	r0		/* for context 0 */
#ifdef CONFIG_PPC_KUEP
	lis	r3, SR_NX@h	/* Kp = 0, Ks = 0, VSID = 0 */
#else
	li	r3, 0		/* Kp = 0, Ks = 0, VSID = 0 */
#endif
	li	r4, 0
3:	mtsrin	r3, r4
	addi	r3, r3, 0x111	/* increment VSID */
	addis	r4, r4, 0x1000	/* address of next segment */
	bdnz	3b
	li	r0, 16 - NUM_USER_SEGMENTS /* load up kernel segment registers */
	mtctr	r0			/* for context 0 */
	rlwinm	r3, r3, 0, ~SR_NX	/* Nx = 0 */
	rlwinm	r3, r3, 0, ~SR_KS	/* Ks = 0 */
	oris	r3, r3, SR_KP@h		/* Kp = 1 */
3:	mtsrin	r3, r4
	addi	r3, r3, 0x111	/* increment VSID */
	addis	r4, r4, 0x1000	/* address of next segment */
	bdnz	3b
	blr

/*
 * This is where the main kernel code starts.
 */
start_here:
	/* ptr to current */
	lis	r2,init_task@h
	ori	r2,r2,init_task@l
	/* Set up for using our exception vectors */
	/* ptr to phys current thread */
	tophys(r4,r2)
	addi	r4,r4,THREAD	/* init task's THREAD */
	mtspr	SPRN_SPRG_THREAD,r4
BEGIN_MMU_FTR_SECTION
	lis	r4, (swapper_pg_dir - PAGE_OFFSET)@h
	ori	r4, r4, (swapper_pg_dir - PAGE_OFFSET)@l
	rlwinm	r4, r4, 4, 0xffff01ff
	mtspr	SPRN_SDR1, r4
END_MMU_FTR_SECTION_IFCLR(MMU_FTR_HPTE_TABLE)

	/* stack */
	lis	r1,init_thread_union@ha
	addi	r1,r1,init_thread_union@l
	li	r0,0
	stwu	r0,THREAD_SIZE-STACK_FRAME_OVERHEAD(r1)
/*
 * Do early platform-specific initialization,
 * and set up the MMU.
 */
#ifdef CONFIG_KASAN
	bl	kasan_early_init
#endif
	li	r3,0
	mr	r4,r31
	bl	machine_init
	bl	__save_cpu_setup
	bl	MMU_init
	bl	MMU_init_hw_patch

/*
 * Go back to running unmapped so we can load up new values
 * for SDR1 (hash table pointer) and the segment registers
 * and change to using our exception vectors.
 */
	lis	r4,2f@h
	ori	r4,r4,2f@l
	tophys(r4,r4)
	li	r3,MSR_KERNEL & ~(MSR_IR|MSR_DR)

	.align	4
	mtspr	SPRN_SRR0,r4
	mtspr	SPRN_SRR1,r3
	rfi
/* Load up the kernel context */
2:	bl	load_up_mmu

#ifdef CONFIG_BDI_SWITCH
	/* Add helper information for the Abatron bdiGDB debugger.
	 * We do this here because we know the mmu is disabled, and
	 * will be enabled for real in just a few instructions.
	 */
	lis	r5, abatron_pteptrs@h
	ori	r5, r5, abatron_pteptrs@l
	stw	r5, 0xf0(0)	/* This much match your Abatron config */
	lis	r6, swapper_pg_dir@h
	ori	r6, r6, swapper_pg_dir@l
	tophys(r5, r5)
	stw	r6, 0(r5)
#endif /* CONFIG_BDI_SWITCH */

/* Now turn on the MMU for real! */
	li	r4,MSR_KERNEL
	lis	r3,start_kernel@h
	ori	r3,r3,start_kernel@l
	mtspr	SPRN_SRR0,r3
	mtspr	SPRN_SRR1,r4
	rfi

/*
 * An undocumented "feature" of 604e requires that the v bit
 * be cleared before changing BAT values.
 *
 * Also, newer IBM firmware does not clear bat3 and 4 so
 * this makes sure it's done.
 *  -- Cort
 */
clear_bats:
	li	r10,0

	mtspr	SPRN_DBAT0U,r10
	mtspr	SPRN_DBAT0L,r10
	mtspr	SPRN_DBAT1U,r10
	mtspr	SPRN_DBAT1L,r10
	mtspr	SPRN_DBAT2U,r10
	mtspr	SPRN_DBAT2L,r10
	mtspr	SPRN_DBAT3U,r10
	mtspr	SPRN_DBAT3L,r10
	mtspr	SPRN_IBAT0U,r10
	mtspr	SPRN_IBAT0L,r10
	mtspr	SPRN_IBAT1U,r10
	mtspr	SPRN_IBAT1L,r10
	mtspr	SPRN_IBAT2U,r10
	mtspr	SPRN_IBAT2L,r10
	mtspr	SPRN_IBAT3U,r10
	mtspr	SPRN_IBAT3L,r10
BEGIN_MMU_FTR_SECTION
	/* Here's a tweak: at this point, CPU setup have
	 * not been called yet, so HIGH_BAT_EN may not be
	 * set in HID0 for the 745x processors. However, it
	 * seems that doesn't affect our ability to actually
	 * write to these SPRs.
	 */
	mtspr	SPRN_DBAT4U,r10
	mtspr	SPRN_DBAT4L,r10
	mtspr	SPRN_DBAT5U,r10
	mtspr	SPRN_DBAT5L,r10
	mtspr	SPRN_DBAT6U,r10
	mtspr	SPRN_DBAT6L,r10
	mtspr	SPRN_DBAT7U,r10
	mtspr	SPRN_DBAT7L,r10
	mtspr	SPRN_IBAT4U,r10
	mtspr	SPRN_IBAT4L,r10
	mtspr	SPRN_IBAT5U,r10
	mtspr	SPRN_IBAT5L,r10
	mtspr	SPRN_IBAT6U,r10
	mtspr	SPRN_IBAT6L,r10
	mtspr	SPRN_IBAT7U,r10
	mtspr	SPRN_IBAT7L,r10
END_MMU_FTR_SECTION_IFSET(MMU_FTR_USE_HIGH_BATS)
	blr

_ENTRY(update_bats)
	lis	r4, 1f@h
	ori	r4, r4, 1f@l
	tophys(r4, r4)
	mfmsr	r6
	mflr	r7
	li	r3, MSR_KERNEL & ~(MSR_IR | MSR_DR)
	rlwinm	r0, r6, 0, ~MSR_RI
	rlwinm	r0, r0, 0, ~MSR_EE
	mtmsr	r0

	.align	4
	mtspr	SPRN_SRR0, r4
	mtspr	SPRN_SRR1, r3
	rfi
1:	bl	clear_bats
	lis	r3, BATS@ha
	addi	r3, r3, BATS@l
	tophys(r3, r3)
	LOAD_BAT(0, r3, r4, r5)
	LOAD_BAT(1, r3, r4, r5)
	LOAD_BAT(2, r3, r4, r5)
	LOAD_BAT(3, r3, r4, r5)
BEGIN_MMU_FTR_SECTION
	LOAD_BAT(4, r3, r4, r5)
	LOAD_BAT(5, r3, r4, r5)
	LOAD_BAT(6, r3, r4, r5)
	LOAD_BAT(7, r3, r4, r5)
END_MMU_FTR_SECTION_IFSET(MMU_FTR_USE_HIGH_BATS)
	li	r3, MSR_KERNEL & ~(MSR_IR | MSR_DR | MSR_RI)
	mtmsr	r3
	mtspr	SPRN_SRR0, r7
	mtspr	SPRN_SRR1, r6
	rfi

flush_tlbs:
	lis	r10, 0x40
1:	addic.	r10, r10, -0x1000
	tlbie	r10
	bgt	1b
	sync
	blr

mmu_off:
 	addi	r4, r3, __after_mmu_off - _start
	mfmsr	r3
	andi.	r0,r3,MSR_DR|MSR_IR		/* MMU enabled? */
	beqlr
	andc	r3,r3,r0

	.align	4
	mtspr	SPRN_SRR0,r4
	mtspr	SPRN_SRR1,r3
	sync
	rfi

/* We use one BAT to map up to 256M of RAM at _PAGE_OFFSET */
initial_bats:
	lis	r11,PAGE_OFFSET@h
	tophys(r8,r11)
#ifdef CONFIG_SMP
	ori	r8,r8,0x12		/* R/W access, M=1 */
#else
	ori	r8,r8,2			/* R/W access */
#endif /* CONFIG_SMP */
	ori	r11,r11,BL_256M<<2|0x2	/* set up BAT registers for 604 */

	mtspr	SPRN_DBAT0L,r8		/* N.B. 6xx have valid */
	mtspr	SPRN_DBAT0U,r11		/* bit in upper BAT register */
	mtspr	SPRN_IBAT0L,r8
	mtspr	SPRN_IBAT0U,r11
	isync
	blr

#ifdef CONFIG_BOOTX_TEXT
setup_disp_bat:
	/*
	 * setup the display bat prepared for us in prom.c
	 */
	mflr	r8
	bl	reloc_offset
	mtlr	r8
	addis	r8,r3,disp_BAT@ha
	addi	r8,r8,disp_BAT@l
	cmpwi	cr0,r8,0
	beqlr
	lwz	r11,0(r8)
	lwz	r8,4(r8)
	mtspr	SPRN_DBAT3L,r8
	mtspr	SPRN_DBAT3U,r11
	blr
#endif /* CONFIG_BOOTX_TEXT */

#ifdef CONFIG_PPC_EARLY_DEBUG_CPM
setup_cpm_bat:
	lis	r8, 0xf000
	ori	r8, r8,	0x002a
	mtspr	SPRN_DBAT1L, r8

	lis	r11, 0xf000
	ori	r11, r11, (BL_1M << 2) | 2
	mtspr	SPRN_DBAT1U, r11

	blr
#endif

#ifdef CONFIG_PPC_EARLY_DEBUG_USBGECKO
setup_usbgecko_bat:
	/* prepare a BAT for early io */
#if defined(CONFIG_GAMECUBE)
	lis	r8, 0x0c00
#elif defined(CONFIG_WII)
	lis	r8, 0x0d00
#else
#error Invalid platform for USB Gecko based early debugging.
#endif
	/*
	 * The virtual address used must match the virtual address
	 * associated to the fixmap entry FIX_EARLY_DEBUG_BASE.
	 */
	lis	r11, 0xfffe	/* top 128K */
	ori	r8, r8, 0x002a	/* uncached, guarded ,rw */
	ori	r11, r11, 0x2	/* 128K, Vs=1, Vp=0 */
	mtspr	SPRN_DBAT1L, r8
	mtspr	SPRN_DBAT1U, r11
	blr
#endif

	.data

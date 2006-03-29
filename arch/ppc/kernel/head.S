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
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *
 *  This file contains the low-level support and setup for the
 *  PowerPC platform, including trap and interrupt dispatch.
 *  (The PPC 8xx embedded CPUs use head_8xx.S instead.)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/cputable.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>

#ifdef CONFIG_APUS
#include <asm/amigappc.h>
#endif

/* 601 only have IBAT; cr0.eq is set on 601 when using this macro */
#define LOAD_BAT(n, reg, RA, RB)	\
	/* see the comment for clear_bats() -- Cort */ \
	li	RA,0;			\
	mtspr	SPRN_IBAT##n##U,RA;	\
	mtspr	SPRN_DBAT##n##U,RA;	\
	lwz	RA,(n*16)+0(reg);	\
	lwz	RB,(n*16)+4(reg);	\
	mtspr	SPRN_IBAT##n##U,RA;	\
	mtspr	SPRN_IBAT##n##L,RB;	\
	beq	1f;			\
	lwz	RA,(n*16)+8(reg);	\
	lwz	RB,(n*16)+12(reg);	\
	mtspr	SPRN_DBAT##n##U,RA;	\
	mtspr	SPRN_DBAT##n##L,RB;	\
1:

	.text
	.stabs	"arch/ppc/kernel/",N_SO,0,0,0f
	.stabs	"head.S",N_SO,0,0,0f
0:
	.globl	_stext
_stext:

/*
 * _start is defined this way because the XCOFF loader in the OpenFirmware
 * on the powermac expects the entry point to be a procedure descriptor.
 */
	.text
	.globl	_start
_start:
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
 * APUS
 *   r3: 'APUS'
 *   r4: physical address of memory base
 *   Linux/m68k style BootInfo structure at &_end.
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
	mr	r31,r3			/* save parameters */
	mr	r30,r4
	mr	r29,r5
	mr	r28,r6
	mr	r27,r7
	li	r24,0			/* cpu # */

/*
 * early_init() does the early machine identification and does
 * the necessary low-level setup and clears the BSS
 *  -- Cort <cort@fsmlabs.com>
 */
	bl	early_init

#ifdef CONFIG_APUS
/* On APUS the __va/__pa constants need to be set to the correct
 * values before continuing.
 */
	mr	r4,r30
	bl	fix_mem_constants
#endif /* CONFIG_APUS */

/* Switch MMU off, clear BATs and flush TLB. At this point, r3 contains
 * the physical address we are running at, returned by early_init()
 */
 	bl	mmu_off
__after_mmu_off:
	bl	clear_bats
	bl	flush_tlbs

	bl	initial_bats
#if !defined(CONFIG_APUS) && defined(CONFIG_BOOTX_TEXT)
	bl	setup_disp_bat
#endif

/*
 * Call setup_cpu for CPU 0 and initialize 6xx Idle
 */
	bl	reloc_offset
	li	r24,0			/* cpu# */
	bl	call_setup_cpu		/* Call setup_cpu for this CPU */
#ifdef CONFIG_6xx
	bl	reloc_offset
	bl	init_idle_6xx
#endif /* CONFIG_6xx */


#ifndef CONFIG_APUS
/*
 * We need to run with _start at physical address 0.
 * If the MMU is already turned on, we copy stuff to KERNELBASE,
 * otherwise we copy it to 0.
 */
	bl	reloc_offset
	mr	r26,r3
	addis	r4,r3,KERNELBASE@h	/* current address of _start */
	cmpwi	0,r4,0			/* are we already running at 0? */
	bne	relocate_kernel
#endif /* CONFIG_APUS */
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
	ori	r0,r0,MSR_DR|MSR_IR
	mtspr	SPRN_SRR1,r0
	lis	r0,start_here@h
	ori	r0,r0,start_here@l
	mtspr	SPRN_SRR0,r0
	SYNC
	RFI				/* enables MMU */

/*
 * We need __secondary_hold as a place to hold the other cpus on
 * an SMP machine, even when we are running a UP kernel.
 */
	. = 0xc0			/* for prep bootloader */
	li	r3,1			/* MTX only has 1 cpu */
	.globl	__secondary_hold
__secondary_hold:
	/* tell the master we're here */
	stw	r3,4(0)
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

/*
 * Exception entry code.  This code runs with address translation
 * turned off, i.e. using physical addresses.
 * We assume sprg3 has the physical address of the current
 * task's thread_struct.
 */
#define EXCEPTION_PROLOG	\
	mtspr	SPRN_SPRG0,r10;	\
	mtspr	SPRN_SPRG1,r11;	\
	mfcr	r10;		\
	EXCEPTION_PROLOG_1;	\
	EXCEPTION_PROLOG_2

#define EXCEPTION_PROLOG_1	\
	mfspr	r11,SPRN_SRR1;		/* check whether user or kernel */ \
	andi.	r11,r11,MSR_PR;	\
	tophys(r11,r1);			/* use tophys(r1) if kernel */ \
	beq	1f;		\
	mfspr	r11,SPRN_SPRG3;	\
	lwz	r11,THREAD_INFO-THREAD(r11);	\
	addi	r11,r11,THREAD_SIZE;	\
	tophys(r11,r11);	\
1:	subi	r11,r11,INT_FRAME_SIZE	/* alloc exc. frame */


#define EXCEPTION_PROLOG_2	\
	CLR_TOP32(r11);		\
	stw	r10,_CCR(r11);		/* save registers */ \
	stw	r12,GPR12(r11);	\
	stw	r9,GPR9(r11);	\
	mfspr	r10,SPRN_SPRG0;	\
	stw	r10,GPR10(r11);	\
	mfspr	r12,SPRN_SPRG1;	\
	stw	r12,GPR11(r11);	\
	mflr	r10;		\
	stw	r10,_LINK(r11);	\
	mfspr	r12,SPRN_SRR0;	\
	mfspr	r9,SPRN_SRR1;	\
	stw	r1,GPR1(r11);	\
	stw	r1,0(r11);	\
	tovirt(r1,r11);			/* set new kernel sp */	\
	li	r10,MSR_KERNEL & ~(MSR_IR|MSR_DR); /* can take exceptions */ \
	MTMSRD(r10);			/* (except for mach check in rtas) */ \
	stw	r0,GPR0(r11);	\
	SAVE_4GPRS(3, r11);	\
	SAVE_2GPRS(7, r11)

/*
 * Note: code which follows this uses cr0.eq (set if from kernel),
 * r11, r12 (SRR0), and r9 (SRR1).
 *
 * Note2: once we have set r1 we are in a position to take exceptions
 * again, and we could thus set MSR:RI at that point.
 */

/*
 * Exception vectors.
 */
#define EXCEPTION(n, label, hdlr, xfer)		\
	. = n;					\
label:						\
	EXCEPTION_PROLOG;			\
	addi	r3,r1,STACK_FRAME_OVERHEAD;	\
	xfer(n, hdlr)

#define EXC_XFER_TEMPLATE(n, hdlr, trap, copyee, tfer, ret)	\
	li	r10,trap;					\
	stw	r10,TRAP(r11);					\
	li	r10,MSR_KERNEL;					\
	copyee(r10, r9);					\
	bl	tfer;						\
i##n:								\
	.long	hdlr;						\
	.long	ret

#define COPY_EE(d, s)		rlwimi d,s,0,16,16
#define NOCOPY(d, s)

#define EXC_XFER_STD(n, hdlr)		\
	EXC_XFER_TEMPLATE(n, hdlr, n, NOCOPY, transfer_to_handler_full,	\
			  ret_from_except_full)

#define EXC_XFER_LITE(n, hdlr)		\
	EXC_XFER_TEMPLATE(n, hdlr, n+1, NOCOPY, transfer_to_handler, \
			  ret_from_except)

#define EXC_XFER_EE(n, hdlr)		\
	EXC_XFER_TEMPLATE(n, hdlr, n, COPY_EE, transfer_to_handler_full, \
			  ret_from_except_full)

#define EXC_XFER_EE_LITE(n, hdlr)	\
	EXC_XFER_TEMPLATE(n, hdlr, n+1, COPY_EE, transfer_to_handler, \
			  ret_from_except)

/* System reset */
/* core99 pmac starts the seconary here by changing the vector, and
   putting it back to what it was (unknown_exception) when done.  */
#if defined(CONFIG_GEMINI) && defined(CONFIG_SMP)
	. = 0x100
	b	__secondary_start_gemini
#else
	EXCEPTION(0x100, Reset, unknown_exception, EXC_XFER_STD)
#endif

/* Machine check */
	. = 0x200
	mtspr	SPRN_SPRG0,r10
	mtspr	SPRN_SPRG1,r11
	mfcr	r10
	EXCEPTION_PROLOG_1
7:	EXCEPTION_PROLOG_2
	addi	r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_STD(0x200, machine_check_exception)

/* Data access exception. */
	. = 0x300
DataAccess:
	EXCEPTION_PROLOG
	mfspr	r10,SPRN_DSISR
	andis.	r0,r10,0xa470		/* weird error? */
	bne	1f			/* if not, try to put a PTE */
	mfspr	r4,SPRN_DAR		/* into the hash table */
	rlwinm	r3,r10,32-15,21,21	/* DSISR_STORE -> _PAGE_RW */
	bl	hash_page
1:	stw	r10,_DSISR(r11)
	mr	r5,r10
	mfspr	r4,SPRN_DAR
	EXC_XFER_EE_LITE(0x300, handle_page_fault)

/* Instruction access exception. */
	. = 0x400
InstructionAccess:
	EXCEPTION_PROLOG
	andis.	r0,r9,0x4000		/* no pte found? */
	beq	1f			/* if so, try to put a PTE */
	li	r3,0			/* into the hash table */
	mr	r4,r12			/* SRR0 is fault address */
	bl	hash_page
1:	mr	r4,r12
	mr	r5,r9
	EXC_XFER_EE_LITE(0x400, handle_page_fault)

/* External interrupt */
	EXCEPTION(0x500, HardwareInterrupt, do_IRQ, EXC_XFER_LITE)

/* Alignment exception */
	. = 0x600
Alignment:
	EXCEPTION_PROLOG
	mfspr	r4,SPRN_DAR
	stw	r4,_DAR(r11)
	mfspr	r5,SPRN_DSISR
	stw	r5,_DSISR(r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_EE(0x600, alignment_exception)

/* Program check exception */
	EXCEPTION(0x700, ProgramCheck, program_check_exception, EXC_XFER_STD)

/* Floating-point unavailable */
	. = 0x800
FPUnavailable:
	EXCEPTION_PROLOG
	bne	load_up_fpu		/* if from user, just load it up */
	addi	r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_EE_LITE(0x800, kernel_fp_unavailable_exception)

/* Decrementer */
	EXCEPTION(0x900, Decrementer, timer_interrupt, EXC_XFER_LITE)

	EXCEPTION(0xa00, Trap_0a, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0xb00, Trap_0b, unknown_exception, EXC_XFER_EE)

/* System call */
	. = 0xc00
SystemCall:
	EXCEPTION_PROLOG
	EXC_XFER_EE_LITE(0xc00, DoSyscall)

/* Single step - not used on 601 */
	EXCEPTION(0xd00, SingleStep, single_step_exception, EXC_XFER_STD)
	EXCEPTION(0xe00, Trap_0e, unknown_exception, EXC_XFER_EE)

/*
 * The Altivec unavailable trap is at 0x0f20.  Foo.
 * We effectively remap it to 0x3000.
 * We include an altivec unavailable exception vector even if
 * not configured for Altivec, so that you can't panic a
 * non-altivec kernel running on a machine with altivec just
 * by executing an altivec instruction.
 */
	. = 0xf00
	b	Trap_0f

	. = 0xf20
	b	AltiVecUnavailable

Trap_0f:
	EXCEPTION_PROLOG
	addi	r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_EE(0xf00, unknown_exception)

/*
 * Handle TLB miss for instruction on 603/603e.
 * Note: we get an alternate set of r0 - r3 to use automatically.
 */
	. = 0x1000
InstructionTLBMiss:
/*
 * r0:	stored ctr
 * r1:	linux style pte ( later becomes ppc hardware pte )
 * r2:	ptr to linux-style pte
 * r3:	scratch
 */
	mfctr	r0
	/* Get PTE (linux-style) and check access */
	mfspr	r3,SPRN_IMISS
	lis	r1,KERNELBASE@h		/* check if kernel address */
	cmplw	0,r3,r1
	mfspr	r2,SPRN_SPRG3
	li	r1,_PAGE_USER|_PAGE_PRESENT /* low addresses tested as user */
	lwz	r2,PGDIR(r2)
	blt+	112f
	lis	r2,swapper_pg_dir@ha	/* if kernel address, use */
	addi	r2,r2,swapper_pg_dir@l	/* kernel page table */
	mfspr	r1,SPRN_SRR1		/* and MSR_PR bit from SRR1 */
	rlwinm	r1,r1,32-12,29,29	/* shift MSR_PR to _PAGE_USER posn */
112:	tophys(r2,r2)
	rlwimi	r2,r3,12,20,29		/* insert top 10 bits of address */
	lwz	r2,0(r2)		/* get pmd entry */
	rlwinm.	r2,r2,0,0,19		/* extract address of pte page */
	beq-	InstructionAddressInvalid	/* return if no mapping */
	rlwimi	r2,r3,22,20,29		/* insert next 10 bits of address */
	lwz	r3,0(r2)		/* get linux-style pte */
	andc.	r1,r1,r3		/* check access & ~permission */
	bne-	InstructionAddressInvalid /* return if access not permitted */
	ori	r3,r3,_PAGE_ACCESSED	/* set _PAGE_ACCESSED in pte */
	/*
	 * NOTE! We are assuming this is not an SMP system, otherwise
	 * we would need to update the pte atomically with lwarx/stwcx.
	 */
	stw	r3,0(r2)		/* update PTE (accessed bit) */
	/* Convert linux-style PTE to low word of PPC-style PTE */
	rlwinm	r1,r3,32-10,31,31	/* _PAGE_RW -> PP lsb */
	rlwinm	r2,r3,32-7,31,31	/* _PAGE_DIRTY -> PP lsb */
	and	r1,r1,r2		/* writable if _RW and _DIRTY */
	rlwimi	r3,r3,32-1,30,30	/* _PAGE_USER -> PP msb */
	rlwimi	r3,r3,32-1,31,31	/* _PAGE_USER -> PP lsb */
	ori	r1,r1,0xe14		/* clear out reserved bits and M */
	andc	r1,r3,r1		/* PP = user? (rw&dirty? 2: 3): 0 */
	mtspr	SPRN_RPA,r1
	mfspr	r3,SPRN_IMISS
	tlbli	r3
	mfspr	r3,SPRN_SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r3
	rfi
InstructionAddressInvalid:
	mfspr	r3,SPRN_SRR1
	rlwinm	r1,r3,9,6,6	/* Get load/store bit */

	addis	r1,r1,0x2000
	mtspr	SPRN_DSISR,r1	/* (shouldn't be needed) */
	mtctr	r0		/* Restore CTR */
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
	. = 0x1100
DataLoadTLBMiss:
/*
 * r0:	stored ctr
 * r1:	linux style pte ( later becomes ppc hardware pte )
 * r2:	ptr to linux-style pte
 * r3:	scratch
 */
	mfctr	r0
	/* Get PTE (linux-style) and check access */
	mfspr	r3,SPRN_DMISS
	lis	r1,KERNELBASE@h		/* check if kernel address */
	cmplw	0,r3,r1
	mfspr	r2,SPRN_SPRG3
	li	r1,_PAGE_USER|_PAGE_PRESENT /* low addresses tested as user */
	lwz	r2,PGDIR(r2)
	blt+	112f
	lis	r2,swapper_pg_dir@ha	/* if kernel address, use */
	addi	r2,r2,swapper_pg_dir@l	/* kernel page table */
	mfspr	r1,SPRN_SRR1		/* and MSR_PR bit from SRR1 */
	rlwinm	r1,r1,32-12,29,29	/* shift MSR_PR to _PAGE_USER posn */
112:	tophys(r2,r2)
	rlwimi	r2,r3,12,20,29		/* insert top 10 bits of address */
	lwz	r2,0(r2)		/* get pmd entry */
	rlwinm.	r2,r2,0,0,19		/* extract address of pte page */
	beq-	DataAddressInvalid	/* return if no mapping */
	rlwimi	r2,r3,22,20,29		/* insert next 10 bits of address */
	lwz	r3,0(r2)		/* get linux-style pte */
	andc.	r1,r1,r3		/* check access & ~permission */
	bne-	DataAddressInvalid	/* return if access not permitted */
	ori	r3,r3,_PAGE_ACCESSED	/* set _PAGE_ACCESSED in pte */
	/*
	 * NOTE! We are assuming this is not an SMP system, otherwise
	 * we would need to update the pte atomically with lwarx/stwcx.
	 */
	stw	r3,0(r2)		/* update PTE (accessed bit) */
	/* Convert linux-style PTE to low word of PPC-style PTE */
	rlwinm	r1,r3,32-10,31,31	/* _PAGE_RW -> PP lsb */
	rlwinm	r2,r3,32-7,31,31	/* _PAGE_DIRTY -> PP lsb */
	and	r1,r1,r2		/* writable if _RW and _DIRTY */
	rlwimi	r3,r3,32-1,30,30	/* _PAGE_USER -> PP msb */
	rlwimi	r3,r3,32-1,31,31	/* _PAGE_USER -> PP lsb */
	ori	r1,r1,0xe14		/* clear out reserved bits and M */
	andc	r1,r3,r1		/* PP = user? (rw&dirty? 2: 3): 0 */
	mtspr	SPRN_RPA,r1
	mfspr	r3,SPRN_DMISS
	tlbld	r3
	mfspr	r3,SPRN_SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r3
	rfi
DataAddressInvalid:
	mfspr	r3,SPRN_SRR1
	rlwinm	r1,r3,9,6,6	/* Get load/store bit */
	addis	r1,r1,0x2000
	mtspr	SPRN_DSISR,r1
	mtctr	r0		/* Restore CTR */
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
	. = 0x1200
DataStoreTLBMiss:
/*
 * r0:	stored ctr
 * r1:	linux style pte ( later becomes ppc hardware pte )
 * r2:	ptr to linux-style pte
 * r3:	scratch
 */
	mfctr	r0
	/* Get PTE (linux-style) and check access */
	mfspr	r3,SPRN_DMISS
	lis	r1,KERNELBASE@h		/* check if kernel address */
	cmplw	0,r3,r1
	mfspr	r2,SPRN_SPRG3
	li	r1,_PAGE_RW|_PAGE_USER|_PAGE_PRESENT /* access flags */
	lwz	r2,PGDIR(r2)
	blt+	112f
	lis	r2,swapper_pg_dir@ha	/* if kernel address, use */
	addi	r2,r2,swapper_pg_dir@l	/* kernel page table */
	mfspr	r1,SPRN_SRR1		/* and MSR_PR bit from SRR1 */
	rlwinm	r1,r1,32-12,29,29	/* shift MSR_PR to _PAGE_USER posn */
112:	tophys(r2,r2)
	rlwimi	r2,r3,12,20,29		/* insert top 10 bits of address */
	lwz	r2,0(r2)		/* get pmd entry */
	rlwinm.	r2,r2,0,0,19		/* extract address of pte page */
	beq-	DataAddressInvalid	/* return if no mapping */
	rlwimi	r2,r3,22,20,29		/* insert next 10 bits of address */
	lwz	r3,0(r2)		/* get linux-style pte */
	andc.	r1,r1,r3		/* check access & ~permission */
	bne-	DataAddressInvalid	/* return if access not permitted */
	ori	r3,r3,_PAGE_ACCESSED|_PAGE_DIRTY
	/*
	 * NOTE! We are assuming this is not an SMP system, otherwise
	 * we would need to update the pte atomically with lwarx/stwcx.
	 */
	stw	r3,0(r2)		/* update PTE (accessed/dirty bits) */
	/* Convert linux-style PTE to low word of PPC-style PTE */
	rlwimi	r3,r3,32-1,30,30	/* _PAGE_USER -> PP msb */
	li	r1,0xe15		/* clear out reserved bits and M */
	andc	r1,r3,r1		/* PP = user? 2: 0 */
	mtspr	SPRN_RPA,r1
	mfspr	r3,SPRN_DMISS
	tlbld	r3
	mfspr	r3,SPRN_SRR1		/* Need to restore CR0 */
	mtcrf	0x80,r3
	rfi

#ifndef CONFIG_ALTIVEC
#define altivec_assist_exception	unknown_exception
#endif

	EXCEPTION(0x1300, Trap_13, instruction_breakpoint_exception, EXC_XFER_EE)
	EXCEPTION(0x1400, SMI, SMIException, EXC_XFER_EE)
	EXCEPTION(0x1500, Trap_15, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x1600, Trap_16, altivec_assist_exception, EXC_XFER_EE)
	EXCEPTION(0x1700, Trap_17, TAUException, EXC_XFER_STD)
	EXCEPTION(0x1800, Trap_18, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x1900, Trap_19, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x1a00, Trap_1a, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x1b00, Trap_1b, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x1c00, Trap_1c, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x1d00, Trap_1d, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x1e00, Trap_1e, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x1f00, Trap_1f, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x2000, RunMode, RunModeException, EXC_XFER_EE)
	EXCEPTION(0x2100, Trap_21, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x2200, Trap_22, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x2300, Trap_23, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x2400, Trap_24, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x2500, Trap_25, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x2600, Trap_26, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x2700, Trap_27, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x2800, Trap_28, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x2900, Trap_29, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x2a00, Trap_2a, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x2b00, Trap_2b, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x2c00, Trap_2c, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x2d00, Trap_2d, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x2e00, Trap_2e, unknown_exception, EXC_XFER_EE)
	EXCEPTION(0x2f00, MOLTrampoline, unknown_exception, EXC_XFER_EE_LITE)

	.globl mol_trampoline
	.set mol_trampoline, i0x2f00

	. = 0x3000

AltiVecUnavailable:
	EXCEPTION_PROLOG
#ifdef CONFIG_ALTIVEC
	bne	load_up_altivec		/* if from user, just load it up */
#endif /* CONFIG_ALTIVEC */
	addi	r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_EE_LITE(0xf20, altivec_unavailable_exception)

#ifdef CONFIG_ALTIVEC
/* Note that the AltiVec support is closely modeled after the FP
 * support.  Changes to one are likely to be applicable to the
 * other!  */
load_up_altivec:
/*
 * Disable AltiVec for the task which had AltiVec previously,
 * and save its AltiVec registers in its thread_struct.
 * Enables AltiVec for use in the kernel on return.
 * On SMP we know the AltiVec units are free, since we give it up every
 * switch.  -- Kumar
 */
	mfmsr	r5
	oris	r5,r5,MSR_VEC@h
	MTMSRD(r5)			/* enable use of AltiVec now */
	isync
/*
 * For SMP, we don't do lazy AltiVec switching because it just gets too
 * horrendously complex, especially when a task switches from one CPU
 * to another.  Instead we call giveup_altivec in switch_to.
 */
#ifndef CONFIG_SMP
	tophys(r6,0)
	addis	r3,r6,last_task_used_altivec@ha
	lwz	r4,last_task_used_altivec@l(r3)
	cmpwi	0,r4,0
	beq	1f
	add	r4,r4,r6
	addi	r4,r4,THREAD	/* want THREAD of last_task_used_altivec */
	SAVE_32VRS(0,r10,r4)
	mfvscr	vr0
	li	r10,THREAD_VSCR
	stvx	vr0,r10,r4
	lwz	r5,PT_REGS(r4)
	add	r5,r5,r6
	lwz	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	lis	r10,MSR_VEC@h
	andc	r4,r4,r10	/* disable altivec for previous task */
	stw	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#endif /* CONFIG_SMP */
	/* enable use of AltiVec after return */
	oris	r9,r9,MSR_VEC@h
	mfspr	r5,SPRN_SPRG3		/* current task's THREAD (phys) */
	li	r4,1
	li	r10,THREAD_VSCR
	stw	r4,THREAD_USED_VR(r5)
	lvx	vr0,r10,r5
	mtvscr	vr0
	REST_32VRS(0,r10,r5)
#ifndef CONFIG_SMP
	subi	r4,r5,THREAD
	sub	r4,r4,r6
	stw	r4,last_task_used_altivec@l(r3)
#endif /* CONFIG_SMP */
	/* restore registers and return */
	/* we haven't used ctr or xer or lr */
	b	fast_exception_return

/*
 * AltiVec unavailable trap from kernel - print a message, but let
 * the task use AltiVec in the kernel until it returns to user mode.
 */
KernelAltiVec:
	lwz	r3,_MSR(r1)
	oris	r3,r3,MSR_VEC@h
	stw	r3,_MSR(r1)	/* enable use of AltiVec after return */
	lis	r3,87f@h
	ori	r3,r3,87f@l
	mr	r4,r2		/* current */
	lwz	r5,_NIP(r1)
	bl	printk
	b	ret_from_except
87:	.string	"AltiVec used in kernel  (task=%p, pc=%x)  \n"
	.align	4,0

/*
 * giveup_altivec(tsk)
 * Disable AltiVec for the task given as the argument,
 * and save the AltiVec registers in its thread_struct.
 * Enables AltiVec for use in the kernel on return.
 */

	.globl	giveup_altivec
giveup_altivec:
	mfmsr	r5
	oris	r5,r5,MSR_VEC@h
	SYNC
	MTMSRD(r5)			/* enable use of AltiVec now */
	isync
	cmpwi	0,r3,0
	beqlr-				/* if no previous owner, done */
	addi	r3,r3,THREAD		/* want THREAD of task */
	lwz	r5,PT_REGS(r3)
	cmpwi	0,r5,0
	SAVE_32VRS(0, r4, r3)
	mfvscr	vr0
	li	r4,THREAD_VSCR
	stvx	vr0,r4,r3
	beq	1f
	lwz	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	lis	r3,MSR_VEC@h
	andc	r4,r4,r3		/* disable AltiVec for previous task */
	stw	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#ifndef CONFIG_SMP
	li	r5,0
	lis	r4,last_task_used_altivec@ha
	stw	r5,last_task_used_altivec@l(r4)
#endif /* CONFIG_SMP */
	blr
#endif /* CONFIG_ALTIVEC */

/*
 * This code is jumped to from the startup code to copy
 * the kernel image to physical address 0.
 */
relocate_kernel:
	addis	r9,r26,klimit@ha	/* fetch klimit */
	lwz	r25,klimit@l(r9)
	addis	r25,r25,-KERNELBASE@h
	li	r3,0			/* Destination base address */
	li	r6,0			/* Destination offset */
	li	r5,0x4000		/* # bytes of memory to copy */
	bl	copy_and_flush		/* copy the first 0x4000 bytes */
	addi	r0,r3,4f@l		/* jump to the address of 4f */
	mtctr	r0			/* in copy and do the rest. */
	bctr				/* jump to the copy */
4:	mr	r5,r25
	bl	copy_and_flush		/* copy the rest */
	b	turn_on_mmu

/*
 * Copy routine used to copy the kernel to start at physical address 0
 * and flush and invalidate the caches as needed.
 * r3 = dest addr, r4 = source addr, r5 = copy limit, r6 = start offset
 * on exit, r3, r4, r5 are unchanged, r6 is updated to be >= r5.
 */
copy_and_flush:
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

#ifdef CONFIG_APUS
/*
 * On APUS the physical base address of the kernel is not known at compile
 * time, which means the __pa/__va constants used are incorrect. In the
 * __init section is recorded the virtual addresses of instructions using
 * these constants, so all that has to be done is fix these before
 * continuing the kernel boot.
 *
 * r4 = The physical address of the kernel base.
 */
fix_mem_constants:
	mr	r10,r4
	addis	r10,r10,-KERNELBASE@h    /* virt_to_phys constant */
	neg	r11,r10	                 /* phys_to_virt constant */

	lis	r12,__vtop_table_begin@h
	ori	r12,r12,__vtop_table_begin@l
	add	r12,r12,r10	         /* table begin phys address */
	lis	r13,__vtop_table_end@h
	ori	r13,r13,__vtop_table_end@l
	add	r13,r13,r10	         /* table end phys address */
	subi	r12,r12,4
	subi	r13,r13,4
1:	lwzu	r14,4(r12)               /* virt address of instruction */
	add     r14,r14,r10              /* phys address of instruction */
	lwz     r15,0(r14)               /* instruction, now insert top */
	rlwimi  r15,r10,16,16,31         /* half of vp const in low half */
	stw	r15,0(r14)               /* of instruction and restore. */
	dcbst	r0,r14			 /* write it to memory */
	sync
	icbi	r0,r14			 /* flush the icache line */
	cmpw	r12,r13
	bne     1b
	sync				/* additional sync needed on g4 */
	isync

/*
 * Map the memory where the exception handlers will
 * be copied to when hash constants have been patched.
 */
#ifdef CONFIG_APUS_FAST_EXCEPT
	lis	r8,0xfff0
#else
	lis	r8,0
#endif
	ori	r8,r8,0x2		/* 128KB, supervisor */
	mtspr	SPRN_DBAT3U,r8
	mtspr	SPRN_DBAT3L,r8

	lis	r12,__ptov_table_begin@h
	ori	r12,r12,__ptov_table_begin@l
	add	r12,r12,r10	         /* table begin phys address */
	lis	r13,__ptov_table_end@h
	ori	r13,r13,__ptov_table_end@l
	add	r13,r13,r10	         /* table end phys address */
	subi	r12,r12,4
	subi	r13,r13,4
1:	lwzu	r14,4(r12)               /* virt address of instruction */
	add     r14,r14,r10              /* phys address of instruction */
	lwz     r15,0(r14)               /* instruction, now insert top */
	rlwimi  r15,r11,16,16,31         /* half of pv const in low half*/
	stw	r15,0(r14)               /* of instruction and restore. */
	dcbst	r0,r14			 /* write it to memory */
	sync
	icbi	r0,r14			 /* flush the icache line */
	cmpw	r12,r13
	bne     1b

	sync				/* additional sync needed on g4 */
	isync				/* No speculative loading until now */
	blr

/***********************************************************************
 *  Please note that on APUS the exception handlers are located at the
 *  physical address 0xfff0000. For this reason, the exception handlers
 *  cannot use relative branches to access the code below.
 ***********************************************************************/
#endif /* CONFIG_APUS */

#ifdef CONFIG_SMP
#ifdef CONFIG_GEMINI
	.globl	__secondary_start_gemini
__secondary_start_gemini:
        mfspr   r4,SPRN_HID0
        ori     r4,r4,HID0_ICFI
        li      r3,0
        ori     r3,r3,HID0_ICE
        andc    r4,r4,r3
        mtspr   SPRN_HID0,r4
        sync
        b       __secondary_start
#endif /* CONFIG_GEMINI */

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
	SYNC
	mtmsr	r0
	isync

	.globl	__secondary_start
__secondary_start:
	/* Copy some CPU settings from CPU 0 */
	bl	__restore_cpu_setup

	lis	r3,-KERNELBASE@h
	mr	r4,r24
	bl	call_setup_cpu		/* Call setup_cpu for this CPU */
#ifdef CONFIG_6xx
	lis	r3,-KERNELBASE@h
	bl	init_idle_6xx
#endif /* CONFIG_6xx */

	/* get current_thread_info and current */
	lis	r1,secondary_ti@ha
	tophys(r1,r1)
	lwz	r1,secondary_ti@l(r1)
	tophys(r2,r1)
	lwz	r2,TI_TASK(r2)

	/* stack */
	addi	r1,r1,THREAD_SIZE-STACK_FRAME_OVERHEAD
	li	r0,0
	tophys(r3,r1)
	stw	r0,0(r3)

	/* load up the MMU */
	bl	load_up_mmu

	/* ptr to phys current thread */
	tophys(r4,r2)
	addi	r4,r4,THREAD	/* phys address of our thread_struct */
	CLR_TOP32(r4)
	mtspr	SPRN_SPRG3,r4
	li	r3,0
	mtspr	SPRN_SPRG2,r3	/* 0 => not in RTAS */

	/* enable MMU and jump to start_secondary */
	li	r4,MSR_KERNEL
	FIX_SRR1(r4,r5)
	lis	r3,start_secondary@h
	ori	r3,r3,start_secondary@l
	mtspr	SPRN_SRR0,r3
	mtspr	SPRN_SRR1,r4
	SYNC
	RFI
#endif /* CONFIG_SMP */

/*
 * Those generic dummy functions are kept for CPUs not
 * included in CONFIG_6xx
 */
#if !defined(CONFIG_6xx)
_GLOBAL(__save_cpu_setup)
	blr
_GLOBAL(__restore_cpu_setup)
	blr
#endif /* !defined(CONFIG_6xx) */


/*
 * Load stuff into the MMU.  Intended to be called with
 * IR=0 and DR=0.
 */
load_up_mmu:
	sync			/* Force all PTE updates to finish */
	isync
	tlbia			/* Clear all TLB entries */
	sync			/* wait for tlbia/tlbie to finish */
	TLBSYNC			/* ... on all CPUs */
	/* Load the SDR1 register (hash table base & size) */
	lis	r6,_SDR1@ha
	tophys(r6,r6)
	lwz	r6,_SDR1@l(r6)
	mtspr	SPRN_SDR1,r6
	li	r0,16		/* load up segment register values */
	mtctr	r0		/* for context 0 */
	lis	r3,0x2000	/* Ku = 1, VSID = 0 */
	li	r4,0
3:	mtsrin	r3,r4
	addi	r3,r3,0x111	/* increment VSID */
	addis	r4,r4,0x1000	/* address of next segment */
	bdnz	3b

/* Load the BAT registers with the values set up by MMU_init.
   MMU_init takes care of whether we're on a 601 or not. */
	mfpvr	r3
	srwi	r3,r3,16
	cmpwi	r3,1
	lis	r3,BATS@ha
	addi	r3,r3,BATS@l
	tophys(r3,r3)
	LOAD_BAT(0,r3,r4,r5)
	LOAD_BAT(1,r3,r4,r5)
	LOAD_BAT(2,r3,r4,r5)
	LOAD_BAT(3,r3,r4,r5)

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
	CLR_TOP32(r4)
	mtspr	SPRN_SPRG3,r4
	li	r3,0
	mtspr	SPRN_SPRG2,r3	/* 0 => not in RTAS */

	/* stack */
	lis	r1,init_thread_union@ha
	addi	r1,r1,init_thread_union@l
	li	r0,0
	stwu	r0,THREAD_SIZE-STACK_FRAME_OVERHEAD(r1)
/*
 * Do early bootinfo parsing, platform-specific initialization,
 * and set up the MMU.
 */
	mr	r3,r31
	mr	r4,r30
	mr	r5,r29
	mr	r6,r28
	mr	r7,r27
	bl	machine_init
	bl	MMU_init

#ifdef CONFIG_APUS
	/* Copy exception code to exception vector base on APUS. */
	lis	r4,KERNELBASE@h
#ifdef CONFIG_APUS_FAST_EXCEPT
	lis	r3,0xfff0		/* Copy to 0xfff00000 */
#else
	lis	r3,0			/* Copy to 0x00000000 */
#endif
	li	r5,0x4000		/* # bytes of memory to copy */
	li	r6,0
	bl	copy_and_flush		/* copy the first 0x4000 bytes */
#endif  /* CONFIG_APUS */

/*
 * Go back to running unmapped so we can load up new values
 * for SDR1 (hash table pointer) and the segment registers
 * and change to using our exception vectors.
 */
	lis	r4,2f@h
	ori	r4,r4,2f@l
	tophys(r4,r4)
	li	r3,MSR_KERNEL & ~(MSR_IR|MSR_DR)
	FIX_SRR1(r3,r5)
	mtspr	SPRN_SRR0,r4
	mtspr	SPRN_SRR1,r3
	SYNC
	RFI
/* Load up the kernel context */
2:	bl	load_up_mmu

#ifdef CONFIG_BDI_SWITCH
	/* Add helper information for the Abatron bdiGDB debugger.
	 * We do this here because we know the mmu is disabled, and
	 * will be enabled for real in just a few instructions.
	 */
	lis	r5, abatron_pteptrs@h
	ori	r5, r5, abatron_pteptrs@l
	stw	r5, 0xf0(r0)	/* This much match your Abatron config */
	lis	r6, swapper_pg_dir@h
	ori	r6, r6, swapper_pg_dir@l
	tophys(r5, r5)
	stw	r6, 0(r5)
#endif /* CONFIG_BDI_SWITCH */

/* Now turn on the MMU for real! */
	li	r4,MSR_KERNEL
	FIX_SRR1(r4,r5)
	lis	r3,start_kernel@h
	ori	r3,r3,start_kernel@l
	mtspr	SPRN_SRR0,r3
	mtspr	SPRN_SRR1,r4
	SYNC
	RFI

/*
 * Set up the segment registers for a new context.
 */
_GLOBAL(set_context)
	mulli	r3,r3,897	/* multiply context by skew factor */
	rlwinm	r3,r3,4,8,27	/* VSID = (context & 0xfffff) << 4 */
	addis	r3,r3,0x6000	/* Set Ks, Ku bits */
	li	r0,NUM_USER_SEGMENTS
	mtctr	r0

#ifdef CONFIG_BDI_SWITCH
	/* Context switch the PTE pointer for the Abatron BDI2000.
	 * The PGDIR is passed as second argument.
	 */
	lis	r5, KERNELBASE@h
	lwz	r5, 0xf0(r5)
	stw	r4, 0x4(r5)
#endif
	li	r4,0
	isync
3:
	mtsrin	r3,r4
	addi	r3,r3,0x111	/* next VSID */
	rlwinm	r3,r3,0,8,3	/* clear out any overflow from VSID field */
	addis	r4,r4,0x1000	/* address of next segment */
	bdnz	3b
	sync
	isync
	blr

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
	mfspr	r9,SPRN_PVR
	rlwinm	r9,r9,16,16,31		/* r9 = 1 for 601, 4 for 604 */
	cmpwi	r9, 1
	beq	1f

	mtspr	SPRN_DBAT0U,r10
	mtspr	SPRN_DBAT0L,r10
	mtspr	SPRN_DBAT1U,r10
	mtspr	SPRN_DBAT1L,r10
	mtspr	SPRN_DBAT2U,r10
	mtspr	SPRN_DBAT2L,r10
	mtspr	SPRN_DBAT3U,r10
	mtspr	SPRN_DBAT3L,r10
1:
	mtspr	SPRN_IBAT0U,r10
	mtspr	SPRN_IBAT0L,r10
	mtspr	SPRN_IBAT1U,r10
	mtspr	SPRN_IBAT1L,r10
	mtspr	SPRN_IBAT2U,r10
	mtspr	SPRN_IBAT2L,r10
	mtspr	SPRN_IBAT3U,r10
	mtspr	SPRN_IBAT3L,r10
BEGIN_FTR_SECTION
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
END_FTR_SECTION_IFSET(CPU_FTR_HAS_HIGH_BATS)
	blr

flush_tlbs:
	lis	r10, 0x40
1:	addic.	r10, r10, -0x1000
	tlbie	r10
	blt	1b
	sync
	blr

mmu_off:
 	addi	r4, r3, __after_mmu_off - _start
	mfmsr	r3
	andi.	r0,r3,MSR_DR|MSR_IR		/* MMU enabled? */
	beqlr
	andc	r3,r3,r0
	mtspr	SPRN_SRR0,r4
	mtspr	SPRN_SRR1,r3
	sync
	RFI

/*
 * Use the first pair of BAT registers to map the 1st 16MB
 * of RAM to KERNELBASE.  From this point on we can't safely
 * call OF any more.
 */
initial_bats:
	lis	r11,KERNELBASE@h
	mfspr	r9,SPRN_PVR
	rlwinm	r9,r9,16,16,31		/* r9 = 1 for 601, 4 for 604 */
	cmpwi	0,r9,1
	bne	4f
	ori	r11,r11,4		/* set up BAT registers for 601 */
	li	r8,0x7f			/* valid, block length = 8MB */
	oris	r9,r11,0x800000@h	/* set up BAT reg for 2nd 8M */
	oris	r10,r8,0x800000@h	/* set up BAT reg for 2nd 8M */
	mtspr	SPRN_IBAT0U,r11		/* N.B. 601 has valid bit in */
	mtspr	SPRN_IBAT0L,r8		/* lower BAT register */
	mtspr	SPRN_IBAT1U,r9
	mtspr	SPRN_IBAT1L,r10
	isync
	blr

4:	tophys(r8,r11)
#ifdef CONFIG_SMP
	ori	r8,r8,0x12		/* R/W access, M=1 */
#else
	ori	r8,r8,2			/* R/W access */
#endif /* CONFIG_SMP */
#ifdef CONFIG_APUS
	ori	r11,r11,BL_8M<<2|0x2	/* set up 8MB BAT registers for 604 */
#else
	ori	r11,r11,BL_256M<<2|0x2	/* set up BAT registers for 604 */
#endif /* CONFIG_APUS */

	mtspr	SPRN_DBAT0L,r8		/* N.B. 6xx (not 601) have valid */
	mtspr	SPRN_DBAT0U,r11		/* bit in upper BAT register */
	mtspr	SPRN_IBAT0L,r8
	mtspr	SPRN_IBAT0U,r11
	isync
	blr

#if !defined(CONFIG_APUS) && defined(CONFIG_BOOTX_TEXT)
setup_disp_bat:
	/*
	 * setup the display bat prepared for us in prom.c
	 */
	mflr	r8
	bl	reloc_offset
	mtlr	r8
	addis	r8,r3,disp_BAT@ha
	addi	r8,r8,disp_BAT@l
	lwz	r11,0(r8)
	lwz	r8,4(r8)
	mfspr	r9,SPRN_PVR
	rlwinm	r9,r9,16,16,31		/* r9 = 1 for 601, 4 for 604 */
	cmpwi	0,r9,1
	beq	1f
	mtspr	SPRN_DBAT3L,r8
	mtspr	SPRN_DBAT3U,r11
	blr
1:	mtspr	SPRN_IBAT3L,r8
	mtspr	SPRN_IBAT3U,r11
	blr

#endif /* !defined(CONFIG_APUS) && defined(CONFIG_BOOTX_TEXT) */

#ifdef CONFIG_8260
/* Jump into the system reset for the rom.
 * We first disable the MMU, and then jump to the ROM reset address.
 *
 * r3 is the board info structure, r4 is the location for starting.
 * I use this for building a small kernel that can load other kernels,
 * rather than trying to write or rely on a rom monitor that can tftp load.
 */
       .globl  m8260_gorom
m8260_gorom:
	mfmsr	r0
	rlwinm	r0,r0,0,17,15	/* clear MSR_EE in r0 */
	sync
	mtmsr	r0
	sync
	mfspr	r11, SPRN_HID0
	lis	r10, 0
	ori	r10,r10,HID0_ICE|HID0_DCE
	andc	r11, r11, r10
	mtspr	SPRN_HID0, r11
	isync
	li	r5, MSR_ME|MSR_RI
	lis	r6,2f@h
	addis	r6,r6,-KERNELBASE@h
	ori	r6,r6,2f@l
	mtspr	SPRN_SRR0,r6
	mtspr	SPRN_SRR1,r5
	isync
	sync
	rfi
2:
	mtlr	r4
	blr
#endif


/*
 * We put a few things here that have to be page-aligned.
 * This stuff goes at the beginning of the data segment,
 * which is page-aligned.
 */
	.data
	.globl	sdata
sdata:
	.globl	empty_zero_page
empty_zero_page:
	.space	4096

	.globl	swapper_pg_dir
swapper_pg_dir:
	.space	4096

/*
 * This space gets a copy of optional info passed to us by the bootstrap
 * Used to pass parameters into the kernel like root=/dev/sda1, etc.
 */
	.globl	cmd_line
cmd_line:
	.space	512

	.globl intercept_table
intercept_table:
	.long 0, 0, i0x200, i0x300, i0x400, 0, i0x600, i0x700
	.long i0x800, 0, 0, 0, 0, i0xd00, 0, 0
	.long 0, 0, 0, i0x1300, 0, 0, 0, 0
	.long 0, 0, 0, 0, 0, 0, 0, 0
	.long 0, 0, 0, 0, 0, 0, 0, 0
	.long 0, 0, 0, 0, 0, 0, 0, 0

/* Room for two PTE pointers, usually the kernel and current user pointers
 * to their respective root page table.
 */
abatron_pteptrs:
	.space	8

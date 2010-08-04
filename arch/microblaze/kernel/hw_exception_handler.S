/*
 * Exception handling for Microblaze
 *
 * Rewriten interrupt handling
 *
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 *
 * uClinux customisation (C) 2005 John Williams
 *
 * MMU code derived from arch/ppc/kernel/head_4xx.S:
 *	Copyright (C) 1995-1996 Gary Thomas <gdt@linuxppc.org>
 *		Initial PowerPC version.
 *	Copyright (C) 1996 Cort Dougan <cort@cs.nmt.edu>
 *		Rewritten for PReP
 *	Copyright (C) 1996 Paul Mackerras <paulus@cs.anu.edu.au>
 *		Low-level exception handers, MMU support, and rewrite.
 *	Copyright (C) 1997 Dan Malek <dmalek@jlc.net>
 *		PowerPC 8xx modifications.
 *	Copyright (C) 1998-1999 TiVo, Inc.
 *		PowerPC 403GCX modifications.
 *	Copyright (C) 1999 Grant Erickson <grant@lcse.umn.edu>
 *		PowerPC 403GCX/405GP modifications.
 *	Copyright 2000 MontaVista Software Inc.
 *		PPC405 modifications
 *	PowerPC 403GCX/405GP modifications.
 *		Author: MontaVista Software, Inc.
 *		frank_rowand@mvista.com or source@mvista.com
 *		debbie_chu@mvista.com
 *
 * Original code
 * Copyright (C) 2004 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

/*
 * Here are the handlers which don't require enabling translation
 * and calling other kernel code thus we can keep their design very simple
 * and do all processing in real mode. All what they need is a valid current
 * (that is an issue for the CONFIG_REGISTER_TASK_PTR case)
 * This handlers use r3,r4,r5,r6 and optionally r[current] to work therefore
 * these registers are saved/restored
 * The handlers which require translation are in entry.S --KAA
 *
 * Microblaze HW Exception Handler
 * - Non self-modifying exception handler for the following exception conditions
 *   - Unalignment
 *   - Instruction bus error
 *   - Data bus error
 *   - Illegal instruction opcode
 *   - Divide-by-zero
 *
 *   - Privileged instruction exception (MMU)
 *   - Data storage exception (MMU)
 *   - Instruction storage exception (MMU)
 *   - Data TLB miss exception (MMU)
 *   - Instruction TLB miss exception (MMU)
 *
 * Note we disable interrupts during exception handling, otherwise we will
 * possibly get multiple re-entrancy if interrupt handles themselves cause
 * exceptions. JW
 */

#include <asm/exceptions.h>
#include <asm/unistd.h>
#include <asm/page.h>

#include <asm/entry.h>
#include <asm/current.h>
#include <linux/linkage.h>

#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/signal.h>
#include <asm/asm-offsets.h>

/* Helpful Macros */
#ifndef CONFIG_MMU
#define EX_HANDLER_STACK_SIZ	(4*19)
#endif
#define NUM_TO_REG(num)		r ## num

#ifdef CONFIG_MMU
	#define RESTORE_STATE			\
		lwi	r5, r1, 0;		\
		mts	rmsr, r5;		\
		nop;				\
		lwi	r3, r1, PT_R3;		\
		lwi	r4, r1, PT_R4;		\
		lwi	r5, r1, PT_R5;		\
		lwi	r6, r1, PT_R6;		\
		lwi	r11, r1, PT_R11;	\
		lwi	r31, r1, PT_R31;	\
		lwi	r1, r0, TOPHYS(r0_ram + 0);
#endif /* CONFIG_MMU */

#define LWREG_NOP			\
	bri	ex_handler_unhandled;	\
	nop;

#define SWREG_NOP			\
	bri	ex_handler_unhandled;	\
	nop;

/* FIXME this is weird - for noMMU kernel is not possible to use brid
 * instruction which can shorten executed time
 */

/* r3 is the source */
#define R3_TO_LWREG_V(regnum)				\
	swi	r3, r1, 4 * regnum;				\
	bri	ex_handler_done;

/* r3 is the source */
#define R3_TO_LWREG(regnum)				\
	or	NUM_TO_REG (regnum), r0, r3;		\
	bri	ex_handler_done;

/* r3 is the target */
#define SWREG_TO_R3_V(regnum)				\
	lwi	r3, r1, 4 * regnum;				\
	bri	ex_sw_tail;

/* r3 is the target */
#define SWREG_TO_R3(regnum)				\
	or	r3, r0, NUM_TO_REG (regnum);		\
	bri	ex_sw_tail;

#ifdef CONFIG_MMU
	#define R3_TO_LWREG_VM_V(regnum)		\
		brid	ex_lw_end_vm;			\
		swi	r3, r7, 4 * regnum;

	#define R3_TO_LWREG_VM(regnum)			\
		brid	ex_lw_end_vm;			\
		or	NUM_TO_REG (regnum), r0, r3;

	#define SWREG_TO_R3_VM_V(regnum)		\
		brid	ex_sw_tail_vm;			\
		lwi	r3, r7, 4 * regnum;

	#define SWREG_TO_R3_VM(regnum)			\
		brid	ex_sw_tail_vm;			\
		or	r3, r0, NUM_TO_REG (regnum);

	/* Shift right instruction depending on available configuration */
	#if CONFIG_XILINX_MICROBLAZE0_USE_BARREL > 0
	#define BSRLI(rD, rA, imm)	\
		bsrli rD, rA, imm
	#elif CONFIG_XILINX_MICROBLAZE0_USE_DIV > 0
	#define BSRLI(rD, rA, imm)	\
		ori rD, r0, (1 << imm);	\
		idivu rD, rD, rA
	#else
	#define BSRLI(rD, rA, imm) BSRLI ## imm (rD, rA)
	/* Only the used shift constants defined here - add more if needed */
	#define BSRLI2(rD, rA)				\
		srl rD, rA;		/* << 1 */	\
		srl rD, rD;		/* << 2 */
	#define BSRLI10(rD, rA)				\
		srl rD, rA;		/* << 1 */	\
		srl rD, rD;		/* << 2 */	\
		srl rD, rD;		/* << 3 */	\
		srl rD, rD;		/* << 4 */	\
		srl rD, rD;		/* << 5 */	\
		srl rD, rD;		/* << 6 */	\
		srl rD, rD;		/* << 7 */	\
		srl rD, rD;		/* << 8 */	\
		srl rD, rD;		/* << 9 */	\
		srl rD, rD		/* << 10 */
	#define BSRLI20(rD, rA)		\
		BSRLI10(rD, rA);	\
		BSRLI10(rD, rD)
	#endif
#endif /* CONFIG_MMU */

.extern other_exception_handler /* Defined in exception.c */

/*
 * hw_exception_handler - Handler for exceptions
 *
 * Exception handler notes:
 * - Handles all exceptions
 * - Does not handle unaligned exceptions during load into r17, r1, r0.
 * - Does not handle unaligned exceptions during store from r17 (cannot be
 *   done) and r1 (slows down common case)
 *
 *  Relevant register structures
 *
 *  EAR - |----|----|----|----|----|----|----|----|
 *      - <  ##   32 bit faulting address     ##  >
 *
 *  ESR - |----|----|----|----|----| - | - |-----|-----|
 *      -                            W   S   REG   EXC
 *
 *
 * STACK FRAME STRUCTURE (for NO_MMU)
 * ---------------------------------
 *
 *      +-------------+         + 0
 *      |     MSR     |
 *      +-------------+         + 4
 *      |     r1      |
 *      |      .      |
 *      |      .      |
 *      |      .      |
 *      |      .      |
 *      |     r18     |
 *      +-------------+         + 76
 *      |      .      |
 *      |      .      |
 *
 * NO_MMU kernel use the same r0_ram pointed space - look to vmlinux.lds.S
 * which is used for storing register values - old style was, that value were
 * stored in stack but in case of failure you lost information about register.
 * Currently you can see register value in memory in specific place.
 * In compare to with previous solution the speed should be the same.
 *
 * MMU exception handler has different handling compare to no MMU kernel.
 * Exception handler use jump table for directing of what happen. For MMU kernel
 * is this approach better because MMU relate exception are handled by asm code
 * in this file. In compare to with MMU expect of unaligned exception
 * is everything handled by C code.
 */

/*
 * every of these handlers is entered having R3/4/5/6/11/current saved on stack
 * and clobbered so care should be taken to restore them if someone is going to
 * return from exception
 */

/* wrappers to restore state before coming to entry.S */

#ifdef CONFIG_MMU
.section .rodata
.align 4
_MB_HW_ExceptionVectorTable:
/*  0 - Undefined */
	.long	TOPHYS(ex_handler_unhandled)
/*  1 - Unaligned data access exception */
	.long	TOPHYS(handle_unaligned_ex)
/*  2 - Illegal op-code exception */
	.long	TOPHYS(full_exception_trapw)
/*  3 - Instruction bus error exception */
	.long	TOPHYS(full_exception_trapw)
/*  4 - Data bus error exception */
	.long	TOPHYS(full_exception_trapw)
/*  5 - Divide by zero exception */
	.long	TOPHYS(full_exception_trapw)
/*  6 - Floating point unit exception */
	.long	TOPHYS(full_exception_trapw)
/*  7 - Privileged instruction exception */
	.long	TOPHYS(full_exception_trapw)
/*  8 - 15 - Undefined */
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
/* 16 - Data storage exception */
	.long	TOPHYS(handle_data_storage_exception)
/* 17 - Instruction storage exception */
	.long	TOPHYS(handle_instruction_storage_exception)
/* 18 - Data TLB miss exception */
	.long	TOPHYS(handle_data_tlb_miss_exception)
/* 19 - Instruction TLB miss exception */
	.long	TOPHYS(handle_instruction_tlb_miss_exception)
/* 20 - 31 - Undefined */
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
	.long	TOPHYS(ex_handler_unhandled)
#endif

.global _hw_exception_handler
.section .text
.align 4
.ent _hw_exception_handler
_hw_exception_handler:
#ifndef CONFIG_MMU
	addik	r1, r1, -(EX_HANDLER_STACK_SIZ); /* Create stack frame */
#else
	swi	r1, r0, TOPHYS(r0_ram + 0); /* GET_SP */
	/* Save date to kernel memory. Here is the problem
	 * when you came from user space */
	ori	r1, r0, TOPHYS(r0_ram + 28);
#endif
	swi	r3, r1, PT_R3
	swi	r4, r1, PT_R4
	swi	r5, r1, PT_R5
	swi	r6, r1, PT_R6

#ifdef CONFIG_MMU
	swi	r11, r1, PT_R11
	swi	r31, r1, PT_R31
	lwi	r31, r0, TOPHYS(PER_CPU(CURRENT_SAVE)) /* get saved current */
#endif

	mfs	r5, rmsr;
	nop
	swi	r5, r1, 0;
	mfs	r4, resr
	nop
	mfs	r3, rear;
	nop

#ifndef CONFIG_MMU
	andi	r5, r4, 0x1000;		/* Check ESR[DS] */
	beqi	r5, not_in_delay_slot;	/* Branch if ESR[DS] not set */
	mfs	r17, rbtr;	/* ESR[DS] set - return address in BTR */
	nop
not_in_delay_slot:
	swi	r17, r1, PT_R17
#endif

	andi	r5, r4, 0x1F;		/* Extract ESR[EXC] */

#ifdef CONFIG_MMU
	/* Calculate exception vector offset = r5 << 2 */
	addk	r6, r5, r5; /* << 1 */
	addk	r6, r6, r6; /* << 2 */

#ifdef DEBUG
/* counting which exception happen */
	lwi	r5, r0, 0x200 + TOPHYS(r0_ram)
	addi	r5, r5, 1
	swi	r5, r0, 0x200 + TOPHYS(r0_ram)
	lwi	r5, r6, 0x200 + TOPHYS(r0_ram)
	addi	r5, r5, 1
	swi	r5, r6, 0x200 + TOPHYS(r0_ram)
#endif
/* end */
	/* Load the HW Exception vector */
	lwi	r6, r6, TOPHYS(_MB_HW_ExceptionVectorTable)
	bra	r6

full_exception_trapw:
	RESTORE_STATE
	bri	full_exception_trap
#else
	/* Exceptions enabled here. This will allow nested exceptions */
	mfs	r6, rmsr;
	nop
	swi	r6, r1, 0; /* RMSR_OFFSET */
	ori	r6, r6, 0x100; /* Turn ON the EE bit */
	andi	r6, r6, ~2; /* Disable interrupts */
	mts	rmsr, r6;
	nop

	xori	r6, r5, 1; /* 00001 = Unaligned Exception */
	/* Jump to unalignment exception handler */
	beqi	r6, handle_unaligned_ex;

handle_other_ex: /* Handle Other exceptions here */
	/* Save other volatiles before we make procedure calls below */
	swi	r7, r1, PT_R7
	swi	r8, r1, PT_R8
	swi	r9, r1, PT_R9
	swi	r10, r1, PT_R10
	swi	r11, r1, PT_R11
	swi	r12, r1, PT_R12
	swi	r14, r1, PT_R14
	swi	r15, r1, PT_R15
	swi	r18, r1, PT_R18

	or	r5, r1, r0
	andi	r6, r4, 0x1F; /* Load ESR[EC] */
	lwi	r7, r0, PER_CPU(KM) /* MS: saving current kernel mode to regs */
	swi	r7, r1, PT_MODE
	mfs	r7, rfsr
	nop
	addk	r8, r17, r0; /* Load exception address */
	bralid	r15, full_exception; /* Branch to the handler */
	nop;
	mts	rfsr, r0;	/* Clear sticky fsr */
	nop

	/*
	 * Trigger execution of the signal handler by enabling
	 * interrupts and calling an invalid syscall.
	 */
	mfs	r5, rmsr;
	nop
	ori	r5, r5, 2;
	mts	rmsr, r5; /* enable interrupt */
	nop
	addi	r12, r0, __NR_syscalls;
	brki	r14, 0x08;
	mfs	r5, rmsr; /* disable interrupt */
	nop
	andi	r5, r5, ~2;
	mts	rmsr, r5;
	nop

	lwi	r7, r1, PT_R7
	lwi	r8, r1, PT_R8
	lwi	r9, r1, PT_R9
	lwi	r10, r1, PT_R10
	lwi	r11, r1, PT_R11
	lwi	r12, r1, PT_R12
	lwi	r14, r1, PT_R14
	lwi	r15, r1, PT_R15
	lwi	r18, r1, PT_R18

	bri	ex_handler_done; /* Complete exception handling */
#endif

/* 0x01 - Unaligned data access exception
 * This occurs when a word access is not aligned on a word boundary,
 * or when a 16-bit access is not aligned on a 16-bit boundary.
 * This handler perform the access, and returns, except for MMU when
 * the unaligned address is last on a 4k page or the physical address is
 * not found in the page table, in which case unaligned_data_trap is called.
 */
handle_unaligned_ex:
	/* Working registers already saved: R3, R4, R5, R6
	 *  R4 = ESR
	 *  R3 = EAR
	 */
#ifdef CONFIG_MMU
	andi	r6, r4, 0x1000			/* Check ESR[DS] */
	beqi	r6, _no_delayslot		/* Branch if ESR[DS] not set */
	mfs	r17, rbtr;	/* ESR[DS] set - return address in BTR */
	nop
_no_delayslot:
	/* jump to high level unaligned handler */
	RESTORE_STATE;
	bri	unaligned_data_trap
#endif
	andi	r6, r4, 0x3E0; /* Mask and extract the register operand */
	srl	r6, r6; /* r6 >> 5 */
	srl	r6, r6;
	srl	r6, r6;
	srl	r6, r6;
	srl	r6, r6;
	/* Store the register operand in a temporary location */
	sbi	r6, r0, TOPHYS(ex_reg_op);

	andi	r6, r4, 0x400; /* Extract ESR[S] */
	bnei	r6, ex_sw;
ex_lw:
	andi	r6, r4, 0x800; /* Extract ESR[W] */
	beqi	r6, ex_lhw;
	lbui	r5, r3, 0; /* Exception address in r3 */
	/* Load a word, byte-by-byte from destination address
		and save it in tmp space */
	sbi	r5, r0, TOPHYS(ex_tmp_data_loc_0);
	lbui	r5, r3, 1;
	sbi	r5, r0, TOPHYS(ex_tmp_data_loc_1);
	lbui	r5, r3, 2;
	sbi	r5, r0, TOPHYS(ex_tmp_data_loc_2);
	lbui	r5, r3, 3;
	sbi	r5, r0, TOPHYS(ex_tmp_data_loc_3);
	/* Get the destination register value into r4 */
	lwi	r4, r0, TOPHYS(ex_tmp_data_loc_0);
	bri	ex_lw_tail;
ex_lhw:
	lbui	r5, r3, 0; /* Exception address in r3 */
	/* Load a half-word, byte-by-byte from destination
		address and save it in tmp space */
	sbi	r5, r0, TOPHYS(ex_tmp_data_loc_0);
	lbui	r5, r3, 1;
	sbi	r5, r0, TOPHYS(ex_tmp_data_loc_1);
	/* Get the destination register value into r4 */
	lhui	r4, r0, TOPHYS(ex_tmp_data_loc_0);
ex_lw_tail:
	/* Get the destination register number into r5 */
	lbui	r5, r0, TOPHYS(ex_reg_op);
	/* Form load_word jump table offset (lw_table + (8 * regnum)) */
	la	r6, r0, TOPHYS(lw_table);
	addk	r5, r5, r5;
	addk	r5, r5, r5;
	addk	r5, r5, r5;
	addk	r5, r5, r6;
	bra	r5;
ex_lw_end: /* Exception handling of load word, ends */
ex_sw:
	/* Get the destination register number into r5 */
	lbui	r5, r0, TOPHYS(ex_reg_op);
	/* Form store_word jump table offset (sw_table + (8 * regnum)) */
	la	r6, r0, TOPHYS(sw_table);
	add	r5, r5, r5;
	add	r5, r5, r5;
	add	r5, r5, r5;
	add	r5, r5, r6;
	bra	r5;
ex_sw_tail:
	mfs	r6, resr;
	nop
	andi	r6, r6, 0x800; /* Extract ESR[W] */
	beqi	r6, ex_shw;
	/* Get the word - delay slot */
	swi	r4, r0, TOPHYS(ex_tmp_data_loc_0);
	/* Store the word, byte-by-byte into destination address */
	lbui	r4, r0, TOPHYS(ex_tmp_data_loc_0);
	sbi	r4, r3, 0;
	lbui	r4, r0, TOPHYS(ex_tmp_data_loc_1);
	sbi	r4, r3, 1;
	lbui	r4, r0, TOPHYS(ex_tmp_data_loc_2);
	sbi	r4, r3, 2;
	lbui	r4, r0, TOPHYS(ex_tmp_data_loc_3);
	sbi	r4, r3, 3;
	bri	ex_handler_done;

ex_shw:
	/* Store the lower half-word, byte-by-byte into destination address */
	swi	r4, r0, TOPHYS(ex_tmp_data_loc_0);
	lbui	r4, r0, TOPHYS(ex_tmp_data_loc_2);
	sbi	r4, r3, 0;
	lbui	r4, r0, TOPHYS(ex_tmp_data_loc_3);
	sbi	r4, r3, 1;
ex_sw_end: /* Exception handling of store word, ends. */

ex_handler_done:
#ifndef CONFIG_MMU
	lwi	r5, r1, 0 /* RMSR */
	mts	rmsr, r5
	nop
	lwi	r3, r1, PT_R3
	lwi	r4, r1, PT_R4
	lwi	r5, r1, PT_R5
	lwi	r6, r1, PT_R6
	lwi	r17, r1, PT_R17

	rted	r17, 0
	addik	r1, r1, (EX_HANDLER_STACK_SIZ); /* Restore stack frame */
#else
	RESTORE_STATE;
	rted	r17, 0
	nop
#endif

#ifdef CONFIG_MMU
	/* Exception vector entry code. This code runs with address translation
	 * turned off (i.e. using physical addresses). */

	/* Exception vectors. */

	/* 0x10 - Data Storage Exception
	 * This happens for just a few reasons. U0 set (but we don't do that),
	 * or zone protection fault (user violation, write to protected page).
	 * If this is just an update of modified status, we do that quickly
	 * and exit. Otherwise, we call heavyweight functions to do the work.
	 */
	handle_data_storage_exception:
		/* Working registers already saved: R3, R4, R5, R6
		 * R3 = ESR
		 */
		mfs	r11, rpid
		nop
		/* If we are faulting a kernel address, we have to use the
		 * kernel page tables.
		 */
		ori	r5, r0, CONFIG_KERNEL_START
		cmpu	r5, r3, r5
		bgti	r5, ex3
		/* First, check if it was a zone fault (which means a user
		 * tried to access a kernel or read-protected page - always
		 * a SEGV). All other faults here must be stores, so no
		 * need to check ESR_S as well. */
		andi	r4, r4, 0x800		/* ESR_Z - zone protection */
		bnei	r4, ex2

		ori	r4, r0, swapper_pg_dir
		mts	rpid, r0		/* TLB will have 0 TID */
		nop
		bri	ex4

		/* Get the PGD for the current thread. */
	ex3:
		/* First, check if it was a zone fault (which means a user
		 * tried to access a kernel or read-protected page - always
		 * a SEGV). All other faults here must be stores, so no
		 * need to check ESR_S as well. */
		andi	r4, r4, 0x800		/* ESR_Z */
		bnei	r4, ex2
		/* get current task address */
		addi	r4 ,CURRENT_TASK, TOPHYS(0);
		lwi	r4, r4, TASK_THREAD+PGDIR
	ex4:
		tophys(r4,r4)
		BSRLI(r5,r3,20)		/* Create L1 (pgdir/pmd) address */
		andi	r5, r5, 0xffc
/* Assume pgdir aligned on 4K boundary, no need for "andi r4,r4,0xfffff003" */
		or	r4, r4, r5
		lwi	r4, r4, 0		/* Get L1 entry */
		andi	r5, r4, 0xfffff000 /* Extract L2 (pte) base address */
		beqi	r5, ex2			/* Bail if no table */

		tophys(r5,r5)
		BSRLI(r6,r3,10)			/* Compute PTE address */
		andi	r6, r6, 0xffc
		andi	r5, r5, 0xfffff003
		or	r5, r5, r6
		lwi	r4, r5, 0		/* Get Linux PTE */

		andi	r6, r4, _PAGE_RW	/* Is it writeable? */
		beqi	r6, ex2			/* Bail if not */

		/* Update 'changed' */
		ori	r4, r4, _PAGE_DIRTY|_PAGE_ACCESSED|_PAGE_HWWRITE
		swi	r4, r5, 0		/* Update Linux page table */

		/* Most of the Linux PTE is ready to load into the TLB LO.
		 * We set ZSEL, where only the LS-bit determines user access.
		 * We set execute, because we don't have the granularity to
		 * properly set this at the page level (Linux problem).
		 * If shared is set, we cause a zero PID->TID load.
		 * Many of these bits are software only. Bits we don't set
		 * here we (properly should) assume have the appropriate value.
		 */
		andni	r4, r4, 0x0ce2		/* Make sure 20, 21 are zero */
		ori	r4, r4, _PAGE_HWEXEC	/* make it executable */

		/* find the TLB index that caused the fault. It has to be here*/
		mts	rtlbsx, r3
		nop
		mfs	r5, rtlbx		/* DEBUG: TBD */
		nop
		mts	rtlblo, r4		/* Load TLB LO */
		nop
						/* Will sync shadow TLBs */

		/* Done...restore registers and get out of here. */
		mts	rpid, r11
		nop
		bri 4

		RESTORE_STATE;
		rted	r17, 0
		nop
	ex2:
		/* The bailout. Restore registers to pre-exception conditions
		 * and call the heavyweights to help us out. */
		mts	rpid, r11
		nop
		bri 4
		RESTORE_STATE;
		bri	page_fault_data_trap


	/* 0x11 - Instruction Storage Exception
	 * This is caused by a fetch from non-execute or guarded pages. */
	handle_instruction_storage_exception:
		/* Working registers already saved: R3, R4, R5, R6
		 * R3 = ESR
		 */

		RESTORE_STATE;
		bri	page_fault_instr_trap

	/* 0x12 - Data TLB Miss Exception
	 * As the name implies, translation is not in the MMU, so search the
	 * page tables and fix it. The only purpose of this function is to
	 * load TLB entries from the page table if they exist.
	 */
	handle_data_tlb_miss_exception:
		/* Working registers already saved: R3, R4, R5, R6
		 * R3 = EAR, R4 = ESR
		 */
		mfs	r11, rpid
		nop

		/* If we are faulting a kernel address, we have to use the
		 * kernel page tables. */
		ori	r6, r0, CONFIG_KERNEL_START
		cmpu	r4, r3, r6
		bgti	r4, ex5
		ori	r4, r0, swapper_pg_dir
		mts	rpid, r0		/* TLB will have 0 TID */
		nop
		bri	ex6

		/* Get the PGD for the current thread. */
	ex5:
		/* get current task address */
		addi	r4 ,CURRENT_TASK, TOPHYS(0);
		lwi	r4, r4, TASK_THREAD+PGDIR
	ex6:
		tophys(r4,r4)
		BSRLI(r5,r3,20)		/* Create L1 (pgdir/pmd) address */
		andi	r5, r5, 0xffc
/* Assume pgdir aligned on 4K boundary, no need for "andi r4,r4,0xfffff003" */
		or	r4, r4, r5
		lwi	r4, r4, 0		/* Get L1 entry */
		andi	r5, r4, 0xfffff000 /* Extract L2 (pte) base address */
		beqi	r5, ex7			/* Bail if no table */

		tophys(r5,r5)
		BSRLI(r6,r3,10)			/* Compute PTE address */
		andi	r6, r6, 0xffc
		andi	r5, r5, 0xfffff003
		or	r5, r5, r6
		lwi	r4, r5, 0		/* Get Linux PTE */

		andi	r6, r4, _PAGE_PRESENT
		beqi	r6, ex7

		ori	r4, r4, _PAGE_ACCESSED
		swi	r4, r5, 0

		/* Most of the Linux PTE is ready to load into the TLB LO.
		 * We set ZSEL, where only the LS-bit determines user access.
		 * We set execute, because we don't have the granularity to
		 * properly set this at the page level (Linux problem).
		 * If shared is set, we cause a zero PID->TID load.
		 * Many of these bits are software only. Bits we don't set
		 * here we (properly should) assume have the appropriate value.
		 */
		brid	finish_tlb_load
		andni	r4, r4, 0x0ce2		/* Make sure 20, 21 are zero */
	ex7:
		/* The bailout. Restore registers to pre-exception conditions
		 * and call the heavyweights to help us out.
		 */
		mts	rpid, r11
		nop
		bri	4
		RESTORE_STATE;
		bri	page_fault_data_trap

	/* 0x13 - Instruction TLB Miss Exception
	 * Nearly the same as above, except we get our information from
	 * different registers and bailout to a different point.
	 */
	handle_instruction_tlb_miss_exception:
		/* Working registers already saved: R3, R4, R5, R6
		 *  R3 = ESR
		 */
		mfs	r11, rpid
		nop

		/* If we are faulting a kernel address, we have to use the
		 * kernel page tables.
		 */
		ori	r4, r0, CONFIG_KERNEL_START
		cmpu	r4, r3, r4
		bgti	r4, ex8
		ori	r4, r0, swapper_pg_dir
		mts	rpid, r0		/* TLB will have 0 TID */
		nop
		bri	ex9

		/* Get the PGD for the current thread. */
	ex8:
		/* get current task address */
		addi	r4 ,CURRENT_TASK, TOPHYS(0);
		lwi	r4, r4, TASK_THREAD+PGDIR
	ex9:
		tophys(r4,r4)
		BSRLI(r5,r3,20)		/* Create L1 (pgdir/pmd) address */
		andi	r5, r5, 0xffc
/* Assume pgdir aligned on 4K boundary, no need for "andi r4,r4,0xfffff003" */
		or	r4, r4, r5
		lwi	r4, r4, 0		/* Get L1 entry */
		andi	r5, r4, 0xfffff000 /* Extract L2 (pte) base address */
		beqi	r5, ex10		/* Bail if no table */

		tophys(r5,r5)
		BSRLI(r6,r3,10)			/* Compute PTE address */
		andi	r6, r6, 0xffc
		andi	r5, r5, 0xfffff003
		or	r5, r5, r6
		lwi	r4, r5, 0		/* Get Linux PTE */

		andi	r6, r4, _PAGE_PRESENT
		beqi	r6, ex10

		ori	r4, r4, _PAGE_ACCESSED
		swi	r4, r5, 0

		/* Most of the Linux PTE is ready to load into the TLB LO.
		 * We set ZSEL, where only the LS-bit determines user access.
		 * We set execute, because we don't have the granularity to
		 * properly set this at the page level (Linux problem).
		 * If shared is set, we cause a zero PID->TID load.
		 * Many of these bits are software only. Bits we don't set
		 * here we (properly should) assume have the appropriate value.
		 */
		brid	finish_tlb_load
		andni	r4, r4, 0x0ce2		/* Make sure 20, 21 are zero */
	ex10:
		/* The bailout. Restore registers to pre-exception conditions
		 * and call the heavyweights to help us out.
		 */
		mts	rpid, r11
		nop
		bri 4
		RESTORE_STATE;
		bri	page_fault_instr_trap

/* Both the instruction and data TLB miss get to this point to load the TLB.
 *	r3 - EA of fault
 *	r4 - TLB LO (info from Linux PTE)
 *	r5, r6 - available to use
 *	PID - loaded with proper value when we get here
 *	Upon exit, we reload everything and RFI.
 * A common place to load the TLB.
 */
	tlb_index:
		.long	1 /* MS: storing last used tlb index */
	finish_tlb_load:
		/* MS: load the last used TLB index. */
		lwi	r5, r0, TOPHYS(tlb_index)
		addik	r5, r5, 1 /* MS: inc tlb_index -> use next one */

/* MS: FIXME this is potential fault, because this is mask not count */
		andi	r5, r5, (MICROBLAZE_TLB_SIZE-1)
		ori	r6, r0, 1
		cmp	r31, r5, r6
		blti	r31, ex12
		addik	r5, r6, 1
	ex12:
		/* MS: save back current TLB index */
		swi	r5, r0, TOPHYS(tlb_index)

		ori	r4, r4, _PAGE_HWEXEC	/* make it executable */
		mts	rtlbx, r5		/* MS: save current TLB */
		nop
		mts	rtlblo,	r4		/* MS: save to TLB LO */
		nop

		/* Create EPN. This is the faulting address plus a static
		 * set of bits. These are size, valid, E, U0, and ensure
		 * bits 20 and 21 are zero.
		 */
		andi	r3, r3, 0xfffff000
		ori	r3, r3, 0x0c0
		mts	rtlbhi,	r3		/* Load TLB HI */
		nop

		/* Done...restore registers and get out of here. */
		mts	rpid, r11
		nop
		bri 4
		RESTORE_STATE;
		rted	r17, 0
		nop

	/* extern void giveup_fpu(struct task_struct *prev)
	 *
	 * The MicroBlaze processor may have an FPU, so this should not just
	 * return: TBD.
	 */
	.globl giveup_fpu;
	.align 4;
	giveup_fpu:
		bralid	r15,0			/* TBD */
		nop

	/* At present, this routine just hangs. - extern void abort(void) */
	.globl abort;
	.align 4;
	abort:
		br	r0

	.globl set_context;
	.align 4;
	set_context:
		mts	rpid, r5	/* Shadow TLBs are automatically */
		nop
		bri	4		/* flushed by changing PID */
		rtsd	r15,8
		nop

#endif
.end _hw_exception_handler

#ifdef CONFIG_MMU
/* Unaligned data access exception last on a 4k page for MMU.
 * When this is called, we are in virtual mode with exceptions enabled
 * and registers 1-13,15,17,18 saved.
 *
 * R3 = ESR
 * R4 = EAR
 * R7 = pointer to saved registers (struct pt_regs *regs)
 *
 * This handler perform the access, and returns via ret_from_exc.
 */
.global _unaligned_data_exception
.ent _unaligned_data_exception
_unaligned_data_exception:
	andi	r8, r3, 0x3E0;	/* Mask and extract the register operand */
	BSRLI(r8,r8,2);		/* r8 >> 2 = register operand * 8 */
	andi	r6, r3, 0x400;	/* Extract ESR[S] */
	bneid	r6, ex_sw_vm;
	andi	r6, r3, 0x800;	/* Extract ESR[W] - delay slot */
ex_lw_vm:
	beqid	r6, ex_lhw_vm;
load1:	lbui	r5, r4, 0;	/* Exception address in r4 - delay slot */
/* Load a word, byte-by-byte from destination address and save it in tmp space*/
	la	r6, r0, ex_tmp_data_loc_0;
	sbi	r5, r6, 0;
load2:	lbui	r5, r4, 1;
	sbi	r5, r6, 1;
load3:	lbui	r5, r4, 2;
	sbi	r5, r6, 2;
load4:	lbui	r5, r4, 3;
	sbi	r5, r6, 3;
	brid	ex_lw_tail_vm;
/* Get the destination register value into r3 - delay slot */
	lwi	r3, r6, 0;
ex_lhw_vm:
	/* Load a half-word, byte-by-byte from destination address and
	 * save it in tmp space */
	la	r6, r0, ex_tmp_data_loc_0;
	sbi	r5, r6, 0;
load5:	lbui	r5, r4, 1;
	sbi	r5, r6, 1;
	lhui	r3, r6, 0;	/* Get the destination register value into r3 */
ex_lw_tail_vm:
	/* Form load_word jump table offset (lw_table_vm + (8 * regnum)) */
	addik	r5, r8, lw_table_vm;
	bra	r5;
ex_lw_end_vm:			/* Exception handling of load word, ends */
	brai	ret_from_exc;
ex_sw_vm:
/* Form store_word jump table offset (sw_table_vm + (8 * regnum)) */
	addik	r5, r8, sw_table_vm;
	bra	r5;
ex_sw_tail_vm:
	la	r5, r0, ex_tmp_data_loc_0;
	beqid	r6, ex_shw_vm;
	swi	r3, r5, 0;	/* Get the word - delay slot */
	/* Store the word, byte-by-byte into destination address */
	lbui	r3, r5, 0;
store1:	sbi	r3, r4, 0;
	lbui	r3, r5, 1;
store2:	sbi	r3, r4, 1;
	lbui	r3, r5, 2;
store3:	sbi	r3, r4, 2;
	lbui	r3, r5, 3;
	brid	ret_from_exc;
store4:	sbi	r3, r4, 3;	/* Delay slot */
ex_shw_vm:
	/* Store the lower half-word, byte-by-byte into destination address */
	lbui	r3, r5, 2;
store5:	sbi	r3, r4, 0;
	lbui	r3, r5, 3;
	brid	ret_from_exc;
store6:	sbi	r3, r4, 1;	/* Delay slot */
ex_sw_end_vm:			/* Exception handling of store word, ends. */

/* We have to prevent cases that get/put_user macros get unaligned pointer
 * to bad page area. We have to find out which origin instruction caused it
 * and called fixup for that origin instruction not instruction in unaligned
 * handler */
ex_unaligned_fixup:
	ori	r5, r7, 0 /* setup pointer to pt_regs */
	lwi	r6, r7, PT_PC; /* faulting address is one instruction above */
	addik	r6, r6, -4 /* for finding proper fixup */
	swi	r6, r7, PT_PC; /* a save back it to PT_PC */
	addik	r7, r0, SIGSEGV
	/* call bad_page_fault for finding aligned fixup, fixup address is saved
	 * in PT_PC which is used as return address from exception */
	la	r15, r0, ret_from_exc-8 /* setup return address */
	brid	bad_page_fault
	nop

/* We prevent all load/store because it could failed any attempt to access */
.section __ex_table,"a";
	.word	load1,ex_unaligned_fixup;
	.word	load2,ex_unaligned_fixup;
	.word	load3,ex_unaligned_fixup;
	.word	load4,ex_unaligned_fixup;
	.word	load5,ex_unaligned_fixup;
	.word	store1,ex_unaligned_fixup;
	.word	store2,ex_unaligned_fixup;
	.word	store3,ex_unaligned_fixup;
	.word	store4,ex_unaligned_fixup;
	.word	store5,ex_unaligned_fixup;
	.word	store6,ex_unaligned_fixup;
.previous;
.end _unaligned_data_exception
#endif /* CONFIG_MMU */

ex_handler_unhandled:
/* FIXME add handle function for unhandled exception - dump register */
	bri 0

/*
 * hw_exception_handler Jump Table
 * - Contains code snippets for each register that caused the unalign exception
 * - Hence exception handler is NOT self-modifying
 * - Separate table for load exceptions and store exceptions.
 * - Each table is of size: (8 * 32) = 256 bytes
 */

.section .text
.align 4
lw_table:
lw_r0:		R3_TO_LWREG	(0);
lw_r1:		LWREG_NOP;
lw_r2:		R3_TO_LWREG	(2);
lw_r3:		R3_TO_LWREG_V	(3);
lw_r4:		R3_TO_LWREG_V	(4);
lw_r5:		R3_TO_LWREG_V	(5);
lw_r6:		R3_TO_LWREG_V	(6);
lw_r7:		R3_TO_LWREG	(7);
lw_r8:		R3_TO_LWREG	(8);
lw_r9:		R3_TO_LWREG	(9);
lw_r10:		R3_TO_LWREG	(10);
lw_r11:		R3_TO_LWREG	(11);
lw_r12:		R3_TO_LWREG	(12);
lw_r13:		R3_TO_LWREG	(13);
lw_r14:		R3_TO_LWREG	(14);
lw_r15:		R3_TO_LWREG	(15);
lw_r16:		R3_TO_LWREG	(16);
lw_r17:		LWREG_NOP;
lw_r18:		R3_TO_LWREG	(18);
lw_r19:		R3_TO_LWREG	(19);
lw_r20:		R3_TO_LWREG	(20);
lw_r21:		R3_TO_LWREG	(21);
lw_r22:		R3_TO_LWREG	(22);
lw_r23:		R3_TO_LWREG	(23);
lw_r24:		R3_TO_LWREG	(24);
lw_r25:		R3_TO_LWREG	(25);
lw_r26:		R3_TO_LWREG	(26);
lw_r27:		R3_TO_LWREG	(27);
lw_r28:		R3_TO_LWREG	(28);
lw_r29:		R3_TO_LWREG	(29);
lw_r30:		R3_TO_LWREG	(30);
#ifdef CONFIG_MMU
lw_r31: 	R3_TO_LWREG_V	(31);
#else
lw_r31:		R3_TO_LWREG	(31);
#endif

sw_table:
sw_r0:		SWREG_TO_R3	(0);
sw_r1:		SWREG_NOP;
sw_r2:		SWREG_TO_R3	(2);
sw_r3:		SWREG_TO_R3_V	(3);
sw_r4:		SWREG_TO_R3_V	(4);
sw_r5:		SWREG_TO_R3_V	(5);
sw_r6:		SWREG_TO_R3_V	(6);
sw_r7:		SWREG_TO_R3	(7);
sw_r8:		SWREG_TO_R3	(8);
sw_r9:		SWREG_TO_R3	(9);
sw_r10:		SWREG_TO_R3	(10);
sw_r11:		SWREG_TO_R3	(11);
sw_r12:		SWREG_TO_R3	(12);
sw_r13:		SWREG_TO_R3	(13);
sw_r14:		SWREG_TO_R3	(14);
sw_r15:		SWREG_TO_R3	(15);
sw_r16:		SWREG_TO_R3	(16);
sw_r17:		SWREG_NOP;
sw_r18:		SWREG_TO_R3	(18);
sw_r19:		SWREG_TO_R3	(19);
sw_r20:		SWREG_TO_R3	(20);
sw_r21:		SWREG_TO_R3	(21);
sw_r22:		SWREG_TO_R3	(22);
sw_r23:		SWREG_TO_R3	(23);
sw_r24:		SWREG_TO_R3	(24);
sw_r25:		SWREG_TO_R3	(25);
sw_r26:		SWREG_TO_R3	(26);
sw_r27:		SWREG_TO_R3	(27);
sw_r28:		SWREG_TO_R3	(28);
sw_r29:		SWREG_TO_R3	(29);
sw_r30:		SWREG_TO_R3	(30);
#ifdef CONFIG_MMU
sw_r31:		SWREG_TO_R3_V	(31);
#else
sw_r31:		SWREG_TO_R3	(31);
#endif

#ifdef CONFIG_MMU
lw_table_vm:
lw_r0_vm:	R3_TO_LWREG_VM		(0);
lw_r1_vm:	R3_TO_LWREG_VM_V	(1);
lw_r2_vm:	R3_TO_LWREG_VM_V	(2);
lw_r3_vm:	R3_TO_LWREG_VM_V	(3);
lw_r4_vm:	R3_TO_LWREG_VM_V	(4);
lw_r5_vm:	R3_TO_LWREG_VM_V	(5);
lw_r6_vm:	R3_TO_LWREG_VM_V	(6);
lw_r7_vm:	R3_TO_LWREG_VM_V	(7);
lw_r8_vm:	R3_TO_LWREG_VM_V	(8);
lw_r9_vm:	R3_TO_LWREG_VM_V	(9);
lw_r10_vm:	R3_TO_LWREG_VM_V	(10);
lw_r11_vm:	R3_TO_LWREG_VM_V	(11);
lw_r12_vm:	R3_TO_LWREG_VM_V	(12);
lw_r13_vm:	R3_TO_LWREG_VM_V	(13);
lw_r14_vm:	R3_TO_LWREG_VM		(14);
lw_r15_vm:	R3_TO_LWREG_VM_V	(15);
lw_r16_vm:	R3_TO_LWREG_VM		(16);
lw_r17_vm:	R3_TO_LWREG_VM_V	(17);
lw_r18_vm:	R3_TO_LWREG_VM_V	(18);
lw_r19_vm:	R3_TO_LWREG_VM		(19);
lw_r20_vm:	R3_TO_LWREG_VM		(20);
lw_r21_vm:	R3_TO_LWREG_VM		(21);
lw_r22_vm:	R3_TO_LWREG_VM		(22);
lw_r23_vm:	R3_TO_LWREG_VM		(23);
lw_r24_vm:	R3_TO_LWREG_VM		(24);
lw_r25_vm:	R3_TO_LWREG_VM		(25);
lw_r26_vm:	R3_TO_LWREG_VM		(26);
lw_r27_vm:	R3_TO_LWREG_VM		(27);
lw_r28_vm:	R3_TO_LWREG_VM		(28);
lw_r29_vm:	R3_TO_LWREG_VM		(29);
lw_r30_vm:	R3_TO_LWREG_VM		(30);
lw_r31_vm:	R3_TO_LWREG_VM_V	(31);

sw_table_vm:
sw_r0_vm:	SWREG_TO_R3_VM		(0);
sw_r1_vm:	SWREG_TO_R3_VM_V	(1);
sw_r2_vm:	SWREG_TO_R3_VM_V	(2);
sw_r3_vm:	SWREG_TO_R3_VM_V	(3);
sw_r4_vm:	SWREG_TO_R3_VM_V	(4);
sw_r5_vm:	SWREG_TO_R3_VM_V	(5);
sw_r6_vm:	SWREG_TO_R3_VM_V	(6);
sw_r7_vm:	SWREG_TO_R3_VM_V	(7);
sw_r8_vm:	SWREG_TO_R3_VM_V	(8);
sw_r9_vm:	SWREG_TO_R3_VM_V	(9);
sw_r10_vm:	SWREG_TO_R3_VM_V	(10);
sw_r11_vm:	SWREG_TO_R3_VM_V	(11);
sw_r12_vm:	SWREG_TO_R3_VM_V	(12);
sw_r13_vm:	SWREG_TO_R3_VM_V	(13);
sw_r14_vm:	SWREG_TO_R3_VM		(14);
sw_r15_vm:	SWREG_TO_R3_VM_V	(15);
sw_r16_vm:	SWREG_TO_R3_VM		(16);
sw_r17_vm:	SWREG_TO_R3_VM_V	(17);
sw_r18_vm:	SWREG_TO_R3_VM_V	(18);
sw_r19_vm:	SWREG_TO_R3_VM		(19);
sw_r20_vm:	SWREG_TO_R3_VM		(20);
sw_r21_vm:	SWREG_TO_R3_VM		(21);
sw_r22_vm:	SWREG_TO_R3_VM		(22);
sw_r23_vm:	SWREG_TO_R3_VM		(23);
sw_r24_vm:	SWREG_TO_R3_VM		(24);
sw_r25_vm:	SWREG_TO_R3_VM		(25);
sw_r26_vm:	SWREG_TO_R3_VM		(26);
sw_r27_vm:	SWREG_TO_R3_VM		(27);
sw_r28_vm:	SWREG_TO_R3_VM		(28);
sw_r29_vm:	SWREG_TO_R3_VM		(29);
sw_r30_vm:	SWREG_TO_R3_VM		(30);
sw_r31_vm:	SWREG_TO_R3_VM_V	(31);
#endif /* CONFIG_MMU */

/* Temporary data structures used in the handler */
.section .data
.align 4
ex_tmp_data_loc_0:
	.byte 0
ex_tmp_data_loc_1:
	.byte 0
ex_tmp_data_loc_2:
	.byte 0
ex_tmp_data_loc_3:
	.byte 0
ex_reg_op:
	.byte 0

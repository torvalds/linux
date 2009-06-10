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
#include <asm/asm-offsets.h>

/* Helpful Macros */
#define EX_HANDLER_STACK_SIZ	(4*19)
#define NUM_TO_REG(num)		r ## num

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

.global _hw_exception_handler
.section .text
.align 4
.ent _hw_exception_handler
_hw_exception_handler:
	addik	r1, r1, -(EX_HANDLER_STACK_SIZ); /* Create stack frame */
	swi	r3, r1, PT_R3
	swi	r4, r1, PT_R4
	swi	r5, r1, PT_R5
	swi	r6, r1, PT_R6

	mfs	r5, rmsr;
	nop
	swi	r5, r1, 0;
	mfs	r4, rbtr	/* Save BTR before jumping to handler */
	nop
	mfs	r3, resr
	nop

	andi	r5, r3, 0x1000;		/* Check ESR[DS] */
	beqi	r5, not_in_delay_slot;	/* Branch if ESR[DS] not set */
	mfs	r17, rbtr;	/* ESR[DS] set - return address in BTR */
	nop
not_in_delay_slot:
	swi	r17, r1, PT_R17

	andi	r5, r3, 0x1F;		/* Extract ESR[EXC] */

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
	andi	r6, r3, 0x1F; /* Load ESR[EC] */
	lwi	r7, r0, PER_CPU(KM) /* MS: saving current kernel mode to regs */
	swi	r7, r1, PT_MODE
	mfs	r7, rfsr
	nop
	addk	r8, r17, r0; /* Load exception address */
	bralid	r15, full_exception; /* Branch to the handler */
	nop;

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

/* 0x01 - Unaligned data access exception
 * This occurs when a word access is not aligned on a word boundary,
 * or when a 16-bit access is not aligned on a 16-bit boundary.
 * This handler perform the access, and returns, except for MMU when
 * the unaligned address is last on a 4k page or the physical address is
 * not found in the page table, in which case unaligned_data_trap is called.
 */
handle_unaligned_ex:
	/* Working registers already saved: R3, R4, R5, R6
	 *  R3 = ESR
	 *  R4 = BTR
	 */
	mfs	r4, rear;
	nop

	andi	r6, r3, 0x3E0; /* Mask and extract the register operand */
	srl	r6, r6; /* r6 >> 5 */
	srl	r6, r6;
	srl	r6, r6;
	srl	r6, r6;
	srl	r6, r6;
	/* Store the register operand in a temporary location */
	sbi	r6, r0, TOPHYS(ex_reg_op);

	andi	r6, r3, 0x400; /* Extract ESR[S] */
	bnei	r6, ex_sw;
ex_lw:
	andi	r6, r3, 0x800; /* Extract ESR[W] */
	beqi	r6, ex_lhw;
	lbui	r5, r4, 0; /* Exception address in r4 */
	/* Load a word, byte-by-byte from destination address
		and save it in tmp space */
	sbi	r5, r0, TOPHYS(ex_tmp_data_loc_0);
	lbui	r5, r4, 1;
	sbi	r5, r0, TOPHYS(ex_tmp_data_loc_1);
	lbui	r5, r4, 2;
	sbi	r5, r0, TOPHYS(ex_tmp_data_loc_2);
	lbui	r5, r4, 3;
	sbi	r5, r0, TOPHYS(ex_tmp_data_loc_3);
	/* Get the destination register value into r3 */
	lwi	r3, r0, TOPHYS(ex_tmp_data_loc_0);
	bri	ex_lw_tail;
ex_lhw:
	lbui	r5, r4, 0; /* Exception address in r4 */
	/* Load a half-word, byte-by-byte from destination
		address and save it in tmp space */
	sbi	r5, r0, TOPHYS(ex_tmp_data_loc_0);
	lbui	r5, r4, 1;
	sbi	r5, r0, TOPHYS(ex_tmp_data_loc_1);
	/* Get the destination register value into r3 */
	lhui	r3, r0, TOPHYS(ex_tmp_data_loc_0);
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
	swi	r3, r0, TOPHYS(ex_tmp_data_loc_0);
	/* Store the word, byte-by-byte into destination address */
	lbui	r3, r0, TOPHYS(ex_tmp_data_loc_0);
	sbi	r3, r4, 0;
	lbui	r3, r0, TOPHYS(ex_tmp_data_loc_1);
	sbi	r3, r4, 1;
	lbui	r3, r0, TOPHYS(ex_tmp_data_loc_2);
	sbi	r3, r4, 2;
	lbui	r3, r0, TOPHYS(ex_tmp_data_loc_3);
	sbi	r3, r4, 3;
	bri	ex_handler_done;

ex_shw:
	/* Store the lower half-word, byte-by-byte into destination address */
	swi	r3, r0, TOPHYS(ex_tmp_data_loc_0);
	lbui	r3, r0, TOPHYS(ex_tmp_data_loc_2);
	sbi	r3, r4, 0;
	lbui	r3, r0, TOPHYS(ex_tmp_data_loc_3);
	sbi	r3, r4, 1;
ex_sw_end: /* Exception handling of store word, ends. */

ex_handler_done:
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

.end _hw_exception_handler

ex_handler_unhandled:
/* FIXME add handle function for unhandled exception - dump register */
	bri 0

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
lw_r31:		R3_TO_LWREG	(31);

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
sw_r31:		SWREG_TO_R3	(31);

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

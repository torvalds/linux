/*
 *  Low level routines for legacy iSeries support.
 *
 *  Extracted from head_64.S
 *
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
 *  This file contains the low-level support and setup for the
 *  PowerPC-64 platform, including trap and interrupt dispatch.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <asm/reg.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/ptrace.h>
#include <asm/cputable.h>

#include "exception.h"

	.text

	.globl system_reset_iSeries
system_reset_iSeries:
	bl	.relative_toc
	mfspr	r13,SPRN_SPRG3		/* Get alpaca address */
	LOAD_REG_ADDR(r23, alpaca)
	li	r0,ALPACA_SIZE
	sub	r23,r13,r23
	divdu	r23,r23,r0		/* r23 has cpu number */
	LOAD_REG_ADDR(r13, paca)
	mulli	r0,r23,PACA_SIZE
	add	r13,r13,r0
	mtspr	SPRN_SPRG_PACA,r13	/* Save it away for the future */
	mfmsr	r24
	ori	r24,r24,MSR_RI
	mtmsrd	r24			/* RI on */
	mr	r24,r23
	cmpwi	0,r24,0			/* Are we processor 0? */
	bne	1f
	b	.__start_initialization_iSeries	/* Start up the first processor */
1:	mfspr	r4,SPRN_CTRLF
	li	r5,CTRL_RUNLATCH	/* Turn off the run light */
	andc	r4,r4,r5
	mtspr	SPRN_CTRLT,r4

/* Spin on __secondary_hold_spinloop until it is updated by the boot cpu. */
/* In the UP case we'll yield() later, and we will not access the paca anyway */
#ifdef CONFIG_SMP
1:
	HMT_LOW
	LOAD_REG_ADDR(r23, __secondary_hold_spinloop)
	ld	r23,0(r23)
	sync
	LOAD_REG_ADDR(r3,current_set)
	sldi	r28,r24,3		/* get current_set[cpu#] */
	ldx	r3,r3,r28
	addi	r1,r3,THREAD_SIZE
	subi	r1,r1,STACK_FRAME_OVERHEAD

	cmpwi	0,r23,0			/* Keep poking the Hypervisor until */
	bne	2f			/* we're released */
	/* Let the Hypervisor know we are alive */
	/* 8002 is a call to HvCallCfg::getLps, a harmless Hypervisor function */
	lis	r3,0x8002
	rldicr	r3,r3,32,15		/* r0 = (r3 << 32) & 0xffff000000000000 */
	li	r0,-1			/* r0=-1 indicates a Hypervisor call */
	sc				/* Invoke the hypervisor via a system call */
	b	1b
#endif

2:
	HMT_LOW
#ifdef CONFIG_SMP
	lbz	r23,PACAPROCSTART(r13)	/* Test if this processor
					 * should start */
	sync
	LOAD_REG_ADDR(r3,current_set)
	sldi	r28,r24,3		/* get current_set[cpu#] */
	ldx	r3,r3,r28
	addi	r1,r3,THREAD_SIZE
	subi	r1,r1,STACK_FRAME_OVERHEAD

	cmpwi	0,r23,0
	beq	iSeries_secondary_smp_loop	/* Loop until told to go */
	b	__secondary_start		/* Loop until told to go */
iSeries_secondary_smp_loop:
	/* Let the Hypervisor know we are alive */
	/* 8002 is a call to HvCallCfg::getLps, a harmless Hypervisor function */
	lis	r3,0x8002
	rldicr	r3,r3,32,15		/* r0 = (r3 << 32) & 0xffff000000000000 */
#else /* CONFIG_SMP */
	/* Yield the processor.  This is required for non-SMP kernels
		which are running on multi-threaded machines. */
	lis	r3,0x8000
	rldicr	r3,r3,32,15		/* r3 = (r3 << 32) & 0xffff000000000000 */
	addi	r3,r3,18		/* r3 = 0x8000000000000012 which is "yield" */
	li	r4,0			/* "yield timed" */
	li	r5,-1			/* "yield forever" */
#endif /* CONFIG_SMP */
	li	r0,-1			/* r0=-1 indicates a Hypervisor call */
	sc				/* Invoke the hypervisor via a system call */
	mfspr	r13,SPRN_SPRG_PACA	/* Put r13 back ???? */
	b	2b			/* If SMP not configured, secondaries
					 * loop forever */

/***  ISeries-LPAR interrupt handlers ***/

	STD_EXCEPTION_ISERIES(machine_check, PACA_EXMC)

	.globl data_access_iSeries
data_access_iSeries:
	mtspr	SPRN_SPRG_SCRATCH0,r13
BEGIN_FTR_SECTION
	mfspr	r13,SPRN_SPRG_PACA
	std	r9,PACA_EXSLB+EX_R9(r13)
	std	r10,PACA_EXSLB+EX_R10(r13)
	mfspr	r10,SPRN_DAR
	mfspr	r9,SPRN_DSISR
	srdi	r10,r10,60
	rlwimi	r10,r9,16,0x20
	mfcr	r9
	cmpwi	r10,0x2c
	beq	.do_stab_bolted_iSeries
	ld	r10,PACA_EXSLB+EX_R10(r13)
	std	r11,PACA_EXGEN+EX_R11(r13)
	ld	r11,PACA_EXSLB+EX_R9(r13)
	std	r12,PACA_EXGEN+EX_R12(r13)
	mfspr	r12,SPRN_SPRG_SCRATCH0
	std	r10,PACA_EXGEN+EX_R10(r13)
	std	r11,PACA_EXGEN+EX_R9(r13)
	std	r12,PACA_EXGEN+EX_R13(r13)
	EXCEPTION_PROLOG_ISERIES_1
FTR_SECTION_ELSE
	EXCEPTION_PROLOG_1(PACA_EXGEN)
	EXCEPTION_PROLOG_ISERIES_1
ALT_FTR_SECTION_END_IFCLR(CPU_FTR_SLB)
	b	data_access_common

.do_stab_bolted_iSeries:
	std	r11,PACA_EXSLB+EX_R11(r13)
	std	r12,PACA_EXSLB+EX_R12(r13)
	mfspr	r10,SPRN_SPRG_SCRATCH0
	std	r10,PACA_EXSLB+EX_R13(r13)
	EXCEPTION_PROLOG_ISERIES_1
	b	.do_stab_bolted

	.globl	data_access_slb_iSeries
data_access_slb_iSeries:
	mtspr	SPRN_SPRG_SCRATCH0,r13	/* save r13 */
	mfspr	r13,SPRN_SPRG_PACA	/* get paca address into r13 */
	std	r3,PACA_EXSLB+EX_R3(r13)
	mfspr	r3,SPRN_DAR
	std	r9,PACA_EXSLB+EX_R9(r13)
	mfcr	r9
#ifdef __DISABLED__
	cmpdi	r3,0
	bge	slb_miss_user_iseries
#endif
	std	r10,PACA_EXSLB+EX_R10(r13)
	std	r11,PACA_EXSLB+EX_R11(r13)
	std	r12,PACA_EXSLB+EX_R12(r13)
	mfspr	r10,SPRN_SPRG_SCRATCH0
	std	r10,PACA_EXSLB+EX_R13(r13)
	ld	r12,PACALPPACAPTR(r13)
	ld	r12,LPPACASRR1(r12)
	b	.slb_miss_realmode

	STD_EXCEPTION_ISERIES(instruction_access, PACA_EXGEN)

	.globl	instruction_access_slb_iSeries
instruction_access_slb_iSeries:
	mtspr	SPRN_SPRG_SCRATCH0,r13	/* save r13 */
	mfspr	r13,SPRN_SPRG_PACA	/* get paca address into r13 */
	std	r3,PACA_EXSLB+EX_R3(r13)
	ld	r3,PACALPPACAPTR(r13)
	ld	r3,LPPACASRR0(r3)	/* get SRR0 value */
	std	r9,PACA_EXSLB+EX_R9(r13)
	mfcr	r9
#ifdef __DISABLED__
	cmpdi	r3,0
	bge	slb_miss_user_iseries
#endif
	std	r10,PACA_EXSLB+EX_R10(r13)
	std	r11,PACA_EXSLB+EX_R11(r13)
	std	r12,PACA_EXSLB+EX_R12(r13)
	mfspr	r10,SPRN_SPRG_SCRATCH0
	std	r10,PACA_EXSLB+EX_R13(r13)
	ld	r12,PACALPPACAPTR(r13)
	ld	r12,LPPACASRR1(r12)
	b	.slb_miss_realmode

#ifdef __DISABLED__
slb_miss_user_iseries:
	std	r10,PACA_EXGEN+EX_R10(r13)
	std	r11,PACA_EXGEN+EX_R11(r13)
	std	r12,PACA_EXGEN+EX_R12(r13)
	mfspr	r10,SPRG_SCRATCH0
	ld	r11,PACA_EXSLB+EX_R9(r13)
	ld	r12,PACA_EXSLB+EX_R3(r13)
	std	r10,PACA_EXGEN+EX_R13(r13)
	std	r11,PACA_EXGEN+EX_R9(r13)
	std	r12,PACA_EXGEN+EX_R3(r13)
	EXCEPTION_PROLOG_ISERIES_1
	b	slb_miss_user_common
#endif

	MASKABLE_EXCEPTION_ISERIES(hardware_interrupt)
	STD_EXCEPTION_ISERIES(alignment, PACA_EXGEN)
	STD_EXCEPTION_ISERIES(program_check, PACA_EXGEN)
	STD_EXCEPTION_ISERIES(fp_unavailable, PACA_EXGEN)
	MASKABLE_EXCEPTION_ISERIES(decrementer)
	STD_EXCEPTION_ISERIES(trap_0a, PACA_EXGEN)
	STD_EXCEPTION_ISERIES(trap_0b, PACA_EXGEN)

	.globl	system_call_iSeries
system_call_iSeries:
	mr	r9,r13
	mfspr	r13,SPRN_SPRG_PACA
	EXCEPTION_PROLOG_ISERIES_1
	b	system_call_common

	STD_EXCEPTION_ISERIES(single_step, PACA_EXGEN)
	STD_EXCEPTION_ISERIES(trap_0e, PACA_EXGEN)
	STD_EXCEPTION_ISERIES(performance_monitor, PACA_EXGEN)

decrementer_iSeries_masked:
	/* We may not have a valid TOC pointer in here. */
	li	r11,1
	ld	r12,PACALPPACAPTR(r13)
	stb	r11,LPPACADECRINT(r12)
	LOAD_REG_IMMEDIATE(r12, tb_ticks_per_jiffy)
	lwz	r12,0(r12)
	mtspr	SPRN_DEC,r12
	/* fall through */

hardware_interrupt_iSeries_masked:
	mtcrf	0x80,r9		/* Restore regs */
	ld	r12,PACALPPACAPTR(r13)
	ld	r11,LPPACASRR0(r12)
	ld	r12,LPPACASRR1(r12)
	mtspr	SPRN_SRR0,r11
	mtspr	SPRN_SRR1,r12
	ld	r9,PACA_EXGEN+EX_R9(r13)
	ld	r10,PACA_EXGEN+EX_R10(r13)
	ld	r11,PACA_EXGEN+EX_R11(r13)
	ld	r12,PACA_EXGEN+EX_R12(r13)
	ld	r13,PACA_EXGEN+EX_R13(r13)
	rfid
	b	.	/* prevent speculative execution */

_INIT_STATIC(__start_initialization_iSeries)
	/* Clear out the BSS */
	LOAD_REG_ADDR(r11,__bss_stop)
	LOAD_REG_ADDR(r8,__bss_start)
	sub	r11,r11,r8		/* bss size			*/
	addi	r11,r11,7		/* round up to an even double word */
	rldicl. r11,r11,61,3		/* shift right by 3		*/
	beq	4f
	addi	r8,r8,-8
	li	r0,0
	mtctr	r11			/* zero this many doublewords	*/
3:	stdu	r0,8(r8)
	bdnz	3b
4:
	LOAD_REG_ADDR(r1,init_thread_union)
	addi	r1,r1,THREAD_SIZE
	li	r0,0
	stdu	r0,-STACK_FRAME_OVERHEAD(r1)

	bl	.iSeries_early_setup
	bl	.early_setup

	/* relocation is on at this point */

	b	.start_here_common

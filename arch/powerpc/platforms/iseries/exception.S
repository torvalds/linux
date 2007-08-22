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

	.text

	.globl system_reset_iSeries
system_reset_iSeries:
	mfspr	r13,SPRN_SPRG3		/* Get paca address */
	mfmsr	r24
	ori	r24,r24,MSR_RI
	mtmsrd	r24			/* RI on */
	lhz	r24,PACAPACAINDEX(r13)	/* Get processor # */
	cmpwi	0,r24,0			/* Are we processor 0? */
	bne	1f
	b	.__start_initialization_iSeries	/* Start up the first processor */
1:	mfspr	r4,SPRN_CTRLF
	li	r5,CTRL_RUNLATCH	/* Turn off the run light */
	andc	r4,r4,r5
	mtspr	SPRN_CTRLT,r4

1:
	HMT_LOW
#ifdef CONFIG_SMP
	lbz	r23,PACAPROCSTART(r13)	/* Test if this processor
					 * should start */
	sync
	LOAD_REG_IMMEDIATE(r3,current_set)
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
	mfspr	r13,SPRN_SPRG3		/* Put r13 back ???? */
	b	1b			/* If SMP not configured, secondaries
					 * loop forever */

_INIT_STATIC(__start_initialization_iSeries)
	/* Clear out the BSS */
	LOAD_REG_IMMEDIATE(r11,__bss_stop)
	LOAD_REG_IMMEDIATE(r8,__bss_start)
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
	LOAD_REG_IMMEDIATE(r1,init_thread_union)
	addi	r1,r1,THREAD_SIZE
	li	r0,0
	stdu	r0,-STACK_FRAME_OVERHEAD(r1)

	LOAD_REG_IMMEDIATE(r2,__toc_start)
	addi	r2,r2,0x4000
	addi	r2,r2,0x4000

	bl	.iSeries_early_setup
	bl	.early_setup

	/* relocation is on at this point */

	b	.start_here_common

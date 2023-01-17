/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include "abort-macro.S"
/*
 * Function: v6_early_abort
 *
 * Params  : r2 = pt_regs
 *	   : r4 = aborted context pc
 *	   : r5 = aborted context psr
 *
 * Returns : r4 - r11, r13 preserved
 *
 * Purpose : obtain information about current aborted instruction.
 * Note: we read user space.  This means we might cause a data
 * abort here if the I-TLB and D-TLB aren't seeing the same
 * picture.  Unfortunately, this does happen.  We live with it.
 */
	.arch	armv6k
	.align	5
ENTRY(v6_early_abort)
	mrc	p15, 0, r1, c5, c0, 0		@ get FSR
	mrc	p15, 0, r0, c6, c0, 0		@ get FAR
/*
 * Faulty SWP instruction on 1136 doesn't set bit 11 in DFSR.
 */
#ifdef CONFIG_ARM_ERRATA_326103
	ldr	ip, =0x4107b36
	mrc	p15, 0, r3, c0, c0, 0		@ get processor id
	teq	ip, r3, lsr #4			@ r0 ARM1136?
	bne	1f
	tst	r5, #PSR_J_BIT			@ Java?
	tsteq	r5, #PSR_T_BIT			@ Thumb?
	bne	1f
	bic	r1, r1, #1 << 11		@ clear bit 11 of FSR
	ldr	r3, [r4]			@ read aborted ARM instruction
 ARM_BE8(rev	r3, r3)

	teq_ldrd tmp=ip, insn=r3		@ insn was LDRD?
	beq	1f				@ yes
	tst	r3, #1 << 20			@ L = 0 -> write
	orreq	r1, r1, #1 << 11		@ yes.
#endif
1:	uaccess_disable ip			@ disable userspace access
	b	do_DataAbort

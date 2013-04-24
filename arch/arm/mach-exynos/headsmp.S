/*
 *  linux/arch/arm/mach-exynos4/headsmp.S
 *
 *  Cloned from linux/arch/arm/mach-realview/headsmp.S
 *
 *  Copyright (c) 2003 ARM Limited
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>

/*
 * exynos4 specific entry point for secondary CPUs.  This provides
 * a "holding pen" into which all secondary cores are held until we're
 * ready for them to initialise.
 */

.arch_extension sec
.arch_extension virt
.text

.align 5
/* We use the same vector table for Hyp and Monitor mode, since
 * we will only use each once and they don't overlap.
 */
mon_vectors:
	W(b)	.	/* reset */
	W(b)	.	/* undef */
	W(b)	2f	/* smc */
	W(b)	.	/* pabt */
	W(b)	.	/* dabt */
	W(b)	1f	/* hyp */
	W(b)	.	/* irq */
	W(b)	.	/* fiq */

/* Return directly back to the caller without leaving Hyp mode: */
1:	mrs	lr, elr_hyp
	mov	pc, lr

/* In monitor mode, set up HVBAR and SCR then return to caller in NS-SVC. */
2:
	mrc	p15, 0, r1, c1, c1, 0		@ SCR
	/*
	 * Set SCR.NS=1(needed for setting HVBAR and also returning to NS state)
	 *        .IRQ,FIQ,EA=0 (don't take aborts/exceptions to Monitor mode)
	 *        .FW,AW=1 (CPSR.A,F modifiable in NS state)
	 *        .nET=0 (early termination OK)
	 *        .SCD=0 (SMC in NS mode OK, so we can call secure firmware)
	 *        .HCE=1 (HVC does Hyp call)
	 */
	bic	r1, r1, #0x07f
	ldr	r2, =0x131
	orr	r1, r1, r2
	mcr	p15, 0, r2, c1, c1, 0		@ SCR
	isb
	ldr	r2, =mon_vectors


	adr	r4, 1f
	ldmia	r4, {r5}
	sub	r4, r4, r5
	add	r2, r2, r4

	mcr	p15, 4, r2, c12, c0, 0		@ set HVBAR

 THUMB(	mrc	p15, 4, r2, c1, c0, 0 )		@ ctrl register
 THUMB(	orr	r2, r2, #1 << 30 )		@ HSCTLR.TE (Thumb exceptions)
 THUMB(	mcr	p15, 4, r2, c1, c0, 0 )
 THUMB(	isb)

	/* ...and return to calling code in NS state */
	movs	pc, lr


	.globl monitor_init
monitor_init:
	ldr	ip, =mon_vectors

	adr	r4, 1f
	ldmia	r4, {r5}
	sub	r4, r4, r5
	add	ip, ip, r4
	mcr	p15, 0, ip, c12, c0, 1

 THUMB(	mrc     p15, 0, r1, c1, c0, 0 )		@ ctrl register
 THUMB( orr     r1, r1, #1 << 30 )		@ SCTLR.TE (Thumb exceptions)
 THUMB( mcr     p15, 0, r1, c1, c0, 0 )
 THUMB( isb )

	mov	pc, lr

	/* Try to go into NS-SVC: void enter_ns(void); */
	.globl enter_ns
enter_ns:
	smc	#0
	mov	pc, lr

	/* void enter_hyp(void); */
	.globl enter_hyp
enter_hyp:
	/* Now we're in NS-SVC, make a Hyp call to get into Hyp mode */
	mov	r0, lr
	mov	r1, sp
	hvc	#0
	/* We will end up here in NS-Hyp. */
	mov	sp, r1
	mov	pc, r0

ENTRY(exynos4_secondary_startup)
	mrc	p15, 0, r0, c0, c0, 5
	and	r0, r0, #0xffffff
	adr	r4, 1f
	ldmia	r4, {r5, r6}
	sub	r4, r4, r5
	add	r6, r6, r4
pen:	ldr	r7, [r6]
	cmp	r7, r0
	bne	pen

	ldr	r1, =__boot_cpu_mode
	add	r1, r1, r4
	ldr	r2, [r1]
	mrs     r0, cpsr
	ands	r0, r0, #MODE_MASK
	subs	r1, r0, r2
	beq	3f
	subs	r2, r2, #HYP_MODE
	bne	3f

	/* Setting NSACR to allow coprocessor access from non-secure mode */
	mrc	p15, 0, r0, c1, c1, 2
	movw	r1, #0x3fff
	orr	r0, r0, r1
	mcr	p15, 0, r0, c1, c1, 2
5:
	bl	monitor_init
	bl	enter_ns
	bl	enter_hyp

3:
	/*
	 * we've been released from the holding pen: secondary_stack
	 * should now contain the SVC stack for this core
	 */
	b	secondary_startup
ENDPROC(exynos4_secondary_startup)

	.align 2
1:	.long	.
	.long	pen_release

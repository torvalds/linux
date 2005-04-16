/*
 *  linux/arch/arm/lib/copy_page-armv4mc.S
 *
 *  Copyright (C) 1995-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  ASM optimised string functions
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/constants.h>

	.text
	.align	5
/*
 * ARMv4 mini-dcache optimised copy_user_page
 *
 * We flush the destination cache lines just before we write the data into the
 * corresponding address.  Since the Dcache is read-allocate, this removes the
 * Dcache aliasing issue.  The writes will be forwarded to the write buffer,
 * and merged as appropriate.
 *
 * Note: We rely on all ARMv4 processors implementing the "invalidate D line"
 * instruction.  If your processor does not supply this, you have to write your
 * own copy_user_page that does the right thing.
 */
ENTRY(v4_mc_copy_user_page)
	stmfd	sp!, {r4, lr}			@ 2
	mov	r4, r0
	mov	r0, r1
	bl	map_page_minicache
	mov	r1, #PAGE_SZ/64			@ 1
	ldmia	r0!, {r2, r3, ip, lr}		@ 4
1:	mcr	p15, 0, r4, c7, c6, 1		@ 1   invalidate D line
	stmia	r4!, {r2, r3, ip, lr}		@ 4
	ldmia	r0!, {r2, r3, ip, lr}		@ 4+1
	stmia	r4!, {r2, r3, ip, lr}		@ 4
	ldmia	r0!, {r2, r3, ip, lr}		@ 4
	mcr	p15, 0, r4, c7, c6, 1		@ 1   invalidate D line
	stmia	r4!, {r2, r3, ip, lr}		@ 4
	ldmia	r0!, {r2, r3, ip, lr}		@ 4
	subs	r1, r1, #1			@ 1
	stmia	r4!, {r2, r3, ip, lr}		@ 4
	ldmneia	r0!, {r2, r3, ip, lr}		@ 4
	bne	1b				@ 1
	ldmfd	sp!, {r4, pc}			@ 3

	.align	5
/*
 * ARMv4 optimised clear_user_page
 *
 * Same story as above.
 */
ENTRY(v4_mc_clear_user_page)
	str	lr, [sp, #-4]!
	mov	r1, #PAGE_SZ/64			@ 1
	mov	r2, #0				@ 1
	mov	r3, #0				@ 1
	mov	ip, #0				@ 1
	mov	lr, #0				@ 1
1:	mcr	p15, 0, r0, c7, c6, 1		@ 1   invalidate D line
	stmia	r0!, {r2, r3, ip, lr}		@ 4
	stmia	r0!, {r2, r3, ip, lr}		@ 4
	mcr	p15, 0, r0, c7, c6, 1		@ 1   invalidate D line
	stmia	r0!, {r2, r3, ip, lr}		@ 4
	stmia	r0!, {r2, r3, ip, lr}		@ 4
	subs	r1, r1, #1			@ 1
	bne	1b				@ 1
	ldr	pc, [sp], #4

	__INITDATA

	.type	v4_mc_user_fns, #object
ENTRY(v4_mc_user_fns)
	.long	v4_mc_clear_user_page
	.long	v4_mc_copy_user_page
	.size	v4_mc_user_fns, . - v4_mc_user_fns

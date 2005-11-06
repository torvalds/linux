/*
 *  linux/arch/arm/lib/copypage.S
 *
 *  Copyright (C) 1995-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  ASM optimised string functions
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/asm-offsets.h>

	.text
	.align	5
/*
 * ARMv4 optimised copy_user_page
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
ENTRY(v4wb_copy_user_page)
	stmfd	sp!, {r4, lr}			@ 2
	mov	r2, #PAGE_SZ/64			@ 1
	ldmia	r1!, {r3, r4, ip, lr}		@ 4
1:	mcr	p15, 0, r0, c7, c6, 1		@ 1   invalidate D line
	stmia	r0!, {r3, r4, ip, lr}		@ 4
	ldmia	r1!, {r3, r4, ip, lr}		@ 4+1
	stmia	r0!, {r3, r4, ip, lr}		@ 4
	ldmia	r1!, {r3, r4, ip, lr}		@ 4
	mcr	p15, 0, r0, c7, c6, 1		@ 1   invalidate D line
	stmia	r0!, {r3, r4, ip, lr}		@ 4
	ldmia	r1!, {r3, r4, ip, lr}		@ 4
	subs	r2, r2, #1			@ 1
	stmia	r0!, {r3, r4, ip, lr}		@ 4
	ldmneia	r1!, {r3, r4, ip, lr}		@ 4
	bne	1b				@ 1
	mcr	p15, 0, r1, c7, c10, 4		@ 1   drain WB
	ldmfd	 sp!, {r4, pc}			@ 3

	.align	5
/*
 * ARMv4 optimised clear_user_page
 *
 * Same story as above.
 */
ENTRY(v4wb_clear_user_page)
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
	mcr	p15, 0, r1, c7, c10, 4		@ 1   drain WB
	ldr	pc, [sp], #4

	__INITDATA

	.type	v4wb_user_fns, #object
ENTRY(v4wb_user_fns)
	.long	v4wb_clear_user_page
	.long	v4wb_copy_user_page
	.size	v4wb_user_fns, . - v4wb_user_fns

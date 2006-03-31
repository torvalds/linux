/*
 *  linux/arch/arm/lib/copypage-xsc3.S
 *
 *  Copyright (C) 2004 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Adapted for 3rd gen XScale core, no more mini-dcache
 * Author: Matt Gilbert (matthew.m.gilbert@intel.com)
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/asm-offsets.h>

/*
 * General note:
 *  We don't really want write-allocate cache behaviour for these functions
 *  since that will just eat through 8K of the cache.
 */

	.text
	.align	5
/*
 * XSC3 optimised copy_user_page
 *  r0 = destination
 *  r1 = source
 *  r2 = virtual user address of ultimate destination page
 *
 * The source page may have some clean entries in the cache already, but we
 * can safely ignore them - break_cow() will flush them out of the cache
 * if we eventually end up using our copied page.
 *
 */
ENTRY(xsc3_mc_copy_user_page)
	stmfd	sp!, {r4, r5, lr}
	mov	lr, #PAGE_SZ/64-1

	pld	[r1, #0]
	pld	[r1, #32]
1:	pld	[r1, #64]
	pld	[r1, #96]

2:	ldrd	r2, [r1], #8
	mov	ip, r0
	ldrd	r4, [r1], #8
	mcr	p15, 0, ip, c7, c6, 1		@ invalidate
	strd	r2, [r0], #8
	ldrd	r2, [r1], #8
	strd	r4, [r0], #8
	ldrd	r4, [r1], #8
	strd	r2, [r0], #8
	strd	r4, [r0], #8
	ldrd	r2, [r1], #8
	mov	ip, r0
	ldrd	r4, [r1], #8
	mcr	p15, 0, ip, c7, c6, 1		@ invalidate
	strd	r2, [r0], #8
	ldrd	r2, [r1], #8
	subs	lr, lr, #1
	strd	r4, [r0], #8
	ldrd	r4, [r1], #8
	strd	r2, [r0], #8
	strd	r4, [r0], #8
	bgt	1b
	beq	2b

	ldmfd	sp!, {r4, r5, pc}

	.align	5
/*
 * XScale optimised clear_user_page
 *  r0 = destination
 *  r1 = virtual user address of ultimate destination page
 */
ENTRY(xsc3_mc_clear_user_page)
	mov	r1, #PAGE_SZ/32
	mov	r2, #0
	mov	r3, #0
1:	mcr	p15, 0, r0, c7, c6, 1		@ invalidate line
	strd	r2, [r0], #8
	strd	r2, [r0], #8
	strd	r2, [r0], #8
	strd	r2, [r0], #8
	subs	r1, r1, #1
	bne	1b
	mov	pc, lr

	__INITDATA

	.type	xsc3_mc_user_fns, #object
ENTRY(xsc3_mc_user_fns)
	.long	xsc3_mc_clear_user_page
	.long	xsc3_mc_copy_user_page
	.size	xsc3_mc_user_fns, . - xsc3_mc_user_fns

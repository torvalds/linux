/*
 *  linux/arch/arm/mm/copypage-xsc3.S
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
#include <linux/init.h>

#include <asm/page.h>

/*
 * General note:
 *  We don't really want write-allocate cache behaviour for these functions
 *  since that will just eat through 8K of the cache.
 */

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
void __attribute__((naked))
xsc3_mc_copy_user_page(void *kto, const void *kfrom, unsigned long vaddr)
{
	asm("\
	stmfd	sp!, {r4, r5, lr}		\n\
	mov	lr, %0				\n\
						\n\
	pld	[r1, #0]			\n\
	pld	[r1, #32]			\n\
1:	pld	[r1, #64]			\n\
	pld	[r1, #96]			\n\
						\n\
2:	ldrd	r2, [r1], #8			\n\
	mov	ip, r0				\n\
	ldrd	r4, [r1], #8			\n\
	mcr	p15, 0, ip, c7, c6, 1		@ invalidate\n\
	strd	r2, [r0], #8			\n\
	ldrd	r2, [r1], #8			\n\
	strd	r4, [r0], #8			\n\
	ldrd	r4, [r1], #8			\n\
	strd	r2, [r0], #8			\n\
	strd	r4, [r0], #8			\n\
	ldrd	r2, [r1], #8			\n\
	mov	ip, r0				\n\
	ldrd	r4, [r1], #8			\n\
	mcr	p15, 0, ip, c7, c6, 1		@ invalidate\n\
	strd	r2, [r0], #8			\n\
	ldrd	r2, [r1], #8			\n\
	subs	lr, lr, #1			\n\
	strd	r4, [r0], #8			\n\
	ldrd	r4, [r1], #8			\n\
	strd	r2, [r0], #8			\n\
	strd	r4, [r0], #8			\n\
	bgt	1b				\n\
	beq	2b				\n\
						\n\
	ldmfd	sp!, {r4, r5, pc}"
	:
	: "I" (PAGE_SIZE / 64 - 1));
}

/*
 * XScale optimised clear_user_page
 *  r0 = destination
 *  r1 = virtual user address of ultimate destination page
 */
void __attribute__((naked))
xsc3_mc_clear_user_page(void *kaddr, unsigned long vaddr)
{
	asm("\
	mov	r1, %0				\n\
	mov	r2, #0				\n\
	mov	r3, #0				\n\
1:	mcr	p15, 0, r0, c7, c6, 1		@ invalidate line\n\
	strd	r2, [r0], #8			\n\
	strd	r2, [r0], #8			\n\
	strd	r2, [r0], #8			\n\
	strd	r2, [r0], #8			\n\
	subs	r1, r1, #1			\n\
	bne	1b				\n\
	mov	pc, lr"
	:
	: "I" (PAGE_SIZE / 32));
}

struct cpu_user_fns xsc3_mc_user_fns __initdata = {
	.cpu_clear_user_page	= xsc3_mc_clear_user_page,
	.cpu_copy_user_page	= xsc3_mc_copy_user_page,
};

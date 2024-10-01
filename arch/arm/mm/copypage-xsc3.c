// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mm/copypage-xsc3.S
 *
 *  Copyright (C) 2004 Intel Corp.
 *
 * Adapted for 3rd gen XScale core, no more mini-dcache
 * Author: Matt Gilbert (matthew.m.gilbert@intel.com)
 */
#include <linux/init.h>
#include <linux/highmem.h>

/*
 * General note:
 *  We don't really want write-allocate cache behaviour for these functions
 *  since that will just eat through 8K of the cache.
 */

/*
 * XSC3 optimised copy_user_highpage
 *
 * The source page may have some clean entries in the cache already, but we
 * can safely ignore them - break_cow() will flush them out of the cache
 * if we eventually end up using our copied page.
 *
 */
static void xsc3_mc_copy_user_page(void *kto, const void *kfrom)
{
	int tmp;

	asm volatile ("\
.arch xscale					\n\
	pld	[%1, #0]			\n\
	pld	[%1, #32]			\n\
1:	pld	[%1, #64]			\n\
	pld	[%1, #96]			\n\
						\n\
2:	ldrd	r2, r3, [%1], #8		\n\
	ldrd	r4, r5, [%1], #8		\n\
	mcr	p15, 0, %0, c7, c6, 1		@ invalidate\n\
	strd	r2, r3, [%0], #8		\n\
	ldrd	r2, r3, [%1], #8		\n\
	strd	r4, r5, [%0], #8		\n\
	ldrd	r4, r5, [%1], #8		\n\
	strd	r2, r3, [%0], #8		\n\
	strd	r4, r5, [%0], #8		\n\
	ldrd	r2, r3, [%1], #8		\n\
	ldrd	r4, r5, [%1], #8		\n\
	mcr	p15, 0, %0, c7, c6, 1		@ invalidate\n\
	strd	r2, r3, [%0], #8		\n\
	ldrd	r2, r3, [%1], #8		\n\
	subs	%2, %2, #1			\n\
	strd	r4, r5, [%0], #8		\n\
	ldrd	r4, r5, [%1], #8		\n\
	strd	r2, r3, [%0], #8		\n\
	strd	r4, r5, [%0], #8		\n\
	bgt	1b				\n\
	beq	2b				"
	: "+&r" (kto), "+&r" (kfrom), "=&r" (tmp)
	: "2" (PAGE_SIZE / 64 - 1)
	: "r2", "r3", "r4", "r5");
}

void xsc3_mc_copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma)
{
	void *kto, *kfrom;

	kto = kmap_atomic(to);
	kfrom = kmap_atomic(from);
	flush_cache_page(vma, vaddr, page_to_pfn(from));
	xsc3_mc_copy_user_page(kto, kfrom);
	kunmap_atomic(kfrom);
	kunmap_atomic(kto);
}

/*
 * XScale optimised clear_user_page
 */
void xsc3_mc_clear_user_highpage(struct page *page, unsigned long vaddr)
{
	void *ptr, *kaddr = kmap_atomic(page);
	asm volatile ("\
.arch xscale					\n\
	mov	r1, %2				\n\
	mov	r2, #0				\n\
	mov	r3, #0				\n\
1:	mcr	p15, 0, %0, c7, c6, 1		@ invalidate line\n\
	strd	r2, r3, [%0], #8		\n\
	strd	r2, r3, [%0], #8		\n\
	strd	r2, r3, [%0], #8		\n\
	strd	r2, r3, [%0], #8		\n\
	subs	r1, r1, #1			\n\
	bne	1b"
	: "=r" (ptr)
	: "0" (kaddr), "I" (PAGE_SIZE / 32)
	: "r1", "r2", "r3");
	kunmap_atomic(kaddr);
}

struct cpu_user_fns xsc3_mc_user_fns __initdata = {
	.cpu_clear_user_highpage = xsc3_mc_clear_user_highpage,
	.cpu_copy_user_highpage	= xsc3_mc_copy_user_highpage,
};

/*
 *  linux/arch/arm/mm/copypage-v4wb.c
 *
 *  Copyright (C) 1995-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/highmem.h>

/*
 * ARMv4 optimised copy_user_highpage
 *
 * We flush the destination cache lines just before we write the data into the
 * corresponding address.  Since the Dcache is read-allocate, this removes the
 * Dcache aliasing issue.  The writes will be forwarded to the write buffer,
 * and merged as appropriate.
 *
 * Note: We rely on all ARMv4 processors implementing the "invalidate D line"
 * instruction.  If your processor does not supply this, you have to write your
 * own copy_user_highpage that does the right thing.
 */
static void __naked
v4wb_copy_user_page(void *kto, const void *kfrom)
{
	asm("\
	stmfd	sp!, {r4, lr}			@ 2\n\
	mov	r2, %2				@ 1\n\
	ldmia	r1!, {r3, r4, ip, lr}		@ 4\n\
1:	mcr	p15, 0, r0, c7, c6, 1		@ 1   invalidate D line\n\
	stmia	r0!, {r3, r4, ip, lr}		@ 4\n\
	ldmia	r1!, {r3, r4, ip, lr}		@ 4+1\n\
	stmia	r0!, {r3, r4, ip, lr}		@ 4\n\
	ldmia	r1!, {r3, r4, ip, lr}		@ 4\n\
	mcr	p15, 0, r0, c7, c6, 1		@ 1   invalidate D line\n\
	stmia	r0!, {r3, r4, ip, lr}		@ 4\n\
	ldmia	r1!, {r3, r4, ip, lr}		@ 4\n\
	subs	r2, r2, #1			@ 1\n\
	stmia	r0!, {r3, r4, ip, lr}		@ 4\n\
	ldmneia	r1!, {r3, r4, ip, lr}		@ 4\n\
	bne	1b				@ 1\n\
	mcr	p15, 0, r1, c7, c10, 4		@ 1   drain WB\n\
	ldmfd	 sp!, {r4, pc}			@ 3"
	:
	: "r" (kto), "r" (kfrom), "I" (PAGE_SIZE / 64));
}

void v4wb_copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma)
{
	void *kto, *kfrom;

	kto = kmap_atomic(to);
	kfrom = kmap_atomic(from);
	flush_cache_page(vma, vaddr, page_to_pfn(from));
	v4wb_copy_user_page(kto, kfrom);
	kunmap_atomic(kfrom);
	kunmap_atomic(kto);
}

/*
 * ARMv4 optimised clear_user_page
 *
 * Same story as above.
 */
void v4wb_clear_user_highpage(struct page *page, unsigned long vaddr)
{
	void *ptr, *kaddr = kmap_atomic(page);
	asm volatile("\
	mov	r1, %2				@ 1\n\
	mov	r2, #0				@ 1\n\
	mov	r3, #0				@ 1\n\
	mov	ip, #0				@ 1\n\
	mov	lr, #0				@ 1\n\
1:	mcr	p15, 0, %0, c7, c6, 1		@ 1   invalidate D line\n\
	stmia	%0!, {r2, r3, ip, lr}		@ 4\n\
	stmia	%0!, {r2, r3, ip, lr}		@ 4\n\
	mcr	p15, 0, %0, c7, c6, 1		@ 1   invalidate D line\n\
	stmia	%0!, {r2, r3, ip, lr}		@ 4\n\
	stmia	%0!, {r2, r3, ip, lr}		@ 4\n\
	subs	r1, r1, #1			@ 1\n\
	bne	1b				@ 1\n\
	mcr	p15, 0, r1, c7, c10, 4		@ 1   drain WB"
	: "=r" (ptr)
	: "0" (kaddr), "I" (PAGE_SIZE / 64)
	: "r1", "r2", "r3", "ip", "lr");
	kunmap_atomic(kaddr);
}

struct cpu_user_fns v4wb_user_fns __initdata = {
	.cpu_clear_user_highpage = v4wb_clear_user_highpage,
	.cpu_copy_user_highpage	= v4wb_copy_user_highpage,
};

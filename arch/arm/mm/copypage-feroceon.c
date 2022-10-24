// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mm/copypage-feroceon.S
 *
 *  Copyright (C) 2008 Marvell Semiconductors
 *
 * This handles copy_user_highpage and clear_user_page on Feroceon
 * more optimally than the generic implementations.
 */
#include <linux/init.h>
#include <linux/highmem.h>

static void feroceon_copy_user_page(void *kto, const void *kfrom)
{
	int tmp;

	asm volatile ("\
.arch	armv5te					\n\
1:	ldmia	%1!, {r2 - r7, ip, lr}		\n\
	pld	[%1, #0]			\n\
	pld	[%1, #32]			\n\
	pld	[%1, #64]			\n\
	pld	[%1, #96]			\n\
	pld	[%1, #128]			\n\
	pld	[%1, #160]			\n\
	pld	[%1, #192]			\n\
	stmia	%0, {r2 - r7, ip, lr}		\n\
	ldmia	%1!, {r2 - r7, ip, lr}		\n\
	mcr	p15, 0, %0, c7, c14, 1		@ clean and invalidate D line\n\
	add	%0, %0, #32			\n\
	stmia	%0, {r2 - r7, ip, lr}		\n\
	ldmia	%1!, {r2 - r7, ip, lr}		\n\
	mcr	p15, 0, %0, c7, c14, 1		@ clean and invalidate D line\n\
	add	%0, %0, #32			\n\
	stmia	%0, {r2 - r7, ip, lr}		\n\
	ldmia	%1!, {r2 - r7, ip, lr}		\n\
	mcr	p15, 0, %0, c7, c14, 1		@ clean and invalidate D line\n\
	add	%0, %0, #32			\n\
	stmia	%0, {r2 - r7, ip, lr}		\n\
	ldmia	%1!, {r2 - r7, ip, lr}		\n\
	mcr	p15, 0, %0, c7, c14, 1		@ clean and invalidate D line\n\
	add	%0, %0, #32			\n\
	stmia	%0, {r2 - r7, ip, lr}		\n\
	ldmia	%1!, {r2 - r7, ip, lr}		\n\
	mcr	p15, 0, %0, c7, c14, 1		@ clean and invalidate D line\n\
	add	%0, %0, #32			\n\
	stmia	%0, {r2 - r7, ip, lr}		\n\
	ldmia	%1!, {r2 - r7, ip, lr}		\n\
	mcr	p15, 0, %0, c7, c14, 1		@ clean and invalidate D line\n\
	add	%0, %0, #32			\n\
	stmia	%0, {r2 - r7, ip, lr}		\n\
	ldmia	%1!, {r2 - r7, ip, lr}		\n\
	mcr	p15, 0, %0, c7, c14, 1		@ clean and invalidate D line\n\
	add	%0, %0, #32			\n\
	stmia	%0, {r2 - r7, ip, lr}		\n\
	subs	%2, %2, #(32 * 8)		\n\
	mcr	p15, 0, %0, c7, c14, 1		@ clean and invalidate D line\n\
	add	%0, %0, #32			\n\
	bne	1b				\n\
	mcr	p15, 0, %2, c7, c10, 4		@ drain WB"
	: "+&r" (kto), "+&r" (kfrom), "=&r" (tmp)
	: "2" (PAGE_SIZE)
	: "r2", "r3", "r4", "r5", "r6", "r7", "ip", "lr");
}

void feroceon_copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma)
{
	void *kto, *kfrom;

	kto = kmap_atomic(to);
	kfrom = kmap_atomic(from);
	flush_cache_page(vma, vaddr, page_to_pfn(from));
	feroceon_copy_user_page(kto, kfrom);
	kunmap_atomic(kfrom);
	kunmap_atomic(kto);
}

void feroceon_clear_user_highpage(struct page *page, unsigned long vaddr)
{
	void *ptr, *kaddr = kmap_atomic(page);
	asm volatile ("\
	mov	r1, %2				\n\
	mov	r2, #0				\n\
	mov	r3, #0				\n\
	mov	r4, #0				\n\
	mov	r5, #0				\n\
	mov	r6, #0				\n\
	mov	r7, #0				\n\
	mov	ip, #0				\n\
	mov	lr, #0				\n\
1:	stmia	%0, {r2-r7, ip, lr}		\n\
	subs	r1, r1, #1			\n\
	mcr	p15, 0, %0, c7, c14, 1		@ clean and invalidate D line\n\
	add	%0, %0, #32			\n\
	bne	1b				\n\
	mcr	p15, 0, r1, c7, c10, 4		@ drain WB"
	: "=r" (ptr)
	: "0" (kaddr), "I" (PAGE_SIZE / 32)
	: "r1", "r2", "r3", "r4", "r5", "r6", "r7", "ip", "lr");
	kunmap_atomic(kaddr);
}

struct cpu_user_fns feroceon_user_fns __initdata = {
	.cpu_clear_user_highpage = feroceon_clear_user_highpage,
	.cpu_copy_user_highpage	= feroceon_copy_user_highpage,
};


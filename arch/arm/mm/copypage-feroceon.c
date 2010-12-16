/*
 *  linux/arch/arm/mm/copypage-feroceon.S
 *
 *  Copyright (C) 2008 Marvell Semiconductors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This handles copy_user_highpage and clear_user_page on Feroceon
 * more optimally than the generic implementations.
 */
#include <linux/init.h>
#include <linux/highmem.h>

static void __naked
feroceon_copy_user_page(void *kto, const void *kfrom)
{
	asm("\
	stmfd	sp!, {r4-r9, lr}		\n\
	mov	ip, %2				\n\
1:	mov	lr, r1				\n\
	ldmia	r1!, {r2 - r9}			\n\
	pld	[lr, #32]			\n\
	pld	[lr, #64]			\n\
	pld	[lr, #96]			\n\
	pld	[lr, #128]			\n\
	pld	[lr, #160]			\n\
	pld	[lr, #192]			\n\
	pld	[lr, #224]			\n\
	stmia	r0, {r2 - r9}			\n\
	ldmia	r1!, {r2 - r9}			\n\
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line\n\
	add	r0, r0, #32			\n\
	stmia	r0, {r2 - r9}			\n\
	ldmia	r1!, {r2 - r9}			\n\
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line\n\
	add	r0, r0, #32			\n\
	stmia	r0, {r2 - r9}			\n\
	ldmia	r1!, {r2 - r9}			\n\
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line\n\
	add	r0, r0, #32			\n\
	stmia	r0, {r2 - r9}			\n\
	ldmia	r1!, {r2 - r9}			\n\
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line\n\
	add	r0, r0, #32			\n\
	stmia	r0, {r2 - r9}			\n\
	ldmia	r1!, {r2 - r9}			\n\
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line\n\
	add	r0, r0, #32			\n\
	stmia	r0, {r2 - r9}			\n\
	ldmia	r1!, {r2 - r9}			\n\
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line\n\
	add	r0, r0, #32			\n\
	stmia	r0, {r2 - r9}			\n\
	ldmia	r1!, {r2 - r9}			\n\
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line\n\
	add	r0, r0, #32			\n\
	stmia	r0, {r2 - r9}			\n\
	subs	ip, ip, #(32 * 8)		\n\
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line\n\
	add	r0, r0, #32			\n\
	bne	1b				\n\
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB\n\
	ldmfd	sp!, {r4-r9, pc}"
	:
	: "r" (kto), "r" (kfrom), "I" (PAGE_SIZE));
}

void feroceon_copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr)
{
	void *kto, *kfrom;

	kto = kmap_atomic(to, KM_USER0);
	kfrom = kmap_atomic(from, KM_USER1);
	feroceon_copy_user_page(kto, kfrom);
	kunmap_atomic(kfrom, KM_USER1);
	kunmap_atomic(kto, KM_USER0);
}

void feroceon_clear_user_highpage(struct page *page, unsigned long vaddr)
{
	void *ptr, *kaddr = kmap_atomic(page, KM_USER0);
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
	kunmap_atomic(kaddr, KM_USER0);
}

struct cpu_user_fns feroceon_user_fns __initdata = {
	.cpu_clear_user_highpage = feroceon_clear_user_highpage,
	.cpu_copy_user_highpage	= feroceon_copy_user_highpage,
};


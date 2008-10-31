/*
 *  linux/arch/arm/mm/copypage-feroceon.S
 *
 *  Copyright (C) 2008 Marvell Semiconductors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This handles copy_user_page and clear_user_page on Feroceon
 * more optimally than the generic implementations.
 */
#include <linux/init.h>

#include <asm/page.h>

void __attribute__((naked))
feroceon_copy_user_page(void *kto, const void *kfrom, unsigned long vaddr)
{
	asm("\
	stmfd	sp!, {r4-r9, lr}		\n\
	mov	ip, %0				\n\
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
	: "I" (PAGE_SIZE));
}

void __attribute__((naked))
feroceon_clear_user_page(void *kaddr, unsigned long vaddr)
{
	asm("\
	stmfd	sp!, {r4-r7, lr}		\n\
	mov	r1, %0				\n\
	mov	r2, #0				\n\
	mov	r3, #0				\n\
	mov	r4, #0				\n\
	mov	r5, #0				\n\
	mov	r6, #0				\n\
	mov	r7, #0				\n\
	mov	ip, #0				\n\
	mov	lr, #0				\n\
1:	stmia	r0, {r2-r7, ip, lr}		\n\
	subs	r1, r1, #1			\n\
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line\n\
	add	r0, r0, #32			\n\
	bne	1b				\n\
	mcr	p15, 0, r1, c7, c10, 4		@ drain WB\n\
	ldmfd	sp!, {r4-r7, pc}"
	:
	: "I" (PAGE_SIZE / 32));
}

struct cpu_user_fns feroceon_user_fns __initdata = {
	.cpu_clear_user_page	= feroceon_clear_user_page,
	.cpu_copy_user_page	= feroceon_copy_user_page,
};


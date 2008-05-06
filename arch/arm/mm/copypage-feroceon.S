/*
 *  linux/arch/arm/lib/copypage-feroceon.S
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
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/asm-offsets.h>

	.text
	.align	5

ENTRY(feroceon_copy_user_page)
	stmfd	sp!, {r4-r9, lr}
	mov	ip, #PAGE_SZ
1:	mov	lr, r1
	ldmia	r1!, {r2 - r9}
	pld	[lr, #32]
	pld	[lr, #64]
	pld	[lr, #96]
	pld	[lr, #128]
	pld	[lr, #160]
	pld	[lr, #192]
	pld	[lr, #224]
	stmia	r0, {r2 - r9}
	ldmia	r1!, {r2 - r9}
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line
	add	r0, r0, #32
	stmia	r0, {r2 - r9}
	ldmia	r1!, {r2 - r9}
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line
	add	r0, r0, #32
	stmia	r0, {r2 - r9}
	ldmia	r1!, {r2 - r9}
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line
	add	r0, r0, #32
	stmia	r0, {r2 - r9}
	ldmia	r1!, {r2 - r9}
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line
	add	r0, r0, #32
	stmia	r0, {r2 - r9}
	ldmia	r1!, {r2 - r9}
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line
	add	r0, r0, #32
	stmia	r0, {r2 - r9}
	ldmia	r1!, {r2 - r9}
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line
	add	r0, r0, #32
	stmia	r0, {r2 - r9}
	ldmia	r1!, {r2 - r9}
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line
	add	r0, r0, #32
	stmia	r0, {r2 - r9}
	subs	ip, ip, #(32 * 8)
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line
	add	r0, r0, #32
	bne	1b
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	ldmfd	sp!, {r4-r9, pc}

	.align	5

ENTRY(feroceon_clear_user_page)
	stmfd	sp!, {r4-r7, lr}
	mov	r1, #PAGE_SZ/32
	mov	r2, #0
	mov	r3, #0
	mov	r4, #0
	mov	r5, #0
	mov	r6, #0
	mov	r7, #0
	mov	ip, #0
	mov	lr, #0
1:	stmia	r0, {r2-r7, ip, lr}
	subs	r1, r1, #1
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D line
	add	r0, r0, #32
	bne	1b
	mcr	p15, 0, r1, c7, c10, 4		@ drain WB
	ldmfd	sp!, {r4-r7, pc}

	__INITDATA

	.type	feroceon_user_fns, #object
ENTRY(feroceon_user_fns)
	.long	feroceon_clear_user_page
	.long	feroceon_copy_user_page
	.size	feroceon_user_fns, . - feroceon_user_fns

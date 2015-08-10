/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/linkage.h>
#include <asm/smp_scu.h>
#include "hardware.h"

	.macro	disable_l1_dcache

	/*
	 * Flush all data from the L1 data cache before disabling
	 * SCTLR.C bit.
	 */
	push	{r0 - r10, lr}
	ldr	r7, =v7_flush_dcache_all
	mov	lr, pc
	mov	pc, r7
	pop	{r0 - r10, lr}

	/* disable d-cache */
	mrc	p15, 0, r7, c1, c0, 0
	bic	r7, r7, #(1 << 2)
	mcr	p15, 0, r7, c1, c0, 0
	dsb
	isb

	push	{r0 - r10, lr}
	ldr	r7, =v7_flush_dcache_all
	mov	lr, pc
	mov	pc, r7
	pop	{r0 - r10, lr}

	.endm

#ifdef CONFIG_SMP
	.align 3

ENTRY(imx7_smp_wfe)
	push	{r4 - r11, lr}

	dsb
	isb

	disable_l1_dcache

	isb

	/* Turn off SMP bit. */
	mrc	p15, 0, r8, c1, c0, 1
	bic	r8, r8, #0x40
	mcr	p15, 0, r8, c1, c0, 1

	isb
	/* Set flag of entering WFE. */
	mov	r7, #0xff
	lsl	r7, r7, r0
	mov	r6, #SCU_PM_DORMANT
	lsl	r6, r6, r0
	ldr	r8, [r1, #0x4]
	bic	r8, r8, r7
	orr	r6, r6, r8
	str	r6, [r1, #0x4]

go_back_wfe:
	wfe

	/* Offset 0x0 stores busfeq done flag */
	ldr	r6, [r1]
	cmp	r6, #1
	beq	go_back_wfe

	/* Turn ON SMP bit. */
	mrc	p15, 0, r8, c1, c0, 1
	orr	r8, r8, #0x40
	mcr	p15, 0, r8, c1, c0, 1

	isb
	/* Enable L1 data cache. */
	mrc	p15, 0, r8, c1, c0, 0
	orr	r8, r8, #0x4
	mcr	p15, 0, r8, c1, c0, 0
	isb

	/* Set flag of exiting WFE. */
	mov	r7, #0xff
	lsl	r7, r7, r0
	mov	r6, #SCU_PM_NORMAL
	lsl	r6, r6, r0
	ldr	r8, [r1, #0x4]
	bic	r8, r8, r7
	orr	r6, r6, r8
	str	r6, [r1, #0x4]

	/* Pop all saved registers. */
	pop	{r4 - r11, lr}
	mov	pc, lr
	.ltorg
ENDPROC(imx7_smp_wfe)
#endif

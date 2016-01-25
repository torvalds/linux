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

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/linkage.h>
#include <asm/smp_scu.h>
#include "hardware.h"

#ifdef CONFIG_SMP
.extern imx_scu_base
#endif

.globl wfe_smp_freq_change_start
.globl wfe_smp_freq_change_end

#ifdef CONFIG_SMP

	.align 3

	.macro	disable_l1_dcache

	/*
	 * Flush all data from the L1 data cache before disabling
	 * SCTLR.C bit.
	 */
	push	{r0 - r11, lr}

	ldr	r7, =v7_flush_kern_cache_all
	mov	lr, pc
	mov	pc, r7
	pop	{r0 - r11, lr}

	/* disable d-cache */
	mrc	p15, 0, r6, c1, c0, 0
	bic	r6, r6, #0x4
	mcr	p15, 0, r6, c1, c0, 0
	dsb
	isb

	push	{r0 - r11, lr}

	ldr	r7, =v7_flush_kern_cache_all
	mov	lr, pc
	mov	pc, r7
	pop	{r0 - r11, lr}

	.endm

ENTRY(wfe_smp_freq_change)
wfe_smp_freq_change_start:
	push	{r4 - r11, lr}

	mov	r6, r0
	mov	r7, r1

	dsb
	isb

	disable_l1_dcache

	isb

	/* Turn off SMP bit. */
	mrc	p15, 0, r8, c1, c0, 1
	bic	r8, r8, #0x40
	mcr	p15, 0, r8, c1, c0, 1

	isb

	/* Inform the SCU we are going to enter WFE. */
	push	{r0 - r11, lr}

	ldr	r0,=imx_scu_base
	ldr	r0, [r0]
	mov	r1, #SCU_PM_DORMANT
	ldr	r3, =scu_power_mode
	mov	lr, pc
	mov	pc, r3

	pop	{r0 - r11, lr}

go_back_wfe:
	wfe

	ldr	r3, [r7]
	cmp	r3, #1
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

	/* Inform the SCU we have exited WFE. */
	push	{r0 - r11, lr}

	ldr	r0,=imx_scu_base
	ldr	r0, [r0]
	mov	r1, #SCU_PM_NORMAL
	ldr	r3, =scu_power_mode
	mov	lr, pc
	mov	pc, r3

	pop	{r0 - r11, lr}

	/* Pop all saved registers. */
	pop	{r4 - r11, lr}
	mov	pc, lr
	.ltorg
wfe_smp_freq_change_end:
#endif

/*
 * arch/arm/mach-ks8695/include/mach/debug-macro.S
 *
 * Copyright (C) 2006 Ben Dooks <ben@simtec.co.uk>
 * Copyright (C) 2006 Simtec Electronics
 *
 * KS8695 - Debug macros
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <mach/hardware.h>
#include <mach/regs-uart.h>

	.macro	addruart, rx, tmp
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1				@ MMU enabled?
		ldreq	\rx, =KS8695_UART_PA		@ physical base address
		ldrne	\rx, =KS8695_UART_VA		@ virtual base address
	.endm

	.macro	senduart, rd, rx
		str	\rd, [\rx, #KS8695_URTH]	@ Write to Transmit Holding Register
	.endm

	.macro	busyuart, rd, rx
1001:		ldr	\rd, [\rx, #KS8695_URLS]	@ Read Line Status Register
		tst	\rd, #URLS_URTE			@ Holding & Shift registers empty?
		beq	1001b
	.endm

	.macro	waituart, rd, rx
1001:		ldr	\rd, [\rx, #KS8695_URLS]	@ Read Line Status Register
		tst	\rd, #URLS_URTHRE		@ Holding Register empty?
		beq	1001b
	.endm

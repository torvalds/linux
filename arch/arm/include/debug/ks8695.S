/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/include/debug/ks8695.S
 *
 * Copyright (C) 2006 Ben Dooks <ben@simtec.co.uk>
 * Copyright (C) 2006 Simtec Electronics
 *
 * KS8695 - Debug macros
 */

#define KS8695_UART_PA	0x03ffe000
#define KS8695_UART_VA	0xf00fe000
#define KS8695_URTH	(0x04)
#define KS8695_URLS	(0x14)
#define URLS_URTE	(1 << 6)
#define URLS_URTHRE	(1 << 5)

	.macro	addruart, rp, rv, tmp
		ldr	\rp, =KS8695_UART_PA		@ physical base address
		ldr	\rv, =KS8695_UART_VA		@ virtual base address
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

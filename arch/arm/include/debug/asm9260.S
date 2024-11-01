/* SPDX-License-Identifier: GPL-2.0-only */
/* Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *  Modified for ASM9260 by Oleksij Remepl <linux@rempel-privat.de>
 */

		.macro	addruart, rp, rv, tmp
		ldr	\rp, = CONFIG_DEBUG_UART_PHYS
		ldr	\rv, = CONFIG_DEBUG_UART_VIRT
		.endm

		.macro	waituarttxrdy,rd,rx
		.endm

		.macro	waituartcts,rd,rx
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #0x50]	@ TXDATA
		.endm

		.macro	busyuart,rd,rx
1002:		ldr	\rd, [\rx, #0x60]	@ STAT
		tst	\rd, #1 << 27		@ TXEMPTY
		beq	1002b			@ wait until transmit done
		.endm

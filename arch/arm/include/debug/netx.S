/*
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#define UART_DATA 0
#define UART_FLAG 0x18
#define UART_FLAG_BUSY (1 << 3)

		.macro	addruart, rp, rv, tmp
		ldr	\rp, =CONFIG_DEBUG_UART_PHYS
		ldr	\rv, =CONFIG_DEBUG_UART_VIRT
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #UART_DATA]
		.endm

		.macro	busyuart,rd,rx
1002:		ldr	\rd, [\rx, #UART_FLAG]
		tst	\rd, #UART_FLAG_BUSY
		bne	1002b
		.endm

		.macro	waituart,rd,rx
1001:		ldr	\rd, [\rx, #UART_FLAG]
		tst	\rd, #UART_FLAG_BUSY
		bne	1001b
		.endm

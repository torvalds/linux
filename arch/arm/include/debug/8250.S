/*
 * arch/arm/include/debug/8250.S
 *
 *  Copyright (C) 1994-2013 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/serial_reg.h>

		.macro	addruart, rp, rv, tmp
		ldr	\rp, =CONFIG_DEBUG_UART_PHYS
		ldr	\rv, =CONFIG_DEBUG_UART_VIRT
		.endm

#ifdef CONFIG_DEBUG_UART_8250_WORD
		.macro	store, rd, rx:vararg
	 ARM_BE8(rev \rd, \rd)
		str	\rd, \rx
	 ARM_BE8(rev \rd, \rd)
		.endm

		.macro	load, rd, rx:vararg
		ldr	\rd, \rx
	ARM_BE8(rev \rd, \rd)
		.endm
#else
		.macro	store, rd, rx:vararg
		strb	\rd, \rx
		.endm

		.macro	load, rd, rx:vararg
		ldrb	\rd, \rx
		.endm
#endif

#define UART_SHIFT CONFIG_DEBUG_UART_8250_SHIFT

		.macro	senduart,rd,rx
		store	\rd, [\rx, #UART_TX << UART_SHIFT]
		.endm

		.macro	busyuart,rd,rx
1002:		load	\rd, [\rx, #UART_LSR << UART_SHIFT]
		and	\rd, \rd, #UART_LSR_TEMT | UART_LSR_THRE
		teq	\rd, #UART_LSR_TEMT | UART_LSR_THRE
		bne	1002b
		.endm

		.macro	waituart,rd,rx
#ifdef CONFIG_DEBUG_UART_8250_FLOW_CONTROL
1001:		load	\rd, [\rx, #UART_MSR << UART_SHIFT]
		tst	\rd, #UART_MSR_CTS
		beq	1001b
#endif
		.endm

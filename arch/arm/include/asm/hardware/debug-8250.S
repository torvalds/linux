/*
 * arch/arm/include/asm/hardware/debug-8250.S
 *
 *  Copyright (C) 1994-2013 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/serial_reg.h>

#ifdef CONFIG_DEBUG_UART_PHYS
		.macro	addruart, rp, rv, tmp
		ldr	\rp, =CONFIG_DEBUG_UART_PHYS
		ldr	\rv, =CONFIG_DEBUG_UART_VIRT
		.endm
#endif

#ifndef UART_SHIFT
#define UART_SHIFT CONFIG_DEBUG_UART_8250_SHIFT
#endif

		.macro	senduart,rd,rx
		strb	\rd, [\rx, #UART_TX << UART_SHIFT]
		.endm

		.macro	busyuart,rd,rx
1002:		ldrb	\rd, [\rx, #UART_LSR << UART_SHIFT]
		and	\rd, \rd, #UART_LSR_TEMT | UART_LSR_THRE
		teq	\rd, #UART_LSR_TEMT | UART_LSR_THRE
		bne	1002b
		.endm

		.macro	waituart,rd,rx
#ifdef CONFIG_DEBUG_UART_8250_FLOW_CONTROL
1001:		ldrb	\rd, [\rx, #UART_MSR << UART_SHIFT]
		tst	\rd, #UART_MSR_CTS
		beq	1001b
#endif
		.endm

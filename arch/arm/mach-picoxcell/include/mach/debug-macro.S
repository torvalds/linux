/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Derived from arch/arm/mach-davinci/include/mach/debug-macro.S to use 32-bit
 * accesses to the 8250.
 */
#include <linux/serial_reg.h>
#include <mach/hardware.h>
#include <mach/map.h>

#define UART_SHIFT 2

		.macro	addruart, rp, rv
		ldr	\rv, =PHYS_TO_IO(PICOXCELL_UART1_BASE)
		ldr	\rp, =PICOXCELL_UART1_BASE
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #UART_TX << UART_SHIFT]
		.endm

		.macro	busyuart,rd,rx
1002:		ldr	\rd, [\rx, #UART_LSR << UART_SHIFT]
		and	\rd, \rd, #UART_LSR_TEMT | UART_LSR_THRE
		teq	\rd, #UART_LSR_TEMT | UART_LSR_THRE
		bne	1002b
		.endm

		/* The UART's don't have any flow control IO's wired up. */
		.macro	waituart,rd,rx
		.endm

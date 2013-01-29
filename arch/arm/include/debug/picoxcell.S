/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define UART_SHIFT 2
#define PICOXCELL_UART1_BASE		0x80230000
#define PHYS_TO_IO(x)			(((x) & 0x00ffffff) | 0xfe000000)

		.macro	addruart, rp, rv, tmp
		ldr	\rv, =PHYS_TO_IO(PICOXCELL_UART1_BASE)
		ldr	\rp, =PICOXCELL_UART1_BASE
		.endm

#include "8250_32.S"

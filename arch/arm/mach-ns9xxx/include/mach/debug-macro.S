/*
 * arch/arm/mach-ns9xxx/include/mach/debug-macro.S
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <mach/hardware.h>
#include <asm/memory.h>

#include <mach/regs-board-a9m9750dev.h>

		.macro	addruart, rp, rv
		ldr	\rp, =NS9XXX_CSxSTAT_PHYS(0)
		ldr	\rv, =io_p2v(NS9XXX_CSxSTAT_PHYS(0))
		.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>

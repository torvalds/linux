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

#include <mach/regs-board-a9m9750dev.h>

		.macro	addruart,rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1
		ldreq	\rx, =NS9XXX_CSxSTAT_PHYS(0)
		ldrne	\rx, =io_p2v(NS9XXX_CSxSTAT_PHYS(0))
		.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>

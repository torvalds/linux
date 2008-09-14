/*
 * arch/arm/mach-ixp23xx/include/mach/debug-macro.S
 *
 * Debugging macro include header
 *
 * Copyright (C) 1994-1999 Russell King
 * Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <mach/ixp23xx.h>

		.macro	addruart,rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1				@ mmu enabled?
		ldreq	\rx, =IXP23XX_PERIPHERAL_PHYS 	@ physical
		ldrne	\rx, =IXP23XX_PERIPHERAL_VIRT	@ virtual
#ifdef __ARMEB__
		orr	\rx, \rx, #0x00000003
#endif
		.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>

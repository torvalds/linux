/*
 * arch/arm/mach-dove/include/mach/debug-macro.S
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <mach/bridge-regs.h>

	.macro	addruart, rx, tmp
	mrc	p15, 0, \rx, c1, c0
	tst	\rx, #1					@ MMU enabled?
	ldreq	\rx, =DOVE_SB_REGS_PHYS_BASE
	ldrne	\rx, =DOVE_SB_REGS_VIRT_BASE
	orr	\rx, \rx, #0x00012000
	.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>

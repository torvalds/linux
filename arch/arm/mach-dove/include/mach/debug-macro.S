/*
 * arch/arm/mach-dove/include/mach/debug-macro.S
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <mach/bridge-regs.h>

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =DOVE_SB_REGS_PHYS_BASE
	ldr	\rv, =DOVE_SB_REGS_VIRT_BASE
	orr	\rp, \rp, #0x00012000
	orr	\rv, \rv, #0x00012000
	.endm

#include <asm/hardware/debug-8250.S>

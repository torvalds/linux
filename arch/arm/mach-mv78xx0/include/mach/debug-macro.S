/*
 * arch/arm/mach-mv78xx0/include/mach/debug-macro.S
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <mach/mv78xx0.h>

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =MV78XX0_REGS_PHYS_BASE
	ldr	\rv, =MV78XX0_REGS_VIRT_BASE
	orr	\rp, \rp, #0x00012000
	orr	\rv, \rv, #0x00012000
	.endm

#include <asm/hardware/debug-8250.S>

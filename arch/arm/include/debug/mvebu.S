/*
 * Early serial output macro for Marvell  SoC
 *
 * Copyright (C) 2012 Marvell
 *
 * Lior Amsalem <alior@marvell.com>
 * Gregory Clement <gregory.clement@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define ARMADA_370_XP_REGS_PHYS_BASE	0xd0000000
#define ARMADA_370_XP_REGS_VIRT_BASE	0xfeb00000

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =ARMADA_370_XP_REGS_PHYS_BASE
	ldr	\rv, =ARMADA_370_XP_REGS_VIRT_BASE
	orr	\rp, \rp, #0x00012000
	orr	\rv, \rv, #0x00012000
	.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>

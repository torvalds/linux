/*
 *
 * arch-arm/mach-u300/include/mach/debug-macro.S
 *
 *
 * Copyright (C) 2006-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * Debugging macro include header.
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */
#include <mach/hardware.h>

	.macro	addruart, rp, rv, tmp
	/* If we move the address using MMU, use this. */
	ldr	\rp,	  = U300_SLOW_PER_PHYS_BASE @ MMU off, physical address
	ldr	\rv,	  = U300_SLOW_PER_VIRT_BASE @ MMU on, virtual address
	orr	\rp, \rp, #0x00003000
	orr	\rv, \rv, #0x00003000
	.endm

#include <asm/hardware/debug-pl01x.S>

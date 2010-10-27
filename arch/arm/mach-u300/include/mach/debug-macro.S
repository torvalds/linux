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

	.macro	addruart, rx, tmp
	/* If we move the address using MMU, use this. */
	mrc	p15, 0, \rx, c1, c0
	tst	\rx, #1			@ MMU enabled?
	ldreq	\rx,	  = U300_SLOW_PER_PHYS_BASE @ MMU off, physical address
	ldrne	\rx,	  = U300_SLOW_PER_VIRT_BASE @ MMU on, virtual address
	orr	\rx, \rx, #0x00003000
	.endm

#include <asm/hardware/debug-pl01x.S>

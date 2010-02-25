/*
 * Debugging macro include header
 *
 *  Copyright (C) 2009 ST-Ericsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <mach/hardware.h>

	.macro	addruart,rx
	mrc	p15, 0, \rx, c1, c0
	tst	\rx, #1					@ MMU enabled?
	ldreq	\rx, =U8500_UART2_BASE			@ no, physical address
	ldrne	\rx, =IO_ADDRESS(U8500_UART2_BASE)	@ yes, virtual address
	.endm

#include <asm/hardware/debug-pl01x.S>

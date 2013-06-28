/*
 * Debugging macro include header
 *
 * Copyright (C) 2010 Broadcom
 * Copyright (C) 1994-1999 Russell King
 * Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define BCM2835_DEBUG_PHYS 0x20201000
#define BCM2835_DEBUG_VIRT 0xf0201000

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =BCM2835_DEBUG_PHYS
	ldr	\rv, =BCM2835_DEBUG_VIRT
	.endm

#include <asm/hardware/debug-pl01x.S>

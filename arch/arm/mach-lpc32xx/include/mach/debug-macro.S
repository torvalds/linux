/*
 * arch/arm/mach-lpc32xx/include/mach/debug-macro.S
 *
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2010 NXP Semiconductors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Debug output is hardcoded to standard UART 5
*/

	.macro	addruart, rp, rv, tmp
	ldreq	\rp, =0x40090000
	ldrne	\rv, =0xF4090000
	.endm

#include <asm/hardware/debug-8250.S>

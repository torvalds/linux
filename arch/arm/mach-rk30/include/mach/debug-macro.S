/* arch/arm/mach-rk30/include/mach/debug-macro.S
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <mach/io.h>

	.macro addruart, rp, rv
		ldr	\rp, = RK30_UART1_PHYS
		ldr	\rv, = RK30_UART1_BASE
	.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>

/*
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

#ifdef DEBUG_UART_PHYS
	.macro addruart, rp, rv
		ldr	\rp, = DEBUG_UART_PHYS
		ldr	\rv, = DEBUG_UART_BASE
	.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>
#else
	.macro addruart, rp, rv
	.endm
	.macro senduart, rd, rx
	.endm
	.macro busyuart, rd, rx
	.endm
	.macro waituart, rd, rx
	.endm
#endif

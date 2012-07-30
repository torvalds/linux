/*
 * arch/arm/plat-sunxi/include/plat/debug-macro.S
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <mach/platform.h>

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =SW_PA_UART0_IO_BASE
	ldr	\rv, =SW_VA_UART0_IO_BASE
#if CONFIG_SW_DEBUG_UART != 0
	add	\rp, \rp, #(0x400 * CONFIG_SW_DEBUG_UART)
	add	\rv, \rv, #(0x400 * CONFIG_SW_DEBUG_UART)
#endif
	.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>

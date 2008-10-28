/*
 * Debugging macro for DaVinci
 *
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

		.macro addruart, rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		moveq	\rx, #0x01000000	@ physical base address
		movne	\rx, #0xfe000000	@ virtual base
		orr	\rx, \rx, #0x00c20000   @ UART 0
		.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>

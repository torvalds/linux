/* arch/arm/mach-rk29/include/mach/debug-macro.S
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
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

/* pull in the relevant register and map files. */

#include <linux/version.h>
#include <mach/rk29_iomap.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	.macro addruart, rp, rv
		ldr	\rp, = RK29_UART1_PHYS
		ldr	\rv, = RK29_UART1_BASE
	.endm
#else
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34))
	.macro addruart, rx, tmp
#else
	.macro addruart, rx
#endif
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1
		ldreq	\rx, = RK29_UART1_PHYS
		ldrne	\rx, = RK29_UART1_BASE
	.endm
#endif

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>

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

	/* note, for the boot process to work we have to keep the UART
	 * virtual address aligned to an 1MiB boundary for the L1
	 * mapping the head code makes. We keep the UART virtual address
	 * aligned and add in the offset when we load the value here.
	 */

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

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>

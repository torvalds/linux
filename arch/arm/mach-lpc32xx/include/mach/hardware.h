/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * arch/arm/mach-lpc32xx/include/mach/hardware.h
 *
 * Copyright (c) 2005 MontaVista Software, Inc. <source@mvista.com>
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

/*
 * Start of virtual addresses for IO devices
 */
#define IO_BASE		0xF0000000

/*
 * This macro relies on fact that for all HW i/o addresses bits 20-23 are 0
 */
#define IO_ADDRESS(x)	IOMEM(((((x) & 0xff000000) >> 4) | ((x) & 0xfffff)) |\
			 IO_BASE)

#define io_p2v(x)	((void __iomem *) (unsigned long) IO_ADDRESS(x))
#define io_v2p(x)	((((x) & 0x0ff00000) << 4) | ((x) & 0x000fffff))

#endif

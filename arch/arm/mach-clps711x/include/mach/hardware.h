/*
 *  arch/arm/mach-clps711x/include/mach/hardware.h
 *
 *  This file contains the hardware definitions of the Prospector P720T.
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __MACH_HARDWARE_H
#define __MACH_HARDWARE_H

#include <mach/clps711x.h>

#define CLPS711X_VIRT_BASE	IOMEM(0xfe000000)

#ifndef __ASSEMBLY__
#define clps_readb(off)		readb(CLPS711X_VIRT_BASE + (off))
#define clps_readw(off)		readw(CLPS711X_VIRT_BASE + (off))
#define clps_readl(off)		readl(CLPS711X_VIRT_BASE + (off))
#define clps_writeb(val,off)	writeb(val, CLPS711X_VIRT_BASE + (off))
#define clps_writew(val,off)	writew(val, CLPS711X_VIRT_BASE + (off))
#define clps_writel(val,off)	writel(val, CLPS711X_VIRT_BASE + (off))
#endif

#define CS0_PHYS_BASE		(0x00000000)
#define CS1_PHYS_BASE		(0x10000000)
#define CS2_PHYS_BASE		(0x20000000)
#define CS3_PHYS_BASE		(0x30000000)
#define CS4_PHYS_BASE		(0x40000000)
#define CS5_PHYS_BASE		(0x50000000)
#define CS6_PHYS_BASE		(0x60000000)
#define CS7_PHYS_BASE		(0x70000000)

#define CLPS711X_SRAM_BASE	CS6_PHYS_BASE
#define CLPS711X_SRAM_SIZE	(48 * 1024)

#define CLPS711X_SDRAM0_BASE	(0xc0000000)
#define CLPS711X_SDRAM1_BASE	(0xd0000000)

#endif

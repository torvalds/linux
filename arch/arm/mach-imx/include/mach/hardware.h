/*
 *  arch/arm/mach-imx/include/mach/hardware.h
 *
 *  Copyright (C) 1999 ARM Limited.
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
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/sizes.h>
#include "imx-regs.h"

#ifndef __ASSEMBLY__
# define __REG(x)	(*((volatile u32 *)IO_ADDRESS(x)))

# define __REG2(x,y)        (*(volatile u32 *)((u32)&__REG(x) + (y)))
#endif

/*
 * Memory map
 */

#define IMX_IO_PHYS		0x00200000
#define IMX_IO_SIZE		0x00100000
#define IMX_IO_BASE		0xe0000000

#define IMX_CS0_PHYS		0x10000000
#define IMX_CS0_SIZE		0x02000000
#define IMX_CS0_VIRT		0xe8000000

#define IMX_CS1_PHYS		0x12000000
#define IMX_CS1_SIZE		0x01000000
#define IMX_CS1_VIRT		0xea000000

#define IMX_CS2_PHYS		0x13000000
#define IMX_CS2_SIZE		0x01000000
#define IMX_CS2_VIRT		0xeb000000

#define IMX_CS3_PHYS		0x14000000
#define IMX_CS3_SIZE		0x01000000
#define IMX_CS3_VIRT		0xec000000

#define IMX_CS4_PHYS		0x15000000
#define IMX_CS4_SIZE		0x01000000
#define IMX_CS4_VIRT		0xed000000

#define IMX_CS5_PHYS		0x16000000
#define IMX_CS5_SIZE		0x01000000
#define IMX_CS5_VIRT		0xee000000

#define IMX_FB_VIRT		0xF1000000
#define IMX_FB_SIZE		(256*1024)

/* macro to get at IO space when running virtually */
#define IO_ADDRESS(x) ((x) | IMX_IO_BASE)

#ifndef __ASSEMBLY__
/*
 * Handy routine to set GPIO functions
 */
extern void imx_gpio_mode( int gpio_mode );

#endif

#define MAXIRQNUM                       62
#define MAXFIQNUM                       62
#define MAXSWINUM                       62

/*
 * Use SDRAM for memory
 */
#define MEM_SIZE		0x01000000

#ifdef CONFIG_ARCH_MX1ADS
#include "mx1ads.h"
#endif

#endif

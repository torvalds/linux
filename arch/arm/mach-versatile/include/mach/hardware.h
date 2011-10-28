/*
 *  arch/arm/mach-versatile/include/mach/hardware.h
 *
 *  This file contains the hardware definitions of the Versatile boards.
 *
 *  Copyright (C) 2003 ARM Limited.
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

/*
 * PCI space virtual addresses
 */
#define VERSATILE_PCI_VIRT_BASE		(void __iomem *)0xe8000000ul
#define VERSATILE_PCI_CFG_VIRT_BASE	(void __iomem *)0xe9000000ul

/* CIK guesswork */
#define PCIBIOS_MIN_IO			0x44000000
#define PCIBIOS_MIN_MEM			0x50000000

#define pcibios_assign_all_busses()     1

/* macro to get at IO space when running virtually */
#define IO_ADDRESS(x)		(((x) & 0x0fffffff) + (((x) >> 4) & 0x0f000000) + 0xf0000000)

#define __io_address(n)		((void __iomem __force *)IO_ADDRESS(n))

#endif

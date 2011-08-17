/*
 * arch/arm/mach-ixp2000/include/mach/memory.h
 *
 * Copyright (c) 2002 Intel Corp.
 * Copyright (c) 2003-2004 MontaVista Software, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#define PLAT_PHYS_OFFSET	UL(0x00000000)

#include <mach/ixp2000-regs.h>

#define IXP2000_PCI_SDRAM_OFFSET	(*IXP2000_PCI_SDRAM_BAR & 0xfffffff0)

#define __phys_to_bus(x)	((x) + (IXP2000_PCI_SDRAM_OFFSET - PHYS_OFFSET))
#define __bus_to_phys(x)	((x) - (IXP2000_PCI_SDRAM_OFFSET - PHYS_OFFSET))

#define __virt_to_bus(v)	__phys_to_bus(__virt_to_phys(v))
#define __bus_to_virt(b)	__phys_to_virt(__bus_to_phys(b))
#define __pfn_to_bus(p)		__phys_to_bus(__pfn_to_phys(p))
#define __bus_to_pfn(b)		__phys_to_pfn(__bus_to_phys(b))

#endif


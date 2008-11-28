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

#define PHYS_OFFSET	UL(0x00000000)

#include <mach/ixp2000-regs.h>

#define __virt_to_bus(v) \
	(((__virt_to_phys(v) - 0x0) + (*IXP2000_PCI_SDRAM_BAR & 0xfffffff0)))

#define __bus_to_virt(b) \
	__phys_to_virt((((b - (*IXP2000_PCI_SDRAM_BAR & 0xfffffff0)) + 0x0)))

#endif


/*
 *  arch/arm/mach-pxa/include/mach/memory.h
 *
 * Author:	Nicolas Pitre
 * Copyright:	(C) 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * Physical DRAM offset.
 */
#define PLAT_PHYS_OFFSET	UL(0xa0000000)

#if !defined(__ASSEMBLY__) && defined(CONFIG_MACH_ARMCORE) && defined(CONFIG_PCI)
void cmx2xx_pci_adjust_zones(unsigned long *size, unsigned long *holes);

#define arch_adjust_zones(size, holes) \
	cmx2xx_pci_adjust_zones(size, holes)

#define ISA_DMA_THRESHOLD	(PHYS_OFFSET + SZ_64M - 1)
#define MAX_DMA_ADDRESS		(PAGE_OFFSET + SZ_64M)
#endif

#endif

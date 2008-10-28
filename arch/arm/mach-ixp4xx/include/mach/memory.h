/*
 * arch/arm/mach-ixp4xx/include/mach/memory.h
 *
 * Copyright (c) 2001-2004 MontaVista Software, Inc.
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#include <asm/sizes.h>

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET	UL(0x00000000)

#if !defined(__ASSEMBLY__) && defined(CONFIG_PCI)

void ixp4xx_adjust_zones(int node, unsigned long *size, unsigned long *holes);

#define arch_adjust_zones(node, size, holes) \
	ixp4xx_adjust_zones(node, size, holes)

#define ISA_DMA_THRESHOLD (SZ_64M - 1)

#endif

/*
 * Virtual view <-> DMA view memory address translations
 * virt_to_bus: Used to translate the virtual address to an
 *		address suitable to be passed to set_dma_addr
 * bus_to_virt: Used to convert an address for DMA operations
 *		to an address that the kernel can use.
 *
 * These are dummies for now.
 */
#define __virt_to_bus(x)	 __virt_to_phys(x)
#define __bus_to_virt(x)	 __phys_to_virt(x)

#endif

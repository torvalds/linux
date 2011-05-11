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
#define PLAT_PHYS_OFFSET	UL(0x00000000)

#if !defined(__ASSEMBLY__) && defined(CONFIG_PCI)

void ixp4xx_adjust_zones(unsigned long *size, unsigned long *holes);

#define arch_adjust_zones(size, holes) \
	ixp4xx_adjust_zones(size, holes)

#define ARM_DMA_ZONE_SIZE	SZ_64M

#endif

#endif

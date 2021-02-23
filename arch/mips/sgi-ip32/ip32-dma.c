// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2006  Ralf Baechle <ralf@linux-mips.org>
 */
#include <linux/dma-direct.h>
#include <asm/ip32/crime.h>

/*
 * Few notes.
 * 1. CPU sees memory as two chunks: 0-256M@0x0, and the rest @0x40000000+256M
 * 2. PCI sees memory as one big chunk @0x0 (or we could use 0x40000000 for
 *    native-endian)
 * 3. All other devices see memory as one big chunk at 0x40000000
 * 4. Non-PCI devices will pass NULL as struct device*
 *
 * Thus we translate differently, depending on device.
 */

#define RAM_OFFSET_MASK 0x3fffffffUL

dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	dma_addr_t dma_addr = paddr & RAM_OFFSET_MASK;

	if (!dev)
		dma_addr += CRIME_HI_MEM_BASE;
	return dma_addr;
}

phys_addr_t dma_to_phys(struct device *dev, dma_addr_t dma_addr)
{
	phys_addr_t paddr = dma_addr & RAM_OFFSET_MASK;

	if (dma_addr >= 256*1024*1024)
		paddr += CRIME_HI_MEM_BASE;
	return paddr;
}

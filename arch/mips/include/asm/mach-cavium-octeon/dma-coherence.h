/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006  Ralf Baechle <ralf@linux-mips.org>
 *
 *
 * Similar to mach-generic/dma-coherence.h except
 * plat_device_is_coherent hard coded to return 1.
 *
 */
#ifndef __ASM_MACH_CAVIUM_OCTEON_DMA_COHERENCE_H
#define __ASM_MACH_CAVIUM_OCTEON_DMA_COHERENCE_H

#include <linux/bug.h>

struct device;

extern void octeon_pci_dma_init(void);

static inline dma_addr_t plat_map_dma_mem(struct device *dev, void *addr,
	size_t size)
{
	BUG();
	return 0;
}

static inline dma_addr_t plat_map_dma_mem_page(struct device *dev,
	struct page *page)
{
	BUG();
	return 0;
}

static inline unsigned long plat_dma_addr_to_phys(struct device *dev,
	dma_addr_t dma_addr)
{
	BUG();
	return 0;
}

static inline void plat_unmap_dma_mem(struct device *dev, dma_addr_t dma_addr,
	size_t size, enum dma_data_direction direction)
{
	BUG();
}

static inline int plat_dma_supported(struct device *dev, u64 mask)
{
	BUG();
	return 0;
}

static inline int plat_device_is_coherent(struct device *dev)
{
	return 1;
}

dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr);
phys_addr_t dma_to_phys(struct device *dev, dma_addr_t daddr);

struct dma_map_ops;
extern struct dma_map_ops *octeon_pci_dma_map_ops;
extern char *octeon_swiotlb;

#endif /* __ASM_MACH_CAVIUM_OCTEON_DMA_COHERENCE_H */

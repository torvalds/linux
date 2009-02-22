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

struct device;

dma_addr_t octeon_map_dma_mem(struct device *, void *, size_t);
void octeon_unmap_dma_mem(struct device *, dma_addr_t);

static inline dma_addr_t plat_map_dma_mem(struct device *dev, void *addr,
	size_t size)
{
	return octeon_map_dma_mem(dev, addr, size);
}

static inline dma_addr_t plat_map_dma_mem_page(struct device *dev,
	struct page *page)
{
	return octeon_map_dma_mem(dev, page_address(page), PAGE_SIZE);
}

static inline unsigned long plat_dma_addr_to_phys(dma_addr_t dma_addr)
{
	return dma_addr;
}

static inline void plat_unmap_dma_mem(struct device *dev, dma_addr_t dma_addr)
{
	octeon_unmap_dma_mem(dev, dma_addr);
}

static inline int plat_dma_supported(struct device *dev, u64 mask)
{
	return 1;
}

static inline void plat_extra_sync_for_device(struct device *dev)
{
	mb();
}

static inline int plat_device_is_coherent(struct device *dev)
{
	return 1;
}

static inline int plat_dma_mapping_error(struct device *dev,
					 dma_addr_t dma_addr)
{
	return dma_addr == -1;
}

#endif /* __ASM_MACH_CAVIUM_OCTEON_DMA_COHERENCE_H */

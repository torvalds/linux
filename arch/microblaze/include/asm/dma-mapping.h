/*
 * Implements the generic device dma API for microblaze and the pci
 *
 * Copyright (C) 2009-2010 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2009-2010 PetaLogix
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 *
 * This file is base on powerpc and x86 dma-mapping.h versions
 * Copyright (C) 2004 IBM
 */

#ifndef _ASM_MICROBLAZE_DMA_MAPPING_H
#define _ASM_MICROBLAZE_DMA_MAPPING_H

/*
 * See Documentation/DMA-API-HOWTO.txt and
 * Documentation/DMA-API.txt for documentation.
 */

#include <linux/types.h>
#include <linux/cache.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/dma-debug.h>
#include <linux/dma-attrs.h>
#include <asm/io.h>
#include <asm/cacheflush.h>

#define DMA_ERROR_CODE		(~(dma_addr_t)0x0)

#define __dma_alloc_coherent(dev, gfp, size, handle)	NULL
#define __dma_free_coherent(size, addr)		((void)0)

/*
 * Available generic sets of operations
 */
extern struct dma_map_ops dma_direct_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return &dma_direct_ops;
}

#include <asm-generic/dma-mapping-common.h>

static inline void __dma_sync(unsigned long paddr,
			      size_t size, enum dma_data_direction direction)
{
	switch (direction) {
	case DMA_TO_DEVICE:
	case DMA_BIDIRECTIONAL:
		flush_dcache_range(paddr, paddr + size);
		break;
	case DMA_FROM_DEVICE:
		invalidate_dcache_range(paddr, paddr + size);
		break;
	default:
		BUG();
	}
}

static inline void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
		enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	__dma_sync(virt_to_phys(vaddr), size, (int)direction);
}

#endif	/* _ASM_MICROBLAZE_DMA_MAPPING_H */

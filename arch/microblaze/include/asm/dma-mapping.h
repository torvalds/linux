/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_DMA_MAPPING_H
#define _ASM_MICROBLAZE_DMA_MAPPING_H

#include <asm/cacheflush.h>
#include <linux/io.h>
#include <linux/bug.h>

struct scatterlist;

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)

/* FIXME */
static inline int
dma_supported(struct device *dev, u64 mask)
{
	return 1;
}

static inline dma_addr_t
dma_map_page(struct device *dev, struct page *page,
	unsigned long offset, size_t size,
	enum dma_data_direction direction)
{
	BUG();
	return 0;
}

static inline void
dma_unmap_page(struct device *dev, dma_addr_t dma_address, size_t size,
	enum dma_data_direction direction)
{
	BUG();
}

static inline int
dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	enum dma_data_direction direction)
{
	BUG();
	return 0;
}

static inline void
dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nhwentries,
	enum dma_data_direction direction)
{
	BUG();
}

static inline void
dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle, size_t size,
			enum dma_data_direction direction)
{
	BUG();
}

static inline void
dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
		size_t size, enum dma_data_direction direction)
{
	BUG();
}

static inline void
dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, int nelems,
		enum dma_data_direction direction)
{
	BUG();
}

static inline void
dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg, int nelems,
		enum dma_data_direction direction)
{
	BUG();
}

static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return 0;
}

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
				dma_addr_t *dma_handle, int flag)
{
	return NULL; /* consistent_alloc(flag, size, dma_handle); */
}

static inline void dma_free_coherent(struct device *dev, size_t size,
			void *vaddr, dma_addr_t dma_handle)
{
	BUG();
}

static inline dma_addr_t
dma_map_single(struct device *dev, void *ptr, size_t size,
	enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	return virt_to_bus(ptr);
}

static inline void dma_unmap_single(struct device *dev, dma_addr_t dma_addr,
				    size_t size,
				    enum dma_data_direction direction)
{
	switch (direction) {
	case DMA_FROM_DEVICE:
		flush_dcache_range((unsigned)dma_addr,
			(unsigned)dma_addr + size);
			/* Fall through */
	case DMA_TO_DEVICE:
		break;
	default:
		BUG();
	}
}

#endif /* _ASM_MICROBLAZE_DMA_MAPPING_H */

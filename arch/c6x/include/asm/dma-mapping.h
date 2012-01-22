/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot <aurelien.jacquiot@ti.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */
#ifndef _ASM_C6X_DMA_MAPPING_H
#define _ASM_C6X_DMA_MAPPING_H

#include <linux/dma-debug.h>
#include <asm-generic/dma-coherent.h>

#define dma_supported(d, m)	1

static inline int dma_set_mask(struct device *dev, u64 dma_mask)
{
	if (!dev->dma_mask || !dma_supported(dev, dma_mask))
		return -EIO;

	*dev->dma_mask = dma_mask;

	return 0;
}

/*
 * DMA errors are defined by all-bits-set in the DMA address.
 */
static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return dma_addr == ~0;
}

extern dma_addr_t dma_map_single(struct device *dev, void *cpu_addr,
				 size_t size, enum dma_data_direction dir);

extern void dma_unmap_single(struct device *dev, dma_addr_t handle,
			     size_t size, enum dma_data_direction dir);

extern int dma_map_sg(struct device *dev, struct scatterlist *sglist,
		      int nents, enum dma_data_direction direction);

extern void dma_unmap_sg(struct device *dev, struct scatterlist *sglist,
			 int nents, enum dma_data_direction direction);

static inline dma_addr_t dma_map_page(struct device *dev, struct page *page,
				      unsigned long offset, size_t size,
				      enum dma_data_direction dir)
{
	dma_addr_t handle;

	handle = dma_map_single(dev, page_address(page) + offset, size, dir);

	debug_dma_map_page(dev, page, offset, size, dir, handle, false);

	return handle;
}

static inline void dma_unmap_page(struct device *dev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{
	dma_unmap_single(dev, handle, size, dir);

	debug_dma_unmap_page(dev, handle, size, dir, false);
}

extern void dma_sync_single_for_cpu(struct device *dev, dma_addr_t handle,
				    size_t size, enum dma_data_direction dir);

extern void dma_sync_single_for_device(struct device *dev, dma_addr_t handle,
				       size_t size,
				       enum dma_data_direction dir);

extern void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
				int nents, enum dma_data_direction dir);

extern void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
				   int nents, enum dma_data_direction dir);

extern void coherent_mem_init(u32 start, u32 size);
extern void *dma_alloc_coherent(struct device *, size_t, dma_addr_t *, gfp_t);
extern void dma_free_coherent(struct device *, size_t, void *, dma_addr_t);

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent((d), (s), (h), (f))
#define dma_free_noncoherent(d, s, v, h)  dma_free_coherent((d), (s), (v), (h))

#endif	/* _ASM_C6X_DMA_MAPPING_H */

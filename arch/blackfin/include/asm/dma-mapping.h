/*
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _BLACKFIN_DMA_MAPPING_H
#define _BLACKFIN_DMA_MAPPING_H

#include <asm/cacheflush.h>
struct scatterlist;

void *dma_alloc_coherent(struct device *dev, size_t size,
			 dma_addr_t *dma_handle, gfp_t gfp);
void dma_free_coherent(struct device *dev, size_t size, void *vaddr,
		       dma_addr_t dma_handle);

/*
 * Now for the API extensions over the pci_ one
 */
#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)
#define dma_supported(d, m)         (1)

static inline int
dma_set_mask(struct device *dev, u64 dma_mask)
{
	if (!dev->dma_mask || !dma_supported(dev, dma_mask))
		return -EIO;

	*dev->dma_mask = dma_mask;

	return 0;
}

static inline int
dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return 0;
}

extern void
__dma_sync(dma_addr_t addr, size_t size, enum dma_data_direction dir);
static inline void
__dma_sync_inline(dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_NONE:
		BUG();
	case DMA_TO_DEVICE:		/* writeback only */
		flush_dcache_range(addr, addr + size);
		break;
	case DMA_FROM_DEVICE: /* invalidate only */
	case DMA_BIDIRECTIONAL: /* flush and invalidate */
		/* Blackfin has no dedicated invalidate (it includes a flush) */
		invalidate_dcache_range(addr, addr + size);
		break;
	}
}
static inline void
_dma_sync(dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
	if (__builtin_constant_p(dir))
		__dma_sync_inline(addr, size, dir);
	else
		__dma_sync(addr, size, dir);
}

static inline dma_addr_t
dma_map_single(struct device *dev, void *ptr, size_t size,
	       enum dma_data_direction dir)
{
	_dma_sync((dma_addr_t)ptr, size, dir);
	return (dma_addr_t) ptr;
}

static inline dma_addr_t
dma_map_page(struct device *dev, struct page *page,
	     unsigned long offset, size_t size,
	     enum dma_data_direction dir)
{
	return dma_map_single(dev, page_address(page) + offset, size, dir);
}

static inline void
dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		 enum dma_data_direction dir)
{
	BUG_ON(!valid_dma_direction(dir));
}

static inline void
dma_unmap_page(struct device *dev, dma_addr_t dma_addr, size_t size,
	       enum dma_data_direction dir)
{
	dma_unmap_single(dev, dma_addr, size, dir);
}

extern int dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
		      enum dma_data_direction dir);

static inline void
dma_unmap_sg(struct device *dev, struct scatterlist *sg,
	     int nhwentries, enum dma_data_direction dir)
{
	BUG_ON(!valid_dma_direction(dir));
}

static inline void
dma_sync_single_range_for_cpu(struct device *dev, dma_addr_t handle,
			      unsigned long offset, size_t size,
			      enum dma_data_direction dir)
{
	BUG_ON(!valid_dma_direction(dir));
}

static inline void
dma_sync_single_range_for_device(struct device *dev, dma_addr_t handle,
				 unsigned long offset, size_t size,
				 enum dma_data_direction dir)
{
	_dma_sync(handle + offset, size, dir);
}

static inline void
dma_sync_single_for_cpu(struct device *dev, dma_addr_t handle, size_t size,
			enum dma_data_direction dir)
{
	dma_sync_single_range_for_cpu(dev, handle, 0, size, dir);
}

static inline void
dma_sync_single_for_device(struct device *dev, dma_addr_t handle, size_t size,
			   enum dma_data_direction dir)
{
	dma_sync_single_range_for_device(dev, handle, 0, size, dir);
}

static inline void
dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, int nents,
		    enum dma_data_direction dir)
{
	BUG_ON(!valid_dma_direction(dir));
}

extern void
dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
		       int nents, enum dma_data_direction dir);

static inline void
dma_cache_sync(struct device *dev, void *vaddr, size_t size,
	       enum dma_data_direction dir)
{
	_dma_sync((dma_addr_t)vaddr, size, dir);
}

#endif				/* _BLACKFIN_DMA_MAPPING_H */

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 - 2005 Tensilica Inc.
 * Copyright (C) 2015 Cadence Design Systems Inc.
 */

#ifndef _XTENSA_DMA_MAPPING_H
#define _XTENSA_DMA_MAPPING_H

#include <asm/cache.h>
#include <asm/io.h>

#include <asm-generic/dma-coherent.h>

#include <linux/mm.h>
#include <linux/scatterlist.h>

#define DMA_ERROR_CODE		(~(dma_addr_t)0x0)

extern struct dma_map_ops xtensa_dma_map_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	if (dev && dev->archdata.dma_ops)
		return dev->archdata.dma_ops;
	else
		return &xtensa_dma_map_ops;
}

#include <asm-generic/dma-mapping-common.h>

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_attrs(d, s, h, f, NULL)
#define dma_free_noncoherent(d, s, v, h) dma_free_attrs(d, s, v, h, NULL)
#define dma_alloc_coherent(d, s, h, f) dma_alloc_attrs(d, s, h, f, NULL)
#define dma_free_coherent(d, s, c, h) dma_free_attrs(d, s, c, h, NULL)

static inline void *dma_alloc_attrs(struct device *dev, size_t size,
				    dma_addr_t *dma_handle, gfp_t gfp,
				    struct dma_attrs *attrs)
{
	void *ret;
	struct dma_map_ops *ops = get_dma_ops(dev);

	if (dma_alloc_from_coherent(dev, size, dma_handle, &ret))
		return ret;

	ret = ops->alloc(dev, size, dma_handle, gfp, attrs);
	debug_dma_alloc_coherent(dev, size, *dma_handle, ret);

	return ret;
}

static inline void dma_free_attrs(struct device *dev, size_t size,
				  void *vaddr, dma_addr_t dma_handle,
				  struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = get_dma_ops(dev);

	if (dma_release_from_coherent(dev, get_order(size), vaddr))
		return;

	ops->free(dev, size, vaddr, dma_handle, attrs);
	debug_dma_free_coherent(dev, size, vaddr, dma_handle);
}

static inline int
dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	struct dma_map_ops *ops = get_dma_ops(dev);

	debug_dma_mapping_error(dev, dma_addr);
	return ops->mapping_error(dev, dma_addr);
}

static inline int
dma_supported(struct device *dev, u64 mask)
{
	return 1;
}

static inline int
dma_set_mask(struct device *dev, u64 mask)
{
	if(!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = mask;

	return 0;
}

void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
		    enum dma_data_direction direction);

#endif	/* _XTENSA_DMA_MAPPING_H */

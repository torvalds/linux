/*
 * DMA operations for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef _ASM_DMA_MAPPING_H
#define _ASM_DMA_MAPPING_H

#include <linux/types.h>
#include <linux/cache.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/dma-debug.h>
#include <linux/dma-attrs.h>
#include <asm/io.h>

struct device;
extern int bad_dma_address;

extern struct dma_map_ops *dma_ops;

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	if (unlikely(dev == NULL))
		return NULL;

	return dma_ops;
}

extern int dma_supported(struct device *dev, u64 mask);
extern int dma_set_mask(struct device *dev, u64 mask);
extern int dma_is_consistent(struct device *dev, dma_addr_t dma_handle);
extern void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
			   enum dma_data_direction direction);

#include <asm-generic/dma-mapping-common.h>

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (!dev->dma_mask)
		return 0;
	return addr + size - 1 <= *dev->dma_mask;
}

static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	struct dma_map_ops *dma_ops = get_dma_ops(dev);

	if (dma_ops->mapping_error)
		return dma_ops->mapping_error(dev, dma_addr);

	return (dma_addr == bad_dma_address);
}

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
				       dma_addr_t *dma_handle, gfp_t flag)
{
	void *ret;
	struct dma_map_ops *ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);

	ret = ops->alloc_coherent(dev, size, dma_handle, flag);

	debug_dma_alloc_coherent(dev, size, *dma_handle, ret);

	return ret;
}

static inline void dma_free_coherent(struct device *dev, size_t size,
				     void *cpu_addr, dma_addr_t dma_handle)
{
	struct dma_map_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);

	dma_ops->free_coherent(dev, size, cpu_addr, dma_handle);

	debug_dma_free_coherent(dev, size, cpu_addr, dma_handle);
}

#endif

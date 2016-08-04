/*
 * DMA operations for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
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
#include <linux/dma-debug.h>
#include <asm/io.h>

struct device;
extern int bad_dma_address;
#define DMA_ERROR_CODE bad_dma_address

extern struct dma_map_ops *dma_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	if (unlikely(dev == NULL))
		return NULL;

	return dma_ops;
}

#define HAVE_ARCH_DMA_SUPPORTED 1
extern int dma_supported(struct device *dev, u64 mask);
extern int dma_is_consistent(struct device *dev, dma_addr_t dma_handle);
extern void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
			   enum dma_data_direction direction);

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (!dev->dma_mask)
		return 0;
	return addr + size - 1 <= *dev->dma_mask;
}

#endif

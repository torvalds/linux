/*
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2009 Wind River Systems Inc
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 */

#ifndef _ASM_NIOS2_DMA_MAPPING_H
#define _ASM_NIOS2_DMA_MAPPING_H

extern const struct dma_map_ops nios2_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &nios2_dma_ops;
}

/*
 * dma_alloc_attrs() always returns non-cacheable memory, so there's no need to
 * do any flushing here.
 */
static inline void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
				  enum dma_data_direction direction)
{
}

#endif /* _ASM_NIOS2_DMA_MAPPING_H */

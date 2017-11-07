/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_DMA_MAPPING_H
#define _ALPHA_DMA_MAPPING_H

extern const struct dma_map_ops *dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return dma_ops;
}

#define dma_cache_sync(dev, va, size, dir)		  ((void)0)

#endif	/* _ALPHA_DMA_MAPPING_H */

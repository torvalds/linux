/*
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _BLACKFIN_DMA_MAPPING_H
#define _BLACKFIN_DMA_MAPPING_H

#include <asm/cacheflush.h>

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

extern const struct dma_map_ops bfin_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &bfin_dma_ops;
}

#endif				/* _BLACKFIN_DMA_MAPPING_H */

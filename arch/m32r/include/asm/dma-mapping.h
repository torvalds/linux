#ifndef _ASM_M32R_DMA_MAPPING_H
#define _ASM_M32R_DMA_MAPPING_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/dma-debug.h>
#include <linux/io.h>

#define DMA_ERROR_CODE (~(dma_addr_t)0x0)

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	if (dev && dev->archdata.dma_ops)
		return dev->archdata.dma_ops;
	return &dma_noop_ops;
}

static inline void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
				  enum dma_data_direction direction)
{
}

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (!dev->dma_mask)
		return false;
	return addr + size - 1 <= *dev->dma_mask;
}

#endif /* _ASM_M32R_DMA_MAPPING_H */

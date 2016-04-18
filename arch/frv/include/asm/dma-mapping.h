#ifndef _ASM_DMA_MAPPING_H
#define _ASM_DMA_MAPPING_H

#include <asm/cache.h>
#include <asm/cacheflush.h>

extern unsigned long __nongprelbss dma_coherent_mem_start;
extern unsigned long __nongprelbss dma_coherent_mem_end;

extern struct dma_map_ops frv_dma_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return &frv_dma_ops;
}

static inline
void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
		    enum dma_data_direction direction)
{
	flush_write_buffers();
}

#endif  /* _ASM_DMA_MAPPING_H */

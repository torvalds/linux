#ifndef ___ASM_SPARC_DMA_MAPPING_H
#define ___ASM_SPARC_DMA_MAPPING_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/dma-mapping_64.h>
#else
#include <asm/dma-mapping_32.h>
#endif

#define DMA_ERROR_CODE	(~(dma_addr_t)0x0)

extern int dma_supported(struct device *dev, u64 mask);
extern int dma_set_mask(struct device *dev, u64 dma_mask);

static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return (dma_addr == DMA_ERROR_CODE);
}

static inline int dma_get_cache_alignment(void)
{
	/*
	 * no easy way to get cache size on all processors, so return
	 * the maximum possible, to be safe
	 */
	return (1 << INTERNODE_CACHE_SHIFT);
}

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)
#define dma_is_consistent(d, h)	(1)

static inline void dma_sync_single_range_for_cpu(struct device *dev,
						 dma_addr_t dma_handle,
						 unsigned long offset,
						 size_t size,
						 enum dma_data_direction dir)
{
	dma_sync_single_for_cpu(dev, dma_handle+offset, size, dir);
}

static inline void dma_sync_single_range_for_device(struct device *dev,
						    dma_addr_t dma_handle,
						    unsigned long offset,
						    size_t size,
						    enum dma_data_direction dir)
{
	dma_sync_single_for_device(dev, dma_handle+offset, size, dir);
}

#endif

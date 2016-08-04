#ifndef __ASM_SH_DMA_MAPPING_H
#define __ASM_SH_DMA_MAPPING_H

extern struct dma_map_ops *dma_ops;
extern void no_iommu_init(void);

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return dma_ops;
}

#define DMA_ERROR_CODE 0

void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
		    enum dma_data_direction dir);

/* arch/sh/mm/consistent.c */
extern void *dma_generic_alloc_coherent(struct device *dev, size_t size,
					dma_addr_t *dma_addr, gfp_t flag,
					unsigned long attrs);
extern void dma_generic_free_coherent(struct device *dev, size_t size,
				      void *vaddr, dma_addr_t dma_handle,
				      unsigned long attrs);

#endif /* __ASM_SH_DMA_MAPPING_H */

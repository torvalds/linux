#ifndef _ASM_SPARC_DMA_MAPPING_H
#define _ASM_SPARC_DMA_MAPPING_H

#include <linux/types.h>

struct device;
struct scatterlist;
struct page;

extern void *dma_alloc_coherent(struct device *dev, size_t size,
				dma_addr_t *dma_handle, gfp_t flag);
extern void dma_free_coherent(struct device *dev, size_t size,
			      void *cpu_addr, dma_addr_t dma_handle);
extern dma_addr_t dma_map_single(struct device *dev, void *cpu_addr,
				 size_t size,
				 enum dma_data_direction direction);
extern void dma_unmap_single(struct device *dev, dma_addr_t dma_addr,
			     size_t size,
			     enum dma_data_direction direction);
extern dma_addr_t dma_map_page(struct device *dev, struct page *page,
			       unsigned long offset, size_t size,
			       enum dma_data_direction direction);
extern void dma_unmap_page(struct device *dev, dma_addr_t dma_address,
			   size_t size, enum dma_data_direction direction);
extern int dma_map_sg(struct device *dev, struct scatterlist *sg,
		      int nents, enum dma_data_direction direction);
extern void dma_unmap_sg(struct device *dev, struct scatterlist *sg,
			 int nents, enum dma_data_direction direction);
extern void dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle,
				    size_t size,
				    enum dma_data_direction direction);
extern void dma_sync_single_for_device(struct device *dev,
				       dma_addr_t dma_handle,
				       size_t size,
				       enum dma_data_direction direction);
extern void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
				int nelems, enum dma_data_direction direction);
extern void dma_sync_sg_for_device(struct device *dev,
				   struct scatterlist *sg, int nelems,
				   enum dma_data_direction direction);

#endif /* _ASM_SPARC_DMA_MAPPING_H */

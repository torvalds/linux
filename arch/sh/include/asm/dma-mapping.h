/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_DMA_MAPPING_H
#define __ASM_SH_DMA_MAPPING_H

extern const struct dma_map_ops *dma_ops;
extern void no_iommu_init(void);

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return dma_ops;
}

extern void *dma_generic_alloc_coherent(struct device *dev, size_t size,
					dma_addr_t *dma_addr, gfp_t flag,
					unsigned long attrs);
extern void dma_generic_free_coherent(struct device *dev, size_t size,
				      void *vaddr, dma_addr_t dma_handle,
				      unsigned long attrs);

void sh_sync_dma_for_device(void *vaddr, size_t size,
	    enum dma_data_direction dir);

#endif /* __ASM_SH_DMA_MAPPING_H */

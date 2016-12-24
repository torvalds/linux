#ifndef _ASM_DMA_MAPPING_H
#define _ASM_DMA_MAPPING_H

#include <linux/scatterlist.h>
#include <asm/dma-coherence.h>
#include <asm/cache.h>

#ifndef CONFIG_SGI_IP27 /* Kludge to fix 2.6.39 build for IP27 */
#include <dma-coherence.h>
#endif

extern struct dma_map_ops *mips_dma_map_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	if (dev && dev->archdata.dma_ops)
		return dev->archdata.dma_ops;
	else
		return mips_dma_map_ops;
}

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (!dev->dma_mask)
		return false;

	return addr + size <= *dev->dma_mask;
}

static inline void dma_mark_clean(void *addr, size_t size) {}

extern void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
	       enum dma_data_direction direction);

#define arch_setup_dma_ops arch_setup_dma_ops
static inline void arch_setup_dma_ops(struct device *dev, u64 dma_base,
				      u64 size, const struct iommu_ops *iommu,
				      bool coherent)
{
#ifdef CONFIG_DMA_PERDEV_COHERENT
	dev->archdata.dma_coherent = coherent;
#endif
}

#endif /* _ASM_DMA_MAPPING_H */

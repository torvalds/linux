#ifndef _ASM_IA64_DMA_MAPPING_H
#define _ASM_IA64_DMA_MAPPING_H

/*
 * Copyright (C) 2003-2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <asm/machvec.h>
#include <linux/scatterlist.h>
#include <asm/swiotlb.h>
#include <linux/dma-debug.h>

#define ARCH_HAS_DMA_GET_REQUIRED_MASK

extern struct dma_map_ops *dma_ops;
extern struct ia64_machine_vector ia64_mv;
extern void set_iommu_machvec(void);

extern void machvec_dma_sync_single(struct device *, dma_addr_t, size_t,
				    enum dma_data_direction);
extern void machvec_dma_sync_sg(struct device *, struct scatterlist *, int,
				enum dma_data_direction);

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
				       dma_addr_t *daddr, gfp_t gfp)
{
	struct dma_map_ops *ops = platform_dma_get_ops(dev);
	void *caddr;

	caddr = ops->alloc_coherent(dev, size, daddr, gfp);
	debug_dma_alloc_coherent(dev, size, *daddr, caddr);
	return caddr;
}

static inline void dma_free_coherent(struct device *dev, size_t size,
				     void *caddr, dma_addr_t daddr)
{
	struct dma_map_ops *ops = platform_dma_get_ops(dev);
	debug_dma_free_coherent(dev, size, caddr, daddr);
	ops->free_coherent(dev, size, caddr, daddr);
}

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)

#define get_dma_ops(dev) platform_dma_get_ops(dev)

#include <asm-generic/dma-mapping-common.h>

static inline int dma_mapping_error(struct device *dev, dma_addr_t daddr)
{
	struct dma_map_ops *ops = platform_dma_get_ops(dev);
	return ops->mapping_error(dev, daddr);
}

static inline int dma_supported(struct device *dev, u64 mask)
{
	struct dma_map_ops *ops = platform_dma_get_ops(dev);
	return ops->dma_supported(dev, mask);
}

static inline int
dma_set_mask (struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;
	*dev->dma_mask = mask;
	return 0;
}

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (!dev->dma_mask)
		return 0;

	return addr + size - 1 <= *dev->dma_mask;
}

static inline dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	return paddr;
}

static inline phys_addr_t dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	return daddr;
}

static inline void
dma_cache_sync (struct device *dev, void *vaddr, size_t size,
	enum dma_data_direction dir)
{
	/*
	 * IA-64 is cache-coherent, so this is mostly a no-op.  However, we do need to
	 * ensure that dma_cache_sync() enforces order, hence the mb().
	 */
	mb();
}

#endif /* _ASM_IA64_DMA_MAPPING_H */

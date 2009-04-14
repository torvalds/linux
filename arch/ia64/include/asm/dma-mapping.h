#ifndef _ASM_IA64_DMA_MAPPING_H
#define _ASM_IA64_DMA_MAPPING_H

/*
 * Copyright (C) 2003-2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <asm/machvec.h>
#include <linux/scatterlist.h>
#include <asm/swiotlb.h>

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
	return ops->alloc_coherent(dev, size, daddr, gfp);
}

static inline void dma_free_coherent(struct device *dev, size_t size,
				     void *caddr, dma_addr_t daddr)
{
	struct dma_map_ops *ops = platform_dma_get_ops(dev);
	ops->free_coherent(dev, size, caddr, daddr);
}

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)

static inline dma_addr_t dma_map_single_attrs(struct device *dev,
					      void *caddr, size_t size,
					      enum dma_data_direction dir,
					      struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = platform_dma_get_ops(dev);
	return ops->map_page(dev, virt_to_page(caddr),
			     (unsigned long)caddr & ~PAGE_MASK, size,
			     dir, attrs);
}

static inline void dma_unmap_single_attrs(struct device *dev, dma_addr_t daddr,
					  size_t size,
					  enum dma_data_direction dir,
					  struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = platform_dma_get_ops(dev);
	ops->unmap_page(dev, daddr, size, dir, attrs);
}

#define dma_map_single(d, a, s, r) dma_map_single_attrs(d, a, s, r, NULL)
#define dma_unmap_single(d, a, s, r) dma_unmap_single_attrs(d, a, s, r, NULL)

static inline int dma_map_sg_attrs(struct device *dev, struct scatterlist *sgl,
				   int nents, enum dma_data_direction dir,
				   struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = platform_dma_get_ops(dev);
	return ops->map_sg(dev, sgl, nents, dir, attrs);
}

static inline void dma_unmap_sg_attrs(struct device *dev,
				      struct scatterlist *sgl, int nents,
				      enum dma_data_direction dir,
				      struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = platform_dma_get_ops(dev);
	ops->unmap_sg(dev, sgl, nents, dir, attrs);
}

#define dma_map_sg(d, s, n, r) dma_map_sg_attrs(d, s, n, r, NULL)
#define dma_unmap_sg(d, s, n, r) dma_unmap_sg_attrs(d, s, n, r, NULL)

static inline void dma_sync_single_for_cpu(struct device *dev, dma_addr_t daddr,
					   size_t size,
					   enum dma_data_direction dir)
{
	struct dma_map_ops *ops = platform_dma_get_ops(dev);
	ops->sync_single_for_cpu(dev, daddr, size, dir);
}

static inline void dma_sync_sg_for_cpu(struct device *dev,
				       struct scatterlist *sgl,
				       int nents, enum dma_data_direction dir)
{
	struct dma_map_ops *ops = platform_dma_get_ops(dev);
	ops->sync_sg_for_cpu(dev, sgl, nents, dir);
}

static inline void dma_sync_single_for_device(struct device *dev,
					      dma_addr_t daddr,
					      size_t size,
					      enum dma_data_direction dir)
{
	struct dma_map_ops *ops = platform_dma_get_ops(dev);
	ops->sync_single_for_device(dev, daddr, size, dir);
}

static inline void dma_sync_sg_for_device(struct device *dev,
					  struct scatterlist *sgl,
					  int nents,
					  enum dma_data_direction dir)
{
	struct dma_map_ops *ops = platform_dma_get_ops(dev);
	ops->sync_sg_for_device(dev, sgl, nents, dir);
}

static inline int dma_mapping_error(struct device *dev, dma_addr_t daddr)
{
	struct dma_map_ops *ops = platform_dma_get_ops(dev);
	return ops->mapping_error(dev, daddr);
}

static inline dma_addr_t dma_map_page(struct device *dev, struct page *page,
				      size_t offset, size_t size,
				      enum dma_data_direction dir)
{
	struct dma_map_ops *ops = platform_dma_get_ops(dev);
	return ops->map_page(dev, page, offset, size, dir, NULL);
}

static inline void dma_unmap_page(struct device *dev, dma_addr_t addr,
				  size_t size, enum dma_data_direction dir)
{
	dma_unmap_single(dev, addr, size, dir);
}

/*
 * Rest of this file is part of the "Advanced DMA API".  Use at your own risk.
 * See Documentation/DMA-API.txt for details.
 */

#define dma_sync_single_range_for_cpu(dev, dma_handle, offset, size, dir)	\
	dma_sync_single_for_cpu(dev, dma_handle, size, dir)
#define dma_sync_single_range_for_device(dev, dma_handle, offset, size, dir)	\
	dma_sync_single_for_device(dev, dma_handle, size, dir)

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

extern int dma_get_cache_alignment(void);

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

#define dma_is_consistent(d, h)	(1)	/* all we do is coherent memory... */

#endif /* _ASM_IA64_DMA_MAPPING_H */

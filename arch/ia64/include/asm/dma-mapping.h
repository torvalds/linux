#ifndef _ASM_IA64_DMA_MAPPING_H
#define _ASM_IA64_DMA_MAPPING_H

/*
 * Copyright (C) 2003-2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <asm/machvec.h>
#include <linux/scatterlist.h>
#include <asm/swiotlb.h>

struct dma_mapping_ops {
	int             (*mapping_error)(struct device *dev,
					 dma_addr_t dma_addr);
	void*           (*alloc_coherent)(struct device *dev, size_t size,
				dma_addr_t *dma_handle, gfp_t gfp);
	void            (*free_coherent)(struct device *dev, size_t size,
				void *vaddr, dma_addr_t dma_handle);
	dma_addr_t      (*map_single)(struct device *hwdev, unsigned long ptr,
				size_t size, int direction);
	void            (*unmap_single)(struct device *dev, dma_addr_t addr,
				size_t size, int direction);
	dma_addr_t      (*map_single_attrs)(struct device *dev, void *cpu_addr,
					    size_t size, int direction,
					    struct dma_attrs *attrs);
	void		(*unmap_single_attrs)(struct device *dev,
					      dma_addr_t dma_addr,
					      size_t size, int direction,
					      struct dma_attrs *attrs);
	void            (*sync_single_for_cpu)(struct device *hwdev,
				dma_addr_t dma_handle, size_t size,
				int direction);
	void            (*sync_single_for_device)(struct device *hwdev,
				dma_addr_t dma_handle, size_t size,
				int direction);
	void            (*sync_single_range_for_cpu)(struct device *hwdev,
				dma_addr_t dma_handle, unsigned long offset,
				size_t size, int direction);
	void            (*sync_single_range_for_device)(struct device *hwdev,
				dma_addr_t dma_handle, unsigned long offset,
				size_t size, int direction);
	void            (*sync_sg_for_cpu)(struct device *hwdev,
				struct scatterlist *sg, int nelems,
				int direction);
	void            (*sync_sg_for_device)(struct device *hwdev,
				struct scatterlist *sg, int nelems,
				int direction);
	int             (*map_sg)(struct device *hwdev, struct scatterlist *sg,
				int nents, int direction);
	void            (*unmap_sg)(struct device *hwdev,
				struct scatterlist *sg, int nents,
				int direction);
	int             (*map_sg_attrs)(struct device *dev,
					struct scatterlist *sg, int nents,
					int direction, struct dma_attrs *attrs);
	void            (*unmap_sg_attrs)(struct device *dev,
					  struct scatterlist *sg, int nents,
					  int direction,
					  struct dma_attrs *attrs);
	int             (*dma_supported_op)(struct device *hwdev, u64 mask);
	int		is_phys;
};

extern struct dma_mapping_ops *dma_ops;
extern struct ia64_machine_vector ia64_mv;
extern void set_iommu_machvec(void);

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
				       dma_addr_t *daddr, gfp_t gfp)
{
	struct dma_mapping_ops *ops = platform_dma_get_ops(dev);
	return ops->alloc_coherent(dev, size, daddr, gfp | GFP_DMA);
}

static inline void dma_free_coherent(struct device *dev, size_t size,
				     void *caddr, dma_addr_t daddr)
{
	struct dma_mapping_ops *ops = platform_dma_get_ops(dev);
	ops->free_coherent(dev, size, caddr, daddr);
}

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)

static inline dma_addr_t dma_map_single_attrs(struct device *dev,
					      void *caddr, size_t size,
					      enum dma_data_direction dir,
					      struct dma_attrs *attrs)
{
	struct dma_mapping_ops *ops = platform_dma_get_ops(dev);
	return ops->map_single_attrs(dev, caddr, size, dir, attrs);
}

static inline void dma_unmap_single_attrs(struct device *dev, dma_addr_t daddr,
					  size_t size,
					  enum dma_data_direction dir,
					  struct dma_attrs *attrs)
{
	struct dma_mapping_ops *ops = platform_dma_get_ops(dev);
	ops->unmap_single_attrs(dev, daddr, size, dir, attrs);
}

#define dma_map_single(d, a, s, r) dma_map_single_attrs(d, a, s, r, NULL)
#define dma_unmap_single(d, a, s, r) dma_unmap_single_attrs(d, a, s, r, NULL)

static inline int dma_map_sg_attrs(struct device *dev, struct scatterlist *sgl,
				   int nents, enum dma_data_direction dir,
				   struct dma_attrs *attrs)
{
	struct dma_mapping_ops *ops = platform_dma_get_ops(dev);
	return ops->map_sg_attrs(dev, sgl, nents, dir, attrs);
}

static inline void dma_unmap_sg_attrs(struct device *dev,
				      struct scatterlist *sgl, int nents,
				      enum dma_data_direction dir,
				      struct dma_attrs *attrs)
{
	struct dma_mapping_ops *ops = platform_dma_get_ops(dev);
	ops->unmap_sg_attrs(dev, sgl, nents, dir, attrs);
}

#define dma_map_sg(d, s, n, r) dma_map_sg_attrs(d, s, n, r, NULL)
#define dma_unmap_sg(d, s, n, r) dma_unmap_sg_attrs(d, s, n, r, NULL)

static inline void dma_sync_single_for_cpu(struct device *dev, dma_addr_t daddr,
					   size_t size,
					   enum dma_data_direction dir)
{
	struct dma_mapping_ops *ops = platform_dma_get_ops(dev);
	ops->sync_single_for_cpu(dev, daddr, size, dir);
}

static inline void dma_sync_sg_for_cpu(struct device *dev,
				       struct scatterlist *sgl,
				       int nents, enum dma_data_direction dir)
{
	struct dma_mapping_ops *ops = platform_dma_get_ops(dev);
	ops->sync_sg_for_cpu(dev, sgl, nents, dir);
}

static inline void dma_sync_single_for_device(struct device *dev,
					      dma_addr_t daddr,
					      size_t size,
					      enum dma_data_direction dir)
{
	struct dma_mapping_ops *ops = platform_dma_get_ops(dev);
	ops->sync_single_for_device(dev, daddr, size, dir);
}

static inline void dma_sync_sg_for_device(struct device *dev,
					  struct scatterlist *sgl,
					  int nents,
					  enum dma_data_direction dir)
{
	struct dma_mapping_ops *ops = platform_dma_get_ops(dev);
	ops->sync_sg_for_device(dev, sgl, nents, dir);
}

static inline int dma_mapping_error(struct device *dev, dma_addr_t daddr)
{
	struct dma_mapping_ops *ops = platform_dma_get_ops(dev);
	return ops->mapping_error(dev, daddr);
}

#define dma_map_page(dev, pg, off, size, dir)				\
	dma_map_single(dev, page_address(pg) + (off), (size), (dir))
#define dma_unmap_page(dev, dma_addr, size, dir)			\
	dma_unmap_single(dev, dma_addr, size, dir)

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
	struct dma_mapping_ops *ops = platform_dma_get_ops(dev);
	return ops->dma_supported_op(dev, mask);
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

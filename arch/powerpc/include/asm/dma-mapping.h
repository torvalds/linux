/*
 * Copyright (C) 2004 IBM
 *
 * Implements the generic device dma API for powerpc.
 * the pci and vio busses
 */
#ifndef _ASM_DMA_MAPPING_H
#define _ASM_DMA_MAPPING_H
#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/cache.h>
/* need struct page definitions */
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/dma-attrs.h>
#include <asm/io.h>

#define DMA_ERROR_CODE		(~(dma_addr_t)0x0)

#ifdef CONFIG_NOT_COHERENT_CACHE
/*
 * DMA-consistent mapping functions for PowerPCs that don't support
 * cache snooping.  These allocate/free a region of uncached mapped
 * memory space for use with DMA devices.  Alternatively, you could
 * allocate the space "normally" and use the cache management functions
 * to ensure it is consistent.
 */
struct device;
extern void *__dma_alloc_coherent(struct device *dev, size_t size,
				  dma_addr_t *handle, gfp_t gfp);
extern void __dma_free_coherent(size_t size, void *vaddr);
extern void __dma_sync(void *vaddr, size_t size, int direction);
extern void __dma_sync_page(struct page *page, unsigned long offset,
				 size_t size, int direction);

#else /* ! CONFIG_NOT_COHERENT_CACHE */
/*
 * Cache coherent cores.
 */

#define __dma_alloc_coherent(dev, gfp, size, handle)	NULL
#define __dma_free_coherent(size, addr)		((void)0)
#define __dma_sync(addr, size, rw)		((void)0)
#define __dma_sync_page(pg, off, sz, rw)	((void)0)

#endif /* ! CONFIG_NOT_COHERENT_CACHE */

static inline unsigned long device_to_mask(struct device *dev)
{
	if (dev->dma_mask && *dev->dma_mask)
		return *dev->dma_mask;
	/* Assume devices without mask can take 32 bit addresses */
	return 0xfffffffful;
}

/*
 * DMA operations are abstracted for G5 vs. i/pSeries, PCI vs. VIO
 */
struct dma_mapping_ops {
	void *		(*alloc_coherent)(struct device *dev, size_t size,
				dma_addr_t *dma_handle, gfp_t flag);
	void		(*free_coherent)(struct device *dev, size_t size,
				void *vaddr, dma_addr_t dma_handle);
	int		(*map_sg)(struct device *dev, struct scatterlist *sg,
				int nents, enum dma_data_direction direction,
				struct dma_attrs *attrs);
	void		(*unmap_sg)(struct device *dev, struct scatterlist *sg,
				int nents, enum dma_data_direction direction,
				struct dma_attrs *attrs);
	int		(*dma_supported)(struct device *dev, u64 mask);
	int		(*set_dma_mask)(struct device *dev, u64 dma_mask);
	dma_addr_t 	(*map_page)(struct device *dev, struct page *page,
				unsigned long offset, size_t size,
				enum dma_data_direction direction,
				struct dma_attrs *attrs);
	void		(*unmap_page)(struct device *dev,
				dma_addr_t dma_address, size_t size,
				enum dma_data_direction direction,
				struct dma_attrs *attrs);
#ifdef CONFIG_PPC_NEED_DMA_SYNC_OPS
	void            (*sync_single_range_for_cpu)(struct device *hwdev,
				dma_addr_t dma_handle, unsigned long offset,
				size_t size,
				enum dma_data_direction direction);
	void            (*sync_single_range_for_device)(struct device *hwdev,
				dma_addr_t dma_handle, unsigned long offset,
				size_t size,
				enum dma_data_direction direction);
	void            (*sync_sg_for_cpu)(struct device *hwdev,
				struct scatterlist *sg, int nelems,
				enum dma_data_direction direction);
	void            (*sync_sg_for_device)(struct device *hwdev,
				struct scatterlist *sg, int nelems,
				enum dma_data_direction direction);
#endif
};

/*
 * Available generic sets of operations
 */
#ifdef CONFIG_PPC64
extern struct dma_mapping_ops dma_iommu_ops;
#endif
extern struct dma_mapping_ops dma_direct_ops;

static inline struct dma_mapping_ops *get_dma_ops(struct device *dev)
{
	/* We don't handle the NULL dev case for ISA for now. We could
	 * do it via an out of line call but it is not needed for now. The
	 * only ISA DMA device we support is the floppy and we have a hack
	 * in the floppy driver directly to get a device for us.
	 */
	if (unlikely(dev == NULL))
		return NULL;

	return dev->archdata.dma_ops;
}

static inline void set_dma_ops(struct device *dev, struct dma_mapping_ops *ops)
{
	dev->archdata.dma_ops = ops;
}

static inline int dma_supported(struct device *dev, u64 mask)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	if (unlikely(dma_ops == NULL))
		return 0;
	if (dma_ops->dma_supported == NULL)
		return 1;
	return dma_ops->dma_supported(dev, mask);
}

/* We have our own implementation of pci_set_dma_mask() */
#define HAVE_ARCH_PCI_SET_DMA_MASK

static inline int dma_set_mask(struct device *dev, u64 dma_mask)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	if (unlikely(dma_ops == NULL))
		return -EIO;
	if (dma_ops->set_dma_mask != NULL)
		return dma_ops->set_dma_mask(dev, dma_mask);
	if (!dev->dma_mask || !dma_supported(dev, dma_mask))
		return -EIO;
	*dev->dma_mask = dma_mask;
	return 0;
}

/*
 * map_/unmap_single actually call through to map/unmap_page now that all the
 * dma_mapping_ops have been converted over. We just have to get the page and
 * offset to pass through to map_page
 */
static inline dma_addr_t dma_map_single_attrs(struct device *dev,
					      void *cpu_addr,
					      size_t size,
					      enum dma_data_direction direction,
					      struct dma_attrs *attrs)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);

	return dma_ops->map_page(dev, virt_to_page(cpu_addr),
				 (unsigned long)cpu_addr % PAGE_SIZE, size,
				 direction, attrs);
}

static inline void dma_unmap_single_attrs(struct device *dev,
					  dma_addr_t dma_addr,
					  size_t size,
					  enum dma_data_direction direction,
					  struct dma_attrs *attrs)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);

	dma_ops->unmap_page(dev, dma_addr, size, direction, attrs);
}

static inline dma_addr_t dma_map_page_attrs(struct device *dev,
					    struct page *page,
					    unsigned long offset, size_t size,
					    enum dma_data_direction direction,
					    struct dma_attrs *attrs)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);

	return dma_ops->map_page(dev, page, offset, size, direction, attrs);
}

static inline void dma_unmap_page_attrs(struct device *dev,
					dma_addr_t dma_address,
					size_t size,
					enum dma_data_direction direction,
					struct dma_attrs *attrs)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);

	dma_ops->unmap_page(dev, dma_address, size, direction, attrs);
}

static inline int dma_map_sg_attrs(struct device *dev, struct scatterlist *sg,
				   int nents, enum dma_data_direction direction,
				   struct dma_attrs *attrs)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);
	return dma_ops->map_sg(dev, sg, nents, direction, attrs);
}

static inline void dma_unmap_sg_attrs(struct device *dev,
				      struct scatterlist *sg,
				      int nhwentries,
				      enum dma_data_direction direction,
				      struct dma_attrs *attrs)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);
	dma_ops->unmap_sg(dev, sg, nhwentries, direction, attrs);
}

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
				       dma_addr_t *dma_handle, gfp_t flag)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);
	return dma_ops->alloc_coherent(dev, size, dma_handle, flag);
}

static inline void dma_free_coherent(struct device *dev, size_t size,
				     void *cpu_addr, dma_addr_t dma_handle)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);
	dma_ops->free_coherent(dev, size, cpu_addr, dma_handle);
}

static inline dma_addr_t dma_map_single(struct device *dev, void *cpu_addr,
					size_t size,
					enum dma_data_direction direction)
{
	return dma_map_single_attrs(dev, cpu_addr, size, direction, NULL);
}

static inline void dma_unmap_single(struct device *dev, dma_addr_t dma_addr,
				    size_t size,
				    enum dma_data_direction direction)
{
	dma_unmap_single_attrs(dev, dma_addr, size, direction, NULL);
}

static inline dma_addr_t dma_map_page(struct device *dev, struct page *page,
				      unsigned long offset, size_t size,
				      enum dma_data_direction direction)
{
	return dma_map_page_attrs(dev, page, offset, size, direction, NULL);
}

static inline void dma_unmap_page(struct device *dev, dma_addr_t dma_address,
				  size_t size,
				  enum dma_data_direction direction)
{
	dma_unmap_page_attrs(dev, dma_address, size, direction, NULL);
}

static inline int dma_map_sg(struct device *dev, struct scatterlist *sg,
			     int nents, enum dma_data_direction direction)
{
	return dma_map_sg_attrs(dev, sg, nents, direction, NULL);
}

static inline void dma_unmap_sg(struct device *dev, struct scatterlist *sg,
				int nhwentries,
				enum dma_data_direction direction)
{
	dma_unmap_sg_attrs(dev, sg, nhwentries, direction, NULL);
}

#ifdef CONFIG_PPC_NEED_DMA_SYNC_OPS
static inline void dma_sync_single_for_cpu(struct device *dev,
		dma_addr_t dma_handle, size_t size,
		enum dma_data_direction direction)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);
	dma_ops->sync_single_range_for_cpu(dev, dma_handle, 0,
					   size, direction);
}

static inline void dma_sync_single_for_device(struct device *dev,
		dma_addr_t dma_handle, size_t size,
		enum dma_data_direction direction)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);
	dma_ops->sync_single_range_for_device(dev, dma_handle,
					      0, size, direction);
}

static inline void dma_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sgl, int nents,
		enum dma_data_direction direction)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);
	dma_ops->sync_sg_for_cpu(dev, sgl, nents, direction);
}

static inline void dma_sync_sg_for_device(struct device *dev,
		struct scatterlist *sgl, int nents,
		enum dma_data_direction direction)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);
	dma_ops->sync_sg_for_device(dev, sgl, nents, direction);
}

static inline void dma_sync_single_range_for_cpu(struct device *dev,
		dma_addr_t dma_handle, unsigned long offset, size_t size,
		enum dma_data_direction direction)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);
	dma_ops->sync_single_range_for_cpu(dev, dma_handle,
					   offset, size, direction);
}

static inline void dma_sync_single_range_for_device(struct device *dev,
		dma_addr_t dma_handle, unsigned long offset, size_t size,
		enum dma_data_direction direction)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);
	dma_ops->sync_single_range_for_device(dev, dma_handle, offset,
					      size, direction);
}
#else /* CONFIG_PPC_NEED_DMA_SYNC_OPS */
static inline void dma_sync_single_for_cpu(struct device *dev,
		dma_addr_t dma_handle, size_t size,
		enum dma_data_direction direction)
{
}

static inline void dma_sync_single_for_device(struct device *dev,
		dma_addr_t dma_handle, size_t size,
		enum dma_data_direction direction)
{
}

static inline void dma_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sgl, int nents,
		enum dma_data_direction direction)
{
}

static inline void dma_sync_sg_for_device(struct device *dev,
		struct scatterlist *sgl, int nents,
		enum dma_data_direction direction)
{
}

static inline void dma_sync_single_range_for_cpu(struct device *dev,
		dma_addr_t dma_handle, unsigned long offset, size_t size,
		enum dma_data_direction direction)
{
}

static inline void dma_sync_single_range_for_device(struct device *dev,
		dma_addr_t dma_handle, unsigned long offset, size_t size,
		enum dma_data_direction direction)
{
}
#endif

static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
#ifdef CONFIG_PPC64
	return (dma_addr == DMA_ERROR_CODE);
#else
	return 0;
#endif
}

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)
#ifdef CONFIG_NOT_COHERENT_CACHE
#define dma_is_consistent(d, h)	(0)
#else
#define dma_is_consistent(d, h)	(1)
#endif

static inline int dma_get_cache_alignment(void)
{
#ifdef CONFIG_PPC64
	/* no easy way to get cache size on all processors, so return
	 * the maximum possible, to be safe */
	return (1 << INTERNODE_CACHE_SHIFT);
#else
	/*
	 * Each processor family will define its own L1_CACHE_SHIFT,
	 * L1_CACHE_BYTES wraps to this, so this is always safe.
	 */
	return L1_CACHE_BYTES;
#endif
}

static inline void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
		enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	__dma_sync(vaddr, size, (int)direction);
}

#endif /* __KERNEL__ */
#endif	/* _ASM_DMA_MAPPING_H */

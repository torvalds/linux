#ifndef _M68K_DMA_MAPPING_H
#define _M68K_DMA_MAPPING_H

#include <asm/cache.h>

struct scatterlist;

#ifndef CONFIG_MMU_SUN3
static inline int dma_supported(struct device *dev, u64 mask)
{
	return 1;
}

static inline int dma_set_mask(struct device *dev, u64 mask)
{
	return 0;
}

extern void *dma_alloc_coherent(struct device *, size_t,
				dma_addr_t *, gfp_t);
extern void dma_free_coherent(struct device *, size_t,
			      void *, dma_addr_t);

static inline void *dma_alloc_attrs(struct device *dev, size_t size,
				    dma_addr_t *dma_handle, gfp_t flag,
				    struct dma_attrs *attrs)
{
	/* attrs is not supported and ignored */
	return dma_alloc_coherent(dev, size, dma_handle, flag);
}

static inline void dma_free_attrs(struct device *dev, size_t size,
				  void *cpu_addr, dma_addr_t dma_handle,
				  struct dma_attrs *attrs)
{
	/* attrs is not supported and ignored */
	dma_free_coherent(dev, size, cpu_addr, dma_handle);
}

static inline void *dma_alloc_noncoherent(struct device *dev, size_t size,
					  dma_addr_t *handle, gfp_t flag)
{
	return dma_alloc_coherent(dev, size, handle, flag);
}
static inline void dma_free_noncoherent(struct device *dev, size_t size,
					void *addr, dma_addr_t handle)
{
	dma_free_coherent(dev, size, addr, handle);
}
static inline void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
				  enum dma_data_direction dir)
{
	/* we use coherent allocation, so not much to do here. */
}

extern dma_addr_t dma_map_single(struct device *, void *, size_t,
				 enum dma_data_direction);
static inline void dma_unmap_single(struct device *dev, dma_addr_t addr,
				    size_t size, enum dma_data_direction dir)
{
}

extern dma_addr_t dma_map_page(struct device *, struct page *,
			       unsigned long, size_t size,
			       enum dma_data_direction);
static inline void dma_unmap_page(struct device *dev, dma_addr_t address,
				  size_t size, enum dma_data_direction dir)
{
}

extern int dma_map_sg(struct device *, struct scatterlist *, int,
		      enum dma_data_direction);
static inline void dma_unmap_sg(struct device *dev, struct scatterlist *sg,
				int nhwentries, enum dma_data_direction dir)
{
}

extern void dma_sync_single_for_device(struct device *, dma_addr_t, size_t,
				       enum dma_data_direction);
extern void dma_sync_sg_for_device(struct device *, struct scatterlist *, int,
				   enum dma_data_direction);

static inline void dma_sync_single_range_for_device(struct device *dev,
		dma_addr_t dma_handle, unsigned long offset, size_t size,
		enum dma_data_direction direction)
{
	/* just sync everything for now */
	dma_sync_single_for_device(dev, dma_handle, offset + size, direction);
}

static inline void dma_sync_single_for_cpu(struct device *dev, dma_addr_t handle,
					   size_t size, enum dma_data_direction dir)
{
}

static inline void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
				       int nents, enum dma_data_direction dir)
{
}

static inline void dma_sync_single_range_for_cpu(struct device *dev,
		dma_addr_t dma_handle, unsigned long offset, size_t size,
		enum dma_data_direction direction)
{
	/* just sync everything for now */
	dma_sync_single_for_cpu(dev, dma_handle, offset + size, direction);
}

static inline int dma_mapping_error(struct device *dev, dma_addr_t handle)
{
	return 0;
}

#else
#include <asm-generic/dma-mapping-broken.h>
#endif

/* drivers/base/dma-mapping.c */
extern int dma_common_mmap(struct device *dev, struct vm_area_struct *vma,
			   void *cpu_addr, dma_addr_t dma_addr, size_t size);
extern int dma_common_get_sgtable(struct device *dev, struct sg_table *sgt,
				  void *cpu_addr, dma_addr_t dma_addr,
				  size_t size);

#define dma_mmap_coherent(d, v, c, h, s) dma_common_mmap(d, v, c, h, s)
#define dma_get_sgtable(d, t, v, h, s) dma_common_get_sgtable(d, t, v, h, s)

#endif  /* _M68K_DMA_MAPPING_H */

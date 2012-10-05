#ifndef _ASM_METAG_DMA_MAPPING_H
#define _ASM_METAG_DMA_MAPPING_H

#include <linux/mm.h>

#include <asm/cache.h>
#include <asm/io.h>
#include <linux/scatterlist.h>
#include <asm/bug.h>

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)

void *dma_alloc_coherent(struct device *dev, size_t size,
			 dma_addr_t *dma_handle, gfp_t flag);

void dma_free_coherent(struct device *dev, size_t size,
		       void *vaddr, dma_addr_t dma_handle);

void dma_sync_for_device(void *vaddr, size_t size, int dma_direction);
void dma_sync_for_cpu(void *vaddr, size_t size, int dma_direction);

int dma_mmap_coherent(struct device *dev, struct vm_area_struct *vma,
		      void *cpu_addr, dma_addr_t dma_addr, size_t size);

int dma_mmap_writecombine(struct device *dev, struct vm_area_struct *vma,
			  void *cpu_addr, dma_addr_t dma_addr, size_t size);

static inline dma_addr_t
dma_map_single(struct device *dev, void *ptr, size_t size,
	       enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
	WARN_ON(size == 0);
	dma_sync_for_device(ptr, size, direction);
	return virt_to_phys(ptr);
}

static inline void
dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		 enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
	dma_sync_for_cpu(phys_to_virt(dma_addr), size, direction);
}

static inline int
dma_map_sg(struct device *dev, struct scatterlist *sglist, int nents,
	   enum dma_data_direction direction)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(!valid_dma_direction(direction));
	WARN_ON(nents == 0 || sglist[0].length == 0);

	for_each_sg(sglist, sg, nents, i) {
		BUG_ON(!sg_page(sg));

		sg->dma_address = sg_phys(sg);
		dma_sync_for_device(sg_virt(sg), sg->length, direction);
	}

	return nents;
}

static inline dma_addr_t
dma_map_page(struct device *dev, struct page *page, unsigned long offset,
	     size_t size, enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
	dma_sync_for_device((void *)(page_to_phys(page) + offset), size,
			    direction);
	return page_to_phys(page) + offset;
}

static inline void
dma_unmap_page(struct device *dev, dma_addr_t dma_address, size_t size,
	       enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
	dma_sync_for_cpu(phys_to_virt(dma_address), size, direction);
}


static inline void
dma_unmap_sg(struct device *dev, struct scatterlist *sglist, int nhwentries,
	     enum dma_data_direction direction)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(!valid_dma_direction(direction));
	WARN_ON(nhwentries == 0 || sglist[0].length == 0);

	for_each_sg(sglist, sg, nhwentries, i) {
		BUG_ON(!sg_page(sg));

		sg->dma_address = sg_phys(sg);
		dma_sync_for_cpu(sg_virt(sg), sg->length, direction);
	}
}

static inline void
dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle, size_t size,
			enum dma_data_direction direction)
{
	dma_sync_for_cpu(phys_to_virt(dma_handle), size, direction);
}

static inline void
dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
			   size_t size, enum dma_data_direction direction)
{
	dma_sync_for_device(phys_to_virt(dma_handle), size, direction);
}

static inline void
dma_sync_single_range_for_cpu(struct device *dev, dma_addr_t dma_handle,
			      unsigned long offset, size_t size,
			      enum dma_data_direction direction)
{
	dma_sync_for_cpu(phys_to_virt(dma_handle)+offset, size,
			 direction);
}

static inline void
dma_sync_single_range_for_device(struct device *dev, dma_addr_t dma_handle,
				 unsigned long offset, size_t size,
				 enum dma_data_direction direction)
{
	dma_sync_for_device(phys_to_virt(dma_handle)+offset, size,
			    direction);
}

static inline void
dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, int nelems,
		    enum dma_data_direction direction)
{
	int i;
	for (i = 0; i < nelems; i++, sg++)
		dma_sync_for_cpu(sg_virt(sg), sg->length, direction);
}

static inline void
dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg, int nelems,
		       enum dma_data_direction direction)
{
	int i;
	for (i = 0; i < nelems; i++, sg++)
		dma_sync_for_device(sg_virt(sg), sg->length, direction);
}

static inline int
dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return 0;
}

#define dma_supported(dev, mask)        (1)

static inline int
dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = mask;

	return 0;
}

/*
 * dma_alloc_noncoherent() returns non-cacheable memory, so there's no need to
 * do any flushing here.
 */
static inline void
dma_cache_sync(struct device *dev, void *vaddr, size_t size,
	       enum dma_data_direction direction)
{
}

#endif

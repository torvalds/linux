/*
 * Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corporation
 *
 * Provide default implementations of the DMA mapping callbacks for
 * directly mapped busses and busses using the iommu infrastructure
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <asm/bug.h>
#include <asm/iommu.h>
#include <asm/abs_addr.h>

/*
 * Generic iommu implementation
 */

/* Allocates a contiguous real buffer and creates mappings over it.
 * Returns the virtual address of the buffer and sets dma_handle
 * to the dma address (mapping) of the first page.
 */
static void *dma_iommu_alloc_coherent(struct device *dev, size_t size,
				      dma_addr_t *dma_handle, gfp_t flag)
{
	return iommu_alloc_coherent(dev, dev->archdata.dma_data, size,
				    dma_handle, device_to_mask(dev), flag,
				    dev->archdata.numa_node);
}

static void dma_iommu_free_coherent(struct device *dev, size_t size,
				    void *vaddr, dma_addr_t dma_handle)
{
	iommu_free_coherent(dev->archdata.dma_data, size, vaddr, dma_handle);
}

/* Creates TCEs for a user provided buffer.  The user buffer must be
 * contiguous real kernel storage (not vmalloc).  The address of the buffer
 * passed here is the kernel (virtual) address of the buffer.  The buffer
 * need not be page aligned, the dma_addr_t returned will point to the same
 * byte within the page as vaddr.
 */
static dma_addr_t dma_iommu_map_single(struct device *dev, void *vaddr,
				       size_t size,
				       enum dma_data_direction direction,
				       struct dma_attrs *attrs)
{
	return iommu_map_single(dev, dev->archdata.dma_data, vaddr, size,
				device_to_mask(dev), direction, attrs);
}


static void dma_iommu_unmap_single(struct device *dev, dma_addr_t dma_handle,
				   size_t size,
				   enum dma_data_direction direction,
				   struct dma_attrs *attrs)
{
	iommu_unmap_single(dev->archdata.dma_data, dma_handle, size, direction,
			   attrs);
}


static int dma_iommu_map_sg(struct device *dev, struct scatterlist *sglist,
			    int nelems, enum dma_data_direction direction,
			    struct dma_attrs *attrs)
{
	return iommu_map_sg(dev, dev->archdata.dma_data, sglist, nelems,
			    device_to_mask(dev), direction, attrs);
}

static void dma_iommu_unmap_sg(struct device *dev, struct scatterlist *sglist,
		int nelems, enum dma_data_direction direction,
		struct dma_attrs *attrs)
{
	iommu_unmap_sg(dev->archdata.dma_data, sglist, nelems, direction,
		       attrs);
}

/* We support DMA to/from any memory page via the iommu */
static int dma_iommu_dma_supported(struct device *dev, u64 mask)
{
	struct iommu_table *tbl = dev->archdata.dma_data;

	if (!tbl || tbl->it_offset > mask) {
		printk(KERN_INFO
		       "Warning: IOMMU offset too big for device mask\n");
		if (tbl)
			printk(KERN_INFO
			       "mask: 0x%08lx, table offset: 0x%08lx\n",
				mask, tbl->it_offset);
		else
			printk(KERN_INFO "mask: 0x%08lx, table unavailable\n",
				mask);
		return 0;
	} else
		return 1;
}

struct dma_mapping_ops dma_iommu_ops = {
	.alloc_coherent	= dma_iommu_alloc_coherent,
	.free_coherent	= dma_iommu_free_coherent,
	.map_single	= dma_iommu_map_single,
	.unmap_single	= dma_iommu_unmap_single,
	.map_sg		= dma_iommu_map_sg,
	.unmap_sg	= dma_iommu_unmap_sg,
	.dma_supported	= dma_iommu_dma_supported,
};
EXPORT_SYMBOL(dma_iommu_ops);

/*
 * Generic direct DMA implementation
 *
 * This implementation supports a per-device offset that can be applied if
 * the address at which memory is visible to devices is not 0. Platform code
 * can set archdata.dma_data to an unsigned long holding the offset. By
 * default the offset is zero.
 */

static unsigned long get_dma_direct_offset(struct device *dev)
{
	return (unsigned long)dev->archdata.dma_data;
}

static void *dma_direct_alloc_coherent(struct device *dev, size_t size,
				       dma_addr_t *dma_handle, gfp_t flag)
{
	struct page *page;
	void *ret;
	int node = dev->archdata.numa_node;

	page = alloc_pages_node(node, flag, get_order(size));
	if (page == NULL)
		return NULL;
	ret = page_address(page);
	memset(ret, 0, size);
	*dma_handle = virt_to_abs(ret) + get_dma_direct_offset(dev);

	return ret;
}

static void dma_direct_free_coherent(struct device *dev, size_t size,
				     void *vaddr, dma_addr_t dma_handle)
{
	free_pages((unsigned long)vaddr, get_order(size));
}

static dma_addr_t dma_direct_map_single(struct device *dev, void *ptr,
					size_t size,
					enum dma_data_direction direction,
					struct dma_attrs *attrs)
{
	return virt_to_abs(ptr) + get_dma_direct_offset(dev);
}

static void dma_direct_unmap_single(struct device *dev, dma_addr_t dma_addr,
				    size_t size,
				    enum dma_data_direction direction,
				    struct dma_attrs *attrs)
{
}

static int dma_direct_map_sg(struct device *dev, struct scatterlist *sgl,
			     int nents, enum dma_data_direction direction,
			     struct dma_attrs *attrs)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i) {
		sg->dma_address = sg_phys(sg) + get_dma_direct_offset(dev);
		sg->dma_length = sg->length;
	}

	return nents;
}

static void dma_direct_unmap_sg(struct device *dev, struct scatterlist *sg,
				int nents, enum dma_data_direction direction,
				struct dma_attrs *attrs)
{
}

static int dma_direct_dma_supported(struct device *dev, u64 mask)
{
	/* Could be improved to check for memory though it better be
	 * done via some global so platforms can set the limit in case
	 * they have limited DMA windows
	 */
	return mask >= DMA_32BIT_MASK;
}

struct dma_mapping_ops dma_direct_ops = {
	.alloc_coherent	= dma_direct_alloc_coherent,
	.free_coherent	= dma_direct_free_coherent,
	.map_single	= dma_direct_map_single,
	.unmap_single	= dma_direct_unmap_single,
	.map_sg		= dma_direct_map_sg,
	.unmap_sg	= dma_direct_unmap_sg,
	.dma_supported	= dma_direct_dma_supported,
};
EXPORT_SYMBOL(dma_direct_ops);

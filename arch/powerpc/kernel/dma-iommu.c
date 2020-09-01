// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corporation
 *
 * Provide default implementations of the DMA mapping callbacks for
 * busses using the iommu infrastructure
 */

#include <linux/dma-direct.h>
#include <linux/pci.h>
#include <asm/iommu.h>

/*
 * Generic iommu implementation
 */

/* Allocates a contiguous real buffer and creates mappings over it.
 * Returns the virtual address of the buffer and sets dma_handle
 * to the dma address (mapping) of the first page.
 */
static void *dma_iommu_alloc_coherent(struct device *dev, size_t size,
				      dma_addr_t *dma_handle, gfp_t flag,
				      unsigned long attrs)
{
	return iommu_alloc_coherent(dev, get_iommu_table_base(dev), size,
				    dma_handle, dev->coherent_dma_mask, flag,
				    dev_to_node(dev));
}

static void dma_iommu_free_coherent(struct device *dev, size_t size,
				    void *vaddr, dma_addr_t dma_handle,
				    unsigned long attrs)
{
	iommu_free_coherent(get_iommu_table_base(dev), size, vaddr, dma_handle);
}

/* Creates TCEs for a user provided buffer.  The user buffer must be
 * contiguous real kernel storage (not vmalloc).  The address passed here
 * comprises a page address and offset into that page. The dma_addr_t
 * returned will point to the same byte within the page as was passed in.
 */
static dma_addr_t dma_iommu_map_page(struct device *dev, struct page *page,
				     unsigned long offset, size_t size,
				     enum dma_data_direction direction,
				     unsigned long attrs)
{
	return iommu_map_page(dev, get_iommu_table_base(dev), page, offset,
			      size, dma_get_mask(dev), direction, attrs);
}


static void dma_iommu_unmap_page(struct device *dev, dma_addr_t dma_handle,
				 size_t size, enum dma_data_direction direction,
				 unsigned long attrs)
{
	iommu_unmap_page(get_iommu_table_base(dev), dma_handle, size, direction,
			 attrs);
}


static int dma_iommu_map_sg(struct device *dev, struct scatterlist *sglist,
			    int nelems, enum dma_data_direction direction,
			    unsigned long attrs)
{
	return ppc_iommu_map_sg(dev, get_iommu_table_base(dev), sglist, nelems,
				dma_get_mask(dev), direction, attrs);
}

static void dma_iommu_unmap_sg(struct device *dev, struct scatterlist *sglist,
		int nelems, enum dma_data_direction direction,
		unsigned long attrs)
{
	ppc_iommu_unmap_sg(get_iommu_table_base(dev), sglist, nelems,
			   direction, attrs);
}

static bool dma_iommu_bypass_supported(struct device *dev, u64 mask)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pci_controller *phb = pci_bus_to_host(pdev->bus);

	if (iommu_fixed_is_weak || !phb->controller_ops.iommu_bypass_supported)
		return false;
	return phb->controller_ops.iommu_bypass_supported(pdev, mask);
}

/* We support DMA to/from any memory page via the iommu */
int dma_iommu_dma_supported(struct device *dev, u64 mask)
{
	struct iommu_table *tbl = get_iommu_table_base(dev);

	if (dev_is_pci(dev) && dma_iommu_bypass_supported(dev, mask)) {
		dev->dma_ops_bypass = true;
		dev_dbg(dev, "iommu: 64-bit OK, using fixed ops\n");
		return 1;
	}

	if (!tbl) {
		dev_err(dev, "Warning: IOMMU dma not supported: mask 0x%08llx, table unavailable\n", mask);
		return 0;
	}

	if (tbl->it_offset > (mask >> tbl->it_page_shift)) {
		dev_info(dev, "Warning: IOMMU offset too big for device mask\n");
		dev_info(dev, "mask: 0x%08llx, table offset: 0x%08lx\n",
				mask, tbl->it_offset << tbl->it_page_shift);
		return 0;
	}

	dev_dbg(dev, "iommu: not 64-bit, using default ops\n");
	dev->dma_ops_bypass = false;
	return 1;
}

u64 dma_iommu_get_required_mask(struct device *dev)
{
	struct iommu_table *tbl = get_iommu_table_base(dev);
	u64 mask;

	if (!tbl)
		return 0;

	mask = 1ULL << (fls_long(tbl->it_offset + tbl->it_size) +
			tbl->it_page_shift - 1);
	mask += mask - 1;

	return mask;
}

const struct dma_map_ops dma_iommu_ops = {
	.alloc			= dma_iommu_alloc_coherent,
	.free			= dma_iommu_free_coherent,
	.map_sg			= dma_iommu_map_sg,
	.unmap_sg		= dma_iommu_unmap_sg,
	.dma_supported		= dma_iommu_dma_supported,
	.map_page		= dma_iommu_map_page,
	.unmap_page		= dma_iommu_unmap_page,
	.get_required_mask	= dma_iommu_get_required_mask,
	.mmap			= dma_common_mmap,
	.get_sgtable		= dma_common_get_sgtable,
	.alloc_pages		= dma_common_alloc_pages,
	.free_pages		= dma_common_free_pages,
};

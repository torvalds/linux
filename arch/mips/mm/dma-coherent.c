/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000  Ani Joshi <ajoshi@unixbox.com>
 * Copyright (C) 2000, 2001  Ralf Baechle <ralf@gnu.org>
 * swiped from i386, and cloned for MIPS by Geert, polished by Ralf.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/pci.h>

#include <asm/cache.h>
#include <asm/io.h>

void *dma_alloc_noncoherent(struct device *dev, size_t size,
	dma_addr_t * dma_handle, int gfp)
{
	void *ret;
	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_HIGHMEM);

	if (dev == NULL || (dev->coherent_dma_mask < 0xffffffff))
		gfp |= GFP_DMA;
	ret = (void *) __get_free_pages(gfp, get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = virt_to_phys(ret);
	}

	return ret;
}

EXPORT_SYMBOL(dma_alloc_noncoherent);

void *dma_alloc_coherent(struct device *dev, size_t size,
	dma_addr_t * dma_handle, int gfp)
	__attribute__((alias("dma_alloc_noncoherent")));

EXPORT_SYMBOL(dma_alloc_coherent);

void dma_free_noncoherent(struct device *dev, size_t size, void *vaddr,
	dma_addr_t dma_handle)
{
	unsigned long addr = (unsigned long) vaddr;

	free_pages(addr, get_order(size));
}

EXPORT_SYMBOL(dma_free_noncoherent);

void dma_free_coherent(struct device *dev, size_t size, void *vaddr,
	dma_addr_t dma_handle) __attribute__((alias("dma_free_noncoherent")));

EXPORT_SYMBOL(dma_free_coherent);

dma_addr_t dma_map_single(struct device *dev, void *ptr, size_t size,
	enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	return __pa(ptr);
}

EXPORT_SYMBOL(dma_map_single);

void dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		 enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
}

EXPORT_SYMBOL(dma_unmap_single);

int dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	enum dma_data_direction direction)
{
	int i;

	BUG_ON(direction == DMA_NONE);

	for (i = 0; i < nents; i++, sg++) {
		sg->dma_address = (dma_addr_t)page_to_phys(sg->page) + sg->offset;
	}

	return nents;
}

EXPORT_SYMBOL(dma_map_sg);

dma_addr_t dma_map_page(struct device *dev, struct page *page,
	unsigned long offset, size_t size, enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	return page_to_phys(page) + offset;
}

EXPORT_SYMBOL(dma_map_page);

void dma_unmap_page(struct device *dev, dma_addr_t dma_address, size_t size,
	       enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
}

EXPORT_SYMBOL(dma_unmap_page);

void dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nhwentries,
	     enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
}

EXPORT_SYMBOL(dma_unmap_sg);

void dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle,
	size_t size, enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
}

EXPORT_SYMBOL(dma_sync_single_for_cpu);

void dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
	size_t size, enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
}

EXPORT_SYMBOL(dma_sync_single_for_device);

void dma_sync_single_range_for_cpu(struct device *dev, dma_addr_t dma_handle,
		      unsigned long offset, size_t size,
		      enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
}

EXPORT_SYMBOL(dma_sync_single_range_for_cpu);

void dma_sync_single_range_for_device(struct device *dev, dma_addr_t dma_handle,
		      unsigned long offset, size_t size,
		      enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
}

EXPORT_SYMBOL(dma_sync_single_range_for_device);

void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, int nelems,
		 enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
}

EXPORT_SYMBOL(dma_sync_sg_for_cpu);

void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg, int nelems,
		 enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
}

EXPORT_SYMBOL(dma_sync_sg_for_device);

int dma_mapping_error(dma_addr_t dma_addr)
{
	return 0;
}

EXPORT_SYMBOL(dma_mapping_error);

int dma_supported(struct device *dev, u64 mask)
{
	/*
	 * we fall back to GFP_DMA when the mask isn't all 1s,
	 * so we can't guarantee allocations that must be
	 * within a tighter range than GFP_DMA..
	 */
	if (mask < 0x00ffffff)
		return 0;

	return 1;
}

EXPORT_SYMBOL(dma_supported);

int dma_is_consistent(dma_addr_t dma_addr)
{
	return 1;
}

EXPORT_SYMBOL(dma_is_consistent);

void dma_cache_sync(void *vaddr, size_t size,
	       enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
}

EXPORT_SYMBOL(dma_cache_sync);

/* The DAC routines are a PCIism.. */

#ifdef CONFIG_PCI

#include <linux/pci.h>

dma64_addr_t pci_dac_page_to_dma(struct pci_dev *pdev,
	struct page *page, unsigned long offset, int direction)
{
	return (dma64_addr_t)page_to_phys(page) + offset;
}

EXPORT_SYMBOL(pci_dac_page_to_dma);

struct page *pci_dac_dma_to_page(struct pci_dev *pdev,
	dma64_addr_t dma_addr)
{
	return mem_map + (dma_addr >> PAGE_SHIFT);
}

EXPORT_SYMBOL(pci_dac_dma_to_page);

unsigned long pci_dac_dma_to_offset(struct pci_dev *pdev,
	dma64_addr_t dma_addr)
{
	return dma_addr & ~PAGE_MASK;
}

EXPORT_SYMBOL(pci_dac_dma_to_offset);

void pci_dac_dma_sync_single_for_cpu(struct pci_dev *pdev,
	dma64_addr_t dma_addr, size_t len, int direction)
{
	BUG_ON(direction == PCI_DMA_NONE);
}

EXPORT_SYMBOL(pci_dac_dma_sync_single_for_cpu);

void pci_dac_dma_sync_single_for_device(struct pci_dev *pdev,
	dma64_addr_t dma_addr, size_t len, int direction)
{
	BUG_ON(direction == PCI_DMA_NONE);
}

EXPORT_SYMBOL(pci_dac_dma_sync_single_for_device);

#endif /* CONFIG_PCI */

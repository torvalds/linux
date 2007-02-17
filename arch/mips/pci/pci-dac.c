/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000  Ani Joshi <ajoshi@unixbox.com>
 * Copyright (C) 2000, 2001, 06  Ralf Baechle <ralf@linux-mips.org>
 * swiped from i386, and cloned for MIPS by Geert, polished by Ralf.
 */

#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/string.h>

#include <asm/cache.h>
#include <asm/io.h>

#include <dma-coherence.h>

#include <linux/pci.h>

dma64_addr_t pci_dac_page_to_dma(struct pci_dev *pdev,
	struct page *page, unsigned long offset, int direction)
{
	struct device *dev = &pdev->dev;

	BUG_ON(direction == DMA_NONE);

	if (!plat_device_is_coherent(dev)) {
		unsigned long addr;

		addr = (unsigned long) page_address(page) + offset;
		dma_cache_wback_inv(addr, PAGE_SIZE);
	}

	return plat_map_dma_mem_page(dev, page) + offset;
}

EXPORT_SYMBOL(pci_dac_page_to_dma);

struct page *pci_dac_dma_to_page(struct pci_dev *pdev,
	dma64_addr_t dma_addr)
{
	return pfn_to_page(plat_dma_addr_to_phys(dma_addr) >> PAGE_SHIFT);
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

	if (!plat_device_is_coherent(&pdev->dev))
		dma_cache_wback_inv(dma_addr + PAGE_OFFSET, len);
}

EXPORT_SYMBOL(pci_dac_dma_sync_single_for_cpu);

void pci_dac_dma_sync_single_for_device(struct pci_dev *pdev,
	dma64_addr_t dma_addr, size_t len, int direction)
{
	BUG_ON(direction == PCI_DMA_NONE);

	if (!plat_device_is_coherent(&pdev->dev))
		dma_cache_wback_inv(dma_addr + PAGE_OFFSET, len);
}

EXPORT_SYMBOL(pci_dac_dma_sync_single_for_device);

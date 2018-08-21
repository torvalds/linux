// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2009-2010 PetaLogix
 * Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corporation
 *
 * Provide default implementations of the DMA mapping callbacks for
 * directly mapped busses.
 */

#include <linux/device.h>
#include <linux/dma-noncoherent.h>
#include <linux/gfp.h>
#include <linux/dma-debug.h>
#include <linux/export.h>
#include <linux/bug.h>
#include <asm/cacheflush.h>

static void __dma_sync(struct device *dev, phys_addr_t paddr, size_t size,
		enum dma_data_direction direction)
{
	switch (direction) {
	case DMA_TO_DEVICE:
	case DMA_BIDIRECTIONAL:
		flush_dcache_range(paddr, paddr + size);
		break;
	case DMA_FROM_DEVICE:
		invalidate_dcache_range(paddr, paddr + size);
		break;
	default:
		BUG();
	}
}

void arch_sync_dma_for_device(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
{
	__dma_sync(dev, paddr, size, dir);
}

void arch_sync_dma_for_cpu(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
{
	__dma_sync(dev, paddr, size, dir);
}

int arch_dma_mmap(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t handle, size_t size,
		unsigned long attrs)
{
#ifdef CONFIG_MMU
	unsigned long user_count = vma_pages(vma);
	unsigned long count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long off = vma->vm_pgoff;
	unsigned long pfn;

	if (off >= count || user_count > (count - off))
		return -ENXIO;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	pfn = consistent_virt_to_pfn(cpu_addr);
	return remap_pfn_range(vma, vma->vm_start, pfn + off,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);
#else
	return -ENXIO;
#endif
}

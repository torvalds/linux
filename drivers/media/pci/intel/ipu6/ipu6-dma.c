// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013--2024 Intel Corporation
 */

#include <linux/cacheflush.h>
#include <linux/dma-mapping.h>
#include <linux/iova.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "ipu6.h"
#include "ipu6-bus.h"
#include "ipu6-dma.h"
#include "ipu6-mmu.h"

struct vm_info {
	struct list_head list;
	struct page **pages;
	dma_addr_t ipu6_iova;
	void *vaddr;
	unsigned long size;
};

static struct vm_info *get_vm_info(struct ipu6_mmu *mmu, dma_addr_t iova)
{
	struct vm_info *info, *save;

	list_for_each_entry_safe(info, save, &mmu->vma_list, list) {
		if (iova >= info->ipu6_iova &&
		    iova < (info->ipu6_iova + info->size))
			return info;
	}

	return NULL;
}

static void __clear_buffer(struct page *page, size_t size, unsigned long attrs)
{
	void *ptr;

	if (!page)
		return;
	/*
	 * Ensure that the allocated pages are zeroed, and that any data
	 * lurking in the kernel direct-mapped region is invalidated.
	 */
	ptr = page_address(page);
	memset(ptr, 0, size);
	if ((attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		clflush_cache_range(ptr, size);
}

static struct page **__alloc_buffer(size_t size, gfp_t gfp, unsigned long attrs)
{
	int count = PHYS_PFN(size);
	int array_size = count * sizeof(struct page *);
	struct page **pages;
	int i = 0;

	pages = kvzalloc(array_size, GFP_KERNEL);
	if (!pages)
		return NULL;

	gfp |= __GFP_NOWARN;

	while (count) {
		int j, order = __fls(count);

		pages[i] = alloc_pages(gfp, order);
		while (!pages[i] && order)
			pages[i] = alloc_pages(gfp, --order);
		if (!pages[i])
			goto error;

		if (order) {
			split_page(pages[i], order);
			j = 1 << order;
			while (j--)
				pages[i + j] = pages[i] + j;
		}

		__clear_buffer(pages[i], PAGE_SIZE << order, attrs);
		i += 1 << order;
		count -= 1 << order;
	}

	return pages;
error:
	while (i--)
		if (pages[i])
			__free_pages(pages[i], 0);
	kvfree(pages);
	return NULL;
}

static void __free_buffer(struct page **pages, size_t size, unsigned long attrs)
{
	int count = PHYS_PFN(size);
	unsigned int i;

	for (i = 0; i < count && pages[i]; i++) {
		__clear_buffer(pages[i], PAGE_SIZE, attrs);
		__free_pages(pages[i], 0);
	}

	kvfree(pages);
}

void ipu6_dma_sync_single(struct ipu6_bus_device *sys, dma_addr_t dma_handle,
			  size_t size)
{
	void *vaddr;
	u32 offset;
	struct vm_info *info;
	struct ipu6_mmu *mmu = sys->mmu;

	info = get_vm_info(mmu, dma_handle);
	if (WARN_ON(!info))
		return;

	offset = dma_handle - info->ipu6_iova;
	if (WARN_ON(size > (info->size - offset)))
		return;

	vaddr = info->vaddr + offset;
	clflush_cache_range(vaddr, size);
}
EXPORT_SYMBOL_NS_GPL(ipu6_dma_sync_single, "INTEL_IPU6");

void ipu6_dma_sync_sg(struct ipu6_bus_device *sys, struct scatterlist *sglist,
		      int nents)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sglist, sg, nents, i)
		clflush_cache_range(sg_virt(sg), sg->length);
}
EXPORT_SYMBOL_NS_GPL(ipu6_dma_sync_sg, "INTEL_IPU6");

void ipu6_dma_sync_sgtable(struct ipu6_bus_device *sys, struct sg_table *sgt)
{
	ipu6_dma_sync_sg(sys, sgt->sgl, sgt->orig_nents);
}
EXPORT_SYMBOL_NS_GPL(ipu6_dma_sync_sgtable, "INTEL_IPU6");

void *ipu6_dma_alloc(struct ipu6_bus_device *sys, size_t size,
		     dma_addr_t *dma_handle, gfp_t gfp,
		     unsigned long attrs)
{
	struct device *dev = &sys->auxdev.dev;
	struct pci_dev *pdev = sys->isp->pdev;
	dma_addr_t pci_dma_addr, ipu6_iova;
	struct ipu6_mmu *mmu = sys->mmu;
	struct vm_info *info;
	unsigned long count;
	struct page **pages;
	struct iova *iova;
	unsigned int i;
	int ret;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return NULL;

	size = PAGE_ALIGN(size);
	count = PHYS_PFN(size);

	iova = alloc_iova(&mmu->dmap->iovad, count,
			  PHYS_PFN(mmu->dmap->mmu_info->aperture_end), 0);
	if (!iova)
		goto out_kfree;

	pages = __alloc_buffer(size, gfp, attrs);
	if (!pages)
		goto out_free_iova;

	dev_dbg(dev, "dma_alloc: size %zu iova low pfn %lu, high pfn %lu\n",
		size, iova->pfn_lo, iova->pfn_hi);
	for (i = 0; iova->pfn_lo + i <= iova->pfn_hi; i++) {
		pci_dma_addr = dma_map_page_attrs(&pdev->dev, pages[i], 0,
						  PAGE_SIZE, DMA_BIDIRECTIONAL,
						  attrs);
		dev_dbg(dev, "dma_alloc: mapped pci_dma_addr %pad\n",
			&pci_dma_addr);
		if (dma_mapping_error(&pdev->dev, pci_dma_addr)) {
			dev_err(dev, "pci_dma_mapping for page[%d] failed", i);
			goto out_unmap;
		}

		ret = ipu6_mmu_map(mmu->dmap->mmu_info,
				   PFN_PHYS(iova->pfn_lo + i), pci_dma_addr,
				   PAGE_SIZE);
		if (ret) {
			dev_err(dev, "ipu6_mmu_map for pci_dma[%d] %pad failed",
				i, &pci_dma_addr);
			dma_unmap_page_attrs(&pdev->dev, pci_dma_addr,
					     PAGE_SIZE, DMA_BIDIRECTIONAL,
					     attrs);
			goto out_unmap;
		}
	}

	info->vaddr = vmap(pages, count, VM_USERMAP, PAGE_KERNEL);
	if (!info->vaddr)
		goto out_unmap;

	*dma_handle = PFN_PHYS(iova->pfn_lo);

	info->pages = pages;
	info->ipu6_iova = *dma_handle;
	info->size = size;
	list_add(&info->list, &mmu->vma_list);

	return info->vaddr;

out_unmap:
	while (i--) {
		ipu6_iova = PFN_PHYS(iova->pfn_lo + i);
		pci_dma_addr = ipu6_mmu_iova_to_phys(mmu->dmap->mmu_info,
						     ipu6_iova);
		dma_unmap_page_attrs(&pdev->dev, pci_dma_addr, PAGE_SIZE,
				     DMA_BIDIRECTIONAL, attrs);

		ipu6_mmu_unmap(mmu->dmap->mmu_info, ipu6_iova, PAGE_SIZE);
	}

	__free_buffer(pages, size, attrs);

out_free_iova:
	__free_iova(&mmu->dmap->iovad, iova);
out_kfree:
	kfree(info);

	return NULL;
}
EXPORT_SYMBOL_NS_GPL(ipu6_dma_alloc, "INTEL_IPU6");

void ipu6_dma_free(struct ipu6_bus_device *sys, size_t size, void *vaddr,
		   dma_addr_t dma_handle, unsigned long attrs)
{
	struct ipu6_mmu *mmu = sys->mmu;
	struct pci_dev *pdev = sys->isp->pdev;
	struct iova *iova = find_iova(&mmu->dmap->iovad, PHYS_PFN(dma_handle));
	dma_addr_t pci_dma_addr, ipu6_iova;
	struct vm_info *info;
	struct page **pages;
	unsigned int i;

	if (WARN_ON(!iova))
		return;

	info = get_vm_info(mmu, dma_handle);
	if (WARN_ON(!info))
		return;

	if (WARN_ON(!info->vaddr))
		return;

	if (WARN_ON(!info->pages))
		return;

	list_del(&info->list);

	size = PAGE_ALIGN(size);

	pages = info->pages;

	vunmap(vaddr);

	for (i = 0; i < PHYS_PFN(size); i++) {
		ipu6_iova = PFN_PHYS(iova->pfn_lo + i);
		pci_dma_addr = ipu6_mmu_iova_to_phys(mmu->dmap->mmu_info,
						     ipu6_iova);
		dma_unmap_page_attrs(&pdev->dev, pci_dma_addr, PAGE_SIZE,
				     DMA_BIDIRECTIONAL, attrs);
	}

	ipu6_mmu_unmap(mmu->dmap->mmu_info, PFN_PHYS(iova->pfn_lo),
		       PFN_PHYS(iova_size(iova)));

	__free_buffer(pages, size, attrs);

	mmu->tlb_invalidate(mmu);

	__free_iova(&mmu->dmap->iovad, iova);

	kfree(info);
}
EXPORT_SYMBOL_NS_GPL(ipu6_dma_free, "INTEL_IPU6");

int ipu6_dma_mmap(struct ipu6_bus_device *sys, struct vm_area_struct *vma,
		  void *addr, dma_addr_t iova, size_t size,
		  unsigned long attrs)
{
	struct ipu6_mmu *mmu = sys->mmu;
	size_t count = PFN_UP(size);
	struct vm_info *info;
	size_t i;
	int ret;

	info = get_vm_info(mmu, iova);
	if (!info)
		return -EFAULT;

	if (!info->vaddr)
		return -EFAULT;

	if (vma->vm_start & ~PAGE_MASK)
		return -EINVAL;

	if (size > info->size)
		return -EFAULT;

	for (i = 0; i < count; i++) {
		ret = vm_insert_page(vma, vma->vm_start + PFN_PHYS(i),
				     info->pages[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

void ipu6_dma_unmap_sg(struct ipu6_bus_device *sys, struct scatterlist *sglist,
		       int nents, enum dma_data_direction dir,
		       unsigned long attrs)
{
	struct device *dev = &sys->auxdev.dev;
	struct ipu6_mmu *mmu = sys->mmu;
	struct iova *iova = find_iova(&mmu->dmap->iovad,
				      PHYS_PFN(sg_dma_address(sglist)));
	struct scatterlist *sg;
	dma_addr_t pci_dma_addr;
	unsigned int i;

	if (!nents)
		return;

	if (WARN_ON(!iova))
		return;

	/*
	 * Before IPU6 mmu unmap, return the pci dma address back to sg
	 * assume the nents is less than orig_nents as the least granule
	 * is 1 SZ_4K page
	 */
	dev_dbg(dev, "trying to unmap concatenated %u ents\n", nents);
	for_each_sg(sglist, sg, nents, i) {
		dev_dbg(dev, "unmap sg[%d] %pad size %u\n", i,
			&sg_dma_address(sg), sg_dma_len(sg));
		pci_dma_addr = ipu6_mmu_iova_to_phys(mmu->dmap->mmu_info,
						     sg_dma_address(sg));
		dev_dbg(dev, "return pci_dma_addr %pad back to sg[%d]\n",
			&pci_dma_addr, i);
		sg_dma_address(sg) = pci_dma_addr;
	}

	dev_dbg(dev, "ipu6_mmu_unmap low pfn %lu high pfn %lu\n",
		iova->pfn_lo, iova->pfn_hi);
	ipu6_mmu_unmap(mmu->dmap->mmu_info, PFN_PHYS(iova->pfn_lo),
		       PFN_PHYS(iova_size(iova)));

	mmu->tlb_invalidate(mmu);
	__free_iova(&mmu->dmap->iovad, iova);
}
EXPORT_SYMBOL_NS_GPL(ipu6_dma_unmap_sg, "INTEL_IPU6");

int ipu6_dma_map_sg(struct ipu6_bus_device *sys, struct scatterlist *sglist,
		    int nents, enum dma_data_direction dir,
		    unsigned long attrs)
{
	struct device *dev = &sys->auxdev.dev;
	struct ipu6_mmu *mmu = sys->mmu;
	struct scatterlist *sg;
	struct iova *iova;
	size_t npages = 0;
	unsigned long iova_addr;
	int i;

	for_each_sg(sglist, sg, nents, i) {
		if (sg->offset) {
			dev_err(dev, "Unsupported non-zero sg[%d].offset %x\n",
				i, sg->offset);
			return -EFAULT;
		}
	}

	for_each_sg(sglist, sg, nents, i)
		npages += PFN_UP(sg_dma_len(sg));

	dev_dbg(dev, "dmamap trying to map %d ents %zu pages\n",
		nents, npages);

	iova = alloc_iova(&mmu->dmap->iovad, npages,
			  PHYS_PFN(mmu->dmap->mmu_info->aperture_end), 0);
	if (!iova)
		return 0;

	dev_dbg(dev, "dmamap: iova low pfn %lu, high pfn %lu\n", iova->pfn_lo,
		iova->pfn_hi);

	iova_addr = iova->pfn_lo;
	for_each_sg(sglist, sg, nents, i) {
		phys_addr_t iova_pa;
		int ret;

		iova_pa = PFN_PHYS(iova_addr);
		dev_dbg(dev, "mapping entry %d: iova %pap phy %pap size %d\n",
			i, &iova_pa, &sg_dma_address(sg), sg_dma_len(sg));

		ret = ipu6_mmu_map(mmu->dmap->mmu_info, PFN_PHYS(iova_addr),
				   sg_dma_address(sg),
				   PAGE_ALIGN(sg_dma_len(sg)));
		if (ret)
			goto out_fail;

		sg_dma_address(sg) = PFN_PHYS(iova_addr);

		iova_addr += PFN_UP(sg_dma_len(sg));
	}

	dev_dbg(dev, "dmamap %d ents %zu pages mapped\n", nents, npages);

	return nents;

out_fail:
	ipu6_dma_unmap_sg(sys, sglist, i, dir, attrs);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ipu6_dma_map_sg, "INTEL_IPU6");

int ipu6_dma_map_sgtable(struct ipu6_bus_device *sys, struct sg_table *sgt,
			 enum dma_data_direction dir, unsigned long attrs)
{
	int nents;

	nents = ipu6_dma_map_sg(sys, sgt->sgl, sgt->nents, dir, attrs);
	if (nents < 0)
		return nents;

	sgt->nents = nents;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ipu6_dma_map_sgtable, "INTEL_IPU6");

void ipu6_dma_unmap_sgtable(struct ipu6_bus_device *sys, struct sg_table *sgt,
			    enum dma_data_direction dir, unsigned long attrs)
{
	ipu6_dma_unmap_sg(sys, sgt->sgl, sgt->nents, dir, attrs);
}
EXPORT_SYMBOL_NS_GPL(ipu6_dma_unmap_sgtable, "INTEL_IPU6");

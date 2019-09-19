// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Intel Corporation
 * Copyright 2018 Google LLC.
 *
 * Author: Tomasz Figa <tfiga@chromium.org>
 * Author: Yong Zhi <yong.zhi@intel.com>
 */

#include <linux/vmalloc.h>

#include "ipu3.h"
#include "ipu3-css-pool.h"
#include "ipu3-mmu.h"
#include "ipu3-dmamap.h"

/*
 * Free a buffer allocated by imgu_dmamap_alloc_buffer()
 */
static void imgu_dmamap_free_buffer(struct page **pages,
				    size_t size)
{
	int count = size >> PAGE_SHIFT;

	while (count--)
		__free_page(pages[count]);
	kvfree(pages);
}

/*
 * Based on the implementation of __iommu_dma_alloc_pages()
 * defined in drivers/iommu/dma-iommu.c
 */
static struct page **imgu_dmamap_alloc_buffer(size_t size, gfp_t gfp)
{
	struct page **pages;
	unsigned int i = 0, count = size >> PAGE_SHIFT;
	unsigned int order_mask = 1;
	const gfp_t high_order_gfp = __GFP_NOWARN | __GFP_NORETRY;

	/* Allocate mem for array of page ptrs */
	pages = kvmalloc_array(count, sizeof(*pages), GFP_KERNEL);

	if (!pages)
		return NULL;

	gfp |= __GFP_HIGHMEM | __GFP_ZERO;

	while (count) {
		struct page *page = NULL;
		unsigned int order_size;

		for (order_mask &= (2U << __fls(count)) - 1;
		     order_mask; order_mask &= ~order_size) {
			unsigned int order = __fls(order_mask);

			order_size = 1U << order;
			page = alloc_pages((order_mask - order_size) ?
					   gfp | high_order_gfp : gfp, order);
			if (!page)
				continue;
			if (!order)
				break;
			if (!PageCompound(page)) {
				split_page(page, order);
				break;
			}

			__free_pages(page, order);
		}
		if (!page) {
			imgu_dmamap_free_buffer(pages, i << PAGE_SHIFT);
			return NULL;
		}
		count -= order_size;
		while (order_size--)
			pages[i++] = page++;
	}

	return pages;
}

/**
 * imgu_dmamap_alloc - allocate and map a buffer into KVA
 * @imgu: struct device pointer
 * @map: struct to store mapping variables
 * @len: size required
 *
 * Returns:
 *  KVA on success
 *  %NULL on failure
 */
void *imgu_dmamap_alloc(struct imgu_device *imgu, struct imgu_css_map *map,
			size_t len)
{
	unsigned long shift = iova_shift(&imgu->iova_domain);
	struct device *dev = &imgu->pci_dev->dev;
	size_t size = PAGE_ALIGN(len);
	struct page **pages;
	dma_addr_t iovaddr;
	struct iova *iova;
	int i, rval;

	dev_dbg(dev, "%s: allocating %zu\n", __func__, size);

	iova = alloc_iova(&imgu->iova_domain, size >> shift,
			  imgu->mmu->aperture_end >> shift, 0);
	if (!iova)
		return NULL;

	pages = imgu_dmamap_alloc_buffer(size, GFP_KERNEL);
	if (!pages)
		goto out_free_iova;

	/* Call IOMMU driver to setup pgt */
	iovaddr = iova_dma_addr(&imgu->iova_domain, iova);
	for (i = 0; i < size / PAGE_SIZE; ++i) {
		rval = imgu_mmu_map(imgu->mmu, iovaddr,
				    page_to_phys(pages[i]), PAGE_SIZE);
		if (rval)
			goto out_unmap;

		iovaddr += PAGE_SIZE;
	}

	/* Now grab a virtual region */
	map->vma = __get_vm_area(size, VM_USERMAP, VMALLOC_START, VMALLOC_END);
	if (!map->vma)
		goto out_unmap;

	map->vma->pages = pages;
	/* And map it in KVA */
	if (map_vm_area(map->vma, PAGE_KERNEL, pages))
		goto out_vunmap;

	map->size = size;
	map->daddr = iova_dma_addr(&imgu->iova_domain, iova);
	map->vaddr = map->vma->addr;

	dev_dbg(dev, "%s: allocated %zu @ IOVA %pad @ VA %p\n", __func__,
		size, &map->daddr, map->vma->addr);

	return map->vma->addr;

out_vunmap:
	vunmap(map->vma->addr);

out_unmap:
	imgu_dmamap_free_buffer(pages, size);
	imgu_mmu_unmap(imgu->mmu, iova_dma_addr(&imgu->iova_domain, iova),
		       i * PAGE_SIZE);
	map->vma = NULL;

out_free_iova:
	__free_iova(&imgu->iova_domain, iova);

	return NULL;
}

void imgu_dmamap_unmap(struct imgu_device *imgu, struct imgu_css_map *map)
{
	struct iova *iova;

	iova = find_iova(&imgu->iova_domain,
			 iova_pfn(&imgu->iova_domain, map->daddr));
	if (WARN_ON(!iova))
		return;

	imgu_mmu_unmap(imgu->mmu, iova_dma_addr(&imgu->iova_domain, iova),
		       iova_size(iova) << iova_shift(&imgu->iova_domain));

	__free_iova(&imgu->iova_domain, iova);
}

/*
 * Counterpart of imgu_dmamap_alloc
 */
void imgu_dmamap_free(struct imgu_device *imgu, struct imgu_css_map *map)
{
	struct vm_struct *area = map->vma;

	dev_dbg(&imgu->pci_dev->dev, "%s: freeing %zu @ IOVA %pad @ VA %p\n",
		__func__, map->size, &map->daddr, map->vaddr);

	if (!map->vaddr)
		return;

	imgu_dmamap_unmap(imgu, map);

	if (WARN_ON(!area) || WARN_ON(!area->pages))
		return;

	imgu_dmamap_free_buffer(area->pages, map->size);
	vunmap(map->vaddr);
	map->vaddr = NULL;
}

int imgu_dmamap_map_sg(struct imgu_device *imgu, struct scatterlist *sglist,
		       int nents, struct imgu_css_map *map)
{
	unsigned long shift = iova_shift(&imgu->iova_domain);
	struct scatterlist *sg;
	struct iova *iova;
	size_t size = 0;
	int i;

	for_each_sg(sglist, sg, nents, i) {
		if (sg->offset)
			return -EINVAL;

		if (i != nents - 1 && !PAGE_ALIGNED(sg->length))
			return -EINVAL;

		size += sg->length;
	}

	size = iova_align(&imgu->iova_domain, size);
	dev_dbg(&imgu->pci_dev->dev, "dmamap: mapping sg %d entries, %zu pages\n",
		nents, size >> shift);

	iova = alloc_iova(&imgu->iova_domain, size >> shift,
			  imgu->mmu->aperture_end >> shift, 0);
	if (!iova)
		return -ENOMEM;

	dev_dbg(&imgu->pci_dev->dev, "dmamap: iova low pfn %lu, high pfn %lu\n",
		iova->pfn_lo, iova->pfn_hi);

	if (imgu_mmu_map_sg(imgu->mmu, iova_dma_addr(&imgu->iova_domain, iova),
			    sglist, nents) < size)
		goto out_fail;

	memset(map, 0, sizeof(*map));
	map->daddr = iova_dma_addr(&imgu->iova_domain, iova);
	map->size = size;

	return 0;

out_fail:
	__free_iova(&imgu->iova_domain, iova);

	return -EFAULT;
}

int imgu_dmamap_init(struct imgu_device *imgu)
{
	unsigned long order, base_pfn;
	int ret = iova_cache_get();

	if (ret)
		return ret;

	order = __ffs(IPU3_PAGE_SIZE);
	base_pfn = max_t(unsigned long, 1, imgu->mmu->aperture_start >> order);
	init_iova_domain(&imgu->iova_domain, 1UL << order, base_pfn);

	return 0;
}

void imgu_dmamap_exit(struct imgu_device *imgu)
{
	put_iova_domain(&imgu->iova_domain);
	iova_cache_put();
}

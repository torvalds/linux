// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/dma-mapping.h>
#include <linux/dma-mapping-fast.h>
#include <linux/qcom-dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/io-pgtable-fast.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/pci.h>
#include <linux/iova.h>
#include <linux/io-pgtable.h>
#include <linux/qcom-iommu-util.h>
#include <trace/hooks/iommu.h>
#include "qcom-dma-iommu-generic.h"

/* some redundant definitions... :( TODO: move to io-pgtable-fast.h */
#define FAST_PAGE_SHIFT		12
#define FAST_PAGE_SIZE (1UL << FAST_PAGE_SHIFT)
#define FAST_PAGE_MASK (~(PAGE_SIZE - 1))

static struct rb_root mappings;
static DEFINE_RWLOCK(mappings_lock);

static int fast_smmu_add_mapping(struct dma_fast_smmu_mapping *fast)
{
	struct rb_node **new = &mappings.rb_node, *parent = NULL;
	struct dma_fast_smmu_mapping *entry;
	int ret = 0;
	unsigned long flags;

	write_lock_irqsave(&mappings_lock, flags);
	while (*new) {
		entry = rb_entry(*new, struct dma_fast_smmu_mapping, node);

		parent = *new;
		if (fast->domain < entry->domain) {
			new = &((*new)->rb_left);
		} else if (fast->domain > entry->domain) {
			new = &((*new)->rb_right);
		} else {
			ret = -EEXIST;
			break;
		}
	}

	if (!ret) {
		rb_link_node(&fast->node, parent, new);
		rb_insert_color(&fast->node, &mappings);
	}
	write_unlock_irqrestore(&mappings_lock, flags);

	return ret;
}

static struct dma_fast_smmu_mapping *__fast_smmu_lookup_mapping(struct iommu_domain *domain)
{
	struct rb_node *node = mappings.rb_node;
	struct dma_fast_smmu_mapping *entry;

	while (node) {
		entry = rb_entry(node, struct dma_fast_smmu_mapping, node);

		if (domain < entry->domain)
			node = node->rb_left;
		else if (domain > entry->domain)
			node = node->rb_right;
		else
			return entry;
	}

	return NULL;
}

static struct dma_fast_smmu_mapping *fast_smmu_lookup_mapping(struct iommu_domain *domain)
{
	struct dma_fast_smmu_mapping *fast;
	unsigned long flags;

	read_lock_irqsave(&mappings_lock, flags);
	fast = __fast_smmu_lookup_mapping(domain);
	read_unlock_irqrestore(&mappings_lock, flags);
	return fast;
}

static struct dma_fast_smmu_mapping *fast_smmu_remove_mapping(struct iommu_domain *domain)
{
	struct dma_fast_smmu_mapping *fast;
	unsigned long flags;

	write_lock_irqsave(&mappings_lock, flags);
	fast = __fast_smmu_lookup_mapping(domain);
	if (fast)
		rb_erase(&fast->node, &mappings);
	write_unlock_irqrestore(&mappings_lock, flags);
	return fast;
}

static pgprot_t __get_dma_pgprot(unsigned long attrs, pgprot_t prot,
				 bool coherent)
{
	if (!coherent || (attrs & DMA_ATTR_WRITE_COMBINE))
		return pgprot_writecombine(prot);
	return prot;
}

static bool is_dma_coherent(struct device *dev, unsigned long attrs)
{
	bool is_coherent;

	if (attrs & DMA_ATTR_FORCE_COHERENT)
		is_coherent = true;
	else if (attrs & DMA_ATTR_FORCE_NON_COHERENT)
		is_coherent = false;
	else if (dev_is_dma_coherent(dev))
		is_coherent = true;
	else
		is_coherent = false;

	return is_coherent;
}

static struct dma_fast_smmu_mapping *dev_get_mapping(struct device *dev)
{
	struct iommu_domain *domain;

	domain = iommu_get_domain_for_dev(dev);
	if (!domain)
		return ERR_PTR(-EINVAL);
	return fast_smmu_lookup_mapping(domain);
}

static dma_addr_t __fast_smmu_alloc_iova(struct dma_fast_smmu_mapping *mapping,
					 unsigned long attrs,
					 size_t size)
{
	unsigned long bit, nbits = size >> FAST_PAGE_SHIFT;
	unsigned long align = (1 << get_order(size)) - 1;
	struct iommu_domain *domain = mapping->domain;

	bit = bitmap_find_next_zero_area(mapping->clean_bitmap,
					  mapping->num_4k_pages,
					  mapping->next_start, nbits, align);
	if (unlikely(bit > mapping->num_4k_pages)) {
		/* try wrapping */
		bit = bitmap_find_next_zero_area(
			mapping->clean_bitmap, mapping->num_4k_pages, 0, nbits,
			align);
		if (unlikely(bit > mapping->num_4k_pages)) {
			/*
			 * If we just re-allocated a VA whose TLB hasn't been
			 * invalidated since it was last used and unmapped, we
			 * need to invalidate it here.  We actually invalidate
			 * the entire TLB so that we don't have to invalidate
			 * the TLB again until we wrap back around.
			 */
			if (mapping->have_stale_tlbs) {
				bool skip_sync = (attrs &
						  DMA_ATTR_SKIP_CPU_SYNC);

				iommu_flush_iotlb_all(domain);
				bitmap_copy(mapping->clean_bitmap,
					    mapping->bitmap,
					    mapping->num_4k_pages);
				mapping->have_stale_tlbs = false;
				av8l_fast_clear_stale_ptes(mapping->pgtbl_ops,
							   mapping->base,
							   mapping->base +
							   mapping->size - 1,
							   skip_sync);
				bit = bitmap_find_next_zero_area(
							mapping->clean_bitmap,
							mapping->num_4k_pages,
								 0, nbits,
								 align);
				if (unlikely(bit > mapping->num_4k_pages))
					return DMA_MAPPING_ERROR;

			} else {
				return DMA_MAPPING_ERROR;
			}
		}
	}

	bitmap_set(mapping->bitmap, bit, nbits);
	bitmap_set(mapping->clean_bitmap, bit, nbits);
	mapping->next_start = bit + nbits;
	if (unlikely(mapping->next_start >= mapping->num_4k_pages))
		mapping->next_start = 0;

	return (bit << FAST_PAGE_SHIFT) + mapping->base;
}

static void __fast_smmu_free_iova(struct dma_fast_smmu_mapping *mapping,
				  dma_addr_t iova, size_t size)
{
	unsigned long start_bit = (iova - mapping->base) >> FAST_PAGE_SHIFT;
	unsigned long nbits = size >> FAST_PAGE_SHIFT;

	/*
	 * We don't invalidate TLBs on unmap.  We invalidate TLBs on map
	 * when we're about to re-allocate a VA that was previously
	 * unmapped but hasn't yet been invalidated.
	 */
	bitmap_clear(mapping->bitmap, start_bit, nbits);
	mapping->have_stale_tlbs = true;
}

static dma_addr_t fast_smmu_map_page(struct device *dev, struct page *page,
				   unsigned long offset, size_t size,
				   enum dma_data_direction dir,
				   unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	dma_addr_t iova;
	unsigned long flags;
	phys_addr_t phys_plus_off = page_to_phys(page) + offset;
	phys_addr_t phys_to_map = round_down(phys_plus_off, FAST_PAGE_SIZE);
	unsigned long offset_from_phys_to_map = phys_plus_off & ~FAST_PAGE_MASK;
	size_t len = ALIGN(size + offset_from_phys_to_map, FAST_PAGE_SIZE);
	bool skip_sync = (attrs & DMA_ATTR_SKIP_CPU_SYNC);
	bool is_coherent = is_dma_coherent(dev, attrs);
	int prot = qcom_dma_info_to_prot(dir, is_coherent, attrs);

	if (!skip_sync && !is_coherent)
		qcom_arch_sync_dma_for_device(phys_plus_off, size, dir);

	spin_lock_irqsave(&mapping->lock, flags);

	iova = __fast_smmu_alloc_iova(mapping, attrs, len);

	if (unlikely(iova == DMA_MAPPING_ERROR))
		goto fail;

	if (unlikely(av8l_fast_map_public(mapping->pgtbl_ops, iova,
					  phys_to_map, len, prot)))
		goto fail_free_iova;

	spin_unlock_irqrestore(&mapping->lock, flags);

	return iova + offset_from_phys_to_map;

fail_free_iova:
	__fast_smmu_free_iova(mapping, iova, size);
fail:
	spin_unlock_irqrestore(&mapping->lock, flags);
	return DMA_MAPPING_ERROR;
}

static void fast_smmu_unmap_page(struct device *dev, dma_addr_t iova,
			       size_t size, enum dma_data_direction dir,
			       unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	unsigned long flags;
	unsigned long offset = iova & ~FAST_PAGE_MASK;
	size_t len = ALIGN(size + offset, FAST_PAGE_SIZE);
	bool skip_sync = (attrs & DMA_ATTR_SKIP_CPU_SYNC);
	bool is_coherent = is_dma_coherent(dev, attrs);

	if (!skip_sync && !is_coherent) {
		phys_addr_t phys;

		phys = av8l_fast_iova_to_phys_public(mapping->pgtbl_ops, iova);
		WARN_ON(!phys);

		qcom_arch_sync_dma_for_cpu(phys, size, dir);
	}

	spin_lock_irqsave(&mapping->lock, flags);
	av8l_fast_unmap_public(mapping->pgtbl_ops, iova, len);
	__fast_smmu_free_iova(mapping, iova, len);
	spin_unlock_irqrestore(&mapping->lock, flags);
}

static void fast_smmu_sync_single_for_cpu(struct device *dev,
		dma_addr_t iova, size_t size, enum dma_data_direction dir)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);

	if (!av8l_fast_iova_coherent_public(mapping->pgtbl_ops, iova)) {
		phys_addr_t phys;

		phys = av8l_fast_iova_to_phys_public(mapping->pgtbl_ops, iova);
		WARN_ON(!phys);

		qcom_arch_sync_dma_for_cpu(phys, size, dir);
	}
}

static void fast_smmu_sync_single_for_device(struct device *dev,
		dma_addr_t iova, size_t size, enum dma_data_direction dir)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);

	if (!av8l_fast_iova_coherent_public(mapping->pgtbl_ops, iova)) {
		phys_addr_t phys;

		phys = av8l_fast_iova_to_phys_public(mapping->pgtbl_ops, iova);
		WARN_ON(!phys);

		qcom_arch_sync_dma_for_device(phys, size, dir);
	}
}

static void fast_smmu_sync_sg_for_cpu(struct device *dev,
				    struct scatterlist *sgl, int nelems,
				    enum dma_data_direction dir)
{
	struct scatterlist *sg;
	dma_addr_t iova = sg_dma_address(sgl);
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	int i;

	if (av8l_fast_iova_coherent_public(mapping->pgtbl_ops, iova))
		return;

	for_each_sg(sgl, sg, nelems, i)
		qcom_arch_sync_dma_for_cpu(sg_phys(sg), sg->length, dir);
}

static void fast_smmu_sync_sg_for_device(struct device *dev,
				       struct scatterlist *sgl, int nelems,
				       enum dma_data_direction dir)
{
	struct scatterlist *sg;
	dma_addr_t iova = sg_dma_address(sgl);
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	int i;

	if (av8l_fast_iova_coherent_public(mapping->pgtbl_ops, iova))
		return;

	for_each_sg(sgl, sg, nelems, i)
		qcom_arch_sync_dma_for_device(sg_phys(sg), sg->length, dir);
}

static int fast_smmu_map_sg(struct device *dev, struct scatterlist *sg,
			    int nents, enum dma_data_direction dir,
			    unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	size_t iova_len;
	bool is_coherent = is_dma_coherent(dev, attrs);
	int prot = qcom_dma_info_to_prot(dir, is_coherent, attrs);
	int ret;
	dma_addr_t iova;
	unsigned long flags;
	size_t unused = 0;

	iova_len = qcom_iommu_dma_prepare_map_sg(dev, mapping->iovad, sg, nents);

	spin_lock_irqsave(&mapping->lock, flags);
	iova = __fast_smmu_alloc_iova(mapping, attrs, iova_len);
	spin_unlock_irqrestore(&mapping->lock, flags);

	if (unlikely(iova == DMA_MAPPING_ERROR))
		goto fail;

	av8l_fast_map_sg_public(mapping->pgtbl_ops, iova, sg, nents, prot,
				&unused);

	ret = qcom_iommu_dma_finalise_sg(dev, sg, nents, iova);

	if ((attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		fast_smmu_sync_sg_for_device(dev, sg, nents, dir);

	return ret;
fail:
	qcom_iommu_dma_invalidate_sg(sg, nents);
	return 0;
}

static void fast_smmu_unmap_sg(struct device *dev,
			       struct scatterlist *sg, int nelems,
			       enum dma_data_direction dir,
			       unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	unsigned long flags;
	dma_addr_t start;
	size_t len, offset;
	struct scatterlist *tmp;
	int i;

	if ((attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		fast_smmu_sync_sg_for_cpu(dev, sg, nelems, dir);

	/*
	 * The scatterlist segments are mapped into a single
	 * contiguous IOVA allocation, so this is incredibly easy.
	 */
	start = sg_dma_address(sg);
	offset = start & ~FAST_PAGE_MASK;
	for_each_sg(sg_next(sg), tmp, nelems - 1, i) {
		if (sg_dma_len(tmp) == 0)
			break;
		sg = tmp;
	}
	len = ALIGN(sg_dma_address(sg) + sg_dma_len(sg) - (start - offset),
		    FAST_PAGE_SIZE);

	av8l_fast_unmap_public(mapping->pgtbl_ops, start, len);

	spin_lock_irqsave(&mapping->lock, flags);
	__fast_smmu_free_iova(mapping, start, len);
	spin_unlock_irqrestore(&mapping->lock, flags);
}

static void __fast_smmu_free_pages(struct page **pages, int count)
{
	int i;

	if (!pages)
		return;
	for (i = 0; i < count; i++)
		__free_page(pages[i]);
	kvfree(pages);
}

static void *fast_smmu_alloc_atomic(struct dma_fast_smmu_mapping *mapping,
				    size_t size, gfp_t gfp, unsigned long attrs,
				    dma_addr_t *handle, bool coherent)
{
	void *addr;
	unsigned long flags;
	struct page *page;
	dma_addr_t dma_addr;
	int prot = qcom_dma_info_to_prot(DMA_BIDIRECTIONAL, coherent, attrs);

	if (coherent) {
		page = alloc_pages(gfp, get_order(size));
		addr = page ? page_address(page) : NULL;
	} else
		addr = qcom_dma_alloc_from_pool(mapping->dev, size, &page, gfp);
	if (!addr)
		return NULL;

	spin_lock_irqsave(&mapping->lock, flags);
	dma_addr = __fast_smmu_alloc_iova(mapping, attrs, size);
	if (dma_addr == DMA_MAPPING_ERROR) {
		dev_err(mapping->dev, "no iova\n");
		spin_unlock_irqrestore(&mapping->lock, flags);
		goto out_free_page;
	}
	if (unlikely(av8l_fast_map_public(mapping->pgtbl_ops, dma_addr,
					  page_to_phys(page), size, prot))) {
		dev_err(mapping->dev, "no map public\n");
		goto out_free_iova;
	}
	spin_unlock_irqrestore(&mapping->lock, flags);
	*handle = dma_addr;
	return addr;

out_free_iova:
	__fast_smmu_free_iova(mapping, dma_addr, size);
	spin_unlock_irqrestore(&mapping->lock, flags);
out_free_page:
	if (coherent)
		__free_pages(page, get_order(size));
	else
		qcom_dma_free_from_pool(mapping->dev, addr, size);
	return NULL;
}

static struct page **__fast_smmu_alloc_pages(unsigned int count, gfp_t gfp)
{
	struct page **pages;
	unsigned int i = 0, array_size = count * sizeof(*pages);

	if (array_size <= PAGE_SIZE)
		pages = kzalloc(array_size, GFP_KERNEL);
	else
		pages = vzalloc(array_size);
	if (!pages)
		return NULL;

	/* IOMMU can map any pages, so himem can also be used here */
	gfp |= __GFP_NOWARN | __GFP_HIGHMEM;

	for (i = 0; i < count; ++i) {
		struct page *page = alloc_page(gfp);

		if (!page) {
			__fast_smmu_free_pages(pages, i);
			return NULL;
		}
		pages[i] = page;
	}
	return pages;
}

static void *__fast_smmu_alloc_contiguous(struct device *dev, size_t size,
			dma_addr_t *handle, gfp_t gfp, unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	bool is_coherent = is_dma_coherent(dev, attrs);
	int prot = qcom_dma_info_to_prot(DMA_BIDIRECTIONAL, is_coherent, attrs);
	pgprot_t remap_prot = __get_dma_pgprot(attrs, PAGE_KERNEL, is_coherent);
	struct page *page;
	dma_addr_t iova;
	unsigned long flags;
	void *coherent_addr;

	page = qcom_dma_alloc_from_contiguous(dev, size >> PAGE_SHIFT,
					get_order(size), gfp & __GFP_NOWARN);
	if (!page)
		return NULL;


	spin_lock_irqsave(&mapping->lock, flags);
	iova = __fast_smmu_alloc_iova(mapping, attrs, size);
	spin_unlock_irqrestore(&mapping->lock, flags);
	if (iova == DMA_MAPPING_ERROR)
		goto release_page;

	if (av8l_fast_map_public(mapping->pgtbl_ops, iova, page_to_phys(page),
				 size, prot))
		goto release_iova;

	if (!is_coherent || PageHighMem(page)) {
		coherent_addr = qcom_dma_common_contiguous_remap(page, size,
							    remap_prot,
						__fast_smmu_alloc_contiguous);
		if (!coherent_addr)
			goto release_mapping;

		if (!is_coherent)
			qcom_arch_dma_prep_coherent(page, size);
	} else {
		coherent_addr = page_address(page);
	}

	memset(coherent_addr, 0, size);
	*handle = iova;
	return coherent_addr;

release_mapping:
	av8l_fast_unmap_public(mapping->pgtbl_ops, iova, size);
release_iova:
	__fast_smmu_free_iova(mapping, iova, size);
release_page:
	qcom_dma_release_from_contiguous(dev, page, size >> PAGE_SHIFT);
	return NULL;
}

static void *fast_smmu_alloc(struct device *dev, size_t size,
			     dma_addr_t *handle, gfp_t gfp,
			     unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	struct sg_table sgt;
	dma_addr_t dma_addr, iova_iter;
	void *addr;
	unsigned long flags;
	struct sg_mapping_iter miter;
	size_t count = ALIGN(size, SZ_4K) >> PAGE_SHIFT;
	bool is_coherent = is_dma_coherent(dev, attrs);
	int prot = qcom_dma_info_to_prot(DMA_BIDIRECTIONAL, is_coherent, attrs);
	pgprot_t remap_prot = __get_dma_pgprot(attrs, PAGE_KERNEL, is_coherent);
	struct page **pages;

	/*
	 * sg_alloc_table_from_pages accepts unsigned int value for count
	 * so check count doesn't exceed UINT_MAX.
	 */

	if (count > UINT_MAX) {
		dev_err(dev, "count: %zx exceeds UNIT_MAX\n", count);
		return NULL;
	}

	gfp |= __GFP_ZERO;

	*handle = DMA_MAPPING_ERROR;
	size = ALIGN(size, SZ_4K);

	if (!gfpflags_allow_blocking(gfp))
		return fast_smmu_alloc_atomic(mapping, size, gfp, attrs, handle,
					      is_coherent);
	else if (attrs & DMA_ATTR_FORCE_CONTIGUOUS)
		return __fast_smmu_alloc_contiguous(dev, size, handle, gfp,
						    attrs);

	pages = __fast_smmu_alloc_pages(count, gfp);
	if (!pages) {
		dev_err(dev, "no pages\n");
		return NULL;
	}

	if (sg_alloc_table_from_pages(&sgt, pages, count, 0, size, gfp)) {
		dev_err(dev, "no sg tablen\n");
		goto out_free_pages;
	}

	if (!is_coherent) {
		/*
		 * The CPU-centric flushing implied by SG_MITER_TO_SG isn't
		 * sufficient here, so skip it by using the "wrong" direction.
		 */
		sg_miter_start(&miter, sgt.sgl, sgt.orig_nents,
			       SG_MITER_FROM_SG);
		while (sg_miter_next(&miter))
			qcom_arch_dma_prep_coherent(miter.page, miter.length);
		sg_miter_stop(&miter);
	}

	spin_lock_irqsave(&mapping->lock, flags);
	dma_addr = __fast_smmu_alloc_iova(mapping, attrs, size);
	if (dma_addr == DMA_MAPPING_ERROR) {
		dev_err(dev, "no iova\n");
		spin_unlock_irqrestore(&mapping->lock, flags);
		goto out_free_sg;
	}
	iova_iter = dma_addr;
	sg_miter_start(&miter, sgt.sgl, sgt.orig_nents,
		       SG_MITER_FROM_SG | SG_MITER_ATOMIC);
	while (sg_miter_next(&miter)) {
		if (unlikely(av8l_fast_map_public(
				     mapping->pgtbl_ops, iova_iter,
				     page_to_phys(miter.page),
				     miter.length, prot))) {
			dev_err(dev, "no map public\n");
			/* TODO: unwind previously successful mappings */
			goto out_free_iova;
		}
		iova_iter += miter.length;
	}
	sg_miter_stop(&miter);
	spin_unlock_irqrestore(&mapping->lock, flags);

	addr = qcom_dma_common_pages_remap(pages, size, remap_prot,
				      __builtin_return_address(0));
	if (!addr) {
		dev_err(dev, "no common pages\n");
		goto out_unmap;
	}

	*handle = dma_addr;
	sg_free_table(&sgt);
	return addr;

out_unmap:
	/* need to take the lock again for page tables and iova */
	spin_lock_irqsave(&mapping->lock, flags);
	av8l_fast_unmap_public(mapping->pgtbl_ops, dma_addr, size);
out_free_iova:
	__fast_smmu_free_iova(mapping, dma_addr, size);
	spin_unlock_irqrestore(&mapping->lock, flags);
out_free_sg:
	sg_free_table(&sgt);
out_free_pages:
	__fast_smmu_free_pages(pages, count);
	return NULL;
}

static void fast_smmu_free(struct device *dev, size_t size,
			   void *cpu_addr, dma_addr_t dma_handle,
			   unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	struct page **pages = NULL;
	struct page *page = NULL;
	unsigned long flags;

	size = ALIGN(size, FAST_PAGE_SIZE);

	spin_lock_irqsave(&mapping->lock, flags);
	av8l_fast_unmap_public(mapping->pgtbl_ops, dma_handle, size);
	__fast_smmu_free_iova(mapping, dma_handle, size);
	spin_unlock_irqrestore(&mapping->lock, flags);

	if (qcom_dma_free_from_pool(dev, cpu_addr, size))
		return;

	if (is_vmalloc_addr(cpu_addr)) {
		pages = qcom_dma_common_find_pages(cpu_addr);
		if (!pages)
			page = vmalloc_to_page(cpu_addr);
		qcom_dma_common_free_remap(cpu_addr, size);
	} else {
		page = virt_to_page(cpu_addr);
	}

	if (pages)
		__fast_smmu_free_pages(pages, size >> FAST_PAGE_SHIFT);

	if (page)
		qcom_dma_free_contiguous(dev, page, size);
}

static int fast_smmu_mmap_attrs(struct device *dev, struct vm_area_struct *vma,
				void *cpu_addr, dma_addr_t dma_addr,
				size_t size, unsigned long attrs)
{
	return qcom_iommu_dma_mmap(dev, vma, cpu_addr, dma_addr, size, attrs);
}

static int fast_smmu_get_sgtable(struct device *dev, struct sg_table *sgt,
				void *cpu_addr, dma_addr_t dma_addr,
				size_t size, unsigned long attrs)
{
	return qcom_iommu_dma_get_sgtable(dev, sgt, cpu_addr, dma_addr, size, attrs);
}

static dma_addr_t fast_smmu_dma_map_resource(
			struct device *dev, phys_addr_t phys_addr,
			size_t size, enum dma_data_direction dir,
			unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	size_t offset = phys_addr & ~FAST_PAGE_MASK;
	size_t len = round_up(size + offset, FAST_PAGE_SIZE);
	dma_addr_t dma_addr;
	int prot;
	unsigned long flags;

	spin_lock_irqsave(&mapping->lock, flags);
	dma_addr = __fast_smmu_alloc_iova(mapping, attrs, len);
	spin_unlock_irqrestore(&mapping->lock, flags);

	if (dma_addr == DMA_MAPPING_ERROR)
		return dma_addr;

	prot = qcom_dma_info_to_prot(dir, false, attrs);
	prot |= IOMMU_MMIO;

	if (iommu_map(mapping->domain, dma_addr, phys_addr - offset,
			len, prot)) {
		spin_lock_irqsave(&mapping->lock, flags);
		__fast_smmu_free_iova(mapping, dma_addr, len);
		spin_unlock_irqrestore(&mapping->lock, flags);
		return DMA_MAPPING_ERROR;
	}
	return dma_addr + offset;
}

static void fast_smmu_dma_unmap_resource(
			struct device *dev, dma_addr_t addr,
			size_t size, enum dma_data_direction dir,
			unsigned long attrs)
{
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	size_t offset = addr & ~FAST_PAGE_MASK;
	size_t len = round_up(size + offset, FAST_PAGE_SIZE);
	unsigned long flags;

	iommu_unmap(mapping->domain, addr - offset, len);
	spin_lock_irqsave(&mapping->lock, flags);
	__fast_smmu_free_iova(mapping, addr, len);
	spin_unlock_irqrestore(&mapping->lock, flags);
}

static void __fast_smmu_mapped_over_stale(struct dma_fast_smmu_mapping *fast,
					  void *priv)
{
	av8l_fast_iopte *pmds, *ptep = priv;
	dma_addr_t iova;
	unsigned long bitmap_idx;
	struct av8l_fast_io_pgtable *data;

	data  = iof_pgtable_ops_to_data(fast->pgtbl_ops);
	pmds = data->pmds;

	bitmap_idx = (unsigned long)(ptep - pmds);
	iova = bitmap_idx << FAST_PAGE_SHIFT;
	dev_err(fast->dev, "Mapped over stale tlb at %pa\n", &iova);
	dev_err(fast->dev, "bitmap (failure at idx %lu):\n", bitmap_idx);
	dev_err(fast->dev, "ptep: %pK pmds: %pK diff: %lu\n", ptep,
		pmds, bitmap_idx);
	print_hex_dump(KERN_ERR, "bmap: ", DUMP_PREFIX_ADDRESS,
		       32, 8, fast->bitmap, fast->bitmap_size, false);
}

static int fast_smmu_notify(struct notifier_block *self,
			    unsigned long action, void *data)
{
	struct dma_fast_smmu_mapping *fast = container_of(
		self, struct dma_fast_smmu_mapping, notifier);

	switch (action) {
	case MAPPED_OVER_STALE_TLB:
		__fast_smmu_mapped_over_stale(fast, data);
		return NOTIFY_OK;
	default:
		WARN(1, "Unhandled notifier action");
		return NOTIFY_DONE;
	}
}

static const struct dma_map_ops fast_smmu_dma_ops = {
	.alloc = fast_smmu_alloc,
	.free = fast_smmu_free,
	.mmap = fast_smmu_mmap_attrs,
	.get_sgtable = fast_smmu_get_sgtable,
	.map_page = fast_smmu_map_page,
	.unmap_page = fast_smmu_unmap_page,
	.sync_single_for_cpu = fast_smmu_sync_single_for_cpu,
	.sync_single_for_device = fast_smmu_sync_single_for_device,
	.map_sg = fast_smmu_map_sg,
	.unmap_sg = fast_smmu_unmap_sg,
	.sync_sg_for_cpu = fast_smmu_sync_sg_for_cpu,
	.sync_sg_for_device = fast_smmu_sync_sg_for_device,
	.map_resource = fast_smmu_dma_map_resource,
	.unmap_resource = fast_smmu_dma_unmap_resource,
};

/**
 * __fast_smmu_create_mapping_sized
 * @base: bottom of the VA range
 * @size: size of the VA range in bytes
 *
 * Creates a mapping structure which holds information about used/unused IO
 * address ranges, which is required to perform mapping with IOMMU aware
 * functions. The only VA range supported is [0, 4GB).
 *
 * The client device need to be attached to the mapping with
 * fast_smmu_attach_device function.
 */
static struct dma_fast_smmu_mapping *__fast_smmu_create_mapping_sized(
	dma_addr_t base, u64 size)
{
	struct dma_fast_smmu_mapping *fast;

	fast = kzalloc(sizeof(struct dma_fast_smmu_mapping), GFP_KERNEL);
	if (!fast)
		goto err;

	fast->base = base;
	fast->size = size;
	fast->num_4k_pages = size >> FAST_PAGE_SHIFT;
	fast->bitmap_size = BITS_TO_LONGS(fast->num_4k_pages) * sizeof(long);

	fast->bitmap = kzalloc(fast->bitmap_size, GFP_KERNEL | __GFP_NOWARN |
								__GFP_NORETRY);
	if (!fast->bitmap)
		fast->bitmap = vzalloc(fast->bitmap_size);

	if (!fast->bitmap)
		goto err2;

	fast->clean_bitmap = kzalloc(fast->bitmap_size, GFP_KERNEL |
				     __GFP_NOWARN | __GFP_NORETRY);
	if (!fast->clean_bitmap)
		fast->clean_bitmap = vzalloc(fast->bitmap_size);

	if (!fast->clean_bitmap)
		goto err3;

	spin_lock_init(&fast->lock);
	mutex_init(&fast->msi_cookie_init_lock);

	fast->iovad = kzalloc(sizeof(*fast->iovad), GFP_KERNEL);
	if (!fast->iovad)
		goto err_free_bitmap;
	init_iova_domain(fast->iovad, FAST_PAGE_SIZE,
			base >> FAST_PAGE_SHIFT);

	return fast;

err_free_bitmap:
	kvfree(fast->clean_bitmap);
err3:
	kvfree(fast->bitmap);
err2:
	kfree(fast);
err:
	return ERR_PTR(-ENOMEM);
}

/*
 * Based off of similar code from dma-iommu.c, but modified to use a different
 * iova allocator
 */
static void fast_smmu_reserve_pci_windows(struct device *dev,
			    struct dma_fast_smmu_mapping *mapping)
{
	struct pci_host_bridge *bridge;
	struct resource_entry *window;
	phys_addr_t start, end;
	struct pci_dev *pci_dev;
	unsigned long flags;

	if (!dev_is_pci(dev))
		return;

	pci_dev = to_pci_dev(dev);
	bridge = qcom_pci_find_host_bridge(pci_dev->bus);

	spin_lock_irqsave(&mapping->lock, flags);
	resource_list_for_each_entry(window, &bridge->windows) {
		if (resource_type(window->res) != IORESOURCE_MEM &&
		    resource_type(window->res) != IORESOURCE_IO)
			continue;

		start = round_down(window->res->start - window->offset,
				FAST_PAGE_SIZE);
		end = round_up(window->res->end - window->offset,
				FAST_PAGE_SIZE);
		start = max_t(unsigned long, mapping->base, start);
		end = min_t(unsigned long, mapping->base + mapping->size, end);
		if (start >= end)
			continue;

		dev_dbg(dev, "iova allocator reserved 0x%pa-0x%pa\n",
				&start, &end);

		start = (start - mapping->base) >> FAST_PAGE_SHIFT;
		end = (end - mapping->base) >> FAST_PAGE_SHIFT;
		bitmap_set(mapping->bitmap, start, end - start);
		bitmap_set(mapping->clean_bitmap, start, end - start);
	}
	spin_unlock_irqrestore(&mapping->lock, flags);
}

static void fast_smmu_reserve_msi_iova(struct device *dev, struct dma_fast_smmu_mapping *fast)
{
	dma_addr_t msi_iova_base;
	u32 msi_size;
	int ret;
	unsigned long flags;

	mutex_lock(&fast->msi_cookie_init_lock);
	spin_lock_irqsave(&fast->lock, flags);

	/* MSI cookie has already been setup. */
	if (fast->domain->iova_cookie)
		goto out;

	if (qcom_iommu_get_msi_size(dev, &msi_size) < 0)
		goto out;

	msi_iova_base = __fast_smmu_alloc_iova(fast, 0, msi_size);
	if (msi_iova_base == DMA_MAPPING_ERROR) {
		dev_err(dev, "iova allocator failed to reserve MSI range of size: 0x%x\n",
			msi_size);
		goto out;
	}
	dev_dbg(dev, "iova allocator reserved 0x%lx-0x%lx for MSI\n", msi_iova_base,
		msi_iova_base + msi_size);
	spin_unlock_irqrestore(&fast->lock, flags);

	ret = iommu_get_msi_cookie(fast->domain, msi_iova_base);
	spin_lock_irqsave(&fast->lock, flags);
	if (ret < 0) {
		dev_err(dev, "failed to obtain MSI iova cookie rc: %d\n", ret);
		__fast_smmu_free_iova(fast, msi_iova_base, msi_size);
	}

out:
	spin_unlock_irqrestore(&fast->lock, flags);
	mutex_unlock(&fast->msi_cookie_init_lock);
}

static void fast_smmu_reserve_iommu_regions(struct device *dev,
		struct dma_fast_smmu_mapping *fast)
{
	struct iommu_resv_region *region;
	unsigned long flags;
	struct dma_fast_smmu_mapping *mapping = dev_get_mapping(dev);
	LIST_HEAD(resv_regions);

	if (dev_is_pci(dev))
		fast_smmu_reserve_pci_windows(dev, fast);

	qcom_iommu_get_resv_regions(dev, &resv_regions);
	spin_lock_irqsave(&mapping->lock, flags);
	list_for_each_entry(region, &resv_regions, list) {
		unsigned long lo, hi;

		lo = max(fast->base, region->start);
		hi = min(fast->base + fast->size - 1,
			 region->start + region->length - 1);

		lo = (lo - fast->base) >> FAST_PAGE_SHIFT;
		hi = (hi - fast->base) >> FAST_PAGE_SHIFT;
		bitmap_set(fast->bitmap, lo, hi - lo + 1);
		bitmap_set(fast->clean_bitmap, lo, hi - lo + 1);
	}
	spin_unlock_irqrestore(&mapping->lock, flags);
	iommu_put_resv_regions(dev, &resv_regions);

	fast_smmu_reserve_msi_iova(dev, fast);
}

void fast_smmu_put_dma_cookie(struct iommu_domain *domain)
{
	struct dma_fast_smmu_mapping *fast = fast_smmu_remove_mapping(domain);

	if (!fast)
		return;

	if (fast->iovad) {
		put_iova_domain(fast->iovad);
		kfree(fast->iovad);
	}

	if (fast->bitmap)
		kvfree(fast->bitmap);

	if (fast->clean_bitmap)
		kvfree(fast->clean_bitmap);

	kfree(fast);
}
EXPORT_SYMBOL(fast_smmu_put_dma_cookie);

/**
 * fast_smmu_init_mapping
 * @dev: valid struct device pointer
 * @domain: valid IOMMU domain pointer
 * @pgtable_ops: The page table ops associated with this domain
 *
 * Called the first time a device is attached to this mapping.
 * Not for dma client use.
 */
int fast_smmu_init_mapping(struct device *dev, struct iommu_domain *domain,
			   struct io_pgtable_ops *pgtable_ops)
{
	u64 dma_base, dma_end, size;
	struct dma_fast_smmu_mapping *fast = fast_smmu_lookup_mapping(domain);

	if (fast) {
		dev_err(dev, "Iova cookie already present\n");
		return -EINVAL;
	}

	if (!pgtable_ops)
		return -EINVAL;

	dma_base = max_t(u64, domain->geometry.aperture_start, 0);
	dma_end = min_t(u64, domain->geometry.aperture_end,
			(SZ_1G * 4ULL - 1));
	size = dma_end - dma_base + 1;
	if (dma_base >= dma_end) {
		dev_err(dev, "Invalid domain geometry\n");
		return -EINVAL;
	}

	fast = __fast_smmu_create_mapping_sized(dma_base, size);
	if (IS_ERR(fast))
		return -ENOMEM;

	fast->domain = domain;
	fast->dev = dev;
	fast_smmu_add_mapping(fast);

	fast->pgtbl_ops = pgtable_ops;

	fast->notifier.notifier_call = fast_smmu_notify;
	av8l_register_notify(&fast->notifier);

	return 0;
}
EXPORT_SYMBOL(fast_smmu_init_mapping);

static void __fast_smmu_setup_dma_ops(void *data, struct device *dev,
					u64 dma_base, u64 dma_limit)
{
	struct dma_fast_smmu_mapping *fast;
	struct iommu_domain *domain;
	int ret;

	domain = iommu_get_domain_for_dev(dev);
	if (!domain)
		return;

	ret = qcom_iommu_get_mappings_configuration(domain);
	if (ret < 0 || !(ret & QCOM_IOMMU_MAPPING_CONF_FAST))
		return;

	fast = dev_get_mapping(dev);
	if (!fast) {
		dev_err(dev, "Missing fastmap iova cookie\n");
		return;
	}

	fast_smmu_reserve_iommu_regions(dev, fast);
	dev->dma_ops = &fast_smmu_dma_ops;
}

/*
 * Called by drivers who create their own iommu domains via
 * iommu_domain_alloc().
 */
void fast_smmu_setup_dma_ops(struct device *dev, u64 dma_base, u64 dma_limit)
{
	__fast_smmu_setup_dma_ops(NULL, dev, dma_base, dma_limit);
}
EXPORT_SYMBOL(fast_smmu_setup_dma_ops);

int __init dma_mapping_fast_init(void)
{
	return register_trace_android_rvh_iommu_setup_dma_ops(
			__fast_smmu_setup_dma_ops, NULL);
}


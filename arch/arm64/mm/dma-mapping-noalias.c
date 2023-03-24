// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for uncached DMA mappings.
 * Part of Cortex-A510 erratum 2454944 workaround.
 *
 * Copyright (C) 2022-2023 ARM Ltd.
 * Author: Robin Murphy <robin.murphy@arm.com>
 *	   Activating swiotlb + disabling lazy vunmap: Beata Michalska
 */
#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>
#include <linux/iommu.h>
#include <linux/slab.h>
#include <linux/swiotlb.h>
#include <linux/vmalloc.h>
#include <asm/cacheflush.h>

/*
 * Bits [58:55] of the translation table descriptor are being reserved
 * by the architecture for software use purposes. With the assumption that
 * those should not be used on linear map addresses (which is not without
 * any guarantee though), those bits are being leveraged to trace potential
 * cacheable aliases. This is still far from being perfect, to say at least:
 * ... categorically the worst, but oh well, needs must...
 */
#define REFCOUNT_INC BIT(55)
#define PTE_REFCOUNT(pte) (((pte) >> 55) & 0xf)

static int pte_set_nc(pte_t *ptep, unsigned long addr, void *data)
{
	pteval_t old_pte, new_pte, pte;
	unsigned int refcount;

	pte = pte_val(READ_ONCE(*ptep));
	do {
		/* Avoid racing against the transient invalid state */
		old_pte = pte | PTE_VALID;
		new_pte = old_pte + REFCOUNT_INC;
		refcount = PTE_REFCOUNT(pte);
		if (WARN_ON(refcount == 15))
			return -EINVAL;
		if (refcount == 0) {
			new_pte &= ~(PTE_ATTRINDX_MASK | PTE_VALID);
			new_pte |= PTE_ATTRINDX(MT_NORMAL_NC);
		}
		pte = cmpxchg_relaxed(&pte_val(*ptep), old_pte, new_pte);
	} while (pte != old_pte);

	*(unsigned int *)data = refcount;
	if (refcount)
		return 0;

	flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
	WRITE_ONCE(*ptep, __pte(new_pte | PTE_VALID));
	return 0;
}

static int pte_clear_nc(pte_t *ptep, unsigned long addr, void *data)
{
	pteval_t old_pte, new_pte, pte;
	unsigned int refcount;

	pte = pte_val(READ_ONCE(*ptep));
	do {
		old_pte = pte | PTE_VALID;
		new_pte = old_pte - REFCOUNT_INC;
		refcount = PTE_REFCOUNT(pte);
		if (WARN_ON(refcount == 0))
			return -EINVAL;
		if (refcount == 1) {
			new_pte &= ~(PTE_ATTRINDX_MASK | PTE_VALID);
			new_pte |= PTE_ATTRINDX(MT_NORMAL_TAGGED);
		}
		pte = cmpxchg_relaxed(&pte_val(*ptep), old_pte, new_pte);
	} while (pte != old_pte);

	if (refcount > 1)
		return 0;

	flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
	WRITE_ONCE(*ptep, __pte(new_pte | PTE_VALID));
	return 0;
}

static int set_nc(void *addr, size_t size)
{
	unsigned int count;
	int ret = apply_to_existing_page_range(&init_mm, (unsigned long)addr,
					       size, pte_set_nc, &count);

	WARN_RATELIMIT(count == 0 && page_mapped(virt_to_page(addr)),
		       "changing linear mapping but cacheable aliases may still exist\n");
	dsb(ishst);
	isb();
	__flush_dcache_area(addr, size);
	return ret;
}

static int clear_nc(void *addr, size_t size)
{
	int ret = apply_to_existing_page_range(&init_mm, (unsigned long)addr,
					       size, pte_clear_nc, NULL);
	dsb(ishst);
	isb();
	__inval_dcache_area(addr, size);
	return ret;
}

static phys_addr_t __arm64_noalias_map(struct device *dev, phys_addr_t phys,
				       size_t size, enum dma_data_direction dir,
				       unsigned long attrs, bool bounce)
{
	bounce = bounce || (phys | size) & ~PAGE_MASK;
	if (bounce) {
		phys = swiotlb_tbl_map_single(dev, phys, size, PAGE_ALIGN(size),
					      dir, attrs);
		if (phys == DMA_MAPPING_ERROR)
			return DMA_MAPPING_ERROR;
	}
	if (set_nc(phys_to_virt(phys & PAGE_MASK), PAGE_ALIGN(size)))
		goto out_unmap;

	return phys;
out_unmap:
	if (bounce)
		swiotlb_tbl_unmap_single(dev, phys, size, PAGE_ALIGN(size), dir,
					 attrs | DMA_ATTR_SKIP_CPU_SYNC);
	return DMA_MAPPING_ERROR;

}

static void __arm64_noalias_unmap(struct device *dev, phys_addr_t phys, size_t size,
				  enum dma_data_direction dir, unsigned long attrs)
{
	clear_nc(phys_to_virt(phys & PAGE_MASK), PAGE_ALIGN(size));
	if (is_swiotlb_buffer(phys))
		swiotlb_tbl_unmap_single(dev, phys, size, PAGE_ALIGN(size), dir, attrs);
}

static void __arm64_noalias_sync_for_device(struct device *dev, phys_addr_t phys,
					    size_t size, enum dma_data_direction dir)
{
	if (is_swiotlb_buffer(phys))
		swiotlb_tbl_sync_single(dev, phys, size, dir, SYNC_FOR_DEVICE);
	else
		arch_sync_dma_for_device(phys, size, dir);
}

static void __arm64_noalias_sync_for_cpu(struct device *dev, phys_addr_t phys,
					 size_t size, enum dma_data_direction dir)
{
	if (is_swiotlb_buffer(phys))
		swiotlb_tbl_sync_single(dev, phys, size, dir, SYNC_FOR_CPU);
	else
		arch_sync_dma_for_cpu(phys, size, dir);
}

static void *arm64_noalias_alloc(struct device *dev, size_t size,
				 dma_addr_t *dma_addr, gfp_t gfp, unsigned long attrs)
{
	struct page *page;
	void *ret;

	if (attrs & DMA_ATTR_NO_WARN)
		gfp |= __GFP_NOWARN;

	size = PAGE_ALIGN(size);
	page = dma_direct_alloc_pages(dev, size, dma_addr, 0, gfp & ~__GFP_ZERO);
	if (!page)
		return NULL;

	ret = page_address(page);
	if (set_nc(ret, size)) {
		dma_direct_free_pages(dev, size, page, *dma_addr, 0);
		return NULL;
	}
	return ret;
}

static void arm64_noalias_free(struct device *dev, size_t size, void *cpu_addr,
			       dma_addr_t dma_addr, unsigned long attrs)
{
	size = PAGE_ALIGN(size);
	clear_nc(cpu_addr, size);
	dma_direct_free_pages(dev, size, virt_to_page(cpu_addr), dma_addr, 0);
}

static dma_addr_t arm64_noalias_map_page(struct device *dev, struct page *page,
					 unsigned long offset, size_t size,
					 enum dma_data_direction dir, unsigned long attrs)
{
	phys_addr_t phys = page_to_phys(page) + offset;
	bool bounce = !dma_capable(dev, phys_to_dma(dev, phys), size, true);

	if (!bounce && dir == DMA_TO_DEVICE) {
		arch_sync_dma_for_device(phys, size, dir);
		return phys_to_dma(dev, phys);
	}

	bounce = bounce || page_mapped(page);
	phys = __arm64_noalias_map(dev, phys, size, dir, attrs, bounce);
	if (phys == DMA_MAPPING_ERROR)
		return DMA_MAPPING_ERROR;

	return phys_to_dma(dev, phys);
}

static void arm64_noalias_unmap_page(struct device *dev, dma_addr_t dma_addr,
				     size_t size, enum dma_data_direction dir,
				     unsigned long attrs)
{
	if (dir == DMA_TO_DEVICE)
		return;
	__arm64_noalias_unmap(dev, dma_to_phys(dev, dma_addr), size, dir, attrs);
}

static void arm64_noalias_unmap_sg(struct device *dev, struct scatterlist *sgl, int nents,
				   enum dma_data_direction dir, unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	if (dir == DMA_TO_DEVICE)
		return;
	for_each_sg(sgl, sg, nents, i)
		__arm64_noalias_unmap(dev, dma_to_phys(dev, sg->dma_address),
				      sg->length, dir, attrs);
}

static int arm64_noalias_map_sg(struct device *dev, struct scatterlist *sgl, int nents,
				enum dma_data_direction dir, unsigned long attrs)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sgl, sg, nents, i) {
		sg->dma_address = arm64_noalias_map_page(dev, sg_page(sg), sg->offset,
							 sg->length, dir, attrs);
		if (sg->dma_address == DMA_MAPPING_ERROR)
			goto out_unmap;
		sg->dma_length = sg->length;
	}

	return nents;

out_unmap:
	arm64_noalias_unmap_sg(dev, sgl, i, dir, attrs | DMA_ATTR_SKIP_CPU_SYNC);
	return 0;
}

static void arm64_noalias_sync_single_for_device(struct device *dev, dma_addr_t addr,
						 size_t size, enum dma_data_direction dir)
{
	__arm64_noalias_sync_for_device(dev, dma_to_phys(dev, addr), size, dir);
}

static void arm64_noalias_sync_single_for_cpu(struct device *dev, dma_addr_t addr,
					      size_t size, enum dma_data_direction dir)
{
	__arm64_noalias_sync_for_cpu(dev, dma_to_phys(dev, addr), size, dir);
}

static void arm64_noalias_sync_sg_for_device(struct device *dev, struct scatterlist *sgl,
					     int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i)
		arm64_noalias_sync_single_for_device(dev, sg->dma_address, sg->length, dir);
}

static void arm64_noalias_sync_sg_for_cpu(struct device *dev, struct scatterlist *sgl,
					  int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i)
		arm64_noalias_sync_single_for_cpu(dev, sg->dma_address, sg->length, dir);
}

static const struct dma_map_ops arm64_noalias_ops = {
	.alloc = arm64_noalias_alloc,
	.free = arm64_noalias_free,
	.alloc_pages = dma_common_alloc_pages,
	.free_pages = dma_common_free_pages,
	.mmap = dma_common_mmap,
	.get_sgtable = dma_common_get_sgtable,
	.map_page = arm64_noalias_map_page,
	.unmap_page = arm64_noalias_unmap_page,
	.map_sg = arm64_noalias_map_sg,
	.unmap_sg = arm64_noalias_unmap_sg,
	.sync_single_for_cpu = arm64_noalias_sync_single_for_cpu,
	.sync_single_for_device = arm64_noalias_sync_single_for_device,
	.sync_sg_for_cpu = arm64_noalias_sync_sg_for_cpu,
	.sync_sg_for_device = arm64_noalias_sync_sg_for_device,
	.dma_supported = dma_direct_supported,
	.get_required_mask = dma_direct_get_required_mask,
	.max_mapping_size = swiotlb_max_mapping_size,
};

#ifdef CONFIG_IOMMU_DMA
static const struct dma_map_ops *iommu_dma_ops;

static void *arm64_iommu_alloc(struct device *dev, size_t size,
			       dma_addr_t *dma_addr, gfp_t gfp, unsigned long attrs)
{
	struct page **pages;
	void *ret;
	int i;

	size = PAGE_ALIGN(size);
	if (!gfpflags_allow_blocking(gfp) || (attrs & DMA_ATTR_FORCE_CONTIGUOUS)) {
		ret = dma_common_alloc_pages(dev, size, dma_addr, 0, gfp);
		return ret ? page_address(ret) : NULL;
	}

	ret = iommu_dma_ops->alloc(dev, size, dma_addr, gfp, attrs);
	if (ret) {
		pages = dma_common_find_pages(ret);
		for (i = 0; i < size / PAGE_SIZE; i++)
			if (set_nc(page_address(pages[i]), PAGE_SIZE))
				goto err;
	}
	return ret;

err:
	while (i--)
		clear_nc(page_address(pages[i]), PAGE_SIZE);
	iommu_dma_ops->free(dev, size, ret, *dma_addr, attrs);
	return NULL;
}

static void arm64_iommu_free(struct device *dev, size_t size, void *cpu_addr,
			     dma_addr_t dma_addr, unsigned long attrs)
{
	struct page **pages = dma_common_find_pages(cpu_addr);
	int i;

	size = PAGE_ALIGN(size);
	if (!pages)
		return dma_common_free_pages(dev, size, virt_to_page(cpu_addr), dma_addr, 0);

	for (i = 0; i < size / PAGE_SIZE; i++)
		clear_nc(page_address(pages[i]), PAGE_SIZE);
	iommu_dma_ops->free(dev, size, cpu_addr, dma_addr, attrs);
}

static dma_addr_t arm64_iommu_map_page(struct device *dev, struct page *page,
				       unsigned long offset, size_t size,
				       enum dma_data_direction dir, unsigned long attrs)
{
	phys_addr_t phys = page_to_phys(page) + offset;
	dma_addr_t ret;

	if (dir == DMA_TO_DEVICE)
		return iommu_dma_ops->map_page(dev, page, offset, size, dir, attrs);

	phys = __arm64_noalias_map(dev, phys, size, dir, attrs, page_mapped(page));
	if (phys == DMA_MAPPING_ERROR)
		return DMA_MAPPING_ERROR;

	attrs |= DMA_ATTR_SKIP_CPU_SYNC;
	ret = iommu_dma_ops->map_page(dev, phys_to_page(phys), offset_in_page(phys),
				       size, dir, attrs);
	if (ret == DMA_MAPPING_ERROR)
		__arm64_noalias_unmap(dev, phys, size, dir, attrs);
	return ret;
}

static void arm64_iommu_unmap_page(struct device *dev, dma_addr_t addr, size_t size,
				   enum dma_data_direction dir, unsigned long attrs)
{
	phys_addr_t phys;

	if (dir == DMA_TO_DEVICE)
		return iommu_dma_ops->unmap_page(dev, addr, size, dir, attrs);

	phys = iommu_iova_to_phys(iommu_get_dma_domain(dev), addr);
	iommu_dma_ops->unmap_page(dev, addr, size, dir, attrs | DMA_ATTR_SKIP_CPU_SYNC);
	__arm64_noalias_unmap(dev, phys, size, dir, attrs);
}

static int arm64_iommu_map_sg(struct device *dev, struct scatterlist *sgl, int nents,
			      enum dma_data_direction dir, unsigned long attrs)
{
	int i, ret;
	struct scatterlist *sg;
	phys_addr_t *orig_phys;

	if (dir == DMA_TO_DEVICE)
		return iommu_dma_ops->map_sg(dev, sgl, nents, dir, attrs);

	orig_phys = kmalloc_array(nents, sizeof(*orig_phys), GFP_ATOMIC);
	if (!orig_phys)
		return 0;

	for_each_sg(sgl, sg, nents, i) {
		phys_addr_t phys = sg_phys(sg);
		/*
		 * Note we do not have the page_mapped() check here, since
		 * bouncing plays complete havoc with dma-buf imports. Those
		 * may well be mapped in userspace, but we hope and pray that
		 * it's via dma_mmap_attrs() so any such mappings are safely
		 * non-cacheable. DO NOT allow a block device or other similar
		 * scatterlist user to get here (disable IOMMUs if necessary),
		 * since we can't mitigate for both conflicting use-cases.
		 */
		phys = __arm64_noalias_map(dev, phys, sg->length, dir, attrs, false);
		if (phys == DMA_MAPPING_ERROR)
			goto out_unmap;

		orig_phys[i] = sg_phys(sg);
		sg_assign_page(sg, phys_to_page(phys));
		sg->offset = offset_in_page(phys);
	}
	ret = iommu_dma_ops->map_sg(dev, sgl, nents, dir, attrs | DMA_ATTR_SKIP_CPU_SYNC);
	if (ret <= 0)
		goto out_unmap;

	for_each_sg(sgl, sg, nents, i) {
		sg_assign_page(sg, phys_to_page(orig_phys[i]));
		sg->offset = offset_in_page(orig_phys[i]);
	}

	kfree(orig_phys);
	return ret;

out_unmap:
	for_each_sg(sgl, sg, nents, i) {
		__arm64_noalias_unmap(dev, sg_phys(sg), sg->length, dir, attrs);
		sg_assign_page(sg, phys_to_page(orig_phys[i]));
		sg->offset = offset_in_page(orig_phys[i]);
	}
	kfree(orig_phys);
	return 0;
}

static void arm64_iommu_unmap_sg(struct device *dev, struct scatterlist *sgl, int nents,
				 enum dma_data_direction dir, unsigned long attrs)
{
	struct iommu_domain *domain;
	struct scatterlist *sg, *tmp;
	dma_addr_t iova;
	int i;

	if (dir == DMA_TO_DEVICE)
		return iommu_dma_ops->unmap_sg(dev, sgl, nents, dir, attrs);

	domain = iommu_get_dma_domain(dev);
	iova = sgl->dma_address;
	tmp = sgl;
	for_each_sg(sgl, sg, nents, i) {
		phys_addr_t phys = iommu_iova_to_phys(domain, iova);

		__arm64_noalias_unmap(dev, phys, sg->length, dir, attrs);
		iova += sg->length;
		if (iova == tmp->dma_address + tmp->dma_length && !sg_is_last(tmp)) {
			tmp = sg_next(tmp);
			iova = tmp->dma_address;
		}
	}
	iommu_dma_ops->unmap_sg(dev, sgl, nents, dir, attrs | DMA_ATTR_SKIP_CPU_SYNC);
}

static void arm64_iommu_sync_single_for_device(struct device *dev, dma_addr_t addr,
					       size_t size, enum dma_data_direction dir)
{
	phys_addr_t phys = iommu_iova_to_phys(iommu_get_dma_domain(dev), addr);

	__arm64_noalias_sync_for_device(dev, phys, size, dir);
}

static void arm64_iommu_sync_single_for_cpu(struct device *dev, dma_addr_t addr,
					    size_t size, enum dma_data_direction dir)
{
	phys_addr_t phys = iommu_iova_to_phys(iommu_get_dma_domain(dev), addr);

	__arm64_noalias_sync_for_cpu(dev, phys, size, dir);
}

static void arm64_iommu_sync_sg_for_device(struct device *dev, struct scatterlist *sgl,
					   int nents, enum dma_data_direction dir)
{
	struct iommu_domain *domain = iommu_get_dma_domain(dev);
	struct scatterlist *sg, *tmp = sgl;
	dma_addr_t iova = sgl->dma_address;
	int i;

	for_each_sg(sgl, sg, nents, i) {
		phys_addr_t phys = iommu_iova_to_phys(domain, iova);

		__arm64_noalias_sync_for_device(dev, phys, sg->length, dir);
		iova += sg->length;
		if (iova == tmp->dma_address + tmp->dma_length && !sg_is_last(tmp)) {
			tmp = sg_next(tmp);
			iova = tmp->dma_address;
		}
	}
}

static void arm64_iommu_sync_sg_for_cpu(struct device *dev, struct scatterlist *sgl,
					int nents, enum dma_data_direction dir)
{
	struct iommu_domain *domain = iommu_get_dma_domain(dev);
	struct scatterlist *sg, *tmp = sgl;
	dma_addr_t iova = sgl->dma_address;
	int i;

	for_each_sg(sgl, sg, nents, i) {
		phys_addr_t phys = iommu_iova_to_phys(domain, iova);

		__arm64_noalias_sync_for_cpu(dev, phys, sg->length, dir);
		iova += sg->length;
		if (iova == tmp->dma_address + tmp->dma_length && !sg_is_last(tmp)) {
			tmp = sg_next(tmp);
			iova = tmp->dma_address;
		}
	}
}

static struct dma_map_ops arm64_iommu_ops = {
	.alloc = arm64_iommu_alloc,
	.free = arm64_iommu_free,
	.alloc_pages = dma_common_alloc_pages,
	.free_pages = dma_common_free_pages,
	.map_page = arm64_iommu_map_page,
	.unmap_page = arm64_iommu_unmap_page,
	.map_sg = arm64_iommu_map_sg,
	.unmap_sg = arm64_iommu_unmap_sg,
	.sync_single_for_cpu = arm64_iommu_sync_single_for_cpu,
	.sync_single_for_device = arm64_iommu_sync_single_for_device,
	.sync_sg_for_cpu = arm64_iommu_sync_sg_for_cpu,
	.sync_sg_for_device = arm64_iommu_sync_sg_for_device,
};

#endif /* CONFIG_IOMMU_DMA */

static inline void arm64_noalias_prepare(void)
{
	if (!is_swiotlb_active())
		swiotlb_late_init_with_default_size(swiotlb_size_or_default());
	if (lazy_vunmap_enable) {
		lazy_vunmap_enable = false;
		vm_unmap_aliases();
	}
}

void arm64_noalias_setup_dma_ops(struct device *dev)
{
	if (dev_is_dma_coherent(dev))
		return;

	dev_info(dev, "applying no-alias DMA workaround\n");
	if (!dev->dma_ops) {
		dev->dma_ops = &arm64_noalias_ops;
		goto done;
	}

	if (IS_ENABLED(CONFIG_IOMMU_DMA)) {
		dev->dma_ops = &arm64_iommu_ops;
		if (iommu_dma_ops)
			goto done;

		iommu_dma_ops = dev->dma_ops;
		arm64_iommu_ops.mmap = iommu_dma_ops->mmap;
		arm64_iommu_ops.get_sgtable = iommu_dma_ops->get_sgtable;
		arm64_iommu_ops.map_resource = iommu_dma_ops->map_resource;
		arm64_iommu_ops.unmap_resource = iommu_dma_ops->unmap_resource;
		arm64_iommu_ops.get_merge_boundary = iommu_dma_ops->get_merge_boundary;
	}
done:
	arm64_noalias_prepare();
}
EXPORT_SYMBOL_GPL(arm64_noalias_setup_dma_ops);

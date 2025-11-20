// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/adreno-smmu-priv.h>
#include <linux/io-pgtable.h>
#include <linux/kmemleak.h>
#include "msm_drv.h"
#include "msm_gpu_trace.h"
#include "msm_mmu.h"

struct msm_iommu {
	struct msm_mmu base;
	struct iommu_domain *domain;

	struct mutex init_lock;  /* protects pagetables counter and prr_page */
	int pagetables;
	struct page *prr_page;

	struct kmem_cache *pt_cache;
};

#define to_msm_iommu(x) container_of(x, struct msm_iommu, base)

struct msm_iommu_pagetable {
	struct msm_mmu base;
	struct msm_mmu *parent;
	struct io_pgtable_ops *pgtbl_ops;
	const struct iommu_flush_ops *tlb;
	struct device *iommu_dev;
	unsigned long pgsize_bitmap;	/* Bitmap of page sizes in use */
	phys_addr_t ttbr;
	u32 asid;

	/** @root_page_table: Stores the root page table pointer. */
	void *root_page_table;
};
static struct msm_iommu_pagetable *to_pagetable(struct msm_mmu *mmu)
{
	return container_of(mmu, struct msm_iommu_pagetable, base);
}

/* based on iommu_pgsize() in iommu.c: */
static size_t calc_pgsize(struct msm_iommu_pagetable *pagetable,
			   unsigned long iova, phys_addr_t paddr,
			   size_t size, size_t *count)
{
	unsigned int pgsize_idx, pgsize_idx_next;
	unsigned long pgsizes;
	size_t offset, pgsize, pgsize_next;
	unsigned long addr_merge = paddr | iova;

	/* Page sizes supported by the hardware and small enough for @size */
	pgsizes = pagetable->pgsize_bitmap & GENMASK(__fls(size), 0);

	/* Constrain the page sizes further based on the maximum alignment */
	if (likely(addr_merge))
		pgsizes &= GENMASK(__ffs(addr_merge), 0);

	/* Make sure we have at least one suitable page size */
	BUG_ON(!pgsizes);

	/* Pick the biggest page size remaining */
	pgsize_idx = __fls(pgsizes);
	pgsize = BIT(pgsize_idx);
	if (!count)
		return pgsize;

	/* Find the next biggest support page size, if it exists */
	pgsizes = pagetable->pgsize_bitmap & ~GENMASK(pgsize_idx, 0);
	if (!pgsizes)
		goto out_set_count;

	pgsize_idx_next = __ffs(pgsizes);
	pgsize_next = BIT(pgsize_idx_next);

	/*
	 * There's no point trying a bigger page size unless the virtual
	 * and physical addresses are similarly offset within the larger page.
	 */
	if ((iova ^ paddr) & (pgsize_next - 1))
		goto out_set_count;

	/* Calculate the offset to the next page size alignment boundary */
	offset = pgsize_next - (addr_merge & (pgsize_next - 1));

	/*
	 * If size is big enough to accommodate the larger page, reduce
	 * the number of smaller pages.
	 */
	if (offset + pgsize_next <= size)
		size = offset;

out_set_count:
	*count = size >> pgsize_idx;
	return pgsize;
}

static int msm_iommu_pagetable_unmap(struct msm_mmu *mmu, u64 iova,
		size_t size)
{
	struct msm_iommu_pagetable *pagetable = to_pagetable(mmu);
	struct io_pgtable_ops *ops = pagetable->pgtbl_ops;
	int ret = 0;

	while (size) {
		size_t pgsize, count;
		ssize_t unmapped;

		pgsize = calc_pgsize(pagetable, iova, iova, size, &count);

		unmapped = ops->unmap_pages(ops, iova, pgsize, count, NULL);
		if (unmapped <= 0) {
			ret = -EINVAL;
			/*
			 * Continue attempting to unamp the remained of the
			 * range, so we don't end up with some dangling
			 * mapped pages
			 */
			unmapped = PAGE_SIZE;
		}

		iova += unmapped;
		size -= unmapped;
	}

	iommu_flush_iotlb_all(to_msm_iommu(pagetable->parent)->domain);

	return ret;
}

static int msm_iommu_pagetable_map_prr(struct msm_mmu *mmu, u64 iova, size_t len, int prot)
{
	struct msm_iommu_pagetable *pagetable = to_pagetable(mmu);
	struct io_pgtable_ops *ops = pagetable->pgtbl_ops;
	struct msm_iommu *iommu = to_msm_iommu(pagetable->parent);
	phys_addr_t phys = page_to_phys(iommu->prr_page);
	u64 addr = iova;

	while (len) {
		size_t mapped = 0;
		size_t size = PAGE_SIZE;
		int ret;

		ret = ops->map_pages(ops, addr, phys, size, 1, prot, GFP_KERNEL, &mapped);

		/* map_pages could fail after mapping some of the pages,
		 * so update the counters before error handling.
		 */
		addr += mapped;
		len  -= mapped;

		if (ret) {
			msm_iommu_pagetable_unmap(mmu, iova, addr - iova);
			return -EINVAL;
		}
	}

	return 0;
}

static int msm_iommu_pagetable_map(struct msm_mmu *mmu, u64 iova,
				   struct sg_table *sgt, size_t off, size_t len,
				   int prot)
{
	struct msm_iommu_pagetable *pagetable = to_pagetable(mmu);
	struct io_pgtable_ops *ops = pagetable->pgtbl_ops;
	struct scatterlist *sg;
	u64 addr = iova;
	unsigned int i;

	if (!sgt)
		return msm_iommu_pagetable_map_prr(mmu, iova, len, prot);

	for_each_sgtable_sg(sgt, sg, i) {
		size_t size = sg->length;
		phys_addr_t phys = sg_phys(sg);

		if (!len)
			break;

		if (size <= off) {
			off -= size;
			continue;
		}

		phys += off;
		size -= off;
		size = min_t(size_t, size, len);
		off = 0;

		while (size) {
			size_t pgsize, count, mapped = 0;
			int ret;

			pgsize = calc_pgsize(pagetable, addr, phys, size, &count);

			ret = ops->map_pages(ops, addr, phys, pgsize, count,
					     prot, GFP_KERNEL, &mapped);

			/* map_pages could fail after mapping some of the pages,
			 * so update the counters before error handling.
			 */
			phys += mapped;
			addr += mapped;
			size -= mapped;
			len  -= mapped;

			if (ret) {
				msm_iommu_pagetable_unmap(mmu, iova, addr - iova);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static void msm_iommu_pagetable_destroy(struct msm_mmu *mmu)
{
	struct msm_iommu_pagetable *pagetable = to_pagetable(mmu);
	struct msm_iommu *iommu = to_msm_iommu(pagetable->parent);
	struct adreno_smmu_priv *adreno_smmu =
		dev_get_drvdata(pagetable->parent->dev);

	/*
	 * If this is the last attached pagetable for the parent,
	 * disable TTBR0 in the arm-smmu driver
	 */
	mutex_lock(&iommu->init_lock);
	if (--iommu->pagetables == 0) {
		adreno_smmu->set_ttbr0_cfg(adreno_smmu->cookie, NULL);

		if (adreno_smmu->set_prr_bit) {
			adreno_smmu->set_prr_bit(adreno_smmu->cookie, false);
			__free_page(iommu->prr_page);
			iommu->prr_page = NULL;
		}
	}
	mutex_unlock(&iommu->init_lock);

	free_io_pgtable_ops(pagetable->pgtbl_ops);
	kfree(pagetable);
}

int msm_iommu_pagetable_params(struct msm_mmu *mmu,
		phys_addr_t *ttbr, int *asid)
{
	struct msm_iommu_pagetable *pagetable;

	if (mmu->type != MSM_MMU_IOMMU_PAGETABLE)
		return -EINVAL;

	pagetable = to_pagetable(mmu);

	if (ttbr)
		*ttbr = pagetable->ttbr;

	if (asid)
		*asid = pagetable->asid;

	return 0;
}

struct iommu_domain_geometry *msm_iommu_get_geometry(struct msm_mmu *mmu)
{
	struct msm_iommu *iommu = to_msm_iommu(mmu);

	return &iommu->domain->geometry;
}

int
msm_iommu_pagetable_walk(struct msm_mmu *mmu, unsigned long iova, uint64_t ptes[4])
{
	struct msm_iommu_pagetable *pagetable;
	struct arm_lpae_io_pgtable_walk_data wd = {};

	if (mmu->type != MSM_MMU_IOMMU_PAGETABLE)
		return -EINVAL;

	pagetable = to_pagetable(mmu);

	if (!pagetable->pgtbl_ops->pgtable_walk)
		return -EINVAL;

	pagetable->pgtbl_ops->pgtable_walk(pagetable->pgtbl_ops, iova, &wd);

	for (int i = 0; i < ARRAY_SIZE(wd.ptes); i++)
		ptes[i] = wd.ptes[i];

	return 0;
}

static void
msm_iommu_pagetable_prealloc_count(struct msm_mmu *mmu, struct msm_mmu_prealloc *p,
				   uint64_t iova, size_t len)
{
	u64 pt_count;

	/*
	 * L1, L2 and L3 page tables.
	 *
	 * We could optimize L3 allocation by iterating over the sgt and merging
	 * 2M contiguous blocks, but it's simpler to over-provision and return
	 * the pages if they're not used.
	 *
	 * The first level descriptor (v8 / v7-lpae page table format) encodes
	 * 30 bits of address.  The second level encodes 29.  For the 3rd it is
	 * 39.
	 *
	 * https://developer.arm.com/documentation/ddi0406/c/System-Level-Architecture/Virtual-Memory-System-Architecture--VMSA-/Long-descriptor-translation-table-format/Long-descriptor-translation-table-format-descriptors?lang=en#BEIHEFFB
	 */
	pt_count = ((ALIGN(iova + len, 1ull << 39) - ALIGN_DOWN(iova, 1ull << 39)) >> 39) +
		   ((ALIGN(iova + len, 1ull << 30) - ALIGN_DOWN(iova, 1ull << 30)) >> 30) +
		   ((ALIGN(iova + len, 1ull << 21) - ALIGN_DOWN(iova, 1ull << 21)) >> 21);

	p->count += pt_count;
}

static struct kmem_cache *
get_pt_cache(struct msm_mmu *mmu)
{
	struct msm_iommu_pagetable *pagetable = to_pagetable(mmu);
	return to_msm_iommu(pagetable->parent)->pt_cache;
}

static int
msm_iommu_pagetable_prealloc_allocate(struct msm_mmu *mmu, struct msm_mmu_prealloc *p)
{
	struct kmem_cache *pt_cache = get_pt_cache(mmu);
	int ret;

	p->pages = kvmalloc_array(p->count, sizeof(p->pages), GFP_KERNEL);
	if (!p->pages)
		return -ENOMEM;

	ret = kmem_cache_alloc_bulk(pt_cache, GFP_KERNEL, p->count, p->pages);
	if (ret != p->count) {
		kfree(p->pages);
		p->pages = NULL;
		p->count = ret;
		return -ENOMEM;
	}

	return 0;
}

static void
msm_iommu_pagetable_prealloc_cleanup(struct msm_mmu *mmu, struct msm_mmu_prealloc *p)
{
	struct kmem_cache *pt_cache = get_pt_cache(mmu);
	uint32_t remaining_pt_count = p->count - p->ptr;

	if (!p->pages)
		return;

	if (p->count > 0)
		trace_msm_mmu_prealloc_cleanup(p->count, remaining_pt_count);

	kmem_cache_free_bulk(pt_cache, remaining_pt_count, &p->pages[p->ptr]);
	kvfree(p->pages);
}

/**
 * alloc_pt() - Custom page table allocator
 * @cookie: Cookie passed at page table allocation time.
 * @size: Size of the page table. This size should be fixed,
 * and determined at creation time based on the granule size.
 * @gfp: GFP flags.
 *
 * We want a custom allocator so we can use a cache for page table
 * allocations and amortize the cost of the over-reservation that's
 * done to allow asynchronous VM operations.
 *
 * Return: non-NULL on success, NULL if the allocation failed for any
 * reason.
 */
static void *
msm_iommu_pagetable_alloc_pt(void *cookie, size_t size, gfp_t gfp)
{
	struct msm_iommu_pagetable *pagetable = cookie;
	struct msm_mmu_prealloc *p = pagetable->base.prealloc;
	void *page;

	/* Allocation of the root page table happening during init. */
	if (unlikely(!pagetable->root_page_table)) {
		struct page *p;

		p = alloc_pages_node(dev_to_node(pagetable->iommu_dev),
				     gfp | __GFP_ZERO, get_order(size));
		page = p ? page_address(p) : NULL;
		pagetable->root_page_table = page;
		return page;
	}

	if (WARN_ON(!p) || WARN_ON(p->ptr >= p->count))
		return NULL;

	page = p->pages[p->ptr++];
	memset(page, 0, size);

	/*
	 * Page table entries don't use virtual addresses, which trips out
	 * kmemleak. kmemleak_alloc_phys() might work, but physical addresses
	 * are mixed with other fields, and I fear kmemleak won't detect that
	 * either.
	 *
	 * Let's just ignore memory passed to the page-table driver for now.
	 */
	kmemleak_ignore(page);

	return page;
}


/**
 * free_pt() - Custom page table free function
 * @cookie: Cookie passed at page table allocation time.
 * @data: Page table to free.
 * @size: Size of the page table. This size should be fixed,
 * and determined at creation time based on the granule size.
 */
static void
msm_iommu_pagetable_free_pt(void *cookie, void *data, size_t size)
{
	struct msm_iommu_pagetable *pagetable = cookie;

	if (unlikely(pagetable->root_page_table == data)) {
		free_pages((unsigned long)data, get_order(size));
		pagetable->root_page_table = NULL;
		return;
	}

	kmem_cache_free(get_pt_cache(&pagetable->base), data);
}

static const struct msm_mmu_funcs pagetable_funcs = {
		.prealloc_count = msm_iommu_pagetable_prealloc_count,
		.prealloc_allocate = msm_iommu_pagetable_prealloc_allocate,
		.prealloc_cleanup = msm_iommu_pagetable_prealloc_cleanup,
		.map = msm_iommu_pagetable_map,
		.unmap = msm_iommu_pagetable_unmap,
		.destroy = msm_iommu_pagetable_destroy,
};

static void msm_iommu_tlb_flush_all(void *cookie)
{
	struct msm_iommu_pagetable *pagetable = cookie;
	struct adreno_smmu_priv *adreno_smmu;

	if (!pm_runtime_get_if_in_use(pagetable->iommu_dev))
		return;

	adreno_smmu = dev_get_drvdata(pagetable->parent->dev);

	pagetable->tlb->tlb_flush_all((void *)adreno_smmu->cookie);

	pm_runtime_put_autosuspend(pagetable->iommu_dev);
}

static void msm_iommu_tlb_flush_walk(unsigned long iova, size_t size,
		size_t granule, void *cookie)
{
	struct msm_iommu_pagetable *pagetable = cookie;
	struct adreno_smmu_priv *adreno_smmu;

	if (!pm_runtime_get_if_in_use(pagetable->iommu_dev))
		return;

	adreno_smmu = dev_get_drvdata(pagetable->parent->dev);

	pagetable->tlb->tlb_flush_walk(iova, size, granule, (void *)adreno_smmu->cookie);

	pm_runtime_put_autosuspend(pagetable->iommu_dev);
}

static void msm_iommu_tlb_add_page(struct iommu_iotlb_gather *gather,
		unsigned long iova, size_t granule, void *cookie)
{
}

static const struct iommu_flush_ops tlb_ops = {
	.tlb_flush_all = msm_iommu_tlb_flush_all,
	.tlb_flush_walk = msm_iommu_tlb_flush_walk,
	.tlb_add_page = msm_iommu_tlb_add_page,
};

static int msm_gpu_fault_handler(struct iommu_domain *domain, struct device *dev,
		unsigned long iova, int flags, void *arg);

static size_t get_tblsz(const struct io_pgtable_cfg *cfg)
{
	int pg_shift, bits_per_level;

	pg_shift = __ffs(cfg->pgsize_bitmap);
	/* arm_lpae_iopte is u64: */
	bits_per_level = pg_shift - ilog2(sizeof(u64));

	return sizeof(u64) << bits_per_level;
}

struct msm_mmu *msm_iommu_pagetable_create(struct msm_mmu *parent, bool kernel_managed)
{
	struct adreno_smmu_priv *adreno_smmu = dev_get_drvdata(parent->dev);
	struct msm_iommu *iommu = to_msm_iommu(parent);
	struct msm_iommu_pagetable *pagetable;
	const struct io_pgtable_cfg *ttbr1_cfg = NULL;
	struct io_pgtable_cfg ttbr0_cfg;
	int ret;

	/* Get the pagetable configuration from the domain */
	if (adreno_smmu->cookie)
		ttbr1_cfg = adreno_smmu->get_ttbr1_cfg(adreno_smmu->cookie);

	/*
	 * If you hit this WARN_ONCE() you are probably missing an entry in
	 * qcom_smmu_impl_of_match[] in arm-smmu-qcom.c
	 */
	if (WARN_ONCE(!ttbr1_cfg, "No per-process page tables"))
		return ERR_PTR(-ENODEV);

	pagetable = kzalloc(sizeof(*pagetable), GFP_KERNEL);
	if (!pagetable)
		return ERR_PTR(-ENOMEM);

	msm_mmu_init(&pagetable->base, parent->dev, &pagetable_funcs,
		MSM_MMU_IOMMU_PAGETABLE);

	/* Clone the TTBR1 cfg as starting point for TTBR0 cfg: */
	ttbr0_cfg = *ttbr1_cfg;

	/* The incoming cfg will have the TTBR1 quirk enabled */
	ttbr0_cfg.quirks &= ~IO_PGTABLE_QUIRK_ARM_TTBR1;
	ttbr0_cfg.tlb = &tlb_ops;

	if (!kernel_managed) {
		ttbr0_cfg.quirks |= IO_PGTABLE_QUIRK_NO_WARN;

		/*
		 * With userspace managed VM (aka VM_BIND), we need to pre-
		 * allocate pages ahead of time for map/unmap operations,
		 * handing them to io-pgtable via custom alloc/free ops as
		 * needed:
		 */
		ttbr0_cfg.alloc = msm_iommu_pagetable_alloc_pt;
		ttbr0_cfg.free  = msm_iommu_pagetable_free_pt;

		/*
		 * Restrict to single page granules.  Otherwise we may run
		 * into a situation where userspace wants to unmap/remap
		 * only a part of a larger block mapping, which is not
		 * possible without unmapping the entire block.  Which in
		 * turn could cause faults if the GPU is accessing other
		 * parts of the block mapping.
		 *
		 * Note that prior to commit 33729a5fc0ca ("iommu/io-pgtable-arm:
		 * Remove split on unmap behavior)" this was handled in
		 * io-pgtable-arm.  But this apparently does not work
		 * correctly on SMMUv3.
		 */
		WARN_ON(!(ttbr0_cfg.pgsize_bitmap & PAGE_SIZE));
		ttbr0_cfg.pgsize_bitmap = PAGE_SIZE;
	}

	pagetable->iommu_dev = ttbr1_cfg->iommu_dev;
	pagetable->pgtbl_ops = alloc_io_pgtable_ops(ARM_64_LPAE_S1,
		&ttbr0_cfg, pagetable);

	if (!pagetable->pgtbl_ops) {
		kfree(pagetable);
		return ERR_PTR(-ENOMEM);
	}

	/*
	 * If this is the first pagetable that we've allocated, send it back to
	 * the arm-smmu driver as a trigger to set up TTBR0
	 */
	mutex_lock(&iommu->init_lock);
	if (iommu->pagetables++ == 0) {
		ret = adreno_smmu->set_ttbr0_cfg(adreno_smmu->cookie, &ttbr0_cfg);
		if (ret) {
			iommu->pagetables--;
			mutex_unlock(&iommu->init_lock);
			free_io_pgtable_ops(pagetable->pgtbl_ops);
			kfree(pagetable);
			return ERR_PTR(ret);
		}

		BUG_ON(iommu->prr_page);
		if (adreno_smmu->set_prr_bit) {
			/*
			 * We need a zero'd page for two reasons:
			 *
			 * 1) Reserve a known physical address to use when
			 *    mapping NULL / sparsely resident regions
			 * 2) Read back zero
			 *
			 * It appears the hw drops writes to the PRR region
			 * on the floor, but reads actually return whatever
			 * is in the PRR page.
			 */
			iommu->prr_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
			adreno_smmu->set_prr_addr(adreno_smmu->cookie,
						  page_to_phys(iommu->prr_page));
			adreno_smmu->set_prr_bit(adreno_smmu->cookie, true);
		}
	}
	mutex_unlock(&iommu->init_lock);

	/* Needed later for TLB flush */
	pagetable->parent = parent;
	pagetable->tlb = ttbr1_cfg->tlb;
	pagetable->pgsize_bitmap = ttbr0_cfg.pgsize_bitmap;
	pagetable->ttbr = ttbr0_cfg.arm_lpae_s1_cfg.ttbr;

	/*
	 * TODO we would like each set of page tables to have a unique ASID
	 * to optimize TLB invalidation.  But iommu_flush_iotlb_all() will
	 * end up flushing the ASID used for TTBR1 pagetables, which is not
	 * what we want.  So for now just use the same ASID as TTBR1.
	 */
	pagetable->asid = 0;

	return &pagetable->base;
}

static int msm_gpu_fault_handler(struct iommu_domain *domain, struct device *dev,
		unsigned long iova, int flags, void *arg)
{
	struct msm_iommu *iommu = arg;
	struct adreno_smmu_priv *adreno_smmu = dev_get_drvdata(iommu->base.dev);
	struct adreno_smmu_fault_info info, *ptr = NULL;

	if (adreno_smmu->get_fault_info) {
		adreno_smmu->get_fault_info(adreno_smmu->cookie, &info);
		ptr = &info;
	}

	if (iommu->base.handler)
		return iommu->base.handler(iommu->base.arg, iova, flags, ptr);

	pr_warn_ratelimited("*** fault: iova=%16lx, flags=%d\n", iova, flags);

	return 0;
}

static int msm_disp_fault_handler(struct iommu_domain *domain, struct device *dev,
				  unsigned long iova, int flags, void *arg)
{
	struct msm_iommu *iommu = arg;

	if (iommu->base.handler)
		return iommu->base.handler(iommu->base.arg, iova, flags, NULL);

	return -ENOSYS;
}

static void msm_iommu_set_stall(struct msm_mmu *mmu, bool enable)
{
	struct adreno_smmu_priv *adreno_smmu = dev_get_drvdata(mmu->dev);

	if (adreno_smmu->set_stall)
		adreno_smmu->set_stall(adreno_smmu->cookie, enable);
}

static void msm_iommu_detach(struct msm_mmu *mmu)
{
	struct msm_iommu *iommu = to_msm_iommu(mmu);

	iommu_detach_device(iommu->domain, mmu->dev);
}

static int msm_iommu_map(struct msm_mmu *mmu, uint64_t iova,
			 struct sg_table *sgt, size_t off, size_t len,
			 int prot)
{
	struct msm_iommu *iommu = to_msm_iommu(mmu);
	size_t ret;

	WARN_ON(off != 0);

	/* The arm-smmu driver expects the addresses to be sign extended */
	if (iova & BIT_ULL(48))
		iova |= GENMASK_ULL(63, 49);

	ret = iommu_map_sgtable(iommu->domain, iova, sgt, prot);
	WARN_ON(!ret);

	return (ret == len) ? 0 : -EINVAL;
}

static int msm_iommu_unmap(struct msm_mmu *mmu, uint64_t iova, size_t len)
{
	struct msm_iommu *iommu = to_msm_iommu(mmu);

	if (iova & BIT_ULL(48))
		iova |= GENMASK_ULL(63, 49);

	iommu_unmap(iommu->domain, iova, len);

	return 0;
}

static void msm_iommu_destroy(struct msm_mmu *mmu)
{
	struct msm_iommu *iommu = to_msm_iommu(mmu);
	iommu_domain_free(iommu->domain);
	kmem_cache_destroy(iommu->pt_cache);
	kfree(iommu);
}

static const struct msm_mmu_funcs funcs = {
		.detach = msm_iommu_detach,
		.map = msm_iommu_map,
		.unmap = msm_iommu_unmap,
		.destroy = msm_iommu_destroy,
		.set_stall = msm_iommu_set_stall,
};

struct msm_mmu *msm_iommu_new(struct device *dev, unsigned long quirks)
{
	struct iommu_domain *domain;
	struct msm_iommu *iommu;
	int ret;

	if (!device_iommu_mapped(dev))
		return ERR_PTR(-ENODEV);

	domain = iommu_paging_domain_alloc(dev);
	if (IS_ERR(domain))
		return ERR_CAST(domain);

	iommu_set_pgtable_quirks(domain, quirks);

	iommu = kzalloc(sizeof(*iommu), GFP_KERNEL);
	if (!iommu) {
		iommu_domain_free(domain);
		return ERR_PTR(-ENOMEM);
	}

	iommu->domain = domain;
	msm_mmu_init(&iommu->base, dev, &funcs, MSM_MMU_IOMMU);

	mutex_init(&iommu->init_lock);

	ret = iommu_attach_device(iommu->domain, dev);
	if (ret) {
		iommu_domain_free(domain);
		kfree(iommu);
		return ERR_PTR(ret);
	}

	return &iommu->base;
}

struct msm_mmu *msm_iommu_disp_new(struct device *dev, unsigned long quirks)
{
	struct msm_iommu *iommu;
	struct msm_mmu *mmu;

	mmu = msm_iommu_new(dev, quirks);
	if (IS_ERR(mmu))
		return mmu;

	iommu = to_msm_iommu(mmu);
	iommu_set_fault_handler(iommu->domain, msm_disp_fault_handler, iommu);

	return mmu;
}

struct msm_mmu *msm_iommu_gpu_new(struct device *dev, struct msm_gpu *gpu, unsigned long quirks)
{
	struct adreno_smmu_priv *adreno_smmu = dev_get_drvdata(dev);
	struct msm_iommu *iommu;
	struct msm_mmu *mmu;

	mmu = msm_iommu_new(dev, quirks);
	if (IS_ERR(mmu))
		return mmu;

	iommu = to_msm_iommu(mmu);
	if (adreno_smmu->cookie) {
		const struct io_pgtable_cfg *cfg =
			adreno_smmu->get_ttbr1_cfg(adreno_smmu->cookie);
		size_t tblsz = get_tblsz(cfg);

		iommu->pt_cache =
			kmem_cache_create("msm-mmu-pt", tblsz, tblsz, 0, NULL);
	}
	iommu_set_fault_handler(iommu->domain, msm_gpu_fault_handler, iommu);

	/* Enable stall on iommu fault: */
	if (adreno_smmu->set_stall)
		adreno_smmu->set_stall(adreno_smmu->cookie, true);

	return mmu;
}

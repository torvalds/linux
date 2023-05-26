// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/adreno-smmu-priv.h>
#include <linux/io-pgtable.h>
#include "msm_drv.h"
#include "msm_mmu.h"

struct msm_iommu {
	struct msm_mmu base;
	struct iommu_domain *domain;
	atomic_t pagetables;
};

#define to_msm_iommu(x) container_of(x, struct msm_iommu, base)

struct msm_iommu_pagetable {
	struct msm_mmu base;
	struct msm_mmu *parent;
	struct io_pgtable_ops *pgtbl_ops;
	unsigned long pgsize_bitmap;	/* Bitmap of page sizes in use */
	phys_addr_t ttbr;
	u32 asid;
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

	while (size) {
		size_t unmapped, pgsize, count;

		pgsize = calc_pgsize(pagetable, iova, iova, size, &count);

		unmapped = ops->unmap_pages(ops, iova, pgsize, count, NULL);
		if (!unmapped)
			break;

		iova += unmapped;
		size -= unmapped;
	}

	iommu_flush_iotlb_all(to_msm_iommu(pagetable->parent)->domain);

	return (size == 0) ? 0 : -EINVAL;
}

static int msm_iommu_pagetable_map(struct msm_mmu *mmu, u64 iova,
		struct sg_table *sgt, size_t len, int prot)
{
	struct msm_iommu_pagetable *pagetable = to_pagetable(mmu);
	struct io_pgtable_ops *ops = pagetable->pgtbl_ops;
	struct scatterlist *sg;
	u64 addr = iova;
	unsigned int i;

	for_each_sgtable_sg(sgt, sg, i) {
		size_t size = sg->length;
		phys_addr_t phys = sg_phys(sg);

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
	if (atomic_dec_return(&iommu->pagetables) == 0)
		adreno_smmu->set_ttbr0_cfg(adreno_smmu->cookie, NULL);

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

static const struct msm_mmu_funcs pagetable_funcs = {
		.map = msm_iommu_pagetable_map,
		.unmap = msm_iommu_pagetable_unmap,
		.destroy = msm_iommu_pagetable_destroy,
};

static void msm_iommu_tlb_flush_all(void *cookie)
{
}

static void msm_iommu_tlb_flush_walk(unsigned long iova, size_t size,
		size_t granule, void *cookie)
{
}

static void msm_iommu_tlb_add_page(struct iommu_iotlb_gather *gather,
		unsigned long iova, size_t granule, void *cookie)
{
}

static const struct iommu_flush_ops null_tlb_ops = {
	.tlb_flush_all = msm_iommu_tlb_flush_all,
	.tlb_flush_walk = msm_iommu_tlb_flush_walk,
	.tlb_add_page = msm_iommu_tlb_add_page,
};

static int msm_fault_handler(struct iommu_domain *domain, struct device *dev,
		unsigned long iova, int flags, void *arg);

struct msm_mmu *msm_iommu_pagetable_create(struct msm_mmu *parent)
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
	ttbr0_cfg.tlb = &null_tlb_ops;

	pagetable->pgtbl_ops = alloc_io_pgtable_ops(ARM_64_LPAE_S1,
		&ttbr0_cfg, iommu->domain);

	if (!pagetable->pgtbl_ops) {
		kfree(pagetable);
		return ERR_PTR(-ENOMEM);
	}

	/*
	 * If this is the first pagetable that we've allocated, send it back to
	 * the arm-smmu driver as a trigger to set up TTBR0
	 */
	if (atomic_inc_return(&iommu->pagetables) == 1) {
		ret = adreno_smmu->set_ttbr0_cfg(adreno_smmu->cookie, &ttbr0_cfg);
		if (ret) {
			free_io_pgtable_ops(pagetable->pgtbl_ops);
			kfree(pagetable);
			return ERR_PTR(ret);
		}
	}

	/* Needed later for TLB flush */
	pagetable->parent = parent;
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

static int msm_fault_handler(struct iommu_domain *domain, struct device *dev,
		unsigned long iova, int flags, void *arg)
{
	struct msm_iommu *iommu = arg;
	struct msm_mmu *mmu = &iommu->base;
	struct adreno_smmu_priv *adreno_smmu = dev_get_drvdata(iommu->base.dev);
	struct adreno_smmu_fault_info info, *ptr = NULL;

	if (adreno_smmu->get_fault_info) {
		adreno_smmu->get_fault_info(adreno_smmu->cookie, &info);
		ptr = &info;
	}

	if (iommu->base.handler)
		return iommu->base.handler(iommu->base.arg, iova, flags, ptr);

	pr_warn_ratelimited("*** fault: iova=%16lx, flags=%d\n", iova, flags);

	if (mmu->funcs->resume_translation)
		mmu->funcs->resume_translation(mmu);

	return 0;
}

static void msm_iommu_resume_translation(struct msm_mmu *mmu)
{
	struct adreno_smmu_priv *adreno_smmu = dev_get_drvdata(mmu->dev);

	if (adreno_smmu->resume_translation)
		adreno_smmu->resume_translation(adreno_smmu->cookie, true);
}

static void msm_iommu_detach(struct msm_mmu *mmu)
{
	struct msm_iommu *iommu = to_msm_iommu(mmu);

	iommu_detach_device(iommu->domain, mmu->dev);
}

static int msm_iommu_map(struct msm_mmu *mmu, uint64_t iova,
		struct sg_table *sgt, size_t len, int prot)
{
	struct msm_iommu *iommu = to_msm_iommu(mmu);
	size_t ret;

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
	kfree(iommu);
}

static const struct msm_mmu_funcs funcs = {
		.detach = msm_iommu_detach,
		.map = msm_iommu_map,
		.unmap = msm_iommu_unmap,
		.destroy = msm_iommu_destroy,
		.resume_translation = msm_iommu_resume_translation,
};

struct msm_mmu *msm_iommu_new(struct device *dev, unsigned long quirks)
{
	struct iommu_domain *domain;
	struct msm_iommu *iommu;
	int ret;

	domain = iommu_domain_alloc(dev->bus);
	if (!domain)
		return NULL;

	iommu_set_pgtable_quirks(domain, quirks);

	iommu = kzalloc(sizeof(*iommu), GFP_KERNEL);
	if (!iommu) {
		iommu_domain_free(domain);
		return ERR_PTR(-ENOMEM);
	}

	iommu->domain = domain;
	msm_mmu_init(&iommu->base, dev, &funcs, MSM_MMU_IOMMU);

	atomic_set(&iommu->pagetables, 0);

	ret = iommu_attach_device(iommu->domain, dev);
	if (ret) {
		iommu_domain_free(domain);
		kfree(iommu);
		return ERR_PTR(ret);
	}

	return &iommu->base;
}

struct msm_mmu *msm_iommu_gpu_new(struct device *dev, struct msm_gpu *gpu, unsigned long quirks)
{
	struct adreno_smmu_priv *adreno_smmu = dev_get_drvdata(dev);
	struct msm_iommu *iommu;
	struct msm_mmu *mmu;

	mmu = msm_iommu_new(dev, quirks);
	if (IS_ERR_OR_NULL(mmu))
		return mmu;

	iommu = to_msm_iommu(mmu);
	iommu_set_fault_handler(iommu->domain, msm_fault_handler, iommu);

	/* Enable stall on iommu fault: */
	if (adreno_smmu->set_stall)
		adreno_smmu->set_stall(adreno_smmu->cookie, true);

	return mmu;
}

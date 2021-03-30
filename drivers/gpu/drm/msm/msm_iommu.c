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
	phys_addr_t ttbr;
	u32 asid;
};
static struct msm_iommu_pagetable *to_pagetable(struct msm_mmu *mmu)
{
	return container_of(mmu, struct msm_iommu_pagetable, base);
}

static int msm_iommu_pagetable_unmap(struct msm_mmu *mmu, u64 iova,
		size_t size)
{
	struct msm_iommu_pagetable *pagetable = to_pagetable(mmu);
	struct io_pgtable_ops *ops = pagetable->pgtbl_ops;
	size_t unmapped = 0;

	/* Unmap the block one page at a time */
	while (size) {
		unmapped += ops->unmap(ops, iova, 4096, NULL);
		iova += 4096;
		size -= 4096;
	}

	iommu_flush_iotlb_all(to_msm_iommu(pagetable->parent)->domain);

	return (unmapped == size) ? 0 : -EINVAL;
}

static int msm_iommu_pagetable_map(struct msm_mmu *mmu, u64 iova,
		struct sg_table *sgt, size_t len, int prot)
{
	struct msm_iommu_pagetable *pagetable = to_pagetable(mmu);
	struct io_pgtable_ops *ops = pagetable->pgtbl_ops;
	struct scatterlist *sg;
	size_t mapped = 0;
	u64 addr = iova;
	unsigned int i;

	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		size_t size = sg->length;
		phys_addr_t phys = sg_phys(sg);

		/* Map the block one page at a time */
		while (size) {
			if (ops->map(ops, addr, phys, 4096, prot, GFP_KERNEL)) {
				msm_iommu_pagetable_unmap(mmu, iova, mapped);
				return -EINVAL;
			}

			phys += 4096;
			addr += 4096;
			size -= 4096;
			mapped += 4096;
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
	if (!ttbr1_cfg)
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
	if (iommu->base.handler)
		return iommu->base.handler(iommu->base.arg, iova, flags);
	pr_warn_ratelimited("*** fault: iova=%16lx, flags=%d\n", iova, flags);
	return 0;
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
};

struct msm_mmu *msm_iommu_new(struct device *dev, struct iommu_domain *domain)
{
	struct msm_iommu *iommu;
	int ret;

	if (!domain)
		return ERR_PTR(-ENODEV);

	iommu = kzalloc(sizeof(*iommu), GFP_KERNEL);
	if (!iommu)
		return ERR_PTR(-ENOMEM);

	iommu->domain = domain;
	msm_mmu_init(&iommu->base, dev, &funcs, MSM_MMU_IOMMU);
	iommu_set_fault_handler(domain, msm_fault_handler, iommu);

	atomic_set(&iommu->pagetables, 0);

	ret = iommu_attach_device(iommu->domain, dev);
	if (ret) {
		kfree(iommu);
		return ERR_PTR(ret);
	}

	return &iommu->base;
}

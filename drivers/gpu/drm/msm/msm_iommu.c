// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include "msm_drv.h"
#include "msm_mmu.h"

struct msm_iommu {
	struct msm_mmu base;
	struct iommu_domain *domain;
};
#define to_msm_iommu(x) container_of(x, struct msm_iommu, base)

static int msm_fault_handler(struct iommu_domain *domain, struct device *dev,
		unsigned long iova, int flags, void *arg)
{
	struct msm_iommu *iommu = arg;
	if (iommu->base.handler)
		return iommu->base.handler(iommu->base.arg, iova, flags);
	pr_warn_ratelimited("*** fault: iova=%16lx, flags=%d\n", iova, flags);
	return 0;
}

static int msm_iommu_attach(struct msm_mmu *mmu)
{
	struct msm_iommu *iommu = to_msm_iommu(mmu);

	return iommu_attach_device(iommu->domain, mmu->dev);
}

static void msm_iommu_detach(struct msm_mmu *mmu)
{
	struct msm_iommu *iommu = to_msm_iommu(mmu);

	iommu_detach_device(iommu->domain, mmu->dev);
}

static int msm_iommu_map(struct msm_mmu *mmu, uint64_t iova,
		struct sg_table *sgt, unsigned len, int prot)
{
	struct msm_iommu *iommu = to_msm_iommu(mmu);
	size_t ret;

	ret = iommu_map_sg(iommu->domain, iova, sgt->sgl, sgt->nents, prot);
	WARN_ON(!ret);

	return (ret == len) ? 0 : -EINVAL;
}

static int msm_iommu_unmap(struct msm_mmu *mmu, uint64_t iova, unsigned len)
{
	struct msm_iommu *iommu = to_msm_iommu(mmu);

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
		.attach = msm_iommu_attach,
		.detach = msm_iommu_detach,
		.map = msm_iommu_map,
		.unmap = msm_iommu_unmap,
		.destroy = msm_iommu_destroy,
};

struct msm_mmu *msm_iommu_new(struct device *dev, struct iommu_domain *domain)
{
	struct msm_iommu *iommu;

	iommu = kzalloc(sizeof(*iommu), GFP_KERNEL);
	if (!iommu)
		return ERR_PTR(-ENOMEM);

	iommu->domain = domain;
	msm_mmu_init(&iommu->base, dev, &funcs);
	iommu_set_fault_handler(domain, msm_fault_handler, iommu);

	return &iommu->base;
}

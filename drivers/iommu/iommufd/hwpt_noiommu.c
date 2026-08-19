// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES
 */
#include <linux/generic_pt/iommu.h>
#include <linux/iommu.h>
#include "../iommu-pages.h"
#include "iommufd_private.h"

static const struct iommu_domain_ops noiommu_amdv1_ops;

struct noiommu_domain {
	union {
		struct iommu_domain domain;
		struct pt_iommu_amdv1 amdv1;
	};
	spinlock_t lock;
};
PT_IOMMU_CHECK_DOMAIN(struct noiommu_domain, amdv1.iommu, domain);

static void noiommu_change_top(struct pt_iommu *iommu_table,
			       phys_addr_t top_paddr, unsigned int top_level)
{
}

static spinlock_t *noiommu_get_top_lock(struct pt_iommu *iommupt)
{
	struct noiommu_domain *domain =
		container_of(iommupt, struct noiommu_domain, amdv1.iommu);

	return &domain->lock;
}

static const struct pt_iommu_driver_ops noiommu_driver_ops = {
	.get_top_lock = noiommu_get_top_lock,
	.change_top = noiommu_change_top,
};

static struct iommu_domain *
noiommu_alloc_paging_flags(struct device *dev, u32 flags,
			   const struct iommu_user_data *user_data)
{
	struct pt_iommu_amdv1_cfg cfg = {};
	struct noiommu_domain *dom;
	int rc;

	if (flags || user_data)
		return ERR_PTR(-EOPNOTSUPP);

	cfg.common.hw_max_vasz_lg2 = 64;
	cfg.common.hw_max_oasz_lg2 = 52;
	cfg.starting_level = 2;
	cfg.common.features =
		(BIT(PT_FEAT_DYNAMIC_TOP) | BIT(PT_FEAT_AMDV1_ENCRYPT_TABLES) |
		 BIT(PT_FEAT_AMDV1_FORCE_COHERENCE));

	dom = kzalloc(sizeof(*dom), GFP_KERNEL);
	if (!dom)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&dom->lock);
	dom->amdv1.iommu.nid = NUMA_NO_NODE;
	dom->amdv1.iommu.driver_ops = &noiommu_driver_ops;
	dom->domain.ops = &noiommu_amdv1_ops;

	/* Use SW-only page table which is based on AMDV1 */
	rc = pt_iommu_amdv1_init(&dom->amdv1, &cfg, GFP_KERNEL);
	if (rc) {
		kfree(dom);
		return ERR_PTR(rc);
	}

	return &dom->domain;
}

static void noiommu_domain_free(struct iommu_domain *iommu_domain)
{
	struct noiommu_domain *domain =
		container_of(iommu_domain, struct noiommu_domain, domain);

	pt_iommu_deinit(&domain->amdv1.iommu);
	kfree(domain);
}

static void noiommu_iotlb_sync(struct iommu_domain *domain,
			       struct iommu_iotlb_gather *gather)
{
	iommu_put_pages_list(&gather->freelist);
}

/*
 * Domain ops for iommufd no-IOMMU mode. Uses AMDV1 format as a
 * SW-only IOPT because it has the best multi-page size options
 * of all the formats. IOVAs serve only for IOVA-to-PA lookups,
 * not for hardware DMA translation.
 */
static const struct iommu_domain_ops noiommu_amdv1_ops = {
	IOMMU_PT_DOMAIN_OPS(amdv1),
	.iotlb_sync = noiommu_iotlb_sync,
	.free = noiommu_domain_free,
};

const struct iommu_ops iommufd_noiommu_ops = {
	.domain_alloc_paging_flags = noiommu_alloc_paging_flags,
};

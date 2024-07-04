// SPDX-License-Identifier: GPL-2.0
/*
 * nested.c - nested mode translation support
 *
 * Copyright (C) 2023 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 *         Jacob Pan <jacob.jun.pan@linux.intel.com>
 *         Yi Liu <yi.l.liu@intel.com>
 */

#define pr_fmt(fmt)	"DMAR: " fmt

#include <linux/iommu.h>
#include <linux/pci.h>
#include <linux/pci-ats.h>

#include "iommu.h"
#include "pasid.h"

static int intel_nested_attach_dev(struct iommu_domain *domain,
				   struct device *dev)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);
	struct dmar_domain *dmar_domain = to_dmar_domain(domain);
	struct intel_iommu *iommu = info->iommu;
	unsigned long flags;
	int ret = 0;

	if (info->domain)
		device_block_translation(dev);

	if (iommu->agaw < dmar_domain->s2_domain->agaw) {
		dev_err_ratelimited(dev, "Adjusted guest address width not compatible\n");
		return -ENODEV;
	}

	/*
	 * Stage-1 domain cannot work alone, it is nested on a s2_domain.
	 * The s2_domain will be used in nested translation, hence needs
	 * to ensure the s2_domain is compatible with this IOMMU.
	 */
	ret = prepare_domain_attach_device(&dmar_domain->s2_domain->domain, dev);
	if (ret) {
		dev_err_ratelimited(dev, "s2 domain is not compatible\n");
		return ret;
	}

	ret = domain_attach_iommu(dmar_domain, iommu);
	if (ret) {
		dev_err_ratelimited(dev, "Failed to attach domain to iommu\n");
		return ret;
	}

	ret = cache_tag_assign_domain(dmar_domain, dev, IOMMU_NO_PASID);
	if (ret)
		goto detach_iommu;

	ret = intel_pasid_setup_nested(iommu, dev,
				       IOMMU_NO_PASID, dmar_domain);
	if (ret)
		goto unassign_tag;

	info->domain = dmar_domain;
	spin_lock_irqsave(&dmar_domain->lock, flags);
	list_add(&info->link, &dmar_domain->devices);
	spin_unlock_irqrestore(&dmar_domain->lock, flags);

	domain_update_iotlb(dmar_domain);

	return 0;
unassign_tag:
	cache_tag_unassign_domain(dmar_domain, dev, IOMMU_NO_PASID);
detach_iommu:
	domain_detach_iommu(dmar_domain, iommu);

	return ret;
}

static void intel_nested_domain_free(struct iommu_domain *domain)
{
	struct dmar_domain *dmar_domain = to_dmar_domain(domain);
	struct dmar_domain *s2_domain = dmar_domain->s2_domain;

	spin_lock(&s2_domain->s1_lock);
	list_del(&dmar_domain->s2_link);
	spin_unlock(&s2_domain->s1_lock);
	kfree(dmar_domain);
}

static int intel_nested_cache_invalidate_user(struct iommu_domain *domain,
					      struct iommu_user_data_array *array)
{
	struct dmar_domain *dmar_domain = to_dmar_domain(domain);
	struct iommu_hwpt_vtd_s1_invalidate inv_entry;
	u32 index, processed = 0;
	int ret = 0;

	if (array->type != IOMMU_HWPT_INVALIDATE_DATA_VTD_S1) {
		ret = -EINVAL;
		goto out;
	}

	for (index = 0; index < array->entry_num; index++) {
		ret = iommu_copy_struct_from_user_array(&inv_entry, array,
							IOMMU_HWPT_INVALIDATE_DATA_VTD_S1,
							index, __reserved);
		if (ret)
			break;

		if ((inv_entry.flags & ~IOMMU_VTD_INV_FLAGS_LEAF) ||
		    inv_entry.__reserved) {
			ret = -EOPNOTSUPP;
			break;
		}

		if (!IS_ALIGNED(inv_entry.addr, VTD_PAGE_SIZE) ||
		    ((inv_entry.npages == U64_MAX) && inv_entry.addr)) {
			ret = -EINVAL;
			break;
		}

		cache_tag_flush_range(dmar_domain, inv_entry.addr,
				      inv_entry.addr + nrpages_to_size(inv_entry.npages) - 1,
				      inv_entry.flags & IOMMU_VTD_INV_FLAGS_LEAF);
		processed++;
	}

out:
	array->entry_num = processed;
	return ret;
}

static const struct iommu_domain_ops intel_nested_domain_ops = {
	.attach_dev		= intel_nested_attach_dev,
	.free			= intel_nested_domain_free,
	.cache_invalidate_user	= intel_nested_cache_invalidate_user,
};

struct iommu_domain *intel_nested_domain_alloc(struct iommu_domain *parent,
					       const struct iommu_user_data *user_data)
{
	struct dmar_domain *s2_domain = to_dmar_domain(parent);
	struct iommu_hwpt_vtd_s1 vtd;
	struct dmar_domain *domain;
	int ret;

	/* Must be nested domain */
	if (user_data->type != IOMMU_HWPT_DATA_VTD_S1)
		return ERR_PTR(-EOPNOTSUPP);
	if (parent->ops != intel_iommu_ops.default_domain_ops ||
	    !s2_domain->nested_parent)
		return ERR_PTR(-EINVAL);

	ret = iommu_copy_struct_from_user(&vtd, user_data,
					  IOMMU_HWPT_DATA_VTD_S1, __reserved);
	if (ret)
		return ERR_PTR(ret);

	domain = kzalloc(sizeof(*domain), GFP_KERNEL_ACCOUNT);
	if (!domain)
		return ERR_PTR(-ENOMEM);

	domain->use_first_level = true;
	domain->s2_domain = s2_domain;
	domain->s1_pgtbl = vtd.pgtbl_addr;
	domain->s1_cfg = vtd;
	domain->domain.ops = &intel_nested_domain_ops;
	domain->domain.type = IOMMU_DOMAIN_NESTED;
	INIT_LIST_HEAD(&domain->devices);
	INIT_LIST_HEAD(&domain->dev_pasids);
	INIT_LIST_HEAD(&domain->cache_tags);
	spin_lock_init(&domain->lock);
	spin_lock_init(&domain->cache_lock);
	xa_init(&domain->iommu_array);

	spin_lock(&s2_domain->s1_lock);
	list_add(&domain->s2_link, &s2_domain->s1_domains);
	spin_unlock(&s2_domain->s1_lock);

	return &domain->domain;
}

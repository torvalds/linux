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
	ret = paging_domain_compatible(&dmar_domain->s2_domain->domain, dev);
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

	ret = iopf_for_domain_set(domain, dev);
	if (ret)
		goto unassign_tag;

	ret = intel_pasid_setup_nested(iommu, dev,
				       IOMMU_NO_PASID, dmar_domain);
	if (ret)
		goto disable_iopf;

	info->domain = dmar_domain;
	info->domain_attached = true;
	spin_lock_irqsave(&dmar_domain->lock, flags);
	list_add(&info->link, &dmar_domain->devices);
	spin_unlock_irqrestore(&dmar_domain->lock, flags);

	return 0;
disable_iopf:
	iopf_for_domain_remove(domain, dev);
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
	kfree(dmar_domain->qi_batch);
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

static int domain_setup_nested(struct intel_iommu *iommu,
			       struct dmar_domain *domain,
			       struct device *dev, ioasid_t pasid,
			       struct iommu_domain *old)
{
	if (!old)
		return intel_pasid_setup_nested(iommu, dev, pasid, domain);
	return intel_pasid_replace_nested(iommu, dev, pasid,
					  iommu_domain_did(old, iommu),
					  domain);
}

static int intel_nested_set_dev_pasid(struct iommu_domain *domain,
				      struct device *dev, ioasid_t pasid,
				      struct iommu_domain *old)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);
	struct dmar_domain *dmar_domain = to_dmar_domain(domain);
	struct intel_iommu *iommu = info->iommu;
	struct dev_pasid_info *dev_pasid;
	int ret;

	if (!pasid_supported(iommu) || dev_is_real_dma_subdevice(dev))
		return -EOPNOTSUPP;

	if (context_copied(iommu, info->bus, info->devfn))
		return -EBUSY;

	ret = paging_domain_compatible(&dmar_domain->s2_domain->domain, dev);
	if (ret)
		return ret;

	dev_pasid = domain_add_dev_pasid(domain, dev, pasid);
	if (IS_ERR(dev_pasid))
		return PTR_ERR(dev_pasid);

	ret = iopf_for_domain_replace(domain, old, dev);
	if (ret)
		goto out_remove_dev_pasid;

	ret = domain_setup_nested(iommu, dmar_domain, dev, pasid, old);
	if (ret)
		goto out_unwind_iopf;

	domain_remove_dev_pasid(old, dev, pasid);

	return 0;

out_unwind_iopf:
	iopf_for_domain_replace(old, domain, dev);
out_remove_dev_pasid:
	domain_remove_dev_pasid(domain, dev, pasid);
	return ret;
}

static const struct iommu_domain_ops intel_nested_domain_ops = {
	.attach_dev		= intel_nested_attach_dev,
	.set_dev_pasid		= intel_nested_set_dev_pasid,
	.free			= intel_nested_domain_free,
	.cache_invalidate_user	= intel_nested_cache_invalidate_user,
};

struct iommu_domain *
intel_iommu_domain_alloc_nested(struct device *dev, struct iommu_domain *parent,
				u32 flags,
				const struct iommu_user_data *user_data)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);
	struct dmar_domain *s2_domain = to_dmar_domain(parent);
	struct intel_iommu *iommu = info->iommu;
	struct iommu_hwpt_vtd_s1 vtd;
	struct dmar_domain *domain;
	int ret;

	if (!nested_supported(iommu) || flags & ~IOMMU_HWPT_ALLOC_PASID)
		return ERR_PTR(-EOPNOTSUPP);

	/* Must be nested domain */
	if (user_data->type != IOMMU_HWPT_DATA_VTD_S1)
		return ERR_PTR(-EOPNOTSUPP);
	if (!intel_domain_is_ss_paging(s2_domain) || !s2_domain->nested_parent)
		return ERR_PTR(-EINVAL);

	ret = iommu_copy_struct_from_user(&vtd, user_data,
					  IOMMU_HWPT_DATA_VTD_S1, __reserved);
	if (ret)
		return ERR_PTR(ret);

	domain = kzalloc(sizeof(*domain), GFP_KERNEL_ACCOUNT);
	if (!domain)
		return ERR_PTR(-ENOMEM);

	domain->s2_domain = s2_domain;
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

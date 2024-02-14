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

	ret = intel_pasid_setup_nested(iommu, dev,
				       IOMMU_NO_PASID, dmar_domain);
	if (ret) {
		domain_detach_iommu(dmar_domain, iommu);
		dev_err_ratelimited(dev, "Failed to setup pasid entry\n");
		return ret;
	}

	info->domain = dmar_domain;
	spin_lock_irqsave(&dmar_domain->lock, flags);
	list_add(&info->link, &dmar_domain->devices);
	spin_unlock_irqrestore(&dmar_domain->lock, flags);

	return 0;
}

static void intel_nested_domain_free(struct iommu_domain *domain)
{
	kfree(to_dmar_domain(domain));
}

static void nested_flush_dev_iotlb(struct dmar_domain *domain, u64 addr,
				   unsigned int mask)
{
	struct device_domain_info *info;
	unsigned long flags;
	u16 sid, qdep;

	spin_lock_irqsave(&domain->lock, flags);
	list_for_each_entry(info, &domain->devices, link) {
		if (!info->ats_enabled)
			continue;
		sid = info->bus << 8 | info->devfn;
		qdep = info->ats_qdep;
		qi_flush_dev_iotlb(info->iommu, sid, info->pfsid,
				   qdep, addr, mask);
		quirk_extra_dev_tlb_flush(info, addr, mask,
					  IOMMU_NO_PASID, qdep);
	}
	spin_unlock_irqrestore(&domain->lock, flags);
}

static void intel_nested_flush_cache(struct dmar_domain *domain, u64 addr,
				     unsigned long npages, bool ih)
{
	struct iommu_domain_info *info;
	unsigned int mask;
	unsigned long i;

	xa_for_each(&domain->iommu_array, i, info)
		qi_flush_piotlb(info->iommu,
				domain_id_iommu(domain, info->iommu),
				IOMMU_NO_PASID, addr, npages, ih);

	if (!domain->has_iotlb_device)
		return;

	if (npages == U64_MAX)
		mask = 64 - VTD_PAGE_SHIFT;
	else
		mask = ilog2(__roundup_pow_of_two(npages));

	nested_flush_dev_iotlb(domain, addr, mask);
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

		intel_nested_flush_cache(dmar_domain, inv_entry.addr,
					 inv_entry.npages,
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
	spin_lock_init(&domain->lock);
	xa_init(&domain->iommu_array);

	return &domain->domain;
}

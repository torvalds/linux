// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Advanced Micro Devices, Inc.
 */

#define pr_fmt(fmt)     "AMD-Vi: " fmt
#define dev_fmt(fmt)    pr_fmt(fmt)

#include <linux/iommu.h>
#include <linux/mm_types.h>

#include "amd_iommu.h"

static inline bool is_pasid_enabled(struct iommu_dev_data *dev_data)
{
	if (dev_data->pasid_enabled && dev_data->max_pasids &&
	    dev_data->gcr3_info.gcr3_tbl != NULL)
		return true;

	return false;
}

static inline bool is_pasid_valid(struct iommu_dev_data *dev_data,
				  ioasid_t pasid)
{
	if (pasid > 0 && pasid < dev_data->max_pasids)
		return true;

	return false;
}

static void remove_dev_pasid(struct pdom_dev_data *pdom_dev_data)
{
	/* Update GCR3 table and flush IOTLB */
	amd_iommu_clear_gcr3(pdom_dev_data->dev_data, pdom_dev_data->pasid);

	list_del(&pdom_dev_data->list);
	kfree(pdom_dev_data);
}

/* Clear PASID from device GCR3 table and remove pdom_dev_data from list */
static void remove_pdom_dev_pasid(struct protection_domain *pdom,
				  struct device *dev, ioasid_t pasid)
{
	struct pdom_dev_data *pdom_dev_data;
	struct iommu_dev_data *dev_data = dev_iommu_priv_get(dev);

	lockdep_assert_held(&pdom->lock);

	for_each_pdom_dev_data(pdom_dev_data, pdom) {
		if (pdom_dev_data->dev_data == dev_data &&
		    pdom_dev_data->pasid == pasid) {
			remove_dev_pasid(pdom_dev_data);
			break;
		}
	}
}

int iommu_sva_set_dev_pasid(struct iommu_domain *domain,
			    struct device *dev, ioasid_t pasid)
{
	struct pdom_dev_data *pdom_dev_data;
	struct protection_domain *sva_pdom = to_pdomain(domain);
	struct iommu_dev_data *dev_data = dev_iommu_priv_get(dev);
	unsigned long flags;
	int ret = -EINVAL;

	/* PASID zero is used for requests from the I/O device without PASID */
	if (!is_pasid_valid(dev_data, pasid))
		return ret;

	/* Make sure PASID is enabled */
	if (!is_pasid_enabled(dev_data))
		return ret;

	/* Add PASID to protection domain pasid list */
	pdom_dev_data = kzalloc(sizeof(*pdom_dev_data), GFP_KERNEL);
	if (pdom_dev_data == NULL)
		return ret;

	pdom_dev_data->pasid = pasid;
	pdom_dev_data->dev_data = dev_data;

	spin_lock_irqsave(&sva_pdom->lock, flags);

	/* Setup GCR3 table */
	ret = amd_iommu_set_gcr3(dev_data, pasid,
				 iommu_virt_to_phys(domain->mm->pgd));
	if (ret) {
		kfree(pdom_dev_data);
		goto out_unlock;
	}

	list_add(&pdom_dev_data->list, &sva_pdom->dev_data_list);

out_unlock:
	spin_unlock_irqrestore(&sva_pdom->lock, flags);
	return ret;
}

void amd_iommu_remove_dev_pasid(struct device *dev, ioasid_t pasid)
{
	struct protection_domain *sva_pdom;
	struct iommu_domain *domain;
	unsigned long flags;

	if (!is_pasid_valid(dev_iommu_priv_get(dev), pasid))
		return;

	/* Get protection domain */
	domain = iommu_get_domain_for_dev_pasid(dev, pasid, IOMMU_DOMAIN_SVA);
	if (!domain)
		return;
	sva_pdom = to_pdomain(domain);

	spin_lock_irqsave(&sva_pdom->lock, flags);

	/* Remove PASID from dev_data_list */
	remove_pdom_dev_pasid(sva_pdom, dev, pasid);

	spin_unlock_irqrestore(&sva_pdom->lock, flags);
}

// SPDX-License-Identifier: GPL-2.0
/*
 * cache.c - Intel VT-d cache invalidation
 *
 * Copyright (C) 2024 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 */

#define pr_fmt(fmt)	"DMAR: " fmt

#include <linux/dmar.h>
#include <linux/iommu.h>
#include <linux/memory.h>
#include <linux/spinlock.h>

#include "iommu.h"
#include "pasid.h"

/* Check if an existing cache tag can be reused for a new association. */
static bool cache_tage_match(struct cache_tag *tag, u16 domain_id,
			     struct intel_iommu *iommu, struct device *dev,
			     ioasid_t pasid, enum cache_tag_type type)
{
	if (tag->type != type)
		return false;

	if (tag->domain_id != domain_id || tag->pasid != pasid)
		return false;

	if (type == CACHE_TAG_IOTLB || type == CACHE_TAG_NESTING_IOTLB)
		return tag->iommu == iommu;

	if (type == CACHE_TAG_DEVTLB || type == CACHE_TAG_NESTING_DEVTLB)
		return tag->dev == dev;

	return false;
}

/* Assign a cache tag with specified type to domain. */
static int cache_tag_assign(struct dmar_domain *domain, u16 did,
			    struct device *dev, ioasid_t pasid,
			    enum cache_tag_type type)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);
	struct intel_iommu *iommu = info->iommu;
	struct cache_tag *tag, *temp;
	unsigned long flags;

	tag = kzalloc(sizeof(*tag), GFP_KERNEL);
	if (!tag)
		return -ENOMEM;

	tag->type = type;
	tag->iommu = iommu;
	tag->domain_id = did;
	tag->pasid = pasid;
	tag->users = 1;

	if (type == CACHE_TAG_DEVTLB || type == CACHE_TAG_NESTING_DEVTLB)
		tag->dev = dev;
	else
		tag->dev = iommu->iommu.dev;

	spin_lock_irqsave(&domain->cache_lock, flags);
	list_for_each_entry(temp, &domain->cache_tags, node) {
		if (cache_tage_match(temp, did, iommu, dev, pasid, type)) {
			temp->users++;
			spin_unlock_irqrestore(&domain->cache_lock, flags);
			kfree(tag);
			return 0;
		}
	}
	list_add_tail(&tag->node, &domain->cache_tags);
	spin_unlock_irqrestore(&domain->cache_lock, flags);

	return 0;
}

/* Unassign a cache tag with specified type from domain. */
static void cache_tag_unassign(struct dmar_domain *domain, u16 did,
			       struct device *dev, ioasid_t pasid,
			       enum cache_tag_type type)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);
	struct intel_iommu *iommu = info->iommu;
	struct cache_tag *tag;
	unsigned long flags;

	spin_lock_irqsave(&domain->cache_lock, flags);
	list_for_each_entry(tag, &domain->cache_tags, node) {
		if (cache_tage_match(tag, did, iommu, dev, pasid, type)) {
			if (--tag->users == 0) {
				list_del(&tag->node);
				kfree(tag);
			}
			break;
		}
	}
	spin_unlock_irqrestore(&domain->cache_lock, flags);
}

static int __cache_tag_assign_domain(struct dmar_domain *domain, u16 did,
				     struct device *dev, ioasid_t pasid)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);
	int ret;

	ret = cache_tag_assign(domain, did, dev, pasid, CACHE_TAG_IOTLB);
	if (ret || !info->ats_enabled)
		return ret;

	ret = cache_tag_assign(domain, did, dev, pasid, CACHE_TAG_DEVTLB);
	if (ret)
		cache_tag_unassign(domain, did, dev, pasid, CACHE_TAG_IOTLB);

	return ret;
}

static void __cache_tag_unassign_domain(struct dmar_domain *domain, u16 did,
					struct device *dev, ioasid_t pasid)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);

	cache_tag_unassign(domain, did, dev, pasid, CACHE_TAG_IOTLB);

	if (info->ats_enabled)
		cache_tag_unassign(domain, did, dev, pasid, CACHE_TAG_DEVTLB);
}

static int __cache_tag_assign_parent_domain(struct dmar_domain *domain, u16 did,
					    struct device *dev, ioasid_t pasid)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);
	int ret;

	ret = cache_tag_assign(domain, did, dev, pasid, CACHE_TAG_NESTING_IOTLB);
	if (ret || !info->ats_enabled)
		return ret;

	ret = cache_tag_assign(domain, did, dev, pasid, CACHE_TAG_NESTING_DEVTLB);
	if (ret)
		cache_tag_unassign(domain, did, dev, pasid, CACHE_TAG_NESTING_IOTLB);

	return ret;
}

static void __cache_tag_unassign_parent_domain(struct dmar_domain *domain, u16 did,
					       struct device *dev, ioasid_t pasid)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);

	cache_tag_unassign(domain, did, dev, pasid, CACHE_TAG_NESTING_IOTLB);

	if (info->ats_enabled)
		cache_tag_unassign(domain, did, dev, pasid, CACHE_TAG_NESTING_DEVTLB);
}

static u16 domain_get_id_for_dev(struct dmar_domain *domain, struct device *dev)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);
	struct intel_iommu *iommu = info->iommu;

	/*
	 * The driver assigns different domain IDs for all domains except
	 * the SVA type.
	 */
	if (domain->domain.type == IOMMU_DOMAIN_SVA)
		return FLPT_DEFAULT_DID;

	return domain_id_iommu(domain, iommu);
}

/*
 * Assign cache tags to a domain when it's associated with a device's
 * PASID using a specific domain ID.
 *
 * On success (return value of 0), cache tags are created and added to the
 * domain's cache tag list. On failure (negative return value), an error
 * code is returned indicating the reason for the failure.
 */
int cache_tag_assign_domain(struct dmar_domain *domain,
			    struct device *dev, ioasid_t pasid)
{
	u16 did = domain_get_id_for_dev(domain, dev);
	int ret;

	ret = __cache_tag_assign_domain(domain, did, dev, pasid);
	if (ret || domain->domain.type != IOMMU_DOMAIN_NESTED)
		return ret;

	ret = __cache_tag_assign_parent_domain(domain->s2_domain, did, dev, pasid);
	if (ret)
		__cache_tag_unassign_domain(domain, did, dev, pasid);

	return ret;
}

/*
 * Remove the cache tags associated with a device's PASID when the domain is
 * detached from the device.
 *
 * The cache tags must be previously assigned to the domain by calling the
 * assign interface.
 */
void cache_tag_unassign_domain(struct dmar_domain *domain,
			       struct device *dev, ioasid_t pasid)
{
	u16 did = domain_get_id_for_dev(domain, dev);

	__cache_tag_unassign_domain(domain, did, dev, pasid);
	if (domain->domain.type == IOMMU_DOMAIN_NESTED)
		__cache_tag_unassign_parent_domain(domain->s2_domain, did, dev, pasid);
}

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
#include <linux/pci.h>
#include <linux/spinlock.h>

#include "iommu.h"
#include "pasid.h"
#include "trace.h"

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
int cache_tag_assign(struct dmar_domain *domain, u16 did, struct device *dev,
		     ioasid_t pasid, enum cache_tag_type type)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);
	struct intel_iommu *iommu = info->iommu;
	struct cache_tag *tag, *temp;
	struct list_head *prev;
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
	prev = &domain->cache_tags;
	list_for_each_entry(temp, &domain->cache_tags, node) {
		if (cache_tage_match(temp, did, iommu, dev, pasid, type)) {
			temp->users++;
			spin_unlock_irqrestore(&domain->cache_lock, flags);
			kfree(tag);
			trace_cache_tag_assign(temp);
			return 0;
		}
		if (temp->iommu == iommu)
			prev = &temp->node;
	}
	/*
	 * Link cache tags of same iommu unit together, so corresponding
	 * flush ops can be batched for iommu unit.
	 */
	list_add(&tag->node, prev);

	spin_unlock_irqrestore(&domain->cache_lock, flags);
	trace_cache_tag_assign(tag);

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
			trace_cache_tag_unassign(tag);
			if (--tag->users == 0) {
				list_del(&tag->node);
				kfree(tag);
			}
			break;
		}
	}
	spin_unlock_irqrestore(&domain->cache_lock, flags);
}

/* domain->qi_batch will be freed in iommu_free_domain() path. */
static int domain_qi_batch_alloc(struct dmar_domain *domain)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&domain->cache_lock, flags);
	if (domain->qi_batch)
		goto out_unlock;

	domain->qi_batch = kzalloc(sizeof(*domain->qi_batch), GFP_ATOMIC);
	if (!domain->qi_batch)
		ret = -ENOMEM;
out_unlock:
	spin_unlock_irqrestore(&domain->cache_lock, flags);

	return ret;
}

static int __cache_tag_assign_domain(struct dmar_domain *domain, u16 did,
				     struct device *dev, ioasid_t pasid)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);
	int ret;

	ret = domain_qi_batch_alloc(domain);
	if (ret)
		return ret;

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

	ret = domain_qi_batch_alloc(domain);
	if (ret)
		return ret;

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

static unsigned long calculate_psi_aligned_address(unsigned long start,
						   unsigned long end,
						   unsigned long *_pages,
						   unsigned long *_mask)
{
	unsigned long pages = aligned_nrpages(start, end - start + 1);
	unsigned long aligned_pages = __roundup_pow_of_two(pages);
	unsigned long bitmask = aligned_pages - 1;
	unsigned long mask = ilog2(aligned_pages);
	unsigned long pfn = IOVA_PFN(start);

	/*
	 * PSI masks the low order bits of the base address. If the
	 * address isn't aligned to the mask, then compute a mask value
	 * needed to ensure the target range is flushed.
	 */
	if (unlikely(bitmask & pfn)) {
		unsigned long end_pfn = pfn + pages - 1, shared_bits;

		/*
		 * Since end_pfn <= pfn + bitmask, the only way bits
		 * higher than bitmask can differ in pfn and end_pfn is
		 * by carrying. This means after masking out bitmask,
		 * high bits starting with the first set bit in
		 * shared_bits are all equal in both pfn and end_pfn.
		 */
		shared_bits = ~(pfn ^ end_pfn) & ~bitmask;
		mask = shared_bits ? __ffs(shared_bits) : MAX_AGAW_PFN_WIDTH;
		aligned_pages = 1UL << mask;
	}

	*_pages = aligned_pages;
	*_mask = mask;

	return ALIGN_DOWN(start, VTD_PAGE_SIZE << mask);
}

static void qi_batch_flush_descs(struct intel_iommu *iommu, struct qi_batch *batch)
{
	if (!iommu || !batch->index)
		return;

	qi_submit_sync(iommu, batch->descs, batch->index, 0);

	/* Reset the index value and clean the whole batch buffer. */
	memset(batch, 0, sizeof(*batch));
}

static void qi_batch_increment_index(struct intel_iommu *iommu, struct qi_batch *batch)
{
	if (++batch->index == QI_MAX_BATCHED_DESC_COUNT)
		qi_batch_flush_descs(iommu, batch);
}

static void qi_batch_add_iotlb(struct intel_iommu *iommu, u16 did, u64 addr,
			       unsigned int size_order, u64 type,
			       struct qi_batch *batch)
{
	qi_desc_iotlb(iommu, did, addr, size_order, type, &batch->descs[batch->index]);
	qi_batch_increment_index(iommu, batch);
}

static void qi_batch_add_dev_iotlb(struct intel_iommu *iommu, u16 sid, u16 pfsid,
				   u16 qdep, u64 addr, unsigned int mask,
				   struct qi_batch *batch)
{
	/*
	 * According to VT-d spec, software is recommended to not submit any Device-TLB
	 * invalidation requests while address remapping hardware is disabled.
	 */
	if (!(iommu->gcmd & DMA_GCMD_TE))
		return;

	qi_desc_dev_iotlb(sid, pfsid, qdep, addr, mask, &batch->descs[batch->index]);
	qi_batch_increment_index(iommu, batch);
}

static void qi_batch_add_piotlb(struct intel_iommu *iommu, u16 did, u32 pasid,
				u64 addr, unsigned long npages, bool ih,
				struct qi_batch *batch)
{
	/*
	 * npages == -1 means a PASID-selective invalidation, otherwise,
	 * a positive value for Page-selective-within-PASID invalidation.
	 * 0 is not a valid input.
	 */
	if (!npages)
		return;

	qi_desc_piotlb(did, pasid, addr, npages, ih, &batch->descs[batch->index]);
	qi_batch_increment_index(iommu, batch);
}

static void qi_batch_add_pasid_dev_iotlb(struct intel_iommu *iommu, u16 sid, u16 pfsid,
					 u32 pasid,  u16 qdep, u64 addr,
					 unsigned int size_order, struct qi_batch *batch)
{
	/*
	 * According to VT-d spec, software is recommended to not submit any
	 * Device-TLB invalidation requests while address remapping hardware
	 * is disabled.
	 */
	if (!(iommu->gcmd & DMA_GCMD_TE))
		return;

	qi_desc_dev_iotlb_pasid(sid, pfsid, pasid, qdep, addr, size_order,
				&batch->descs[batch->index]);
	qi_batch_increment_index(iommu, batch);
}

static void cache_tag_flush_iotlb(struct dmar_domain *domain, struct cache_tag *tag,
				  unsigned long addr, unsigned long pages,
				  unsigned long mask, int ih)
{
	struct intel_iommu *iommu = tag->iommu;
	u64 type = DMA_TLB_PSI_FLUSH;

	if (domain->use_first_level) {
		qi_batch_add_piotlb(iommu, tag->domain_id, tag->pasid, addr,
				    pages, ih, domain->qi_batch);
		return;
	}

	/*
	 * Fallback to domain selective flush if no PSI support or the size
	 * is too big.
	 */
	if (!cap_pgsel_inv(iommu->cap) ||
	    mask > cap_max_amask_val(iommu->cap) || pages == -1) {
		addr = 0;
		mask = 0;
		ih = 0;
		type = DMA_TLB_DSI_FLUSH;
	}

	if (ecap_qis(iommu->ecap))
		qi_batch_add_iotlb(iommu, tag->domain_id, addr | ih, mask, type,
				   domain->qi_batch);
	else
		__iommu_flush_iotlb(iommu, tag->domain_id, addr | ih, mask, type);
}

static void cache_tag_flush_devtlb_psi(struct dmar_domain *domain, struct cache_tag *tag,
				       unsigned long addr, unsigned long mask)
{
	struct intel_iommu *iommu = tag->iommu;
	struct device_domain_info *info;
	u16 sid;

	info = dev_iommu_priv_get(tag->dev);
	sid = PCI_DEVID(info->bus, info->devfn);

	if (tag->pasid == IOMMU_NO_PASID) {
		qi_batch_add_dev_iotlb(iommu, sid, info->pfsid, info->ats_qdep,
				       addr, mask, domain->qi_batch);
		if (info->dtlb_extra_inval)
			qi_batch_add_dev_iotlb(iommu, sid, info->pfsid, info->ats_qdep,
					       addr, mask, domain->qi_batch);
		return;
	}

	qi_batch_add_pasid_dev_iotlb(iommu, sid, info->pfsid, tag->pasid,
				     info->ats_qdep, addr, mask, domain->qi_batch);
	if (info->dtlb_extra_inval)
		qi_batch_add_pasid_dev_iotlb(iommu, sid, info->pfsid, tag->pasid,
					     info->ats_qdep, addr, mask,
					     domain->qi_batch);
}

static void cache_tag_flush_devtlb_all(struct dmar_domain *domain, struct cache_tag *tag)
{
	struct intel_iommu *iommu = tag->iommu;
	struct device_domain_info *info;
	u16 sid;

	info = dev_iommu_priv_get(tag->dev);
	sid = PCI_DEVID(info->bus, info->devfn);

	qi_batch_add_dev_iotlb(iommu, sid, info->pfsid, info->ats_qdep, 0,
			       MAX_AGAW_PFN_WIDTH, domain->qi_batch);
	if (info->dtlb_extra_inval)
		qi_batch_add_dev_iotlb(iommu, sid, info->pfsid, info->ats_qdep, 0,
				       MAX_AGAW_PFN_WIDTH, domain->qi_batch);
}

/*
 * Invalidates a range of IOVA from @start (inclusive) to @end (inclusive)
 * when the memory mappings in the target domain have been modified.
 */
void cache_tag_flush_range(struct dmar_domain *domain, unsigned long start,
			   unsigned long end, int ih)
{
	struct intel_iommu *iommu = NULL;
	unsigned long pages, mask, addr;
	struct cache_tag *tag;
	unsigned long flags;

	addr = calculate_psi_aligned_address(start, end, &pages, &mask);

	spin_lock_irqsave(&domain->cache_lock, flags);
	list_for_each_entry(tag, &domain->cache_tags, node) {
		if (iommu && iommu != tag->iommu)
			qi_batch_flush_descs(iommu, domain->qi_batch);
		iommu = tag->iommu;

		switch (tag->type) {
		case CACHE_TAG_IOTLB:
		case CACHE_TAG_NESTING_IOTLB:
			cache_tag_flush_iotlb(domain, tag, addr, pages, mask, ih);
			break;
		case CACHE_TAG_NESTING_DEVTLB:
			/*
			 * Address translation cache in device side caches the
			 * result of nested translation. There is no easy way
			 * to identify the exact set of nested translations
			 * affected by a change in S2. So just flush the entire
			 * device cache.
			 */
			addr = 0;
			mask = MAX_AGAW_PFN_WIDTH;
			fallthrough;
		case CACHE_TAG_DEVTLB:
			cache_tag_flush_devtlb_psi(domain, tag, addr, mask);
			break;
		}

		trace_cache_tag_flush_range(tag, start, end, addr, pages, mask);
	}
	qi_batch_flush_descs(iommu, domain->qi_batch);
	spin_unlock_irqrestore(&domain->cache_lock, flags);
}

/*
 * Invalidates all ranges of IOVA when the memory mappings in the target
 * domain have been modified.
 */
void cache_tag_flush_all(struct dmar_domain *domain)
{
	struct intel_iommu *iommu = NULL;
	struct cache_tag *tag;
	unsigned long flags;

	spin_lock_irqsave(&domain->cache_lock, flags);
	list_for_each_entry(tag, &domain->cache_tags, node) {
		if (iommu && iommu != tag->iommu)
			qi_batch_flush_descs(iommu, domain->qi_batch);
		iommu = tag->iommu;

		switch (tag->type) {
		case CACHE_TAG_IOTLB:
		case CACHE_TAG_NESTING_IOTLB:
			cache_tag_flush_iotlb(domain, tag, 0, -1, 0, 0);
			break;
		case CACHE_TAG_DEVTLB:
		case CACHE_TAG_NESTING_DEVTLB:
			cache_tag_flush_devtlb_all(domain, tag);
			break;
		}

		trace_cache_tag_flush_all(tag);
	}
	qi_batch_flush_descs(iommu, domain->qi_batch);
	spin_unlock_irqrestore(&domain->cache_lock, flags);
}

/*
 * Invalidate a range of IOVA when new mappings are created in the target
 * domain.
 *
 * - VT-d spec, Section 6.1 Caching Mode: When the CM field is reported as
 *   Set, any software updates to remapping structures other than first-
 *   stage mapping requires explicit invalidation of the caches.
 * - VT-d spec, Section 6.8 Write Buffer Flushing: For hardware that requires
 *   write buffer flushing, software must explicitly perform write-buffer
 *   flushing, if cache invalidation is not required.
 */
void cache_tag_flush_range_np(struct dmar_domain *domain, unsigned long start,
			      unsigned long end)
{
	struct intel_iommu *iommu = NULL;
	unsigned long pages, mask, addr;
	struct cache_tag *tag;
	unsigned long flags;

	addr = calculate_psi_aligned_address(start, end, &pages, &mask);

	spin_lock_irqsave(&domain->cache_lock, flags);
	list_for_each_entry(tag, &domain->cache_tags, node) {
		if (iommu && iommu != tag->iommu)
			qi_batch_flush_descs(iommu, domain->qi_batch);
		iommu = tag->iommu;

		if (!cap_caching_mode(iommu->cap) || domain->use_first_level) {
			iommu_flush_write_buffer(iommu);
			continue;
		}

		if (tag->type == CACHE_TAG_IOTLB ||
		    tag->type == CACHE_TAG_NESTING_IOTLB)
			cache_tag_flush_iotlb(domain, tag, addr, pages, mask, 0);

		trace_cache_tag_flush_range_np(tag, start, end, addr, pages, mask);
	}
	qi_batch_flush_descs(iommu, domain->qi_batch);
	spin_unlock_irqrestore(&domain->cache_lock, flags);
}

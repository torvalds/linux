// SPDX-License-Identifier: GPL-2.0
/*
 * intel-pasid.c - PASID idr, table and entry manipulation
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 */

#define pr_fmt(fmt)	"DMAR: " fmt

#include <linux/bitops.h>
#include <linux/cpufeature.h>
#include <linux/dmar.h>
#include <linux/iommu.h>
#include <linux/memory.h>
#include <linux/pci.h>
#include <linux/pci-ats.h>
#include <linux/spinlock.h>

#include "iommu.h"
#include "pasid.h"

/*
 * Intel IOMMU system wide PASID name space:
 */
u32 intel_pasid_max_id = PASID_MAX;

/*
 * Per device pasid table management:
 */

/*
 * Allocate a pasid table for @dev. It should be called in a
 * single-thread context.
 */
int intel_pasid_alloc_table(struct device *dev)
{
	struct device_domain_info *info;
	struct pasid_table *pasid_table;
	struct page *pages;
	u32 max_pasid = 0;
	int order, size;

	might_sleep();
	info = dev_iommu_priv_get(dev);
	if (WARN_ON(!info || !dev_is_pci(dev)))
		return -ENODEV;
	if (WARN_ON(info->pasid_table))
		return -EEXIST;

	pasid_table = kzalloc(sizeof(*pasid_table), GFP_KERNEL);
	if (!pasid_table)
		return -ENOMEM;

	if (info->pasid_supported)
		max_pasid = min_t(u32, pci_max_pasids(to_pci_dev(dev)),
				  intel_pasid_max_id);

	size = max_pasid >> (PASID_PDE_SHIFT - 3);
	order = size ? get_order(size) : 0;
	pages = alloc_pages_node(info->iommu->node,
				 GFP_KERNEL | __GFP_ZERO, order);
	if (!pages) {
		kfree(pasid_table);
		return -ENOMEM;
	}

	pasid_table->table = page_address(pages);
	pasid_table->order = order;
	pasid_table->max_pasid = 1 << (order + PAGE_SHIFT + 3);
	info->pasid_table = pasid_table;

	if (!ecap_coherent(info->iommu->ecap))
		clflush_cache_range(pasid_table->table, (1 << order) * PAGE_SIZE);

	return 0;
}

void intel_pasid_free_table(struct device *dev)
{
	struct device_domain_info *info;
	struct pasid_table *pasid_table;
	struct pasid_dir_entry *dir;
	struct pasid_entry *table;
	int i, max_pde;

	info = dev_iommu_priv_get(dev);
	if (!info || !dev_is_pci(dev) || !info->pasid_table)
		return;

	pasid_table = info->pasid_table;
	info->pasid_table = NULL;

	/* Free scalable mode PASID directory tables: */
	dir = pasid_table->table;
	max_pde = pasid_table->max_pasid >> PASID_PDE_SHIFT;
	for (i = 0; i < max_pde; i++) {
		table = get_pasid_table_from_pde(&dir[i]);
		free_pgtable_page(table);
	}

	free_pages((unsigned long)pasid_table->table, pasid_table->order);
	kfree(pasid_table);
}

struct pasid_table *intel_pasid_get_table(struct device *dev)
{
	struct device_domain_info *info;

	info = dev_iommu_priv_get(dev);
	if (!info)
		return NULL;

	return info->pasid_table;
}

static int intel_pasid_get_dev_max_id(struct device *dev)
{
	struct device_domain_info *info;

	info = dev_iommu_priv_get(dev);
	if (!info || !info->pasid_table)
		return 0;

	return info->pasid_table->max_pasid;
}

static struct pasid_entry *intel_pasid_get_entry(struct device *dev, u32 pasid)
{
	struct device_domain_info *info;
	struct pasid_table *pasid_table;
	struct pasid_dir_entry *dir;
	struct pasid_entry *entries;
	int dir_index, index;

	pasid_table = intel_pasid_get_table(dev);
	if (WARN_ON(!pasid_table || pasid >= intel_pasid_get_dev_max_id(dev)))
		return NULL;

	dir = pasid_table->table;
	info = dev_iommu_priv_get(dev);
	dir_index = pasid >> PASID_PDE_SHIFT;
	index = pasid & PASID_PTE_MASK;

retry:
	entries = get_pasid_table_from_pde(&dir[dir_index]);
	if (!entries) {
		entries = alloc_pgtable_page(info->iommu->node, GFP_ATOMIC);
		if (!entries)
			return NULL;

		/*
		 * The pasid directory table entry won't be freed after
		 * allocation. No worry about the race with free and
		 * clear. However, this entry might be populated by others
		 * while we are preparing it. Use theirs with a retry.
		 */
		if (cmpxchg64(&dir[dir_index].val, 0ULL,
			      (u64)virt_to_phys(entries) | PASID_PTE_PRESENT)) {
			free_pgtable_page(entries);
			goto retry;
		}
		if (!ecap_coherent(info->iommu->ecap)) {
			clflush_cache_range(entries, VTD_PAGE_SIZE);
			clflush_cache_range(&dir[dir_index].val, sizeof(*dir));
		}
	}

	return &entries[index];
}

/*
 * Interfaces for PASID table entry manipulation:
 */
static void
intel_pasid_clear_entry(struct device *dev, u32 pasid, bool fault_ignore)
{
	struct pasid_entry *pe;

	pe = intel_pasid_get_entry(dev, pasid);
	if (WARN_ON(!pe))
		return;

	if (fault_ignore && pasid_pte_is_present(pe))
		pasid_clear_entry_with_fpd(pe);
	else
		pasid_clear_entry(pe);
}

static void
pasid_cache_invalidation_with_pasid(struct intel_iommu *iommu,
				    u16 did, u32 pasid)
{
	struct qi_desc desc;

	desc.qw0 = QI_PC_DID(did) | QI_PC_GRAN(QI_PC_PASID_SEL) |
		QI_PC_PASID(pasid) | QI_PC_TYPE;
	desc.qw1 = 0;
	desc.qw2 = 0;
	desc.qw3 = 0;

	qi_submit_sync(iommu, &desc, 1, 0);
}

static void
devtlb_invalidation_with_pasid(struct intel_iommu *iommu,
			       struct device *dev, u32 pasid)
{
	struct device_domain_info *info;
	u16 sid, qdep, pfsid;

	info = dev_iommu_priv_get(dev);
	if (!info || !info->ats_enabled)
		return;

	sid = info->bus << 8 | info->devfn;
	qdep = info->ats_qdep;
	pfsid = info->pfsid;

	/*
	 * When PASID 0 is used, it indicates RID2PASID(DMA request w/o PASID),
	 * devTLB flush w/o PASID should be used. For non-zero PASID under
	 * SVA usage, device could do DMA with multiple PASIDs. It is more
	 * efficient to flush devTLB specific to the PASID.
	 */
	if (pasid == IOMMU_NO_PASID)
		qi_flush_dev_iotlb(iommu, sid, pfsid, qdep, 0, 64 - VTD_PAGE_SHIFT);
	else
		qi_flush_dev_iotlb_pasid(iommu, sid, pfsid, pasid, qdep, 0, 64 - VTD_PAGE_SHIFT);
}

void intel_pasid_tear_down_entry(struct intel_iommu *iommu, struct device *dev,
				 u32 pasid, bool fault_ignore)
{
	struct pasid_entry *pte;
	u16 did, pgtt;

	spin_lock(&iommu->lock);
	pte = intel_pasid_get_entry(dev, pasid);
	if (WARN_ON(!pte) || !pasid_pte_is_present(pte)) {
		spin_unlock(&iommu->lock);
		return;
	}

	did = pasid_get_domain_id(pte);
	pgtt = pasid_pte_get_pgtt(pte);
	intel_pasid_clear_entry(dev, pasid, fault_ignore);
	spin_unlock(&iommu->lock);

	if (!ecap_coherent(iommu->ecap))
		clflush_cache_range(pte, sizeof(*pte));

	pasid_cache_invalidation_with_pasid(iommu, did, pasid);

	if (pgtt == PASID_ENTRY_PGTT_PT || pgtt == PASID_ENTRY_PGTT_FL_ONLY)
		qi_flush_piotlb(iommu, did, pasid, 0, -1, 0);
	else
		iommu->flush.flush_iotlb(iommu, did, 0, 0, DMA_TLB_DSI_FLUSH);

	/* Device IOTLB doesn't need to be flushed in caching mode. */
	if (!cap_caching_mode(iommu->cap))
		devtlb_invalidation_with_pasid(iommu, dev, pasid);
}

/*
 * This function flushes cache for a newly setup pasid table entry.
 * Caller of it should not modify the in-use pasid table entries.
 */
static void pasid_flush_caches(struct intel_iommu *iommu,
				struct pasid_entry *pte,
			       u32 pasid, u16 did)
{
	if (!ecap_coherent(iommu->ecap))
		clflush_cache_range(pte, sizeof(*pte));

	if (cap_caching_mode(iommu->cap)) {
		pasid_cache_invalidation_with_pasid(iommu, did, pasid);
		qi_flush_piotlb(iommu, did, pasid, 0, -1, 0);
	} else {
		iommu_flush_write_buffer(iommu);
	}
}

/*
 * Set up the scalable mode pasid table entry for first only
 * translation type.
 */
int intel_pasid_setup_first_level(struct intel_iommu *iommu,
				  struct device *dev, pgd_t *pgd,
				  u32 pasid, u16 did, int flags)
{
	struct pasid_entry *pte;

	if (!ecap_flts(iommu->ecap)) {
		pr_err("No first level translation support on %s\n",
		       iommu->name);
		return -EINVAL;
	}

	if ((flags & PASID_FLAG_FL5LP) && !cap_fl5lp_support(iommu->cap)) {
		pr_err("No 5-level paging support for first-level on %s\n",
		       iommu->name);
		return -EINVAL;
	}

	spin_lock(&iommu->lock);
	pte = intel_pasid_get_entry(dev, pasid);
	if (!pte) {
		spin_unlock(&iommu->lock);
		return -ENODEV;
	}

	if (pasid_pte_is_present(pte)) {
		spin_unlock(&iommu->lock);
		return -EBUSY;
	}

	pasid_clear_entry(pte);

	/* Setup the first level page table pointer: */
	pasid_set_flptr(pte, (u64)__pa(pgd));

	if (flags & PASID_FLAG_FL5LP)
		pasid_set_flpm(pte, 1);

	if (flags & PASID_FLAG_PAGE_SNOOP)
		pasid_set_pgsnp(pte);

	pasid_set_domain_id(pte, did);
	pasid_set_address_width(pte, iommu->agaw);
	pasid_set_page_snoop(pte, !!ecap_smpwc(iommu->ecap));
	pasid_set_nxe(pte);

	/* Setup Present and PASID Granular Transfer Type: */
	pasid_set_translation_type(pte, PASID_ENTRY_PGTT_FL_ONLY);
	pasid_set_present(pte);
	spin_unlock(&iommu->lock);

	pasid_flush_caches(iommu, pte, pasid, did);

	return 0;
}

/*
 * Skip top levels of page tables for iommu which has less agaw
 * than default. Unnecessary for PT mode.
 */
static int iommu_skip_agaw(struct dmar_domain *domain,
			   struct intel_iommu *iommu,
			   struct dma_pte **pgd)
{
	int agaw;

	for (agaw = domain->agaw; agaw > iommu->agaw; agaw--) {
		*pgd = phys_to_virt(dma_pte_addr(*pgd));
		if (!dma_pte_present(*pgd))
			return -EINVAL;
	}

	return agaw;
}

/*
 * Set up the scalable mode pasid entry for second only translation type.
 */
int intel_pasid_setup_second_level(struct intel_iommu *iommu,
				   struct dmar_domain *domain,
				   struct device *dev, u32 pasid)
{
	struct pasid_entry *pte;
	struct dma_pte *pgd;
	u64 pgd_val;
	int agaw;
	u16 did;

	/*
	 * If hardware advertises no support for second level
	 * translation, return directly.
	 */
	if (!ecap_slts(iommu->ecap)) {
		pr_err("No second level translation support on %s\n",
		       iommu->name);
		return -EINVAL;
	}

	pgd = domain->pgd;
	agaw = iommu_skip_agaw(domain, iommu, &pgd);
	if (agaw < 0) {
		dev_err(dev, "Invalid domain page table\n");
		return -EINVAL;
	}

	pgd_val = virt_to_phys(pgd);
	did = domain_id_iommu(domain, iommu);

	spin_lock(&iommu->lock);
	pte = intel_pasid_get_entry(dev, pasid);
	if (!pte) {
		spin_unlock(&iommu->lock);
		return -ENODEV;
	}

	if (pasid_pte_is_present(pte)) {
		spin_unlock(&iommu->lock);
		return -EBUSY;
	}

	pasid_clear_entry(pte);
	pasid_set_domain_id(pte, did);
	pasid_set_slptr(pte, pgd_val);
	pasid_set_address_width(pte, agaw);
	pasid_set_translation_type(pte, PASID_ENTRY_PGTT_SL_ONLY);
	pasid_set_fault_enable(pte);
	pasid_set_page_snoop(pte, !!ecap_smpwc(iommu->ecap));
	if (domain->dirty_tracking)
		pasid_set_ssade(pte);

	pasid_set_present(pte);
	spin_unlock(&iommu->lock);

	pasid_flush_caches(iommu, pte, pasid, did);

	return 0;
}

/*
 * Set up dirty tracking on a second only or nested translation type.
 */
int intel_pasid_setup_dirty_tracking(struct intel_iommu *iommu,
				     struct device *dev, u32 pasid,
				     bool enabled)
{
	struct pasid_entry *pte;
	u16 did, pgtt;

	spin_lock(&iommu->lock);

	pte = intel_pasid_get_entry(dev, pasid);
	if (!pte) {
		spin_unlock(&iommu->lock);
		dev_err_ratelimited(
			dev, "Failed to get pasid entry of PASID %d\n", pasid);
		return -ENODEV;
	}

	did = pasid_get_domain_id(pte);
	pgtt = pasid_pte_get_pgtt(pte);
	if (pgtt != PASID_ENTRY_PGTT_SL_ONLY &&
	    pgtt != PASID_ENTRY_PGTT_NESTED) {
		spin_unlock(&iommu->lock);
		dev_err_ratelimited(
			dev,
			"Dirty tracking not supported on translation type %d\n",
			pgtt);
		return -EOPNOTSUPP;
	}

	if (pasid_get_ssade(pte) == enabled) {
		spin_unlock(&iommu->lock);
		return 0;
	}

	if (enabled)
		pasid_set_ssade(pte);
	else
		pasid_clear_ssade(pte);
	spin_unlock(&iommu->lock);

	if (!ecap_coherent(iommu->ecap))
		clflush_cache_range(pte, sizeof(*pte));

	/*
	 * From VT-d spec table 25 "Guidance to Software for Invalidations":
	 *
	 * - PASID-selective-within-Domain PASID-cache invalidation
	 *   If (PGTT=SS or Nested)
	 *    - Domain-selective IOTLB invalidation
	 *   Else
	 *    - PASID-selective PASID-based IOTLB invalidation
	 * - If (pasid is RID_PASID)
	 *    - Global Device-TLB invalidation to affected functions
	 *   Else
	 *    - PASID-based Device-TLB invalidation (with S=1 and
	 *      Addr[63:12]=0x7FFFFFFF_FFFFF) to affected functions
	 */
	pasid_cache_invalidation_with_pasid(iommu, did, pasid);

	iommu->flush.flush_iotlb(iommu, did, 0, 0, DMA_TLB_DSI_FLUSH);

	/* Device IOTLB doesn't need to be flushed in caching mode. */
	if (!cap_caching_mode(iommu->cap))
		devtlb_invalidation_with_pasid(iommu, dev, pasid);

	return 0;
}

/*
 * Set up the scalable mode pasid entry for passthrough translation type.
 */
int intel_pasid_setup_pass_through(struct intel_iommu *iommu,
				   struct device *dev, u32 pasid)
{
	u16 did = FLPT_DEFAULT_DID;
	struct pasid_entry *pte;

	spin_lock(&iommu->lock);
	pte = intel_pasid_get_entry(dev, pasid);
	if (!pte) {
		spin_unlock(&iommu->lock);
		return -ENODEV;
	}

	if (pasid_pte_is_present(pte)) {
		spin_unlock(&iommu->lock);
		return -EBUSY;
	}

	pasid_clear_entry(pte);
	pasid_set_domain_id(pte, did);
	pasid_set_address_width(pte, iommu->agaw);
	pasid_set_translation_type(pte, PASID_ENTRY_PGTT_PT);
	pasid_set_fault_enable(pte);
	pasid_set_page_snoop(pte, !!ecap_smpwc(iommu->ecap));
	pasid_set_present(pte);
	spin_unlock(&iommu->lock);

	pasid_flush_caches(iommu, pte, pasid, did);

	return 0;
}

/*
 * Set the page snoop control for a pasid entry which has been set up.
 */
void intel_pasid_setup_page_snoop_control(struct intel_iommu *iommu,
					  struct device *dev, u32 pasid)
{
	struct pasid_entry *pte;
	u16 did;

	spin_lock(&iommu->lock);
	pte = intel_pasid_get_entry(dev, pasid);
	if (WARN_ON(!pte || !pasid_pte_is_present(pte))) {
		spin_unlock(&iommu->lock);
		return;
	}

	pasid_set_pgsnp(pte);
	did = pasid_get_domain_id(pte);
	spin_unlock(&iommu->lock);

	if (!ecap_coherent(iommu->ecap))
		clflush_cache_range(pte, sizeof(*pte));

	/*
	 * VT-d spec 3.4 table23 states guides for cache invalidation:
	 *
	 * - PASID-selective-within-Domain PASID-cache invalidation
	 * - PASID-selective PASID-based IOTLB invalidation
	 * - If (pasid is RID_PASID)
	 *    - Global Device-TLB invalidation to affected functions
	 *   Else
	 *    - PASID-based Device-TLB invalidation (with S=1 and
	 *      Addr[63:12]=0x7FFFFFFF_FFFFF) to affected functions
	 */
	pasid_cache_invalidation_with_pasid(iommu, did, pasid);
	qi_flush_piotlb(iommu, did, pasid, 0, -1, 0);

	/* Device IOTLB doesn't need to be flushed in caching mode. */
	if (!cap_caching_mode(iommu->cap))
		devtlb_invalidation_with_pasid(iommu, dev, pasid);
}

/**
 * intel_pasid_setup_nested() - Set up PASID entry for nested translation.
 * @iommu:      IOMMU which the device belong to
 * @dev:        Device to be set up for translation
 * @pasid:      PASID to be programmed in the device PASID table
 * @domain:     User stage-1 domain nested on a stage-2 domain
 *
 * This is used for nested translation. The input domain should be
 * nested type and nested on a parent with 'is_nested_parent' flag
 * set.
 */
int intel_pasid_setup_nested(struct intel_iommu *iommu, struct device *dev,
			     u32 pasid, struct dmar_domain *domain)
{
	struct iommu_hwpt_vtd_s1 *s1_cfg = &domain->s1_cfg;
	pgd_t *s1_gpgd = (pgd_t *)(uintptr_t)domain->s1_pgtbl;
	struct dmar_domain *s2_domain = domain->s2_domain;
	u16 did = domain_id_iommu(domain, iommu);
	struct dma_pte *pgd = s2_domain->pgd;
	struct pasid_entry *pte;

	/* Address width should match the address width supported by hardware */
	switch (s1_cfg->addr_width) {
	case ADDR_WIDTH_4LEVEL:
		break;
	case ADDR_WIDTH_5LEVEL:
		if (!cap_fl5lp_support(iommu->cap)) {
			dev_err_ratelimited(dev,
					    "5-level paging not supported\n");
			return -EINVAL;
		}
		break;
	default:
		dev_err_ratelimited(dev, "Invalid stage-1 address width %d\n",
				    s1_cfg->addr_width);
		return -EINVAL;
	}

	if ((s1_cfg->flags & IOMMU_VTD_S1_SRE) && !ecap_srs(iommu->ecap)) {
		pr_err_ratelimited("No supervisor request support on %s\n",
				   iommu->name);
		return -EINVAL;
	}

	if ((s1_cfg->flags & IOMMU_VTD_S1_EAFE) && !ecap_eafs(iommu->ecap)) {
		pr_err_ratelimited("No extended access flag support on %s\n",
				   iommu->name);
		return -EINVAL;
	}

	spin_lock(&iommu->lock);
	pte = intel_pasid_get_entry(dev, pasid);
	if (!pte) {
		spin_unlock(&iommu->lock);
		return -ENODEV;
	}
	if (pasid_pte_is_present(pte)) {
		spin_unlock(&iommu->lock);
		return -EBUSY;
	}

	pasid_clear_entry(pte);

	if (s1_cfg->addr_width == ADDR_WIDTH_5LEVEL)
		pasid_set_flpm(pte, 1);

	pasid_set_flptr(pte, (uintptr_t)s1_gpgd);

	if (s1_cfg->flags & IOMMU_VTD_S1_SRE) {
		pasid_set_sre(pte);
		if (s1_cfg->flags & IOMMU_VTD_S1_WPE)
			pasid_set_wpe(pte);
	}

	if (s1_cfg->flags & IOMMU_VTD_S1_EAFE)
		pasid_set_eafe(pte);

	if (s2_domain->force_snooping)
		pasid_set_pgsnp(pte);

	pasid_set_slptr(pte, virt_to_phys(pgd));
	pasid_set_fault_enable(pte);
	pasid_set_domain_id(pte, did);
	pasid_set_address_width(pte, s2_domain->agaw);
	pasid_set_page_snoop(pte, !!ecap_smpwc(iommu->ecap));
	if (s2_domain->dirty_tracking)
		pasid_set_ssade(pte);
	pasid_set_translation_type(pte, PASID_ENTRY_PGTT_NESTED);
	pasid_set_present(pte);
	spin_unlock(&iommu->lock);

	pasid_flush_caches(iommu, pte, pasid, did);

	return 0;
}

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
#include "../iommu-pages.h"

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
	struct pasid_dir_entry *dir;
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
	dir = iommu_alloc_pages_node(info->iommu->node, GFP_KERNEL, order);
	if (!dir) {
		kfree(pasid_table);
		return -ENOMEM;
	}

	pasid_table->table = dir;
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
		iommu_free_page(table);
	}

	iommu_free_pages(pasid_table->table, pasid_table->order);
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
		u64 tmp;

		entries = iommu_alloc_page_node(info->iommu->node, GFP_ATOMIC);
		if (!entries)
			return NULL;

		/*
		 * The pasid directory table entry won't be freed after
		 * allocation. No worry about the race with free and
		 * clear. However, this entry might be populated by others
		 * while we are preparing it. Use theirs with a retry.
		 */
		tmp = 0ULL;
		if (!try_cmpxchg64(&dir[dir_index].val, &tmp,
				   (u64)virt_to_phys(entries) | PASID_PTE_PRESENT)) {
			iommu_free_page(entries);
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

	if (pci_dev_is_disconnected(to_pci_dev(dev)))
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

/*
 * Interfaces to setup or teardown a pasid table to the scalable-mode
 * context table entry:
 */

static void device_pasid_table_teardown(struct device *dev, u8 bus, u8 devfn)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);
	struct intel_iommu *iommu = info->iommu;
	struct context_entry *context;
	u16 did;

	spin_lock(&iommu->lock);
	context = iommu_context_addr(iommu, bus, devfn, false);
	if (!context) {
		spin_unlock(&iommu->lock);
		return;
	}

	did = context_domain_id(context);
	context_clear_entry(context);
	__iommu_flush_cache(iommu, context, sizeof(*context));
	spin_unlock(&iommu->lock);
	intel_context_flush_present(info, context, did, false);
}

static int pci_pasid_table_teardown(struct pci_dev *pdev, u16 alias, void *data)
{
	struct device *dev = data;

	if (dev == &pdev->dev)
		device_pasid_table_teardown(dev, PCI_BUS_NUM(alias), alias & 0xff);

	return 0;
}

void intel_pasid_teardown_sm_context(struct device *dev)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);

	if (!dev_is_pci(dev)) {
		device_pasid_table_teardown(dev, info->bus, info->devfn);
		return;
	}

	pci_for_each_dma_alias(to_pci_dev(dev), pci_pasid_table_teardown, dev);
}

/*
 * Get the PASID directory size for scalable mode context entry.
 * Value of X in the PDTS field of a scalable mode context entry
 * indicates PASID directory with 2^(X + 7) entries.
 */
static unsigned long context_get_sm_pds(struct pasid_table *table)
{
	unsigned long pds, max_pde;

	max_pde = table->max_pasid >> PASID_PDE_SHIFT;
	pds = find_first_bit(&max_pde, MAX_NR_PASID_BITS);
	if (pds < 7)
		return 0;

	return pds - 7;
}

static int context_entry_set_pasid_table(struct context_entry *context,
					 struct device *dev)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);
	struct pasid_table *table = info->pasid_table;
	struct intel_iommu *iommu = info->iommu;
	unsigned long pds;

	context_clear_entry(context);

	pds = context_get_sm_pds(table);
	context->lo = (u64)virt_to_phys(table->table) | context_pdts(pds);
	context_set_sm_rid2pasid(context, IOMMU_NO_PASID);

	if (info->ats_supported)
		context_set_sm_dte(context);
	if (info->pasid_supported)
		context_set_pasid(context);

	context_set_fault_enable(context);
	context_set_present(context);
	__iommu_flush_cache(iommu, context, sizeof(*context));

	return 0;
}

static int device_pasid_table_setup(struct device *dev, u8 bus, u8 devfn)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);
	struct intel_iommu *iommu = info->iommu;
	struct context_entry *context;

	spin_lock(&iommu->lock);
	context = iommu_context_addr(iommu, bus, devfn, true);
	if (!context) {
		spin_unlock(&iommu->lock);
		return -ENOMEM;
	}

	if (context_present(context) && !context_copied(iommu, bus, devfn)) {
		spin_unlock(&iommu->lock);
		return 0;
	}

	if (context_copied(iommu, bus, devfn)) {
		context_clear_entry(context);
		__iommu_flush_cache(iommu, context, sizeof(*context));

		/*
		 * For kdump cases, old valid entries may be cached due to
		 * the in-flight DMA and copied pgtable, but there is no
		 * unmapping behaviour for them, thus we need explicit cache
		 * flushes for all affected domain IDs and PASIDs used in
		 * the copied PASID table. Given that we have no idea about
		 * which domain IDs and PASIDs were used in the copied tables,
		 * upgrade them to global PASID and IOTLB cache invalidation.
		 */
		iommu->flush.flush_context(iommu, 0,
					   PCI_DEVID(bus, devfn),
					   DMA_CCMD_MASK_NOBIT,
					   DMA_CCMD_DEVICE_INVL);
		qi_flush_pasid_cache(iommu, 0, QI_PC_GLOBAL, 0);
		iommu->flush.flush_iotlb(iommu, 0, 0, 0, DMA_TLB_GLOBAL_FLUSH);
		devtlb_invalidation_with_pasid(iommu, dev, IOMMU_NO_PASID);

		/*
		 * At this point, the device is supposed to finish reset at
		 * its driver probe stage, so no in-flight DMA will exist,
		 * and we don't need to worry anymore hereafter.
		 */
		clear_context_copied(iommu, bus, devfn);
	}

	context_entry_set_pasid_table(context, dev);
	spin_unlock(&iommu->lock);

	/*
	 * It's a non-present to present mapping. If hardware doesn't cache
	 * non-present entry we don't need to flush the caches. If it does
	 * cache non-present entries, then it does so in the special
	 * domain #0, which we have to flush:
	 */
	if (cap_caching_mode(iommu->cap)) {
		iommu->flush.flush_context(iommu, 0,
					   PCI_DEVID(bus, devfn),
					   DMA_CCMD_MASK_NOBIT,
					   DMA_CCMD_DEVICE_INVL);
		iommu->flush.flush_iotlb(iommu, 0, 0, 0, DMA_TLB_DSI_FLUSH);
	}

	return 0;
}

static int pci_pasid_table_setup(struct pci_dev *pdev, u16 alias, void *data)
{
	struct device *dev = data;

	if (dev != &pdev->dev)
		return 0;

	return device_pasid_table_setup(dev, PCI_BUS_NUM(alias), alias & 0xff);
}

/*
 * Set the device's PASID table to its context table entry.
 *
 * The PASID table is set to the context entries of both device itself
 * and its alias requester ID for DMA.
 */
int intel_pasid_setup_sm_context(struct device *dev)
{
	struct device_domain_info *info = dev_iommu_priv_get(dev);

	if (!dev_is_pci(dev))
		return device_pasid_table_setup(dev, info->bus, info->devfn);

	return pci_for_each_dma_alias(to_pci_dev(dev), pci_pasid_table_setup, dev);
}

/*
 * Global Device-TLB invalidation following changes in a context entry which
 * was present.
 */
static void __context_flush_dev_iotlb(struct device_domain_info *info)
{
	if (!info->ats_enabled)
		return;

	qi_flush_dev_iotlb(info->iommu, PCI_DEVID(info->bus, info->devfn),
			   info->pfsid, info->ats_qdep, 0, MAX_AGAW_PFN_WIDTH);

	/*
	 * There is no guarantee that the device DMA is stopped when it reaches
	 * here. Therefore, always attempt the extra device TLB invalidation
	 * quirk. The impact on performance is acceptable since this is not a
	 * performance-critical path.
	 */
	quirk_extra_dev_tlb_flush(info, 0, MAX_AGAW_PFN_WIDTH, IOMMU_NO_PASID,
				  info->ats_qdep);
}

/*
 * Cache invalidations after change in a context table entry that was present
 * according to the Spec 6.5.3.3 (Guidance to Software for Invalidations). If
 * IOMMU is in scalable mode and all PASID table entries of the device were
 * non-present, set flush_domains to false. Otherwise, true.
 */
void intel_context_flush_present(struct device_domain_info *info,
				 struct context_entry *context,
				 u16 did, bool flush_domains)
{
	struct intel_iommu *iommu = info->iommu;
	struct pasid_entry *pte;
	int i;

	/*
	 * Device-selective context-cache invalidation. The Domain-ID field
	 * of the Context-cache Invalidate Descriptor is ignored by hardware
	 * when operating in scalable mode. Therefore the @did value doesn't
	 * matter in scalable mode.
	 */
	iommu->flush.flush_context(iommu, did, PCI_DEVID(info->bus, info->devfn),
				   DMA_CCMD_MASK_NOBIT, DMA_CCMD_DEVICE_INVL);

	/*
	 * For legacy mode:
	 * - Domain-selective IOTLB invalidation
	 * - Global Device-TLB invalidation to all affected functions
	 */
	if (!sm_supported(iommu)) {
		iommu->flush.flush_iotlb(iommu, did, 0, 0, DMA_TLB_DSI_FLUSH);
		__context_flush_dev_iotlb(info);

		return;
	}

	/*
	 * For scalable mode:
	 * - Domain-selective PASID-cache invalidation to affected domains
	 * - Domain-selective IOTLB invalidation to affected domains
	 * - Global Device-TLB invalidation to affected functions
	 */
	if (flush_domains) {
		/*
		 * If the IOMMU is running in scalable mode and there might
		 * be potential PASID translations, the caller should hold
		 * the lock to ensure that context changes and cache flushes
		 * are atomic.
		 */
		assert_spin_locked(&iommu->lock);
		for (i = 0; i < info->pasid_table->max_pasid; i++) {
			pte = intel_pasid_get_entry(info->dev, i);
			if (!pte || !pasid_pte_is_present(pte))
				continue;

			did = pasid_get_domain_id(pte);
			qi_flush_pasid_cache(iommu, did, QI_PC_ALL_PASIDS, 0);
			iommu->flush.flush_iotlb(iommu, did, 0, 0, DMA_TLB_DSI_FLUSH);
		}
	}

	__context_flush_dev_iotlb(info);
}

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
#include <linux/intel-iommu.h>
#include <linux/iommu.h>
#include <linux/memory.h>
#include <linux/pci.h>
#include <linux/pci-ats.h>
#include <linux/spinlock.h>

#include "pasid.h"

/*
 * Intel IOMMU system wide PASID name space:
 */
u32 intel_pasid_max_id = PASID_MAX;

int vcmd_alloc_pasid(struct intel_iommu *iommu, u32 *pasid)
{
	unsigned long flags;
	u8 status_code;
	int ret = 0;
	u64 res;

	raw_spin_lock_irqsave(&iommu->register_lock, flags);
	dmar_writeq(iommu->reg + DMAR_VCMD_REG, VCMD_CMD_ALLOC);
	IOMMU_WAIT_OP(iommu, DMAR_VCRSP_REG, dmar_readq,
		      !(res & VCMD_VRSP_IP), res);
	raw_spin_unlock_irqrestore(&iommu->register_lock, flags);

	status_code = VCMD_VRSP_SC(res);
	switch (status_code) {
	case VCMD_VRSP_SC_SUCCESS:
		*pasid = VCMD_VRSP_RESULT_PASID(res);
		break;
	case VCMD_VRSP_SC_NO_PASID_AVAIL:
		pr_info("IOMMU: %s: No PASID available\n", iommu->name);
		ret = -ENOSPC;
		break;
	default:
		ret = -ENODEV;
		pr_warn("IOMMU: %s: Unexpected error code %d\n",
			iommu->name, status_code);
	}

	return ret;
}

void vcmd_free_pasid(struct intel_iommu *iommu, u32 pasid)
{
	unsigned long flags;
	u8 status_code;
	u64 res;

	raw_spin_lock_irqsave(&iommu->register_lock, flags);
	dmar_writeq(iommu->reg + DMAR_VCMD_REG,
		    VCMD_CMD_OPERAND(pasid) | VCMD_CMD_FREE);
	IOMMU_WAIT_OP(iommu, DMAR_VCRSP_REG, dmar_readq,
		      !(res & VCMD_VRSP_IP), res);
	raw_spin_unlock_irqrestore(&iommu->register_lock, flags);

	status_code = VCMD_VRSP_SC(res);
	switch (status_code) {
	case VCMD_VRSP_SC_SUCCESS:
		break;
	case VCMD_VRSP_SC_INVALID_PASID:
		pr_info("IOMMU: %s: Invalid PASID\n", iommu->name);
		break;
	default:
		pr_warn("IOMMU: %s: Unexpected error code %d\n",
			iommu->name, status_code);
	}
}

/*
 * Per device pasid table management:
 */
static inline void
device_attach_pasid_table(struct device_domain_info *info,
			  struct pasid_table *pasid_table)
{
	info->pasid_table = pasid_table;
	list_add(&info->table, &pasid_table->dev);
}

static inline void
device_detach_pasid_table(struct device_domain_info *info,
			  struct pasid_table *pasid_table)
{
	info->pasid_table = NULL;
	list_del(&info->table);
}

struct pasid_table_opaque {
	struct pasid_table	**pasid_table;
	int			segment;
	int			bus;
	int			devfn;
};

static int search_pasid_table(struct device_domain_info *info, void *opaque)
{
	struct pasid_table_opaque *data = opaque;

	if (info->iommu->segment == data->segment &&
	    info->bus == data->bus &&
	    info->devfn == data->devfn &&
	    info->pasid_table) {
		*data->pasid_table = info->pasid_table;
		return 1;
	}

	return 0;
}

static int get_alias_pasid_table(struct pci_dev *pdev, u16 alias, void *opaque)
{
	struct pasid_table_opaque *data = opaque;

	data->segment = pci_domain_nr(pdev->bus);
	data->bus = PCI_BUS_NUM(alias);
	data->devfn = alias & 0xff;

	return for_each_device_domain(&search_pasid_table, data);
}

/*
 * Allocate a pasid table for @dev. It should be called in a
 * single-thread context.
 */
int intel_pasid_alloc_table(struct device *dev)
{
	struct device_domain_info *info;
	struct pasid_table *pasid_table;
	struct pasid_table_opaque data;
	struct page *pages;
	u32 max_pasid = 0;
	int ret, order;
	int size;

	might_sleep();
	info = get_domain_info(dev);
	if (WARN_ON(!info || !dev_is_pci(dev) || info->pasid_table))
		return -EINVAL;

	/* DMA alias device already has a pasid table, use it: */
	data.pasid_table = &pasid_table;
	ret = pci_for_each_dma_alias(to_pci_dev(dev),
				     &get_alias_pasid_table, &data);
	if (ret)
		goto attach_out;

	pasid_table = kzalloc(sizeof(*pasid_table), GFP_KERNEL);
	if (!pasid_table)
		return -ENOMEM;
	INIT_LIST_HEAD(&pasid_table->dev);

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

attach_out:
	device_attach_pasid_table(info, pasid_table);

	return 0;
}

void intel_pasid_free_table(struct device *dev)
{
	struct device_domain_info *info;
	struct pasid_table *pasid_table;
	struct pasid_dir_entry *dir;
	struct pasid_entry *table;
	int i, max_pde;

	info = get_domain_info(dev);
	if (!info || !dev_is_pci(dev) || !info->pasid_table)
		return;

	pasid_table = info->pasid_table;
	device_detach_pasid_table(info, pasid_table);

	if (!list_empty(&pasid_table->dev))
		return;

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

	info = get_domain_info(dev);
	if (!info)
		return NULL;

	return info->pasid_table;
}

static int intel_pasid_get_dev_max_id(struct device *dev)
{
	struct device_domain_info *info;

	info = get_domain_info(dev);
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
	info = get_domain_info(dev);
	dir_index = pasid >> PASID_PDE_SHIFT;
	index = pasid & PASID_PTE_MASK;

retry:
	entries = get_pasid_table_from_pde(&dir[dir_index]);
	if (!entries) {
		entries = alloc_pgtable_page(info->iommu->node);
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
	}

	return &entries[index];
}

/*
 * Interfaces for PASID table entry manipulation:
 */
static inline void pasid_clear_entry(struct pasid_entry *pe)
{
	WRITE_ONCE(pe->val[0], 0);
	WRITE_ONCE(pe->val[1], 0);
	WRITE_ONCE(pe->val[2], 0);
	WRITE_ONCE(pe->val[3], 0);
	WRITE_ONCE(pe->val[4], 0);
	WRITE_ONCE(pe->val[5], 0);
	WRITE_ONCE(pe->val[6], 0);
	WRITE_ONCE(pe->val[7], 0);
}

static inline void pasid_clear_entry_with_fpd(struct pasid_entry *pe)
{
	WRITE_ONCE(pe->val[0], PASID_PTE_FPD);
	WRITE_ONCE(pe->val[1], 0);
	WRITE_ONCE(pe->val[2], 0);
	WRITE_ONCE(pe->val[3], 0);
	WRITE_ONCE(pe->val[4], 0);
	WRITE_ONCE(pe->val[5], 0);
	WRITE_ONCE(pe->val[6], 0);
	WRITE_ONCE(pe->val[7], 0);
}

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

static inline void pasid_set_bits(u64 *ptr, u64 mask, u64 bits)
{
	u64 old;

	old = READ_ONCE(*ptr);
	WRITE_ONCE(*ptr, (old & ~mask) | bits);
}

/*
 * Setup the DID(Domain Identifier) field (Bit 64~79) of scalable mode
 * PASID entry.
 */
static inline void
pasid_set_domain_id(struct pasid_entry *pe, u64 value)
{
	pasid_set_bits(&pe->val[1], GENMASK_ULL(15, 0), value);
}

/*
 * Get domain ID value of a scalable mode PASID entry.
 */
static inline u16
pasid_get_domain_id(struct pasid_entry *pe)
{
	return (u16)(READ_ONCE(pe->val[1]) & GENMASK_ULL(15, 0));
}

/*
 * Setup the SLPTPTR(Second Level Page Table Pointer) field (Bit 12~63)
 * of a scalable mode PASID entry.
 */
static inline void
pasid_set_slptr(struct pasid_entry *pe, u64 value)
{
	pasid_set_bits(&pe->val[0], VTD_PAGE_MASK, value);
}

/*
 * Setup the AW(Address Width) field (Bit 2~4) of a scalable mode PASID
 * entry.
 */
static inline void
pasid_set_address_width(struct pasid_entry *pe, u64 value)
{
	pasid_set_bits(&pe->val[0], GENMASK_ULL(4, 2), value << 2);
}

/*
 * Setup the PGTT(PASID Granular Translation Type) field (Bit 6~8)
 * of a scalable mode PASID entry.
 */
static inline void
pasid_set_translation_type(struct pasid_entry *pe, u64 value)
{
	pasid_set_bits(&pe->val[0], GENMASK_ULL(8, 6), value << 6);
}

/*
 * Enable fault processing by clearing the FPD(Fault Processing
 * Disable) field (Bit 1) of a scalable mode PASID entry.
 */
static inline void pasid_set_fault_enable(struct pasid_entry *pe)
{
	pasid_set_bits(&pe->val[0], 1 << 1, 0);
}

/*
 * Setup the SRE(Supervisor Request Enable) field (Bit 128) of a
 * scalable mode PASID entry.
 */
static inline void pasid_set_sre(struct pasid_entry *pe)
{
	pasid_set_bits(&pe->val[2], 1 << 0, 1);
}

/*
 * Setup the WPE(Write Protect Enable) field (Bit 132) of a
 * scalable mode PASID entry.
 */
static inline void pasid_set_wpe(struct pasid_entry *pe)
{
	pasid_set_bits(&pe->val[2], 1 << 4, 1 << 4);
}

/*
 * Setup the P(Present) field (Bit 0) of a scalable mode PASID
 * entry.
 */
static inline void pasid_set_present(struct pasid_entry *pe)
{
	pasid_set_bits(&pe->val[0], 1 << 0, 1);
}

/*
 * Setup Page Walk Snoop bit (Bit 87) of a scalable mode PASID
 * entry.
 */
static inline void pasid_set_page_snoop(struct pasid_entry *pe, bool value)
{
	pasid_set_bits(&pe->val[1], 1 << 23, value << 23);
}

/*
 * Setup the Page Snoop (PGSNP) field (Bit 88) of a scalable mode
 * PASID entry.
 */
static inline void
pasid_set_pgsnp(struct pasid_entry *pe)
{
	pasid_set_bits(&pe->val[1], 1ULL << 24, 1ULL << 24);
}

/*
 * Setup the First Level Page table Pointer field (Bit 140~191)
 * of a scalable mode PASID entry.
 */
static inline void
pasid_set_flptr(struct pasid_entry *pe, u64 value)
{
	pasid_set_bits(&pe->val[2], VTD_PAGE_MASK, value);
}

/*
 * Setup the First Level Paging Mode field (Bit 130~131) of a
 * scalable mode PASID entry.
 */
static inline void
pasid_set_flpm(struct pasid_entry *pe, u64 value)
{
	pasid_set_bits(&pe->val[2], GENMASK_ULL(3, 2), value << 2);
}

/*
 * Setup the Extended Access Flag Enable (EAFE) field (Bit 135)
 * of a scalable mode PASID entry.
 */
static inline void
pasid_set_eafe(struct pasid_entry *pe)
{
	pasid_set_bits(&pe->val[2], 1 << 7, 1 << 7);
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

	info = get_domain_info(dev);
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
	if (pasid == PASID_RID2PASID)
		qi_flush_dev_iotlb(iommu, sid, pfsid, qdep, 0, 64 - VTD_PAGE_SHIFT);
	else
		qi_flush_dev_iotlb_pasid(iommu, sid, pfsid, pasid, qdep, 0, 64 - VTD_PAGE_SHIFT);
}

void intel_pasid_tear_down_entry(struct intel_iommu *iommu, struct device *dev,
				 u32 pasid, bool fault_ignore)
{
	struct pasid_entry *pte;
	u16 did, pgtt;

	pte = intel_pasid_get_entry(dev, pasid);
	if (WARN_ON(!pte))
		return;

	if (!pasid_pte_is_present(pte))
		return;

	did = pasid_get_domain_id(pte);
	pgtt = pasid_pte_get_pgtt(pte);

	intel_pasid_clear_entry(dev, pasid, fault_ignore);

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

static inline int pasid_enable_wpe(struct pasid_entry *pte)
{
#ifdef CONFIG_X86
	unsigned long cr0 = read_cr0();

	/* CR0.WP is normally set but just to be sure */
	if (unlikely(!(cr0 & X86_CR0_WP))) {
		pr_err_ratelimited("No CPU write protect!\n");
		return -EINVAL;
	}
#endif
	pasid_set_wpe(pte);

	return 0;
};

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

	pte = intel_pasid_get_entry(dev, pasid);
	if (WARN_ON(!pte))
		return -EINVAL;

	/* Caller must ensure PASID entry is not in use. */
	if (pasid_pte_is_present(pte))
		return -EBUSY;

	pasid_clear_entry(pte);

	/* Setup the first level page table pointer: */
	pasid_set_flptr(pte, (u64)__pa(pgd));
	if (flags & PASID_FLAG_SUPERVISOR_MODE) {
		if (!ecap_srs(iommu->ecap)) {
			pr_err("No supervisor request support on %s\n",
			       iommu->name);
			return -EINVAL;
		}
		pasid_set_sre(pte);
		if (pasid_enable_wpe(pte))
			return -EINVAL;

	}

	if (flags & PASID_FLAG_FL5LP) {
		if (cap_5lp_support(iommu->cap)) {
			pasid_set_flpm(pte, 1);
		} else {
			pr_err("No 5-level paging support for first-level\n");
			pasid_clear_entry(pte);
			return -EINVAL;
		}
	}

	if (flags & PASID_FLAG_PAGE_SNOOP)
		pasid_set_pgsnp(pte);

	pasid_set_domain_id(pte, did);
	pasid_set_address_width(pte, iommu->agaw);
	pasid_set_page_snoop(pte, !!ecap_smpwc(iommu->ecap));

	/* Setup Present and PASID Granular Transfer Type: */
	pasid_set_translation_type(pte, PASID_ENTRY_PGTT_FL_ONLY);
	pasid_set_present(pte);
	pasid_flush_caches(iommu, pte, pasid, did);

	return 0;
}

/*
 * Skip top levels of page tables for iommu which has less agaw
 * than default. Unnecessary for PT mode.
 */
static inline int iommu_skip_agaw(struct dmar_domain *domain,
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
	did = domain->iommu_did[iommu->seq_id];

	pte = intel_pasid_get_entry(dev, pasid);
	if (!pte) {
		dev_err(dev, "Failed to get pasid entry of PASID %d\n", pasid);
		return -ENODEV;
	}

	/* Caller must ensure PASID entry is not in use. */
	if (pasid_pte_is_present(pte))
		return -EBUSY;

	pasid_clear_entry(pte);
	pasid_set_domain_id(pte, did);
	pasid_set_slptr(pte, pgd_val);
	pasid_set_address_width(pte, agaw);
	pasid_set_translation_type(pte, PASID_ENTRY_PGTT_SL_ONLY);
	pasid_set_fault_enable(pte);
	pasid_set_page_snoop(pte, !!ecap_smpwc(iommu->ecap));

	if (domain->domain.type == IOMMU_DOMAIN_UNMANAGED)
		pasid_set_pgsnp(pte);

	/*
	 * Since it is a second level only translation setup, we should
	 * set SRE bit as well (addresses are expected to be GPAs).
	 */
	if (pasid != PASID_RID2PASID)
		pasid_set_sre(pte);
	pasid_set_present(pte);
	pasid_flush_caches(iommu, pte, pasid, did);

	return 0;
}

/*
 * Set up the scalable mode pasid entry for passthrough translation type.
 */
int intel_pasid_setup_pass_through(struct intel_iommu *iommu,
				   struct dmar_domain *domain,
				   struct device *dev, u32 pasid)
{
	u16 did = FLPT_DEFAULT_DID;
	struct pasid_entry *pte;

	pte = intel_pasid_get_entry(dev, pasid);
	if (!pte) {
		dev_err(dev, "Failed to get pasid entry of PASID %d\n", pasid);
		return -ENODEV;
	}

	/* Caller must ensure PASID entry is not in use. */
	if (pasid_pte_is_present(pte))
		return -EBUSY;

	pasid_clear_entry(pte);
	pasid_set_domain_id(pte, did);
	pasid_set_address_width(pte, iommu->agaw);
	pasid_set_translation_type(pte, PASID_ENTRY_PGTT_PT);
	pasid_set_fault_enable(pte);
	pasid_set_page_snoop(pte, !!ecap_smpwc(iommu->ecap));

	/*
	 * We should set SRE bit as well since the addresses are expected
	 * to be GPAs.
	 */
	pasid_set_sre(pte);
	pasid_set_present(pte);
	pasid_flush_caches(iommu, pte, pasid, did);

	return 0;
}

static int
intel_pasid_setup_bind_data(struct intel_iommu *iommu, struct pasid_entry *pte,
			    struct iommu_gpasid_bind_data_vtd *pasid_data)
{
	/*
	 * Not all guest PASID table entry fields are passed down during bind,
	 * here we only set up the ones that are dependent on guest settings.
	 * Execution related bits such as NXE, SMEP are not supported.
	 * Other fields, such as snoop related, are set based on host needs
	 * regardless of guest settings.
	 */
	if (pasid_data->flags & IOMMU_SVA_VTD_GPASID_SRE) {
		if (!ecap_srs(iommu->ecap)) {
			pr_err_ratelimited("No supervisor request support on %s\n",
					   iommu->name);
			return -EINVAL;
		}
		pasid_set_sre(pte);
		/* Enable write protect WP if guest requested */
		if (pasid_data->flags & IOMMU_SVA_VTD_GPASID_WPE)
			pasid_set_wpe(pte);
	}

	if (pasid_data->flags & IOMMU_SVA_VTD_GPASID_EAFE) {
		if (!ecap_eafs(iommu->ecap)) {
			pr_err_ratelimited("No extended access flag support on %s\n",
					   iommu->name);
			return -EINVAL;
		}
		pasid_set_eafe(pte);
	}

	/*
	 * Memory type is only applicable to devices inside processor coherent
	 * domain. Will add MTS support once coherent devices are available.
	 */
	if (pasid_data->flags & IOMMU_SVA_VTD_GPASID_MTS_MASK) {
		pr_warn_ratelimited("No memory type support %s\n",
				    iommu->name);
		return -EINVAL;
	}

	return 0;
}

/**
 * intel_pasid_setup_nested() - Set up PASID entry for nested translation.
 * This could be used for guest shared virtual address. In this case, the
 * first level page tables are used for GVA-GPA translation in the guest,
 * second level page tables are used for GPA-HPA translation.
 *
 * @iommu:      IOMMU which the device belong to
 * @dev:        Device to be set up for translation
 * @gpgd:       FLPTPTR: First Level Page translation pointer in GPA
 * @pasid:      PASID to be programmed in the device PASID table
 * @pasid_data: Additional PASID info from the guest bind request
 * @domain:     Domain info for setting up second level page tables
 * @addr_width: Address width of the first level (guest)
 */
int intel_pasid_setup_nested(struct intel_iommu *iommu, struct device *dev,
			     pgd_t *gpgd, u32 pasid,
			     struct iommu_gpasid_bind_data_vtd *pasid_data,
			     struct dmar_domain *domain, int addr_width)
{
	struct pasid_entry *pte;
	struct dma_pte *pgd;
	int ret = 0;
	u64 pgd_val;
	int agaw;
	u16 did;

	if (!ecap_nest(iommu->ecap)) {
		pr_err_ratelimited("IOMMU: %s: No nested translation support\n",
				   iommu->name);
		return -EINVAL;
	}

	if (!(domain->flags & DOMAIN_FLAG_NESTING_MODE)) {
		pr_err_ratelimited("Domain is not in nesting mode, %x\n",
				   domain->flags);
		return -EINVAL;
	}

	pte = intel_pasid_get_entry(dev, pasid);
	if (WARN_ON(!pte))
		return -EINVAL;

	/*
	 * Caller must ensure PASID entry is not in use, i.e. not bind the
	 * same PASID to the same device twice.
	 */
	if (pasid_pte_is_present(pte))
		return -EBUSY;

	pasid_clear_entry(pte);

	/* Sanity checking performed by caller to make sure address
	 * width matching in two dimensions:
	 * 1. CPU vs. IOMMU
	 * 2. Guest vs. Host.
	 */
	switch (addr_width) {
#ifdef CONFIG_X86
	case ADDR_WIDTH_5LEVEL:
		if (!cpu_feature_enabled(X86_FEATURE_LA57) ||
		    !cap_5lp_support(iommu->cap)) {
			dev_err_ratelimited(dev,
					    "5-level paging not supported\n");
			return -EINVAL;
		}

		pasid_set_flpm(pte, 1);
		break;
#endif
	case ADDR_WIDTH_4LEVEL:
		pasid_set_flpm(pte, 0);
		break;
	default:
		dev_err_ratelimited(dev, "Invalid guest address width %d\n",
				    addr_width);
		return -EINVAL;
	}

	/* First level PGD is in GPA, must be supported by the second level */
	if ((uintptr_t)gpgd > domain->max_addr) {
		dev_err_ratelimited(dev,
				    "Guest PGD %lx not supported, max %llx\n",
				    (uintptr_t)gpgd, domain->max_addr);
		return -EINVAL;
	}
	pasid_set_flptr(pte, (uintptr_t)gpgd);

	ret = intel_pasid_setup_bind_data(iommu, pte, pasid_data);
	if (ret)
		return ret;

	/* Setup the second level based on the given domain */
	pgd = domain->pgd;

	agaw = iommu_skip_agaw(domain, iommu, &pgd);
	if (agaw < 0) {
		dev_err_ratelimited(dev, "Invalid domain page table\n");
		return -EINVAL;
	}
	pgd_val = virt_to_phys(pgd);
	pasid_set_slptr(pte, pgd_val);
	pasid_set_fault_enable(pte);

	did = domain->iommu_did[iommu->seq_id];
	pasid_set_domain_id(pte, did);

	pasid_set_address_width(pte, agaw);
	pasid_set_page_snoop(pte, !!ecap_smpwc(iommu->ecap));

	pasid_set_translation_type(pte, PASID_ENTRY_PGTT_NESTED);
	pasid_set_present(pte);
	pasid_flush_caches(iommu, pte, pasid, did);

	return ret;
}

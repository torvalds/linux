// SPDX-License-Identifier: GPL-2.0
/**
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

#include "intel-pasid.h"

/*
 * Intel IOMMU system wide PASID name space:
 */
static DEFINE_SPINLOCK(pasid_lock);
u32 intel_pasid_max_id = PASID_MAX;
static DEFINE_IDR(pasid_idr);

int intel_pasid_alloc_id(void *ptr, int start, int end, gfp_t gfp)
{
	int ret, min, max;

	min = max_t(int, start, PASID_MIN);
	max = min_t(int, end, intel_pasid_max_id);

	WARN_ON(in_interrupt());
	idr_preload(gfp);
	spin_lock(&pasid_lock);
	ret = idr_alloc(&pasid_idr, ptr, min, max, GFP_ATOMIC);
	spin_unlock(&pasid_lock);
	idr_preload_end();

	return ret;
}

void intel_pasid_free_id(int pasid)
{
	spin_lock(&pasid_lock);
	idr_remove(&pasid_idr, pasid);
	spin_unlock(&pasid_lock);
}

void *intel_pasid_lookup_id(int pasid)
{
	void *p;

	spin_lock(&pasid_lock);
	p = idr_find(&pasid_idr, pasid);
	spin_unlock(&pasid_lock);

	return p;
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
	int max_pasid = 0;
	int ret, order;
	int size;

	might_sleep();
	info = dev->archdata.iommu;
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
		max_pasid = min_t(int, pci_max_pasids(to_pci_dev(dev)),
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

/* Get PRESENT bit of a PASID directory entry. */
static inline bool
pasid_pde_is_present(struct pasid_dir_entry *pde)
{
	return READ_ONCE(pde->val) & PASID_PTE_PRESENT;
}

/* Get PASID table from a PASID directory entry. */
static inline struct pasid_entry *
get_pasid_table_from_pde(struct pasid_dir_entry *pde)
{
	if (!pasid_pde_is_present(pde))
		return NULL;

	return phys_to_virt(READ_ONCE(pde->val) & PDE_PFN_MASK);
}

void intel_pasid_free_table(struct device *dev)
{
	struct device_domain_info *info;
	struct pasid_table *pasid_table;
	struct pasid_dir_entry *dir;
	struct pasid_entry *table;
	int i, max_pde;

	info = dev->archdata.iommu;
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

	info = dev->archdata.iommu;
	if (!info)
		return NULL;

	return info->pasid_table;
}

int intel_pasid_get_dev_max_id(struct device *dev)
{
	struct device_domain_info *info;

	info = dev->archdata.iommu;
	if (!info || !info->pasid_table)
		return 0;

	return info->pasid_table->max_pasid;
}

struct pasid_entry *intel_pasid_get_entry(struct device *dev, int pasid)
{
	struct device_domain_info *info;
	struct pasid_table *pasid_table;
	struct pasid_dir_entry *dir;
	struct pasid_entry *entries;
	int dir_index, index;

	pasid_table = intel_pasid_get_table(dev);
	if (WARN_ON(!pasid_table || pasid < 0 ||
		    pasid >= intel_pasid_get_dev_max_id(dev)))
		return NULL;

	dir = pasid_table->table;
	info = dev->archdata.iommu;
	dir_index = pasid >> PASID_PDE_SHIFT;
	index = pasid & PASID_PTE_MASK;

	spin_lock(&pasid_lock);
	entries = get_pasid_table_from_pde(&dir[dir_index]);
	if (!entries) {
		entries = alloc_pgtable_page(info->iommu->node);
		if (!entries) {
			spin_unlock(&pasid_lock);
			return NULL;
		}

		WRITE_ONCE(dir[dir_index].val,
			   (u64)virt_to_phys(entries) | PASID_PTE_PRESENT);
	}
	spin_unlock(&pasid_lock);

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

static void intel_pasid_clear_entry(struct device *dev, int pasid)
{
	struct pasid_entry *pe;

	pe = intel_pasid_get_entry(dev, pasid);
	if (WARN_ON(!pe))
		return;

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
	pasid_set_bits(&pe->val[1], 1 << 23, value);
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

static void
pasid_cache_invalidation_with_pasid(struct intel_iommu *iommu,
				    u16 did, int pasid)
{
	struct qi_desc desc;

	desc.qw0 = QI_PC_DID(did) | QI_PC_PASID_SEL | QI_PC_PASID(pasid);
	desc.qw1 = 0;
	desc.qw2 = 0;
	desc.qw3 = 0;

	qi_submit_sync(&desc, iommu);
}

static void
iotlb_invalidation_with_pasid(struct intel_iommu *iommu, u16 did, u32 pasid)
{
	struct qi_desc desc;

	desc.qw0 = QI_EIOTLB_PASID(pasid) | QI_EIOTLB_DID(did) |
			QI_EIOTLB_GRAN(QI_GRAN_NONG_PASID) | QI_EIOTLB_TYPE;
	desc.qw1 = 0;
	desc.qw2 = 0;
	desc.qw3 = 0;

	qi_submit_sync(&desc, iommu);
}

static void
devtlb_invalidation_with_pasid(struct intel_iommu *iommu,
			       struct device *dev, int pasid)
{
	struct device_domain_info *info;
	u16 sid, qdep, pfsid;

	info = dev->archdata.iommu;
	if (!info || !info->ats_enabled)
		return;

	sid = info->bus << 8 | info->devfn;
	qdep = info->ats_qdep;
	pfsid = info->pfsid;

	qi_flush_dev_iotlb(iommu, sid, pfsid, qdep, 0, 64 - VTD_PAGE_SHIFT);
}

void intel_pasid_tear_down_entry(struct intel_iommu *iommu,
				 struct device *dev, int pasid)
{
	struct pasid_entry *pte;
	u16 did;

	pte = intel_pasid_get_entry(dev, pasid);
	if (WARN_ON(!pte))
		return;

	did = pasid_get_domain_id(pte);
	intel_pasid_clear_entry(dev, pasid);

	if (!ecap_coherent(iommu->ecap))
		clflush_cache_range(pte, sizeof(*pte));

	pasid_cache_invalidation_with_pasid(iommu, did, pasid);
	iotlb_invalidation_with_pasid(iommu, did, pasid);

	/* Device IOTLB doesn't need to be flushed in caching mode. */
	if (!cap_caching_mode(iommu->cap))
		devtlb_invalidation_with_pasid(iommu, dev, pasid);
}

/*
 * Set up the scalable mode pasid table entry for first only
 * translation type.
 */
int intel_pasid_setup_first_level(struct intel_iommu *iommu,
				  struct device *dev, pgd_t *pgd,
				  int pasid, u16 did, int flags)
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
	}

#ifdef CONFIG_X86
	if (cpu_feature_enabled(X86_FEATURE_LA57))
		pasid_set_flpm(pte, 1);
#endif /* CONFIG_X86 */

	pasid_set_domain_id(pte, did);
	pasid_set_address_width(pte, iommu->agaw);
	pasid_set_page_snoop(pte, !!ecap_smpwc(iommu->ecap));

	/* Setup Present and PASID Granular Transfer Type: */
	pasid_set_translation_type(pte, 1);
	pasid_set_present(pte);

	if (!ecap_coherent(iommu->ecap))
		clflush_cache_range(pte, sizeof(*pte));

	if (cap_caching_mode(iommu->cap)) {
		pasid_cache_invalidation_with_pasid(iommu, did, pasid);
		iotlb_invalidation_with_pasid(iommu, did, pasid);
	} else {
		iommu_flush_write_buffer(iommu);
	}

	return 0;
}

/*
 * Set up the scalable mode pasid entry for second only translation type.
 */
int intel_pasid_setup_second_level(struct intel_iommu *iommu,
				   struct dmar_domain *domain,
				   struct device *dev, int pasid)
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

	/*
	 * Skip top levels of page tables for iommu which has less agaw
	 * than default. Unnecessary for PT mode.
	 */
	pgd = domain->pgd;
	for (agaw = domain->agaw; agaw > iommu->agaw; agaw--) {
		pgd = phys_to_virt(dma_pte_addr(pgd));
		if (!dma_pte_present(pgd)) {
			dev_err(dev, "Invalid domain page table\n");
			return -EINVAL;
		}
	}

	pgd_val = virt_to_phys(pgd);
	did = domain->iommu_did[iommu->seq_id];

	pte = intel_pasid_get_entry(dev, pasid);
	if (!pte) {
		dev_err(dev, "Failed to get pasid entry of PASID %d\n", pasid);
		return -ENODEV;
	}

	pasid_clear_entry(pte);
	pasid_set_domain_id(pte, did);
	pasid_set_slptr(pte, pgd_val);
	pasid_set_address_width(pte, agaw);
	pasid_set_translation_type(pte, 2);
	pasid_set_fault_enable(pte);
	pasid_set_page_snoop(pte, !!ecap_smpwc(iommu->ecap));

	/*
	 * Since it is a second level only translation setup, we should
	 * set SRE bit as well (addresses are expected to be GPAs).
	 */
	pasid_set_sre(pte);
	pasid_set_present(pte);

	if (!ecap_coherent(iommu->ecap))
		clflush_cache_range(pte, sizeof(*pte));

	if (cap_caching_mode(iommu->cap)) {
		pasid_cache_invalidation_with_pasid(iommu, did, pasid);
		iotlb_invalidation_with_pasid(iommu, did, pasid);
	} else {
		iommu_flush_write_buffer(iommu);
	}

	return 0;
}

/*
 * Set up the scalable mode pasid entry for passthrough translation type.
 */
int intel_pasid_setup_pass_through(struct intel_iommu *iommu,
				   struct dmar_domain *domain,
				   struct device *dev, int pasid)
{
	u16 did = FLPT_DEFAULT_DID;
	struct pasid_entry *pte;

	pte = intel_pasid_get_entry(dev, pasid);
	if (!pte) {
		dev_err(dev, "Failed to get pasid entry of PASID %d\n", pasid);
		return -ENODEV;
	}

	pasid_clear_entry(pte);
	pasid_set_domain_id(pte, did);
	pasid_set_address_width(pte, iommu->agaw);
	pasid_set_translation_type(pte, 4);
	pasid_set_fault_enable(pte);
	pasid_set_page_snoop(pte, !!ecap_smpwc(iommu->ecap));

	/*
	 * We should set SRE bit as well since the addresses are expected
	 * to be GPAs.
	 */
	pasid_set_sre(pte);
	pasid_set_present(pte);

	if (!ecap_coherent(iommu->ecap))
		clflush_cache_range(pte, sizeof(*pte));

	if (cap_caching_mode(iommu->cap)) {
		pasid_cache_invalidation_with_pasid(iommu, did, pasid);
		iotlb_invalidation_with_pasid(iommu, did, pasid);
	} else {
		iommu_flush_write_buffer(iommu);
	}

	return 0;
}

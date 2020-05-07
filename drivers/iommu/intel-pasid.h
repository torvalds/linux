/* SPDX-License-Identifier: GPL-2.0 */
/*
 * intel-pasid.h - PASID idr, table and entry header
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 */

#ifndef __INTEL_PASID_H
#define __INTEL_PASID_H

#define PASID_RID2PASID			0x0
#define PASID_MIN			0x1
#define PASID_MAX			0x100000
#define PASID_PTE_MASK			0x3F
#define PASID_PTE_PRESENT		1
#define PDE_PFN_MASK			PAGE_MASK
#define PASID_PDE_SHIFT			6
#define MAX_NR_PASID_BITS		20
#define PASID_TBL_ENTRIES		BIT(PASID_PDE_SHIFT)

#define is_pasid_enabled(entry)		(((entry)->lo >> 3) & 0x1)
#define get_pasid_dir_size(entry)	(1 << ((((entry)->lo >> 9) & 0x7) + 7))

/*
 * Domain ID reserved for pasid entries programmed for first-level
 * only and pass-through transfer modes.
 */
#define FLPT_DEFAULT_DID		1

/*
 * The SUPERVISOR_MODE flag indicates a first level translation which
 * can be used for access to kernel addresses. It is valid only for
 * access to the kernel's static 1:1 mapping of physical memory — not
 * to vmalloc or even module mappings.
 */
#define PASID_FLAG_SUPERVISOR_MODE	BIT(0)

/*
 * The PASID_FLAG_FL5LP flag Indicates using 5-level paging for first-
 * level translation, otherwise, 4-level paging will be used.
 */
#define PASID_FLAG_FL5LP		BIT(1)

struct pasid_dir_entry {
	u64 val;
};

struct pasid_entry {
	u64 val[8];
};

/* The representative of a PASID table */
struct pasid_table {
	void			*table;		/* pasid table pointer */
	int			order;		/* page order of pasid table */
	int			max_pasid;	/* max pasid */
	struct list_head	dev;		/* device list */
};

/* Get PRESENT bit of a PASID directory entry. */
static inline bool pasid_pde_is_present(struct pasid_dir_entry *pde)
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

/* Get PRESENT bit of a PASID table entry. */
static inline bool pasid_pte_is_present(struct pasid_entry *pte)
{
	return READ_ONCE(pte->val[0]) & PASID_PTE_PRESENT;
}

extern u32 intel_pasid_max_id;
int intel_pasid_alloc_id(void *ptr, int start, int end, gfp_t gfp);
void intel_pasid_free_id(int pasid);
void *intel_pasid_lookup_id(int pasid);
int intel_pasid_alloc_table(struct device *dev);
void intel_pasid_free_table(struct device *dev);
struct pasid_table *intel_pasid_get_table(struct device *dev);
int intel_pasid_get_dev_max_id(struct device *dev);
struct pasid_entry *intel_pasid_get_entry(struct device *dev, int pasid);
int intel_pasid_setup_first_level(struct intel_iommu *iommu,
				  struct device *dev, pgd_t *pgd,
				  int pasid, u16 did, int flags);
int intel_pasid_setup_second_level(struct intel_iommu *iommu,
				   struct dmar_domain *domain,
				   struct device *dev, int pasid);
int intel_pasid_setup_pass_through(struct intel_iommu *iommu,
				   struct dmar_domain *domain,
				   struct device *dev, int pasid);
void intel_pasid_tear_down_entry(struct intel_iommu *iommu,
				 struct device *dev, int pasid);

#endif /* __INTEL_PASID_H */

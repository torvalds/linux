/* SPDX-License-Identifier: GPL-2.0 */
/*
 * pasid.h - PASID idr, table and entry header
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 */

#ifndef __INTEL_PASID_H
#define __INTEL_PASID_H

#define PASID_MAX			0x100000
#define PASID_PTE_MASK			0x3F
#define PASID_PTE_PRESENT		1
#define PASID_PTE_FPD			2
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
#define NUM_RESERVED_DID		2

#define PASID_FLAG_NESTED		BIT(1)
#define PASID_FLAG_PAGE_SNOOP		BIT(2)

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

#define PASID_ENTRY_PGTT_FL_ONLY	(1)
#define PASID_ENTRY_PGTT_SL_ONLY	(2)
#define PASID_ENTRY_PGTT_NESTED		(3)
#define PASID_ENTRY_PGTT_PT		(4)

/* The representative of a PASID table */
struct pasid_table {
	void			*table;		/* pasid table pointer */
	int			order;		/* page order of pasid table */
	u32			max_pasid;	/* max pasid */
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

/* Get PGTT field of a PASID table entry */
static inline u16 pasid_pte_get_pgtt(struct pasid_entry *pte)
{
	return (u16)((READ_ONCE(pte->val[0]) >> 6) & 0x7);
}

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

static inline void pasid_set_bits(u64 *ptr, u64 mask, u64 bits)
{
	u64 old;

	old = READ_ONCE(*ptr);
	WRITE_ONCE(*ptr, (old & ~mask) | bits);
}

static inline u64 pasid_get_bits(u64 *ptr)
{
	return READ_ONCE(*ptr);
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
 * Enable second level A/D bits by setting the SLADE (Second Level
 * Access Dirty Enable) field (Bit 9) of a scalable mode PASID
 * entry.
 */
static inline void pasid_set_ssade(struct pasid_entry *pe)
{
	pasid_set_bits(&pe->val[0], 1 << 9, 1 << 9);
}

/*
 * Disable second level A/D bits by clearing the SLADE (Second Level
 * Access Dirty Enable) field (Bit 9) of a scalable mode PASID
 * entry.
 */
static inline void pasid_clear_ssade(struct pasid_entry *pe)
{
	pasid_set_bits(&pe->val[0], 1 << 9, 0);
}

/*
 * Checks if second level A/D bits specifically the SLADE (Second Level
 * Access Dirty Enable) field (Bit 9) of a scalable mode PASID
 * entry is set.
 */
static inline bool pasid_get_ssade(struct pasid_entry *pe)
{
	return pasid_get_bits(&pe->val[0]) & (1 << 9);
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
 * Setup No Execute Enable bit (Bit 133) of a scalable mode PASID
 * entry. It is required when XD bit of the first level page table
 * entry is about to be set.
 */
static inline void pasid_set_nxe(struct pasid_entry *pe)
{
	pasid_set_bits(&pe->val[2], 1 << 5, 1 << 5);
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
static inline void pasid_set_eafe(struct pasid_entry *pe)
{
	pasid_set_bits(&pe->val[2], 1 << 7, 1 << 7);
}

extern unsigned int intel_pasid_max_id;
int intel_pasid_alloc_table(struct device *dev);
void intel_pasid_free_table(struct device *dev);
struct pasid_table *intel_pasid_get_table(struct device *dev);
int intel_pasid_setup_first_level(struct intel_iommu *iommu,
				  struct device *dev, pgd_t *pgd,
				  u32 pasid, u16 did, int flags);
int intel_pasid_setup_second_level(struct intel_iommu *iommu,
				   struct dmar_domain *domain,
				   struct device *dev, u32 pasid);
int intel_pasid_setup_dirty_tracking(struct intel_iommu *iommu,
				     struct device *dev, u32 pasid,
				     bool enabled);
int intel_pasid_setup_pass_through(struct intel_iommu *iommu,
				   struct device *dev, u32 pasid);
int intel_pasid_setup_nested(struct intel_iommu *iommu, struct device *dev,
			     u32 pasid, struct dmar_domain *domain);
void intel_pasid_tear_down_entry(struct intel_iommu *iommu,
				 struct device *dev, u32 pasid,
				 bool fault_ignore);
void intel_pasid_setup_page_snoop_control(struct intel_iommu *iommu,
					  struct device *dev, u32 pasid);
#endif /* __INTEL_PASID_H */

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

/*
 * Domain ID reserved for pasid entries programmed for first-level
 * only and pass-through transfer modes.
 */
#define FLPT_DEFAULT_DID		1

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

extern u32 intel_pasid_max_id;
int intel_pasid_alloc_id(void *ptr, int start, int end, gfp_t gfp);
void intel_pasid_free_id(int pasid);
void *intel_pasid_lookup_id(int pasid);
int intel_pasid_alloc_table(struct device *dev);
void intel_pasid_free_table(struct device *dev);
struct pasid_table *intel_pasid_get_table(struct device *dev);
int intel_pasid_get_dev_max_id(struct device *dev);
struct pasid_entry *intel_pasid_get_entry(struct device *dev, int pasid);
void intel_pasid_clear_entry(struct device *dev, int pasid);
int intel_pasid_setup_second_level(struct intel_iommu *iommu,
				   struct dmar_domain *domain,
				   struct device *dev, int pasid);
int intel_pasid_setup_pass_through(struct intel_iommu *iommu,
				   struct dmar_domain *domain,
				   struct device *dev, int pasid);
void intel_pasid_tear_down_entry(struct intel_iommu *iommu,
				 struct device *dev, int pasid);

#endif /* __INTEL_PASID_H */

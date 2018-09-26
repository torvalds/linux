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

#define PASID_MIN			0x1
#define PASID_MAX			0x100000

struct pasid_entry {
	u64 val;
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

#endif /* __INTEL_PASID_H */

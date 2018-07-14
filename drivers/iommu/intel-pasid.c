// SPDX-License-Identifier: GPL-2.0
/**
 * intel-pasid.c - PASID idr, table and entry manipulation
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 */

#define pr_fmt(fmt)	"DMAR: " fmt

#include <linux/dmar.h>
#include <linux/intel-iommu.h>
#include <linux/iommu.h>
#include <linux/memory.h>
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

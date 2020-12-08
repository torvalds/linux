// SPDX-License-Identifier: GPL-2.0
/*
 * Helpers for IOMMU drivers implementing SVA
 */
#include <linux/mutex.h>
#include <linux/sched/mm.h>

#include "iommu-sva-lib.h"

static DEFINE_MUTEX(iommu_sva_lock);
static DECLARE_IOASID_SET(iommu_sva_pasid);

/**
 * iommu_sva_alloc_pasid - Allocate a PASID for the mm
 * @mm: the mm
 * @min: minimum PASID value (inclusive)
 * @max: maximum PASID value (inclusive)
 *
 * Try to allocate a PASID for this mm, or take a reference to the existing one
 * provided it fits within the [@min, @max] range. On success the PASID is
 * available in mm->pasid, and must be released with iommu_sva_free_pasid().
 * @min must be greater than 0, because 0 indicates an unused mm->pasid.
 *
 * Returns 0 on success and < 0 on error.
 */
int iommu_sva_alloc_pasid(struct mm_struct *mm, ioasid_t min, ioasid_t max)
{
	int ret = 0;
	ioasid_t pasid;

	if (min == INVALID_IOASID || max == INVALID_IOASID ||
	    min == 0 || max < min)
		return -EINVAL;

	mutex_lock(&iommu_sva_lock);
	if (mm->pasid) {
		if (mm->pasid >= min && mm->pasid <= max)
			ioasid_get(mm->pasid);
		else
			ret = -EOVERFLOW;
	} else {
		pasid = ioasid_alloc(&iommu_sva_pasid, min, max, mm);
		if (pasid == INVALID_IOASID)
			ret = -ENOMEM;
		else
			mm->pasid = pasid;
	}
	mutex_unlock(&iommu_sva_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(iommu_sva_alloc_pasid);

/**
 * iommu_sva_free_pasid - Release the mm's PASID
 * @mm: the mm
 *
 * Drop one reference to a PASID allocated with iommu_sva_alloc_pasid()
 */
void iommu_sva_free_pasid(struct mm_struct *mm)
{
	mutex_lock(&iommu_sva_lock);
	if (ioasid_put(mm->pasid))
		mm->pasid = 0;
	mutex_unlock(&iommu_sva_lock);
}
EXPORT_SYMBOL_GPL(iommu_sva_free_pasid);

/* ioasid_find getter() requires a void * argument */
static bool __mmget_not_zero(void *mm)
{
	return mmget_not_zero(mm);
}

/**
 * iommu_sva_find() - Find mm associated to the given PASID
 * @pasid: Process Address Space ID assigned to the mm
 *
 * On success a reference to the mm is taken, and must be released with mmput().
 *
 * Returns the mm corresponding to this PASID, or an error if not found.
 */
struct mm_struct *iommu_sva_find(ioasid_t pasid)
{
	return ioasid_find(&iommu_sva_pasid, pasid, __mmget_not_zero);
}
EXPORT_SYMBOL_GPL(iommu_sva_find);

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SVA library for IOMMU drivers
 */
#ifndef _IOMMU_SVA_LIB_H
#define _IOMMU_SVA_LIB_H

#include <linux/ioasid.h>
#include <linux/mm_types.h>

int iommu_sva_alloc_pasid(struct mm_struct *mm, ioasid_t min, ioasid_t max);
void iommu_sva_free_pasid(struct mm_struct *mm);
struct mm_struct *iommu_sva_find(ioasid_t pasid);

#endif /* _IOMMU_SVA_LIB_H */

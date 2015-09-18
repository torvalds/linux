/* shmobile-ipmmu.h
 *
 * Copyright (C) 2012  Hideki EIRAKU
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#ifndef __SHMOBILE_IPMMU_H__
#define __SHMOBILE_IPMMU_H__

struct shmobile_ipmmu {
	struct device *dev;
	void __iomem *ipmmu_base;
	int tlb_enabled;
	spinlock_t flush_lock;
	const char * const *dev_names;
	unsigned int num_dev_names;
};

#ifdef CONFIG_SHMOBILE_IPMMU_TLB
void ipmmu_tlb_flush(struct shmobile_ipmmu *ipmmu);
void ipmmu_tlb_set(struct shmobile_ipmmu *ipmmu, unsigned long phys, int size,
		   int asid);
int ipmmu_iommu_init(struct shmobile_ipmmu *ipmmu);
#else
static inline int ipmmu_iommu_init(struct shmobile_ipmmu *ipmmu)
{
	return -EINVAL;
}
#endif

#endif /* __SHMOBILE_IPMMU_H__ */

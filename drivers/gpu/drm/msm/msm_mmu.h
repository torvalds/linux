/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __MSM_MMU_H__
#define __MSM_MMU_H__

#include <linux/iommu.h>

struct msm_mmu_funcs {
	void (*detach)(struct msm_mmu *mmu);
	int (*map)(struct msm_mmu *mmu, uint64_t iova, struct sg_table *sgt,
			size_t len, int prot);
	int (*unmap)(struct msm_mmu *mmu, uint64_t iova, size_t len);
	void (*destroy)(struct msm_mmu *mmu);
	void (*resume_translation)(struct msm_mmu *mmu);
};

enum msm_mmu_type {
	MSM_MMU_GPUMMU,
	MSM_MMU_IOMMU,
	MSM_MMU_IOMMU_PAGETABLE,
};

struct msm_mmu {
	const struct msm_mmu_funcs *funcs;
	struct device *dev;
	int (*handler)(void *arg, unsigned long iova, int flags, void *data);
	void *arg;
	enum msm_mmu_type type;
};

static inline void msm_mmu_init(struct msm_mmu *mmu, struct device *dev,
		const struct msm_mmu_funcs *funcs, enum msm_mmu_type type)
{
	mmu->dev = dev;
	mmu->funcs = funcs;
	mmu->type = type;
}

struct msm_mmu *msm_iommu_new(struct device *dev, unsigned long quirks);
struct msm_mmu *msm_iommu_gpu_new(struct device *dev, struct msm_gpu *gpu, unsigned long quirks);
struct msm_mmu *msm_iommu_disp_new(struct device *dev, unsigned long quirks);

static inline void msm_mmu_set_fault_handler(struct msm_mmu *mmu, void *arg,
		int (*handler)(void *arg, unsigned long iova, int flags, void *data))
{
	mmu->arg = arg;
	mmu->handler = handler;
}

struct msm_mmu *msm_iommu_pagetable_create(struct msm_mmu *parent);

int msm_iommu_pagetable_params(struct msm_mmu *mmu, phys_addr_t *ttbr,
			       int *asid);
int msm_iommu_pagetable_walk(struct msm_mmu *mmu, unsigned long iova, uint64_t ptes[4]);
struct iommu_domain_geometry *msm_iommu_get_geometry(struct msm_mmu *mmu);

#endif /* __MSM_MMU_H__ */

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
};

struct msm_mmu {
	const struct msm_mmu_funcs *funcs;
	struct device *dev;
	int (*handler)(void *arg, unsigned long iova, int flags);
	void *arg;
};

static inline void msm_mmu_init(struct msm_mmu *mmu, struct device *dev,
		const struct msm_mmu_funcs *funcs)
{
	mmu->dev = dev;
	mmu->funcs = funcs;
}

struct msm_mmu *msm_iommu_new(struct device *dev, struct iommu_domain *domain);
struct msm_mmu *msm_gpummu_new(struct device *dev, struct msm_gpu *gpu);

static inline void msm_mmu_set_fault_handler(struct msm_mmu *mmu, void *arg,
		int (*handler)(void *arg, unsigned long iova, int flags))
{
	mmu->arg = arg;
	mmu->handler = handler;
}

void msm_gpummu_params(struct msm_mmu *mmu, dma_addr_t *pt_base,
		dma_addr_t *tran_error);

#endif /* __MSM_MMU_H__ */

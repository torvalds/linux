/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MSM_MMU_H__
#define __MSM_MMU_H__

#include <linux/iommu.h>

struct msm_mmu_funcs {
	int (*attach)(struct msm_mmu *mmu, const char * const *names, int cnt);
	void (*detach)(struct msm_mmu *mmu, const char * const *names, int cnt);
	int (*map)(struct msm_mmu *mmu, uint64_t iova, struct sg_table *sgt,
			unsigned len, int prot);
	int (*unmap)(struct msm_mmu *mmu, uint64_t iova, struct sg_table *sgt,
			unsigned len);
	void (*destroy)(struct msm_mmu *mmu);
};

struct msm_mmu {
	const struct msm_mmu_funcs *funcs;
	struct device *dev;
};

static inline void msm_mmu_init(struct msm_mmu *mmu, struct device *dev,
		const struct msm_mmu_funcs *funcs)
{
	mmu->dev = dev;
	mmu->funcs = funcs;
}

struct msm_mmu *msm_iommu_new(struct device *dev, struct iommu_domain *domain);
struct msm_mmu *msm_gpummu_new(struct device *dev, struct msm_gpu *gpu);

#endif /* __MSM_MMU_H__ */

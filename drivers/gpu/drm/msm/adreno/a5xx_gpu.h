/* Copyright (c) 2016 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __A5XX_GPU_H__
#define __A5XX_GPU_H__

#include "adreno_gpu.h"

/* Bringing over the hack from the previous targets */
#undef ROP_COPY
#undef ROP_XOR

#include "a5xx.xml.h"

struct a5xx_gpu {
	struct adreno_gpu base;
	struct platform_device *pdev;

	struct drm_gem_object *pm4_bo;
	uint64_t pm4_iova;

	struct drm_gem_object *pfp_bo;
	uint64_t pfp_iova;

	struct drm_gem_object *gpmu_bo;
	uint64_t gpmu_iova;
	uint32_t gpmu_dwords;

	uint32_t lm_leakage;
};

#define to_a5xx_gpu(x) container_of(x, struct a5xx_gpu, base)

int a5xx_power_init(struct msm_gpu *gpu);
void a5xx_gpmu_ucode_init(struct msm_gpu *gpu);

static inline int spin_usecs(struct msm_gpu *gpu, uint32_t usecs,
		uint32_t reg, uint32_t mask, uint32_t value)
{
	while (usecs--) {
		udelay(1);
		if ((gpu_read(gpu, reg) & mask) == value)
			return 0;
		cpu_relax();
	}

	return -ETIMEDOUT;
}


#endif /* __A5XX_GPU_H__ */

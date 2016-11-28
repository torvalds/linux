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
};

#define to_a5xx_gpu(x) container_of(x, struct a5xx_gpu, base)

#endif /* __A5XX_GPU_H__ */

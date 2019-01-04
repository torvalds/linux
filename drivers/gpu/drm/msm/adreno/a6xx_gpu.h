/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2017 The Linux Foundation. All rights reserved. */

#ifndef __A6XX_GPU_H__
#define __A6XX_GPU_H__


#include "adreno_gpu.h"
#include "a6xx.xml.h"

#include "a6xx_gmu.h"

extern bool hang_debug;

struct a6xx_gpu {
	struct adreno_gpu base;

	struct drm_gem_object *sqe_bo;
	uint64_t sqe_iova;

	struct msm_ringbuffer *cur_ring;

	struct a6xx_gmu gmu;
};

#define to_a6xx_gpu(x) container_of(x, struct a6xx_gpu, base)

/*
 * Given a register and a count, return a value to program into
 * REG_CP_PROTECT_REG(n) - this will block both reads and writes for _len
 * registers starting at _reg.
 */
#define A6XX_PROTECT_RW(_reg, _len) \
	((1 << 31) | \
	(((_len) & 0x3FFF) << 18) | ((_reg) & 0x3FFFF))

/*
 * Same as above, but allow reads over the range. For areas of mixed use (such
 * as performance counters) this allows us to protect a much larger range with a
 * single register
 */
#define A6XX_PROTECT_RDONLY(_reg, _len) \
	((((_len) & 0x3FFF) << 18) | ((_reg) & 0x3FFFF))


int a6xx_gmu_resume(struct a6xx_gpu *gpu);
int a6xx_gmu_stop(struct a6xx_gpu *gpu);

int a6xx_gmu_wait_for_idle(struct a6xx_gpu *gpu);

int a6xx_gmu_reset(struct a6xx_gpu *a6xx_gpu);
bool a6xx_gmu_isidle(struct a6xx_gmu *gmu);

int a6xx_gmu_set_oob(struct a6xx_gmu *gmu, enum a6xx_gmu_oob_state state);
void a6xx_gmu_clear_oob(struct a6xx_gmu *gmu, enum a6xx_gmu_oob_state state);

int a6xx_gmu_probe(struct a6xx_gpu *a6xx_gpu, struct device_node *node);
void a6xx_gmu_remove(struct a6xx_gpu *a6xx_gpu);
void a6xx_gmu_set_freq(struct msm_gpu *gpu, unsigned long freq);
unsigned long a6xx_gmu_get_freq(struct msm_gpu *gpu);
#endif /* __A6XX_GPU_H__ */

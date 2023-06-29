/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2017, 2019 The Linux Foundation. All rights reserved. */

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

	struct drm_gem_object *shadow_bo;
	uint64_t shadow_iova;
	uint32_t *shadow;

	bool has_whereami;

	void __iomem *llc_mmio;
	void *llc_slice;
	void *htw_llc_slice;
	bool have_mmu500;
	bool hung;
};

#define to_a6xx_gpu(x) container_of(x, struct a6xx_gpu, base)

/*
 * Given a register and a count, return a value to program into
 * REG_CP_PROTECT_REG(n) - this will block both reads and writes for _len
 * registers starting at _reg.
 */
#define A6XX_PROTECT_NORDWR(_reg, _len) \
	((1 << 31) | \
	(((_len) & 0x3FFF) << 18) | ((_reg) & 0x3FFFF))

/*
 * Same as above, but allow reads over the range. For areas of mixed use (such
 * as performance counters) this allows us to protect a much larger range with a
 * single register
 */
#define A6XX_PROTECT_RDONLY(_reg, _len) \
	((((_len) & 0x3FFF) << 18) | ((_reg) & 0x3FFFF))

static inline bool a6xx_has_gbif(struct adreno_gpu *gpu)
{
	if(adreno_is_a630(gpu))
		return false;

	return true;
}

#define shadowptr(_a6xx_gpu, _ring) ((_a6xx_gpu)->shadow_iova + \
		((_ring)->id * sizeof(uint32_t)))

int a6xx_gmu_resume(struct a6xx_gpu *gpu);
int a6xx_gmu_stop(struct a6xx_gpu *gpu);

int a6xx_gmu_wait_for_idle(struct a6xx_gmu *gmu);

bool a6xx_gmu_isidle(struct a6xx_gmu *gmu);

int a6xx_gmu_set_oob(struct a6xx_gmu *gmu, enum a6xx_gmu_oob_state state);
void a6xx_gmu_clear_oob(struct a6xx_gmu *gmu, enum a6xx_gmu_oob_state state);

int a6xx_gmu_init(struct a6xx_gpu *a6xx_gpu, struct device_node *node);
int a6xx_gmu_wrapper_init(struct a6xx_gpu *a6xx_gpu, struct device_node *node);
void a6xx_gmu_remove(struct a6xx_gpu *a6xx_gpu);

void a6xx_gmu_set_freq(struct msm_gpu *gpu, struct dev_pm_opp *opp,
		       bool suspended);
unsigned long a6xx_gmu_get_freq(struct msm_gpu *gpu);

void a6xx_show(struct msm_gpu *gpu, struct msm_gpu_state *state,
		struct drm_printer *p);

struct msm_gpu_state *a6xx_gpu_state_get(struct msm_gpu *gpu);
int a6xx_gpu_state_put(struct msm_gpu_state *state);

void a6xx_bus_clear_pending_transactions(struct adreno_gpu *adreno_gpu, bool gx_off);
void a6xx_gpu_sw_reset(struct msm_gpu *gpu, bool assert);

#endif /* __A6XX_GPU_H__ */

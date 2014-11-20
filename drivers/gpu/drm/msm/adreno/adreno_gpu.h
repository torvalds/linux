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

#ifndef __ADRENO_GPU_H__
#define __ADRENO_GPU_H__

#include <linux/firmware.h>

#include "msm_gpu.h"

#include "adreno_common.xml.h"
#include "adreno_pm4.xml.h"

struct adreno_rev {
	uint8_t  core;
	uint8_t  major;
	uint8_t  minor;
	uint8_t  patchid;
};

#define ADRENO_REV(core, major, minor, patchid) \
	((struct adreno_rev){ core, major, minor, patchid })

struct adreno_gpu_funcs {
	struct msm_gpu_funcs base;
};

struct adreno_info {
	struct adreno_rev rev;
	uint32_t revn;
	const char *name;
	const char *pm4fw, *pfpfw;
	uint32_t gmem;
	struct msm_gpu *(*init)(struct drm_device *dev);
};

const struct adreno_info *adreno_info(struct adreno_rev rev);

struct adreno_rbmemptrs {
	volatile uint32_t rptr;
	volatile uint32_t wptr;
	volatile uint32_t fence;
};

struct adreno_gpu {
	struct msm_gpu base;
	struct adreno_rev rev;
	const struct adreno_info *info;
	uint32_t gmem;  /* actual gmem size */
	uint32_t revn;  /* numeric revision name */
	const struct adreno_gpu_funcs *funcs;

	/* interesting register offsets to dump: */
	const unsigned int *registers;

	/* firmware: */
	const struct firmware *pm4, *pfp;

	/* ringbuffer rptr/wptr: */
	// TODO should this be in msm_ringbuffer?  I think it would be
	// different for z180..
	struct adreno_rbmemptrs *memptrs;
	struct drm_gem_object *memptrs_bo;
	uint32_t memptrs_iova;
};
#define to_adreno_gpu(x) container_of(x, struct adreno_gpu, base)

/* platform config data (ie. from DT, or pdata) */
struct adreno_platform_config {
	struct adreno_rev rev;
	uint32_t fast_rate, slow_rate, bus_freq;
#ifdef CONFIG_MSM_BUS_SCALING
	struct msm_bus_scale_pdata *bus_scale_table;
#endif
};

#define ADRENO_IDLE_TIMEOUT msecs_to_jiffies(1000)

#define spin_until(X) ({                                   \
	int __ret = -ETIMEDOUT;                            \
	unsigned long __t = jiffies + ADRENO_IDLE_TIMEOUT; \
	do {                                               \
		if (X) {                                   \
			__ret = 0;                         \
			break;                             \
		}                                          \
	} while (time_before(jiffies, __t));               \
	__ret;                                             \
})


static inline bool adreno_is_a3xx(struct adreno_gpu *gpu)
{
	return (gpu->revn >= 300) && (gpu->revn < 400);
}

static inline bool adreno_is_a305(struct adreno_gpu *gpu)
{
	return gpu->revn == 305;
}

static inline bool adreno_is_a320(struct adreno_gpu *gpu)
{
	return gpu->revn == 320;
}

static inline bool adreno_is_a330(struct adreno_gpu *gpu)
{
	return gpu->revn == 330;
}

static inline bool adreno_is_a330v2(struct adreno_gpu *gpu)
{
	return adreno_is_a330(gpu) && (gpu->rev.patchid > 0);
}

int adreno_get_param(struct msm_gpu *gpu, uint32_t param, uint64_t *value);
int adreno_hw_init(struct msm_gpu *gpu);
uint32_t adreno_last_fence(struct msm_gpu *gpu);
void adreno_recover(struct msm_gpu *gpu);
int adreno_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit,
		struct msm_file_private *ctx);
void adreno_flush(struct msm_gpu *gpu);
void adreno_idle(struct msm_gpu *gpu);
#ifdef CONFIG_DEBUG_FS
void adreno_show(struct msm_gpu *gpu, struct seq_file *m);
#endif
void adreno_dump(struct msm_gpu *gpu);
void adreno_wait_ring(struct msm_gpu *gpu, uint32_t ndwords);

int adreno_gpu_init(struct drm_device *drm, struct platform_device *pdev,
		struct adreno_gpu *gpu, const struct adreno_gpu_funcs *funcs);
void adreno_gpu_cleanup(struct adreno_gpu *gpu);


/* ringbuffer helpers (the parts that are adreno specific) */

static inline void
OUT_PKT0(struct msm_ringbuffer *ring, uint16_t regindx, uint16_t cnt)
{
	adreno_wait_ring(ring->gpu, cnt+1);
	OUT_RING(ring, CP_TYPE0_PKT | ((cnt-1) << 16) | (regindx & 0x7FFF));
}

/* no-op packet: */
static inline void
OUT_PKT2(struct msm_ringbuffer *ring)
{
	adreno_wait_ring(ring->gpu, 1);
	OUT_RING(ring, CP_TYPE2_PKT);
}

static inline void
OUT_PKT3(struct msm_ringbuffer *ring, uint8_t opcode, uint16_t cnt)
{
	adreno_wait_ring(ring->gpu, cnt+1);
	OUT_RING(ring, CP_TYPE3_PKT | ((cnt-1) << 16) | ((opcode & 0xFF) << 8));
}


#endif /* __ADRENO_GPU_H__ */

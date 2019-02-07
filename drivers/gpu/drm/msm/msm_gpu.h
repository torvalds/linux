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

#ifndef __MSM_GPU_H__
#define __MSM_GPU_H__

#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#include "msm_drv.h"
#include "msm_fence.h"
#include "msm_ringbuffer.h"

struct msm_gem_submit;
struct msm_gpu_perfcntr;
struct msm_gpu_state;

struct msm_gpu_config {
	const char *ioname;
	const char *irqname;
	uint64_t va_start;
	uint64_t va_end;
	unsigned int nr_rings;
};

/* So far, with hardware that I've seen to date, we can have:
 *  + zero, one, or two z180 2d cores
 *  + a3xx or a2xx 3d core, which share a common CP (the firmware
 *    for the CP seems to implement some different PM4 packet types
 *    but the basics of cmdstream submission are the same)
 *
 * Which means that the eventual complete "class" hierarchy, once
 * support for all past and present hw is in place, becomes:
 *  + msm_gpu
 *    + adreno_gpu
 *      + a3xx_gpu
 *      + a2xx_gpu
 *    + z180_gpu
 */
struct msm_gpu_funcs {
	int (*get_param)(struct msm_gpu *gpu, uint32_t param, uint64_t *value);
	int (*hw_init)(struct msm_gpu *gpu);
	int (*pm_suspend)(struct msm_gpu *gpu);
	int (*pm_resume)(struct msm_gpu *gpu);
	void (*submit)(struct msm_gpu *gpu, struct msm_gem_submit *submit,
			struct msm_file_private *ctx);
	void (*flush)(struct msm_gpu *gpu, struct msm_ringbuffer *ring);
	irqreturn_t (*irq)(struct msm_gpu *irq);
	struct msm_ringbuffer *(*active_ring)(struct msm_gpu *gpu);
	void (*recover)(struct msm_gpu *gpu);
	void (*destroy)(struct msm_gpu *gpu);
#if defined(CONFIG_DEBUG_FS) || defined(CONFIG_DEV_COREDUMP)
	/* show GPU status in debugfs: */
	void (*show)(struct msm_gpu *gpu, struct msm_gpu_state *state,
			struct drm_printer *p);
	/* for generation specific debugfs: */
	int (*debugfs_init)(struct msm_gpu *gpu, struct drm_minor *minor);
#endif
	int (*gpu_busy)(struct msm_gpu *gpu, uint64_t *value);
	struct msm_gpu_state *(*gpu_state_get)(struct msm_gpu *gpu);
	int (*gpu_state_put)(struct msm_gpu_state *state);
};

struct msm_gpu {
	const char *name;
	struct drm_device *dev;
	struct platform_device *pdev;
	const struct msm_gpu_funcs *funcs;

	/* performance counters (hw & sw): */
	spinlock_t perf_lock;
	bool perfcntr_active;
	struct {
		bool active;
		ktime_t time;
	} last_sample;
	uint32_t totaltime, activetime;    /* sw counters */
	uint32_t last_cntrs[5];            /* hw counters */
	const struct msm_gpu_perfcntr *perfcntrs;
	uint32_t num_perfcntrs;

	struct msm_ringbuffer *rb[MSM_GPU_MAX_RINGS];
	int nr_rings;

	/* list of GEM active objects: */
	struct list_head active_list;

	/* does gpu need hw_init? */
	bool needs_hw_init;

	/* worker for handling active-list retiring: */
	struct work_struct retire_work;

	void __iomem *mmio;
	int irq;

	struct msm_gem_address_space *aspace;

	/* Power Control: */
	struct regulator *gpu_reg, *gpu_cx;
	struct clk_bulk_data *grp_clks;
	int nr_clocks;
	struct clk *ebi1_clk, *core_clk, *rbbmtimer_clk;
	uint32_t fast_rate;

	/* Hang and Inactivity Detection:
	 */
#define DRM_MSM_INACTIVE_PERIOD   66 /* in ms (roughly four frames) */

#define DRM_MSM_HANGCHECK_PERIOD 500 /* in ms */
#define DRM_MSM_HANGCHECK_JIFFIES msecs_to_jiffies(DRM_MSM_HANGCHECK_PERIOD)
	struct timer_list hangcheck_timer;
	struct work_struct recover_work;

	struct drm_gem_object *memptrs_bo;

	struct {
		struct devfreq *devfreq;
		u64 busy_cycles;
		ktime_t time;
	} devfreq;

	struct msm_gpu_state *crashstate;
};

/* It turns out that all targets use the same ringbuffer size */
#define MSM_GPU_RINGBUFFER_SZ SZ_32K
#define MSM_GPU_RINGBUFFER_BLKSIZE 32

#define MSM_GPU_RB_CNTL_DEFAULT \
		(AXXX_CP_RB_CNTL_BUFSZ(ilog2(MSM_GPU_RINGBUFFER_SZ / 8)) | \
		AXXX_CP_RB_CNTL_BLKSZ(ilog2(MSM_GPU_RINGBUFFER_BLKSIZE / 8)))

static inline bool msm_gpu_active(struct msm_gpu *gpu)
{
	int i;

	for (i = 0; i < gpu->nr_rings; i++) {
		struct msm_ringbuffer *ring = gpu->rb[i];

		if (ring->seqno > ring->memptrs->fence)
			return true;
	}

	return false;
}

/* Perf-Counters:
 * The select_reg and select_val are just there for the benefit of the child
 * class that actually enables the perf counter..  but msm_gpu base class
 * will handle sampling/displaying the counters.
 */

struct msm_gpu_perfcntr {
	uint32_t select_reg;
	uint32_t sample_reg;
	uint32_t select_val;
	const char *name;
};

struct msm_gpu_submitqueue {
	int id;
	u32 flags;
	u32 prio;
	int faults;
	struct list_head node;
	struct kref ref;
};

struct msm_gpu_state_bo {
	u64 iova;
	size_t size;
	void *data;
};

struct msm_gpu_state {
	struct kref ref;
	struct timespec64 time;

	struct {
		u64 iova;
		u32 fence;
		u32 seqno;
		u32 rptr;
		u32 wptr;
		void *data;
		int data_size;
	} ring[MSM_GPU_MAX_RINGS];

	int nr_registers;
	u32 *registers;

	u32 rbbm_status;

	char *comm;
	char *cmd;

	int nr_bos;
	struct msm_gpu_state_bo *bos;
};

static inline void gpu_write(struct msm_gpu *gpu, u32 reg, u32 data)
{
	msm_writel(data, gpu->mmio + (reg << 2));
}

static inline u32 gpu_read(struct msm_gpu *gpu, u32 reg)
{
	return msm_readl(gpu->mmio + (reg << 2));
}

static inline void gpu_rmw(struct msm_gpu *gpu, u32 reg, u32 mask, u32 or)
{
	uint32_t val = gpu_read(gpu, reg);

	val &= ~mask;
	gpu_write(gpu, reg, val | or);
}

static inline u64 gpu_read64(struct msm_gpu *gpu, u32 lo, u32 hi)
{
	u64 val;

	/*
	 * Why not a readq here? Two reasons: 1) many of the LO registers are
	 * not quad word aligned and 2) the GPU hardware designers have a bit
	 * of a history of putting registers where they fit, especially in
	 * spins. The longer a GPU family goes the higher the chance that
	 * we'll get burned.  We could do a series of validity checks if we
	 * wanted to, but really is a readq() that much better? Nah.
	 */

	/*
	 * For some lo/hi registers (like perfcounters), the hi value is latched
	 * when the lo is read, so make sure to read the lo first to trigger
	 * that
	 */
	val = (u64) msm_readl(gpu->mmio + (lo << 2));
	val |= ((u64) msm_readl(gpu->mmio + (hi << 2)) << 32);

	return val;
}

static inline void gpu_write64(struct msm_gpu *gpu, u32 lo, u32 hi, u64 val)
{
	/* Why not a writeq here? Read the screed above */
	msm_writel(lower_32_bits(val), gpu->mmio + (lo << 2));
	msm_writel(upper_32_bits(val), gpu->mmio + (hi << 2));
}

int msm_gpu_pm_suspend(struct msm_gpu *gpu);
int msm_gpu_pm_resume(struct msm_gpu *gpu);

int msm_gpu_hw_init(struct msm_gpu *gpu);

void msm_gpu_perfcntr_start(struct msm_gpu *gpu);
void msm_gpu_perfcntr_stop(struct msm_gpu *gpu);
int msm_gpu_perfcntr_sample(struct msm_gpu *gpu, uint32_t *activetime,
		uint32_t *totaltime, uint32_t ncntrs, uint32_t *cntrs);

void msm_gpu_retire(struct msm_gpu *gpu);
void msm_gpu_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit,
		struct msm_file_private *ctx);

int msm_gpu_init(struct drm_device *drm, struct platform_device *pdev,
		struct msm_gpu *gpu, const struct msm_gpu_funcs *funcs,
		const char *name, struct msm_gpu_config *config);

void msm_gpu_cleanup(struct msm_gpu *gpu);

struct msm_gpu *adreno_load_gpu(struct drm_device *dev);
void __init adreno_register(void);
void __exit adreno_unregister(void);

static inline void msm_submitqueue_put(struct msm_gpu_submitqueue *queue)
{
	if (queue)
		kref_put(&queue->ref, msm_submitqueue_destroy);
}

static inline struct msm_gpu_state *msm_gpu_crashstate_get(struct msm_gpu *gpu)
{
	struct msm_gpu_state *state = NULL;

	mutex_lock(&gpu->dev->struct_mutex);

	if (gpu->crashstate) {
		kref_get(&gpu->crashstate->ref);
		state = gpu->crashstate;
	}

	mutex_unlock(&gpu->dev->struct_mutex);

	return state;
}

static inline void msm_gpu_crashstate_put(struct msm_gpu *gpu)
{
	mutex_lock(&gpu->dev->struct_mutex);

	if (gpu->crashstate) {
		if (gpu->funcs->gpu_state_put(gpu->crashstate))
			gpu->crashstate = NULL;
	}

	mutex_unlock(&gpu->dev->struct_mutex);
}

#endif /* __MSM_GPU_H__ */

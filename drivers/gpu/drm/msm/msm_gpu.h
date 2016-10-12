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
	void (*flush)(struct msm_gpu *gpu);
	void (*idle)(struct msm_gpu *gpu);
	irqreturn_t (*irq)(struct msm_gpu *irq);
	uint32_t (*last_fence)(struct msm_gpu *gpu);
	void (*recover)(struct msm_gpu *gpu);
	void (*destroy)(struct msm_gpu *gpu);
#ifdef CONFIG_DEBUG_FS
	/* show GPU status in debugfs: */
	void (*show)(struct msm_gpu *gpu, struct seq_file *m);
#endif
};

struct msm_gpu {
	const char *name;
	struct drm_device *dev;
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

	/* ringbuffer: */
	struct msm_ringbuffer *rb;
	uint32_t rb_iova;

	/* list of GEM active objects: */
	struct list_head active_list;

	/* fencing: */
	struct msm_fence_context *fctx;

	/* is gpu powered/active? */
	int active_cnt;
	bool inactive;

	/* worker for handling active-list retiring: */
	struct work_struct retire_work;

	void __iomem *mmio;
	int irq;

	struct msm_mmu *mmu;
	int id;

	/* Power Control: */
	struct regulator *gpu_reg, *gpu_cx;
	struct clk *ebi1_clk, *grp_clks[6];
	uint32_t fast_rate, slow_rate, bus_freq;

#ifdef DOWNSTREAM_CONFIG_MSM_BUS_SCALING
	struct msm_bus_scale_pdata *bus_scale_table;
	uint32_t bsc;
#endif

	/* Hang and Inactivity Detection:
	 */
#define DRM_MSM_INACTIVE_PERIOD   66 /* in ms (roughly four frames) */
#define DRM_MSM_INACTIVE_JIFFIES  msecs_to_jiffies(DRM_MSM_INACTIVE_PERIOD)
	struct timer_list inactive_timer;
	struct work_struct inactive_work;
#define DRM_MSM_HANGCHECK_PERIOD 500 /* in ms */
#define DRM_MSM_HANGCHECK_JIFFIES msecs_to_jiffies(DRM_MSM_HANGCHECK_PERIOD)
	struct timer_list hangcheck_timer;
	uint32_t hangcheck_fence;
	struct work_struct recover_work;

	struct list_head submit_list;
};

static inline bool msm_gpu_active(struct msm_gpu *gpu)
{
	return gpu->fctx->last_fence > gpu->funcs->last_fence(gpu);
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

static inline void gpu_write(struct msm_gpu *gpu, u32 reg, u32 data)
{
	msm_writel(data, gpu->mmio + (reg << 2));
}

static inline u32 gpu_read(struct msm_gpu *gpu, u32 reg)
{
	return msm_readl(gpu->mmio + (reg << 2));
}

int msm_gpu_pm_suspend(struct msm_gpu *gpu);
int msm_gpu_pm_resume(struct msm_gpu *gpu);

void msm_gpu_perfcntr_start(struct msm_gpu *gpu);
void msm_gpu_perfcntr_stop(struct msm_gpu *gpu);
int msm_gpu_perfcntr_sample(struct msm_gpu *gpu, uint32_t *activetime,
		uint32_t *totaltime, uint32_t ncntrs, uint32_t *cntrs);

void msm_gpu_retire(struct msm_gpu *gpu);
void msm_gpu_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit,
		struct msm_file_private *ctx);

int msm_gpu_init(struct drm_device *drm, struct platform_device *pdev,
		struct msm_gpu *gpu, const struct msm_gpu_funcs *funcs,
		const char *name, const char *ioname, const char *irqname, int ringsz);
void msm_gpu_cleanup(struct msm_gpu *gpu);

struct msm_gpu *adreno_load_gpu(struct drm_device *dev);
void __init adreno_register(void);
void __exit adreno_unregister(void);

#endif /* __MSM_GPU_H__ */

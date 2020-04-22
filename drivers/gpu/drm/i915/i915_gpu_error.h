/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright � 2008-2018 Intel Corporation
 */

#ifndef _I915_GPU_ERROR_H_
#define _I915_GPU_ERROR_H_

#include <linux/atomic.h>
#include <linux/kref.h>
#include <linux/ktime.h>
#include <linux/sched.h>

#include <drm/drm_mm.h>

#include "gt/intel_engine.h"
#include "gt/uc/intel_uc_fw.h"

#include "intel_device_info.h"

#include "i915_gem.h"
#include "i915_gem_gtt.h"
#include "i915_params.h"
#include "i915_scheduler.h"

struct drm_i915_private;
struct i915_vma_compress;
struct intel_engine_capture_vma;
struct intel_overlay_error_state;
struct intel_display_error_state;

struct i915_vma_coredump {
	struct i915_vma_coredump *next;

	char name[20];

	u64 gtt_offset;
	u64 gtt_size;
	u32 gtt_page_sizes;

	int num_pages;
	int page_count;
	int unused;
	u32 *pages[0];
};

struct i915_request_coredump {
	unsigned long flags;
	pid_t pid;
	u32 context;
	u32 seqno;
	u32 start;
	u32 head;
	u32 tail;
	struct i915_sched_attr sched_attr;
};

struct intel_engine_coredump {
	const struct intel_engine_cs *engine;

	bool simulated;
	u32 reset_count;

	/* position of active request inside the ring */
	u32 rq_head, rq_post, rq_tail;

	/* Register state */
	u32 ccid;
	u32 start;
	u32 tail;
	u32 head;
	u32 ctl;
	u32 mode;
	u32 hws;
	u32 ipeir;
	u32 ipehr;
	u32 esr;
	u32 bbstate;
	u32 instpm;
	u32 instps;
	u64 bbaddr;
	u64 acthd;
	u32 fault_reg;
	u64 faddr;
	u32 rc_psmi; /* sleep state */
	struct intel_instdone instdone;

	struct i915_gem_context_coredump {
		char comm[TASK_COMM_LEN];

		u64 total_runtime;
		u32 avg_runtime;

		pid_t pid;
		int active;
		int guilty;
		struct i915_sched_attr sched_attr;
	} context;

	struct i915_vma_coredump *vma;

	struct i915_request_coredump execlist[EXECLIST_MAX_PORTS];
	unsigned int num_ports;

	struct {
		u32 gfx_mode;
		union {
			u64 pdp[4];
			u32 pp_dir_base;
		};
	} vm_info;

	struct intel_engine_coredump *next;
};

struct intel_gt_coredump {
	const struct intel_gt *_gt;
	bool awake;
	bool simulated;

	/* Generic register state */
	u32 eir;
	u32 pgtbl_er;
	u32 ier;
	u32 gtier[6], ngtier;
	u32 derrmr;
	u32 forcewake;
	u32 error; /* gen6+ */
	u32 err_int; /* gen7 */
	u32 fault_data0; /* gen8, gen9 */
	u32 fault_data1; /* gen8, gen9 */
	u32 done_reg;
	u32 gac_eco;
	u32 gam_ecochk;
	u32 gab_ctl;
	u32 gfx_mode;
	u32 gtt_cache;
	u32 aux_err; /* gen12 */
	u32 sfc_done[GEN12_SFC_DONE_MAX]; /* gen12 */
	u32 gam_done; /* gen12 */

	u32 nfence;
	u64 fence[I915_MAX_NUM_FENCES];

	struct intel_engine_coredump *engine;

	struct intel_uc_coredump {
		struct intel_uc_fw guc_fw;
		struct intel_uc_fw huc_fw;
		struct i915_vma_coredump *guc_log;
	} *uc;

	struct intel_gt_coredump *next;
};

struct i915_gpu_coredump {
	struct kref ref;
	ktime_t time;
	ktime_t boottime;
	ktime_t uptime;
	unsigned long capture;

	struct drm_i915_private *i915;

	struct intel_gt_coredump *gt;

	char error_msg[128];
	bool simulated;
	bool wakelock;
	bool suspended;
	int iommu;
	u32 reset_count;
	u32 suspend_count;

	struct intel_device_info device_info;
	struct intel_runtime_info runtime_info;
	struct intel_driver_caps driver_caps;
	struct i915_params params;

	struct intel_overlay_error_state *overlay;
	struct intel_display_error_state *display;

	struct scatterlist *sgl, *fit;
};

struct i915_gpu_error {
	/* For reset and error_state handling. */
	spinlock_t lock;
	/* Protected by the above dev->gpu_error.lock. */
	struct i915_gpu_coredump *first_error;

	atomic_t pending_fb_pin;

	/** Number of times the device has been reset (global) */
	atomic_t reset_count;

	/** Number of times an engine has been reset */
	atomic_t reset_engine_count[I915_NUM_ENGINES];
};

struct drm_i915_error_state_buf {
	struct drm_i915_private *i915;
	struct scatterlist *sgl, *cur, *end;

	char *buf;
	size_t bytes;
	size_t size;
	loff_t iter;

	int err;
};

#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)

__printf(2, 3)
void i915_error_printf(struct drm_i915_error_state_buf *e, const char *f, ...);

struct i915_gpu_coredump *i915_gpu_coredump(struct drm_i915_private *i915);
void i915_capture_error_state(struct drm_i915_private *i915);

struct i915_gpu_coredump *
i915_gpu_coredump_alloc(struct drm_i915_private *i915, gfp_t gfp);

struct intel_gt_coredump *
intel_gt_coredump_alloc(struct intel_gt *gt, gfp_t gfp);

struct intel_engine_coredump *
intel_engine_coredump_alloc(struct intel_engine_cs *engine, gfp_t gfp);

struct intel_engine_capture_vma *
intel_engine_coredump_add_request(struct intel_engine_coredump *ee,
				  struct i915_request *rq,
				  gfp_t gfp);

void intel_engine_coredump_add_vma(struct intel_engine_coredump *ee,
				   struct intel_engine_capture_vma *capture,
				   struct i915_vma_compress *compress);

struct i915_vma_compress *
i915_vma_capture_prepare(struct intel_gt_coredump *gt);

void i915_vma_capture_finish(struct intel_gt_coredump *gt,
			     struct i915_vma_compress *compress);

void i915_error_state_store(struct i915_gpu_coredump *error);

static inline struct i915_gpu_coredump *
i915_gpu_coredump_get(struct i915_gpu_coredump *gpu)
{
	kref_get(&gpu->ref);
	return gpu;
}

ssize_t
i915_gpu_coredump_copy_to_buffer(struct i915_gpu_coredump *error,
				 char *buf, loff_t offset, size_t count);

void __i915_gpu_coredump_free(struct kref *kref);
static inline void i915_gpu_coredump_put(struct i915_gpu_coredump *gpu)
{
	if (gpu)
		kref_put(&gpu->ref, __i915_gpu_coredump_free);
}

struct i915_gpu_coredump *i915_first_error_state(struct drm_i915_private *i915);
void i915_reset_error_state(struct drm_i915_private *i915);
void i915_disable_error_state(struct drm_i915_private *i915, int err);

#else

static inline void i915_capture_error_state(struct drm_i915_private *i915)
{
}

static inline struct i915_gpu_coredump *
i915_gpu_coredump_alloc(struct drm_i915_private *i915, gfp_t gfp)
{
	return NULL;
}

static inline struct intel_gt_coredump *
intel_gt_coredump_alloc(struct intel_gt *gt, gfp_t gfp)
{
	return NULL;
}

static inline struct intel_engine_coredump *
intel_engine_coredump_alloc(struct intel_engine_cs *engine, gfp_t gfp)
{
	return NULL;
}

static inline struct intel_engine_capture_vma *
intel_engine_coredump_add_request(struct intel_engine_coredump *ee,
				  struct i915_request *rq,
				  gfp_t gfp)
{
	return NULL;
}

static inline void
intel_engine_coredump_add_vma(struct intel_engine_coredump *ee,
			      struct intel_engine_capture_vma *capture,
			      struct i915_vma_compress *compress)
{
}

static inline struct i915_vma_compress *
i915_vma_capture_prepare(struct intel_gt_coredump *gt)
{
	return NULL;
}

static inline void
i915_vma_capture_finish(struct intel_gt_coredump *gt,
			struct i915_vma_compress *compress)
{
}

static inline void
i915_error_state_store(struct i915_gpu_coredump *error)
{
}

static inline void i915_gpu_coredump_put(struct i915_gpu_coredump *gpu)
{
}

static inline struct i915_gpu_coredump *
i915_first_error_state(struct drm_i915_private *i915)
{
	return ERR_PTR(-ENODEV);
}

static inline void i915_reset_error_state(struct drm_i915_private *i915)
{
}

static inline void i915_disable_error_state(struct drm_i915_private *i915,
					    int err)
{
}

#endif /* IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR) */

#endif /* _I915_GPU_ERROR_H_ */

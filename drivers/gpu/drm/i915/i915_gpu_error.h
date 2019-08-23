/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright � 2008-2018 Intel Corporation
 */

#ifndef _I915_GPU_ERROR_H_
#define _I915_GPU_ERROR_H_

#include <linux/kref.h>
#include <linux/ktime.h>
#include <linux/sched.h>

#include <drm/drm_mm.h>

#include "gt/intel_engine.h"

#include "intel_device_info.h"
#include "intel_uc_fw.h"

#include "i915_gem.h"
#include "i915_gem_gtt.h"
#include "i915_params.h"
#include "i915_scheduler.h"

struct drm_i915_private;
struct intel_overlay_error_state;
struct intel_display_error_state;

struct i915_gpu_state {
	struct kref ref;
	ktime_t time;
	ktime_t boottime;
	ktime_t uptime;
	unsigned long capture;
	unsigned long epoch;

	struct drm_i915_private *i915;

	char error_msg[128];
	bool simulated;
	bool awake;
	bool wakelock;
	bool suspended;
	int iommu;
	u32 reset_count;
	u32 suspend_count;
	struct intel_device_info device_info;
	struct intel_runtime_info runtime_info;
	struct intel_driver_caps driver_caps;
	struct i915_params params;

	struct i915_error_uc {
		struct intel_uc_fw guc_fw;
		struct intel_uc_fw huc_fw;
		struct drm_i915_error_object *guc_log;
	} uc;

	/* Generic register state */
	u32 eir;
	u32 pgtbl_er;
	u32 ier;
	u32 gtier[6], ngtier;
	u32 ccid;
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

	u32 nfence;
	u64 fence[I915_MAX_NUM_FENCES];
	struct intel_overlay_error_state *overlay;
	struct intel_display_error_state *display;

	struct drm_i915_error_engine {
		int engine_id;
		/* Software tracked state */
		bool idle;
		unsigned long hangcheck_timestamp;
		struct i915_address_space *vm;
		int num_requests;
		u32 reset_count;

		/* position of active request inside the ring */
		u32 rq_head, rq_post, rq_tail;

		/* our own tracking of ring head and tail */
		u32 cpu_ring_head;
		u32 cpu_ring_tail;

		/* Register state */
		u32 start;
		u32 tail;
		u32 head;
		u32 ctl;
		u32 mode;
		u32 hws;
		u32 ipeir;
		u32 ipehr;
		u32 bbstate;
		u32 instpm;
		u32 instps;
		u64 bbaddr;
		u64 acthd;
		u32 fault_reg;
		u64 faddr;
		u32 rc_psmi; /* sleep state */
		struct intel_instdone instdone;

		struct drm_i915_error_context {
			char comm[TASK_COMM_LEN];
			pid_t pid;
			u32 hw_id;
			int active;
			int guilty;
			struct i915_sched_attr sched_attr;
		} context;

		struct drm_i915_error_object {
			u64 gtt_offset;
			u64 gtt_size;
			int num_pages;
			int page_count;
			int unused;
			u32 *pages[0];
		} *ringbuffer, *batchbuffer, *wa_batchbuffer, *ctx, *hws_page;

		struct drm_i915_error_object **user_bo;
		long user_bo_count;

		struct drm_i915_error_object *wa_ctx;
		struct drm_i915_error_object *default_state;

		struct drm_i915_error_request {
			unsigned long flags;
			long jiffies;
			pid_t pid;
			u32 context;
			u32 seqno;
			u32 start;
			u32 head;
			u32 tail;
			struct i915_sched_attr sched_attr;
		} *requests, execlist[EXECLIST_MAX_PORTS];
		unsigned int num_ports;

		struct {
			u32 gfx_mode;
			union {
				u64 pdp[4];
				u32 pp_dir_base;
			};
		} vm_info;
	} engine[I915_NUM_ENGINES];

	struct drm_i915_error_buffer {
		u32 size;
		u32 name;
		u64 gtt_offset;
		u32 read_domains;
		u32 write_domain;
		s32 fence_reg:I915_MAX_NUM_FENCE_BITS;
		u32 tiling:2;
		u32 dirty:1;
		u32 purgeable:1;
		u32 userptr:1;
		u32 cache_level:3;
	} *active_bo[I915_NUM_ENGINES], *pinned_bo;
	u32 active_bo_count[I915_NUM_ENGINES], pinned_bo_count;
	struct i915_address_space *active_vm[I915_NUM_ENGINES];

	struct scatterlist *sgl, *fit;
};

struct i915_gpu_error {
	/* For hangcheck timer */
#define DRM_I915_HANGCHECK_PERIOD 1500 /* in ms */
#define DRM_I915_HANGCHECK_JIFFIES msecs_to_jiffies(DRM_I915_HANGCHECK_PERIOD)

	struct delayed_work hangcheck_work;

	/* For reset and error_state handling. */
	spinlock_t lock;
	/* Protected by the above dev->gpu_error.lock. */
	struct i915_gpu_state *first_error;

	atomic_t pending_fb_pin;

	/**
	 * flags: Control various stages of the GPU reset
	 *
	 * #I915_RESET_BACKOFF - When we start a global reset, we need to
	 * serialise with any other users attempting to do the same, and
	 * any global resources that may be clobber by the reset (such as
	 * FENCE registers).
	 *
	 * #I915_RESET_ENGINE[num_engines] - Since the driver doesn't need to
	 * acquire the struct_mutex to reset an engine, we need an explicit
	 * flag to prevent two concurrent reset attempts in the same engine.
	 * As the number of engines continues to grow, allocate the flags from
	 * the most significant bits.
	 *
	 * #I915_WEDGED - If reset fails and we can no longer use the GPU,
	 * we set the #I915_WEDGED bit. Prior to command submission, e.g.
	 * i915_request_alloc(), this bit is checked and the sequence
	 * aborted (with -EIO reported to userspace) if set.
	 */
	unsigned long flags;
#define I915_RESET_BACKOFF	0
#define I915_RESET_MODESET	1
#define I915_RESET_ENGINE	2
#define I915_WEDGED		(BITS_PER_LONG - 1)

	/** Number of times the device has been reset (global) */
	u32 reset_count;

	/** Number of times an engine has been reset */
	u32 reset_engine_count[I915_NUM_ENGINES];

	struct mutex wedge_mutex; /* serialises wedging/unwedging */

	/**
	 * Waitqueue to signal when a hang is detected. Used to for waiters
	 * to release the struct_mutex for the reset to procede.
	 */
	wait_queue_head_t wait_queue;

	/**
	 * Waitqueue to signal when the reset has completed. Used by clients
	 * that wait for dev_priv->mm.wedged to settle.
	 */
	wait_queue_head_t reset_queue;

	struct srcu_struct reset_backoff_srcu;
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

struct i915_gpu_state *i915_capture_gpu_state(struct drm_i915_private *i915);
void i915_capture_error_state(struct drm_i915_private *dev_priv,
			      intel_engine_mask_t engine_mask,
			      const char *error_msg);

static inline struct i915_gpu_state *
i915_gpu_state_get(struct i915_gpu_state *gpu)
{
	kref_get(&gpu->ref);
	return gpu;
}

ssize_t i915_gpu_state_copy_to_buffer(struct i915_gpu_state *error,
				      char *buf, loff_t offset, size_t count);

void __i915_gpu_state_free(struct kref *kref);
static inline void i915_gpu_state_put(struct i915_gpu_state *gpu)
{
	if (gpu)
		kref_put(&gpu->ref, __i915_gpu_state_free);
}

struct i915_gpu_state *i915_first_error_state(struct drm_i915_private *i915);
void i915_reset_error_state(struct drm_i915_private *i915);
void i915_disable_error_state(struct drm_i915_private *i915, int err);

#else

static inline void i915_capture_error_state(struct drm_i915_private *dev_priv,
					    u32 engine_mask,
					    const char *error_msg)
{
}

static inline struct i915_gpu_state *
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

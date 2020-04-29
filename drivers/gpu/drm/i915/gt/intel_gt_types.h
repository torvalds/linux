/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_GT_TYPES__
#define __INTEL_GT_TYPES__

#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "uc/intel_uc.h"

#include "i915_vma.h"
#include "intel_engine_types.h"
#include "intel_llc_types.h"
#include "intel_reset_types.h"
#include "intel_rc6_types.h"
#include "intel_rps_types.h"
#include "intel_wakeref.h"

struct drm_i915_private;
struct i915_ggtt;
struct intel_engine_cs;
struct intel_uncore;

struct intel_gt {
	struct drm_i915_private *i915;
	struct intel_uncore *uncore;
	struct i915_ggtt *ggtt;

	struct intel_uc uc;

	struct intel_gt_timelines {
		spinlock_t lock; /* protects active_list */
		struct list_head active_list;

		/* Pack multiple timelines' seqnos into the same page */
		spinlock_t hwsp_lock;
		struct list_head hwsp_free_list;
	} timelines;

	struct intel_gt_requests {
		/**
		 * We leave the user IRQ off as much as possible,
		 * but this means that requests will finish and never
		 * be retired once the system goes idle. Set a timer to
		 * fire periodically while the ring is running. When it
		 * fires, go retire requests.
		 */
		struct delayed_work retire_work;
	} requests;

	struct intel_wakeref wakeref;
	atomic_t user_wakeref;

	struct list_head closed_vma;
	spinlock_t closed_lock; /* guards the list of closed_vma */

	struct intel_reset reset;

	/**
	 * Is the GPU currently considered idle, or busy executing
	 * userspace requests? Whilst idle, we allow runtime power
	 * management to power down the hardware and display clocks.
	 * In order to reduce the effect on performance, there
	 * is a slight delay before we do so.
	 */
	intel_wakeref_t awake;

	struct intel_llc llc;
	struct intel_rc6 rc6;
	struct intel_rps rps;

	ktime_t last_init_time;

	struct i915_vma *scratch;

	spinlock_t irq_lock;
	u32 gt_imr;
	u32 pm_ier;
	u32 pm_imr;

	u32 pm_guc_events;

	struct intel_engine_cs *engine[I915_NUM_ENGINES];
	struct intel_engine_cs *engine_class[MAX_ENGINE_CLASS + 1]
					    [MAX_ENGINE_INSTANCE + 1];

	/*
	 * Default address space (either GGTT or ppGTT depending on arch).
	 *
	 * Reserved for exclusive use by the kernel.
	 */
	struct i915_address_space *vm;
};

enum intel_gt_scratch_field {
	/* 8 bytes */
	INTEL_GT_SCRATCH_FIELD_DEFAULT = 0,

	/* 8 bytes */
	INTEL_GT_SCRATCH_FIELD_RENDER_FLUSH = 128,

	/* 8 bytes */
	INTEL_GT_SCRATCH_FIELD_COHERENTL3_WA = 256,

	/* 6 * 8 bytes */
	INTEL_GT_SCRATCH_FIELD_PERF_CS_GPR = 2048,

	/* 4 bytes */
	INTEL_GT_SCRATCH_FIELD_PERF_PREDICATE_RESULT_1 = 2096,
};

#endif /* __INTEL_GT_TYPES_H__ */

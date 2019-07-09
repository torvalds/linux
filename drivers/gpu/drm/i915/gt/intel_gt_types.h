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

#include "i915_vma.h"
#include "intel_wakeref.h"

struct drm_i915_private;
struct i915_ggtt;
struct intel_uncore;

struct intel_gt {
	struct drm_i915_private *i915;
	struct intel_uncore *uncore;
	struct i915_ggtt *ggtt;

	struct intel_gt_timelines {
		struct mutex mutex; /* protects list */
		struct list_head active_list;

		/* Pack multiple timelines' seqnos into the same page */
		spinlock_t hwsp_lock;
		struct list_head hwsp_free_list;
	} timelines;

	struct list_head active_rings;

	struct intel_wakeref wakeref;

	struct list_head closed_vma;
	spinlock_t closed_lock; /* guards the list of closed_vma */

	/**
	 * Is the GPU currently considered idle, or busy executing
	 * userspace requests? Whilst idle, we allow runtime power
	 * management to power down the hardware and display clocks.
	 * In order to reduce the effect on performance, there
	 * is a slight delay before we do so.
	 */
	intel_wakeref_t awake;

	struct blocking_notifier_head pm_notifications;

	ktime_t last_init_time;

	struct i915_vma *scratch;

	u32 pm_imr;
	u32 pm_ier;
};

enum intel_gt_scratch_field {
	/* 8 bytes */
	INTEL_GT_SCRATCH_FIELD_DEFAULT = 0,

	/* 8 bytes */
	INTEL_GT_SCRATCH_FIELD_CLEAR_SLM_WA = 128,

	/* 8 bytes */
	INTEL_GT_SCRATCH_FIELD_RENDER_FLUSH = 128,

	/* 8 bytes */
	INTEL_GT_SCRATCH_FIELD_COHERENTL3_WA = 256,

};

#endif /* __INTEL_GT_TYPES_H__ */

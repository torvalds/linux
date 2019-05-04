/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_CONTEXT_TYPES__
#define __INTEL_CONTEXT_TYPES__

#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "i915_active_types.h"
#include "intel_engine_types.h"
#include "intel_sseu.h"

struct i915_gem_context;
struct i915_vma;
struct intel_context;
struct intel_ring;

struct intel_context_ops {
	int (*pin)(struct intel_context *ce);
	void (*unpin)(struct intel_context *ce);

	void (*enter)(struct intel_context *ce);
	void (*exit)(struct intel_context *ce);

	void (*reset)(struct intel_context *ce);
	void (*destroy)(struct kref *kref);
};

struct intel_context {
	struct kref ref;

	struct i915_gem_context *gem_context;
	struct intel_engine_cs *engine;
	struct intel_engine_cs *active;

	struct list_head signal_link;
	struct list_head signals;

	struct i915_vma *state;
	struct intel_ring *ring;

	u32 *lrc_reg_state;
	u64 lrc_desc;

	unsigned int active_count; /* notionally protected by timeline->mutex */

	atomic_t pin_count;
	struct mutex pin_mutex; /* guards pinning and associated on-gpuing */

	intel_engine_mask_t saturated; /* submitting semaphores too late? */

	/**
	 * active_tracker: Active tracker for the external rq activity
	 * on this intel_context object.
	 */
	struct i915_active_request active_tracker;

	const struct intel_context_ops *ops;

	/** sseu: Control eu/slice partitioning */
	struct intel_sseu sseu;
};

#endif /* __INTEL_CONTEXT_TYPES__ */

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
#include "i915_utils.h"
#include "intel_engine_types.h"
#include "intel_sseu.h"

struct i915_gem_context;
struct i915_vma;
struct intel_context;
struct intel_ring;

struct intel_context_ops {
	int (*alloc)(struct intel_context *ce);

	int (*pin)(struct intel_context *ce);
	void (*unpin)(struct intel_context *ce);

	void (*enter)(struct intel_context *ce);
	void (*exit)(struct intel_context *ce);

	void (*reset)(struct intel_context *ce);
	void (*destroy)(struct kref *kref);
};

struct intel_context {
	struct kref ref;

	struct intel_engine_cs *engine;
	struct intel_engine_cs *inflight;
#define intel_context_inflight(ce) ptr_mask_bits((ce)->inflight, 2)
#define intel_context_inflight_count(ce) ptr_unmask_bits((ce)->inflight, 2)

	struct i915_address_space *vm;
	struct i915_gem_context *gem_context;

	struct list_head signal_link;
	struct list_head signals;

	struct i915_vma *state;
	struct intel_ring *ring;
	struct intel_timeline *timeline;

	unsigned long flags;
#define CONTEXT_ALLOC_BIT 0

	u32 *lrc_reg_state;
	u64 lrc_desc;

	unsigned int active_count; /* protected by timeline->mutex */

	atomic_t pin_count;
	struct mutex pin_mutex; /* guards pinning and associated on-gpuing */

	/**
	 * active: Active tracker for the rq activity (inc. external) on this
	 * intel_context object.
	 */
	struct i915_active active;

	const struct intel_context_ops *ops;

	/** sseu: Control eu/slice partitioning */
	struct intel_sseu sseu;
};

#endif /* __INTEL_CONTEXT_TYPES__ */

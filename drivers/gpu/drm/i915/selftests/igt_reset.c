/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include "igt_reset.h"

#include "../i915_drv.h"
#include "../intel_ringbuffer.h"

void igt_global_reset_lock(struct drm_i915_private *i915)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	pr_debug("%s: current gpu_error=%08lx\n",
		 __func__, i915->gpu_error.flags);

	while (test_and_set_bit(I915_RESET_BACKOFF, &i915->gpu_error.flags))
		wait_event(i915->gpu_error.reset_queue,
			   !test_bit(I915_RESET_BACKOFF,
				     &i915->gpu_error.flags));

	for_each_engine(engine, i915, id) {
		while (test_and_set_bit(I915_RESET_ENGINE + id,
					&i915->gpu_error.flags))
			wait_on_bit(&i915->gpu_error.flags,
				    I915_RESET_ENGINE + id,
				    TASK_UNINTERRUPTIBLE);
	}
}

void igt_global_reset_unlock(struct drm_i915_private *i915)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, i915, id)
		clear_bit(I915_RESET_ENGINE + id, &i915->gpu_error.flags);

	clear_bit(I915_RESET_BACKOFF, &i915->gpu_error.flags);
	wake_up_all(&i915->gpu_error.reset_queue);
}

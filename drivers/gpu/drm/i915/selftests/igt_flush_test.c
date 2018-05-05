/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include "../i915_drv.h"

#include "../i915_selftest.h"
#include "igt_flush_test.h"

struct wedge_me {
	struct delayed_work work;
	struct drm_i915_private *i915;
	const void *symbol;
};

static void wedge_me(struct work_struct *work)
{
	struct wedge_me *w = container_of(work, typeof(*w), work.work);

	pr_err("%pS timed out, cancelling all further testing.\n", w->symbol);

	GEM_TRACE("%pS timed out.\n", w->symbol);
	GEM_TRACE_DUMP();

	i915_gem_set_wedged(w->i915);
}

static void __init_wedge(struct wedge_me *w,
			 struct drm_i915_private *i915,
			 long timeout,
			 const void *symbol)
{
	w->i915 = i915;
	w->symbol = symbol;

	INIT_DELAYED_WORK_ONSTACK(&w->work, wedge_me);
	schedule_delayed_work(&w->work, timeout);
}

static void __fini_wedge(struct wedge_me *w)
{
	cancel_delayed_work_sync(&w->work);
	destroy_delayed_work_on_stack(&w->work);
	w->i915 = NULL;
}

#define wedge_on_timeout(W, DEV, TIMEOUT)				\
	for (__init_wedge((W), (DEV), (TIMEOUT), __builtin_return_address(0)); \
	     (W)->i915;							\
	     __fini_wedge((W)))

int igt_flush_test(struct drm_i915_private *i915, unsigned int flags)
{
	struct wedge_me w;

	cond_resched();

	wedge_on_timeout(&w, i915, HZ)
		i915_gem_wait_for_idle(i915, flags);

	return i915_terminally_wedged(&i915->gpu_error) ? -EIO : 0;
}

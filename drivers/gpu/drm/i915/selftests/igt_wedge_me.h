/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#ifndef IGT_WEDGE_ME_H
#define IGT_WEDGE_ME_H

#include <linux/workqueue.h>

#include "../i915_gem.h"

struct drm_i915_private;

struct igt_wedge_me {
	struct delayed_work work;
	struct drm_i915_private *i915;
	const char *name;
};

static void __igt_wedge_me(struct work_struct *work)
{
	struct igt_wedge_me *w = container_of(work, typeof(*w), work.work);

	pr_err("%s timed out, cancelling test.\n", w->name);

	GEM_TRACE("%s timed out.\n", w->name);
	GEM_TRACE_DUMP();

	i915_gem_set_wedged(w->i915);
}

static void __igt_init_wedge(struct igt_wedge_me *w,
			     struct drm_i915_private *i915,
			     long timeout,
			     const char *name)
{
	w->i915 = i915;
	w->name = name;

	INIT_DELAYED_WORK_ONSTACK(&w->work, __igt_wedge_me);
	schedule_delayed_work(&w->work, timeout);
}

static void __igt_fini_wedge(struct igt_wedge_me *w)
{
	cancel_delayed_work_sync(&w->work);
	destroy_delayed_work_on_stack(&w->work);
	w->i915 = NULL;
}

#define igt_wedge_on_timeout(W, DEV, TIMEOUT)				\
	for (__igt_init_wedge((W), (DEV), (TIMEOUT), __func__);		\
	     (W)->i915;							\
	     __igt_fini_wedge((W)))

#endif /* IGT_WEDGE_ME_H */

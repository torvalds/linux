/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2016 Intel Corporation
 */

#ifndef __I915_TIMELINE_TYPES_H__
#define __I915_TIMELINE_TYPES_H__

#include <linux/list.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "i915_active_types.h"

struct drm_i915_private;
struct i915_vma;
struct i915_timeline_cacheline;
struct i915_syncmap;

struct i915_timeline {
	u64 fence_context;
	u32 seqno;

	spinlock_t lock;
#define TIMELINE_CLIENT 0 /* default subclass */
#define TIMELINE_ENGINE 1
	struct mutex mutex; /* protects the flow of requests */

	unsigned int pin_count;
	const u32 *hwsp_seqno;
	struct i915_vma *hwsp_ggtt;
	u32 hwsp_offset;

	struct i915_timeline_cacheline *hwsp_cacheline;

	bool has_initial_breadcrumb;

	/**
	 * List of breadcrumbs associated with GPU requests currently
	 * outstanding.
	 */
	struct list_head requests;

	/* Contains an RCU guarded pointer to the last request. No reference is
	 * held to the request, users must carefully acquire a reference to
	 * the request using i915_active_request_get_request_rcu(), or hold the
	 * struct_mutex.
	 */
	struct i915_active_request last_request;

	/**
	 * We track the most recent seqno that we wait on in every context so
	 * that we only have to emit a new await and dependency on a more
	 * recent sync point. As the contexts may be executed out-of-order, we
	 * have to track each individually and can not rely on an absolute
	 * global_seqno. When we know that all tracked fences are completed
	 * (i.e. when the driver is idle), we know that the syncmap is
	 * redundant and we can discard it without loss of generality.
	 */
	struct i915_syncmap *sync;

	/**
	 * Barrier provides the ability to serialize ordering between different
	 * timelines.
	 *
	 * Users can call i915_timeline_set_barrier which will make all
	 * subsequent submissions to this timeline be executed only after the
	 * barrier has been completed.
	 */
	struct i915_active_request barrier;

	struct list_head link;
	struct drm_i915_private *i915;

	struct kref kref;
};

#endif /* __I915_TIMELINE_TYPES_H__ */

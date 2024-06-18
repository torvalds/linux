/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2016 Intel Corporation
 */

#ifndef __I915_TIMELINE_TYPES_H__
#define __I915_TIMELINE_TYPES_H__

#include <linux/list.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/types.h>

#include "i915_active_types.h"

struct i915_vma;
struct i915_syncmap;
struct intel_gt;

struct intel_timeline {
	u64 fence_context;
	u32 seqno;

	struct mutex mutex; /* protects the flow of requests */

	/*
	 * pin_count and active_count track essentially the same thing:
	 * How many requests are in flight or may be under construction.
	 *
	 * We need two distinct counters so that we can assign different
	 * lifetimes to the events for different use-cases. For example,
	 * we want to permanently keep the timeline pinned for the kernel
	 * context so that we can issue requests at any time without having
	 * to acquire space in the GGTT. However, we want to keep tracking
	 * the activity (to be able to detect when we become idle) along that
	 * permanently pinned timeline and so end up requiring two counters.
	 *
	 * Note that the active_count is protected by the intel_timeline.mutex,
	 * but the pin_count is protected by a combination of serialisation
	 * from the intel_context caller plus internal atomicity.
	 */
	atomic_t pin_count;
	atomic_t active_count;

	void *hwsp_map;
	const u32 *hwsp_seqno;
	struct i915_vma *hwsp_ggtt;
	u32 hwsp_offset;

	bool has_initial_breadcrumb;

	/**
	 * List of breadcrumbs associated with GPU requests currently
	 * outstanding.
	 */
	struct list_head requests;

	/*
	 * Contains an RCU guarded pointer to the last request. No reference is
	 * held to the request, users must carefully acquire a reference to
	 * the request using i915_active_fence_get(), or manage the RCU
	 * protection themselves (cf the i915_active_fence API).
	 */
	struct i915_active_fence last_request;

	struct i915_active active;

	/** A chain of completed timelines ready for early retirement. */
	struct intel_timeline *retire;

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

	struct list_head link;
	struct intel_gt *gt;

	struct list_head engine_link;

	struct kref kref;
	struct rcu_head rcu;
};

#endif /* __I915_TIMELINE_TYPES_H__ */

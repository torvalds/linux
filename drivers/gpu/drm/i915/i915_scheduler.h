/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#ifndef _I915_SCHEDULER_H_
#define _I915_SCHEDULER_H_

#include <linux/bitops.h>
#include <linux/kernel.h>

#include <uapi/drm/i915_drm.h>

struct drm_i915_private;
struct i915_request;
struct intel_engine_cs;

enum {
	I915_PRIORITY_MIN = I915_CONTEXT_MIN_USER_PRIORITY - 1,
	I915_PRIORITY_NORMAL = I915_CONTEXT_DEFAULT_PRIORITY,
	I915_PRIORITY_MAX = I915_CONTEXT_MAX_USER_PRIORITY + 1,

	I915_PRIORITY_INVALID = INT_MIN
};

#define I915_USER_PRIORITY_SHIFT 3
#define I915_USER_PRIORITY(x) ((x) << I915_USER_PRIORITY_SHIFT)

#define I915_PRIORITY_COUNT BIT(I915_USER_PRIORITY_SHIFT)
#define I915_PRIORITY_MASK (I915_PRIORITY_COUNT - 1)

#define I915_PRIORITY_WAIT		((u8)BIT(0))
#define I915_PRIORITY_NEWCLIENT		((u8)BIT(1))
#define I915_PRIORITY_NOSEMAPHORE	((u8)BIT(2))

#define __NO_PREEMPTION (I915_PRIORITY_WAIT)

struct i915_sched_attr {
	/**
	 * @priority: execution and service priority
	 *
	 * All clients are equal, but some are more equal than others!
	 *
	 * Requests from a context with a greater (more positive) value of
	 * @priority will be executed before those with a lower @priority
	 * value, forming a simple QoS.
	 *
	 * The &drm_i915_private.kernel_context is assigned the lowest priority.
	 */
	int priority;
};

/*
 * "People assume that time is a strict progression of cause to effect, but
 * actually, from a nonlinear, non-subjective viewpoint, it's more like a big
 * ball of wibbly-wobbly, timey-wimey ... stuff." -The Doctor, 2015
 *
 * Requests exist in a complex web of interdependencies. Each request
 * has to wait for some other request to complete before it is ready to be run
 * (e.g. we have to wait until the pixels have been rendering into a texture
 * before we can copy from it). We track the readiness of a request in terms
 * of fences, but we also need to keep the dependency tree for the lifetime
 * of the request (beyond the life of an individual fence). We use the tree
 * at various points to reorder the requests whilst keeping the requests
 * in order with respect to their various dependencies.
 *
 * There is no active component to the "scheduler". As we know the dependency
 * DAG of each request, we are able to insert it into a sorted queue when it
 * is ready, and are able to reorder its portion of the graph to accommodate
 * dynamic priority changes.
 */
struct i915_sched_node {
	struct list_head signalers_list; /* those before us, we depend upon */
	struct list_head waiters_list; /* those after us, they depend upon us */
	struct list_head link;
	struct i915_sched_attr attr;
	unsigned int flags;
#define I915_SCHED_HAS_SEMAPHORE	BIT(0)
};

struct i915_dependency {
	struct i915_sched_node *signaler;
	struct list_head signal_link;
	struct list_head wait_link;
	struct list_head dfs_link;
	unsigned long flags;
#define I915_DEPENDENCY_ALLOC BIT(0)
};

struct i915_priolist {
	struct list_head requests[I915_PRIORITY_COUNT];
	struct rb_node node;
	unsigned long used;
	int priority;
};

#define priolist_for_each_request(it, plist, idx) \
	for (idx = 0; idx < ARRAY_SIZE((plist)->requests); idx++) \
		list_for_each_entry(it, &(plist)->requests[idx], sched.link)

#define priolist_for_each_request_consume(it, n, plist, idx) \
	for (; \
	     (plist)->used ? (idx = __ffs((plist)->used)), 1 : 0; \
	     (plist)->used &= ~BIT(idx)) \
		list_for_each_entry_safe(it, n, \
					 &(plist)->requests[idx], \
					 sched.link)

void i915_sched_node_init(struct i915_sched_node *node);

bool __i915_sched_node_add_dependency(struct i915_sched_node *node,
				      struct i915_sched_node *signal,
				      struct i915_dependency *dep,
				      unsigned long flags);

int i915_sched_node_add_dependency(struct i915_sched_node *node,
				   struct i915_sched_node *signal);

void i915_sched_node_fini(struct i915_sched_node *node);

void i915_schedule(struct i915_request *request,
		   const struct i915_sched_attr *attr);

void i915_schedule_bump_priority(struct i915_request *rq, unsigned int bump);

struct list_head *
i915_sched_lookup_priolist(struct intel_engine_cs *engine, int prio);

void __i915_priolist_free(struct i915_priolist *p);
static inline void i915_priolist_free(struct i915_priolist *p)
{
	if (p->priority != I915_PRIORITY_NORMAL)
		__i915_priolist_free(p);
}

#endif /* _I915_SCHEDULER_H_ */

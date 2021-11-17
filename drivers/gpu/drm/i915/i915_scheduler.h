/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#ifndef _I915_SCHEDULER_H_
#define _I915_SCHEDULER_H_

#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/kernel.h>

#include "i915_scheduler_types.h"

struct drm_printer;

#define priolist_for_each_request(it, plist) \
	list_for_each_entry(it, &(plist)->requests, sched.link)

#define priolist_for_each_request_consume(it, n, plist) \
	list_for_each_entry_safe(it, n, &(plist)->requests, sched.link)

void i915_sched_node_init(struct i915_sched_node *node);
void i915_sched_node_reinit(struct i915_sched_node *node);

bool __i915_sched_node_add_dependency(struct i915_sched_node *node,
				      struct i915_sched_node *signal,
				      struct i915_dependency *dep,
				      unsigned long flags);

int i915_sched_node_add_dependency(struct i915_sched_node *node,
				   struct i915_sched_node *signal,
				   unsigned long flags);

void i915_sched_node_fini(struct i915_sched_node *node);

void i915_schedule(struct i915_request *request,
		   const struct i915_sched_attr *attr);

struct list_head *
i915_sched_lookup_priolist(struct i915_sched_engine *sched_engine, int prio);

void __i915_priolist_free(struct i915_priolist *p);
static inline void i915_priolist_free(struct i915_priolist *p)
{
	if (p->priority != I915_PRIORITY_NORMAL)
		__i915_priolist_free(p);
}

struct i915_sched_engine *
i915_sched_engine_create(unsigned int subclass);

static inline struct i915_sched_engine *
i915_sched_engine_get(struct i915_sched_engine *sched_engine)
{
	kref_get(&sched_engine->ref);
	return sched_engine;
}

static inline void
i915_sched_engine_put(struct i915_sched_engine *sched_engine)
{
	kref_put(&sched_engine->ref, sched_engine->destroy);
}

static inline bool
i915_sched_engine_is_empty(struct i915_sched_engine *sched_engine)
{
	return RB_EMPTY_ROOT(&sched_engine->queue.rb_root);
}

static inline void
i915_sched_engine_reset_on_empty(struct i915_sched_engine *sched_engine)
{
	if (i915_sched_engine_is_empty(sched_engine))
		sched_engine->no_priolist = false;
}

static inline void
i915_sched_engine_active_lock_bh(struct i915_sched_engine *sched_engine)
{
	local_bh_disable(); /* prevent local softirq and lock recursion */
	tasklet_lock(&sched_engine->tasklet);
}

static inline void
i915_sched_engine_active_unlock_bh(struct i915_sched_engine *sched_engine)
{
	tasklet_unlock(&sched_engine->tasklet);
	local_bh_enable(); /* restore softirq, and kick ksoftirqd! */
}

void i915_request_show_with_schedule(struct drm_printer *m,
				     const struct i915_request *rq,
				     const char *prefix,
				     int indent);

static inline bool
i915_sched_engine_disabled(struct i915_sched_engine *sched_engine)
{
	return sched_engine->disabled(sched_engine);
}

void i915_scheduler_module_exit(void);
int i915_scheduler_module_init(void);

#endif /* _I915_SCHEDULER_H_ */

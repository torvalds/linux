/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include <linux/slab.h>
#include <linux/workqueue.h>

#include "i915_active.h"
#include "gem/i915_gem_context.h"
#include "gem/i915_gem_object.h"
#include "i915_globals.h"
#include "i915_request.h"
#include "i915_scheduler.h"
#include "i915_vma.h"

static LIST_HEAD(globals);

static atomic_t active;
static atomic_t epoch;
static struct park_work {
	struct delayed_work work;
	struct rcu_head rcu;
	unsigned long flags;
#define PENDING 0
	int epoch;
} park;

static void i915_globals_shrink(void)
{
	struct i915_global *global;

	/*
	 * kmem_cache_shrink() discards empty slabs and reorders partially
	 * filled slabs to prioritise allocating from the mostly full slabs,
	 * with the aim of reducing fragmentation.
	 */
	list_for_each_entry(global, &globals, link)
		global->shrink();
}

static void __i915_globals_grace(struct rcu_head *rcu)
{
	/* Ratelimit parking as shrinking is quite slow */
	schedule_delayed_work(&park.work, round_jiffies_up_relative(2 * HZ));
}

static void __i915_globals_queue_rcu(void)
{
	park.epoch = atomic_inc_return(&epoch);
	if (!atomic_read(&active)) {
		init_rcu_head(&park.rcu);
		call_rcu(&park.rcu, __i915_globals_grace);
	}
}

static void __i915_globals_park(struct work_struct *work)
{
	destroy_rcu_head(&park.rcu);

	/* Confirm nothing woke up in the last grace period */
	if (park.epoch != atomic_read(&epoch)) {
		__i915_globals_queue_rcu();
		return;
	}

	clear_bit(PENDING, &park.flags);
	i915_globals_shrink();
}

void __init i915_global_register(struct i915_global *global)
{
	GEM_BUG_ON(!global->shrink);
	GEM_BUG_ON(!global->exit);

	list_add_tail(&global->link, &globals);
}

static void __i915_globals_cleanup(void)
{
	struct i915_global *global, *next;

	list_for_each_entry_safe_reverse(global, next, &globals, link)
		global->exit();
}

static __initconst int (* const initfn[])(void) = {
	i915_global_active_init,
	i915_global_buddy_init,
	i915_global_context_init,
	i915_global_gem_context_init,
	i915_global_objects_init,
	i915_global_request_init,
	i915_global_scheduler_init,
	i915_global_vma_init,
};

int __init i915_globals_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(initfn); i++) {
		int err;

		err = initfn[i]();
		if (err) {
			__i915_globals_cleanup();
			return err;
		}
	}

	INIT_DELAYED_WORK(&park.work, __i915_globals_park);
	return 0;
}

void i915_globals_park(void)
{
	/*
	 * Defer shrinking the global slab caches (and other work) until
	 * after a RCU grace period has completed with no activity. This
	 * is to try and reduce the latency impact on the consumers caused
	 * by us shrinking the caches the same time as they are trying to
	 * allocate, with the assumption being that if we idle long enough
	 * for an RCU grace period to elapse since the last use, it is likely
	 * to be longer until we need the caches again.
	 */
	if (!atomic_dec_and_test(&active))
		return;

	/* Queue cleanup after the next RCU grace period has freed slabs */
	if (!test_and_set_bit(PENDING, &park.flags))
		__i915_globals_queue_rcu();
}

void i915_globals_unpark(void)
{
	atomic_inc(&epoch);
	atomic_inc(&active);
}

static void __exit __i915_globals_flush(void)
{
	atomic_inc(&active); /* skip shrinking */

	rcu_barrier(); /* wait for the work to be queued */
	flush_delayed_work(&park.work);

	atomic_dec(&active);
}

void __exit i915_globals_exit(void)
{
	GEM_BUG_ON(atomic_read(&active));

	__i915_globals_flush();
	__i915_globals_cleanup();

	/* And ensure that our DESTROY_BY_RCU slabs are truly destroyed */
	rcu_barrier();
}

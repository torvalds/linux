/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include <linux/slab.h>
#include <linux/workqueue.h>

#include "i915_active.h"
#include "i915_gem_context.h"
#include "i915_gem_object.h"
#include "i915_globals.h"
#include "i915_request.h"
#include "i915_scheduler.h"
#include "i915_vma.h"

int __init i915_globals_init(void)
{
	int err;

	err = i915_global_active_init();
	if (err)
		return err;

	err = i915_global_context_init();
	if (err)
		goto err_active;

	err = i915_global_objects_init();
	if (err)
		goto err_context;

	err = i915_global_request_init();
	if (err)
		goto err_objects;

	err = i915_global_scheduler_init();
	if (err)
		goto err_request;

	err = i915_global_vma_init();
	if (err)
		goto err_scheduler;

	return 0;

err_scheduler:
	i915_global_scheduler_exit();
err_request:
	i915_global_request_exit();
err_objects:
	i915_global_objects_exit();
err_context:
	i915_global_context_exit();
err_active:
	i915_global_active_exit();
	return err;
}

static void i915_globals_shrink(void)
{
	/*
	 * kmem_cache_shrink() discards empty slabs and reorders partially
	 * filled slabs to prioritise allocating from the mostly full slabs,
	 * with the aim of reducing fragmentation.
	 */
	i915_global_active_shrink();
	i915_global_context_shrink();
	i915_global_objects_shrink();
	i915_global_request_shrink();
	i915_global_scheduler_shrink();
	i915_global_vma_shrink();
}

static atomic_t active;
static atomic_t epoch;
struct park_work {
	struct rcu_work work;
	int epoch;
};

static void __i915_globals_park(struct work_struct *work)
{
	struct park_work *wrk = container_of(work, typeof(*wrk), work.work);

	/* Confirm nothing woke up in the last grace period */
	if (wrk->epoch == atomic_read(&epoch))
		i915_globals_shrink();

	kfree(wrk);
}

void i915_globals_park(void)
{
	struct park_work *wrk;

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

	wrk = kmalloc(sizeof(*wrk), GFP_KERNEL);
	if (!wrk)
		return;

	wrk->epoch = atomic_inc_return(&epoch);
	INIT_RCU_WORK(&wrk->work, __i915_globals_park);
	queue_rcu_work(system_wq, &wrk->work);
}

void i915_globals_unpark(void)
{
	atomic_inc(&epoch);
	atomic_inc(&active);
}

void __exit i915_globals_exit(void)
{
	/* Flush any residual park_work */
	rcu_barrier();
	flush_scheduled_work();

	i915_global_vma_exit();
	i915_global_scheduler_exit();
	i915_global_request_exit();
	i915_global_objects_exit();
	i915_global_context_exit();
	i915_global_active_exit();

	/* And ensure that our DESTROY_BY_RCU slabs are truly destroyed */
	rcu_barrier();
}

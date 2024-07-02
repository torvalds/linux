// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_preempt_fence.h"

#include <linux/slab.h>

#include "xe_exec_queue.h"
#include "xe_vm.h"

static void preempt_fence_work_func(struct work_struct *w)
{
	bool cookie = dma_fence_begin_signalling();
	struct xe_preempt_fence *pfence =
		container_of(w, typeof(*pfence), preempt_work);
	struct xe_exec_queue *q = pfence->q;

	if (pfence->error)
		dma_fence_set_error(&pfence->base, pfence->error);
	else
		q->ops->suspend_wait(q);

	dma_fence_signal(&pfence->base);
	dma_fence_end_signalling(cookie);

	xe_vm_queue_rebind_worker(q->vm);

	xe_exec_queue_put(q);
}

static const char *
preempt_fence_get_driver_name(struct dma_fence *fence)
{
	return "xe";
}

static const char *
preempt_fence_get_timeline_name(struct dma_fence *fence)
{
	return "preempt";
}

static bool preempt_fence_enable_signaling(struct dma_fence *fence)
{
	struct xe_preempt_fence *pfence =
		container_of(fence, typeof(*pfence), base);
	struct xe_exec_queue *q = pfence->q;

	pfence->error = q->ops->suspend(q);
	queue_work(q->vm->xe->preempt_fence_wq, &pfence->preempt_work);
	return true;
}

static const struct dma_fence_ops preempt_fence_ops = {
	.get_driver_name = preempt_fence_get_driver_name,
	.get_timeline_name = preempt_fence_get_timeline_name,
	.enable_signaling = preempt_fence_enable_signaling,
};

/**
 * xe_preempt_fence_alloc() - Allocate a preempt fence with minimal
 * initialization
 *
 * Allocate a preempt fence, and initialize its list head.
 * If the preempt_fence allocated has been armed with
 * xe_preempt_fence_arm(), it must be freed using dma_fence_put(). If not,
 * it must be freed using xe_preempt_fence_free().
 *
 * Return: A struct xe_preempt_fence pointer used for calling into
 * xe_preempt_fence_arm() or xe_preempt_fence_free().
 * An error pointer on error.
 */
struct xe_preempt_fence *xe_preempt_fence_alloc(void)
{
	struct xe_preempt_fence *pfence;

	pfence = kmalloc(sizeof(*pfence), GFP_KERNEL);
	if (!pfence)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&pfence->link);
	INIT_WORK(&pfence->preempt_work, preempt_fence_work_func);

	return pfence;
}

/**
 * xe_preempt_fence_free() - Free a preempt fence allocated using
 * xe_preempt_fence_alloc().
 * @pfence: pointer obtained from xe_preempt_fence_alloc();
 *
 * Free a preempt fence that has not yet been armed.
 */
void xe_preempt_fence_free(struct xe_preempt_fence *pfence)
{
	list_del(&pfence->link);
	kfree(pfence);
}

/**
 * xe_preempt_fence_arm() - Arm a preempt fence allocated using
 * xe_preempt_fence_alloc().
 * @pfence: The struct xe_preempt_fence pointer returned from
 *          xe_preempt_fence_alloc().
 * @q: The struct xe_exec_queue used for arming.
 * @context: The dma-fence context used for arming.
 * @seqno: The dma-fence seqno used for arming.
 *
 * Inserts the preempt fence into @context's timeline, takes @link off any
 * list, and registers the struct xe_exec_queue as the xe_engine to be preempted.
 *
 * Return: A pointer to a struct dma_fence embedded into the preempt fence.
 * This function doesn't error.
 */
struct dma_fence *
xe_preempt_fence_arm(struct xe_preempt_fence *pfence, struct xe_exec_queue *q,
		     u64 context, u32 seqno)
{
	list_del_init(&pfence->link);
	pfence->q = xe_exec_queue_get(q);
	dma_fence_init(&pfence->base, &preempt_fence_ops,
		      &q->compute.lock, context, seqno);

	return &pfence->base;
}

/**
 * xe_preempt_fence_create() - Helper to create and arm a preempt fence.
 * @q: The struct xe_exec_queue used for arming.
 * @context: The dma-fence context used for arming.
 * @seqno: The dma-fence seqno used for arming.
 *
 * Allocates and inserts the preempt fence into @context's timeline,
 * and registers @e as the struct xe_exec_queue to be preempted.
 *
 * Return: A pointer to the resulting struct dma_fence on success. An error
 * pointer on error. In particular if allocation fails it returns
 * ERR_PTR(-ENOMEM);
 */
struct dma_fence *
xe_preempt_fence_create(struct xe_exec_queue *q,
			u64 context, u32 seqno)
{
	struct xe_preempt_fence *pfence;

	pfence = xe_preempt_fence_alloc();
	if (IS_ERR(pfence))
		return ERR_CAST(pfence);

	return xe_preempt_fence_arm(pfence, q, context, seqno);
}

bool xe_fence_is_xe_preempt(const struct dma_fence *fence)
{
	return fence->ops == &preempt_fence_ops;
}

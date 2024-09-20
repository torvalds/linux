// SPDX-License-Identifier: GPL-2.0-only
/*
 * dma-fence-array: aggregate fences to be waited together
 *
 * Copyright (C) 2016 Collabora Ltd
 * Copyright (C) 2016 Advanced Micro Devices, Inc.
 * Authors:
 *	Gustavo Padovan <gustavo@padovan.org>
 *	Christian König <christian.koenig@amd.com>
 */

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/dma-fence-array.h>

#define PENDING_ERROR 1

static const char *dma_fence_array_get_driver_name(struct dma_fence *fence)
{
	return "dma_fence_array";
}

static const char *dma_fence_array_get_timeline_name(struct dma_fence *fence)
{
	return "unbound";
}

static void dma_fence_array_set_pending_error(struct dma_fence_array *array,
					      int error)
{
	/*
	 * Propagate the first error reported by any of our fences, but only
	 * before we ourselves are signaled.
	 */
	if (error)
		cmpxchg(&array->base.error, PENDING_ERROR, error);
}

static void dma_fence_array_clear_pending_error(struct dma_fence_array *array)
{
	/* Clear the error flag if not actually set. */
	cmpxchg(&array->base.error, PENDING_ERROR, 0);
}

static void irq_dma_fence_array_work(struct irq_work *wrk)
{
	struct dma_fence_array *array = container_of(wrk, typeof(*array), work);

	dma_fence_array_clear_pending_error(array);

	dma_fence_signal(&array->base);
	dma_fence_put(&array->base);
}

static void dma_fence_array_cb_func(struct dma_fence *f,
				    struct dma_fence_cb *cb)
{
	struct dma_fence_array_cb *array_cb =
		container_of(cb, struct dma_fence_array_cb, cb);
	struct dma_fence_array *array = array_cb->array;

	dma_fence_array_set_pending_error(array, f->error);

	if (atomic_dec_and_test(&array->num_pending))
		irq_work_queue(&array->work);
	else
		dma_fence_put(&array->base);
}

static bool dma_fence_array_enable_signaling(struct dma_fence *fence)
{
	struct dma_fence_array *array = to_dma_fence_array(fence);
	struct dma_fence_array_cb *cb = array->callbacks;
	unsigned i;

	for (i = 0; i < array->num_fences; ++i) {
		cb[i].array = array;
		/*
		 * As we may report that the fence is signaled before all
		 * callbacks are complete, we need to take an additional
		 * reference count on the array so that we do not free it too
		 * early. The core fence handling will only hold the reference
		 * until we signal the array as complete (but that is now
		 * insufficient).
		 */
		dma_fence_get(&array->base);
		if (dma_fence_add_callback(array->fences[i], &cb[i].cb,
					   dma_fence_array_cb_func)) {
			int error = array->fences[i]->error;

			dma_fence_array_set_pending_error(array, error);
			dma_fence_put(&array->base);
			if (atomic_dec_and_test(&array->num_pending)) {
				dma_fence_array_clear_pending_error(array);
				return false;
			}
		}
	}

	return true;
}

static bool dma_fence_array_signaled(struct dma_fence *fence)
{
	struct dma_fence_array *array = to_dma_fence_array(fence);

	if (atomic_read(&array->num_pending) > 0)
		return false;

	dma_fence_array_clear_pending_error(array);
	return true;
}

static void dma_fence_array_release(struct dma_fence *fence)
{
	struct dma_fence_array *array = to_dma_fence_array(fence);
	unsigned i;

	for (i = 0; i < array->num_fences; ++i)
		dma_fence_put(array->fences[i]);

	kfree(array->fences);
	dma_fence_free(fence);
}

static void dma_fence_array_set_deadline(struct dma_fence *fence,
					 ktime_t deadline)
{
	struct dma_fence_array *array = to_dma_fence_array(fence);
	unsigned i;

	for (i = 0; i < array->num_fences; ++i)
		dma_fence_set_deadline(array->fences[i], deadline);
}

const struct dma_fence_ops dma_fence_array_ops = {
	.get_driver_name = dma_fence_array_get_driver_name,
	.get_timeline_name = dma_fence_array_get_timeline_name,
	.enable_signaling = dma_fence_array_enable_signaling,
	.signaled = dma_fence_array_signaled,
	.release = dma_fence_array_release,
	.set_deadline = dma_fence_array_set_deadline,
};
EXPORT_SYMBOL(dma_fence_array_ops);

/**
 * dma_fence_array_create - Create a custom fence array
 * @num_fences:		[in]	number of fences to add in the array
 * @fences:		[in]	array containing the fences
 * @context:		[in]	fence context to use
 * @seqno:		[in]	sequence number to use
 * @signal_on_any:	[in]	signal on any fence in the array
 *
 * Allocate a dma_fence_array object and initialize the base fence with
 * dma_fence_init().
 * In case of error it returns NULL.
 *
 * The caller should allocate the fences array with num_fences size
 * and fill it with the fences it wants to add to the object. Ownership of this
 * array is taken and dma_fence_put() is used on each fence on release.
 *
 * If @signal_on_any is true the fence array signals if any fence in the array
 * signals, otherwise it signals when all fences in the array signal.
 */
struct dma_fence_array *dma_fence_array_create(int num_fences,
					       struct dma_fence **fences,
					       u64 context, unsigned seqno,
					       bool signal_on_any)
{
	struct dma_fence_array *array;

	WARN_ON(!num_fences || !fences);

	array = kzalloc(struct_size(array, callbacks, num_fences), GFP_KERNEL);
	if (!array)
		return NULL;

	array->num_fences = num_fences;

	spin_lock_init(&array->lock);
	dma_fence_init(&array->base, &dma_fence_array_ops, &array->lock,
		       context, seqno);
	init_irq_work(&array->work, irq_dma_fence_array_work);

	atomic_set(&array->num_pending, signal_on_any ? 1 : num_fences);
	array->fences = fences;

	array->base.error = PENDING_ERROR;

	/*
	 * dma_fence_array objects should never contain any other fence
	 * containers or otherwise we run into recursion and potential kernel
	 * stack overflow on operations on the dma_fence_array.
	 *
	 * The correct way of handling this is to flatten out the array by the
	 * caller instead.
	 *
	 * Enforce this here by checking that we don't create a dma_fence_array
	 * with any container inside.
	 */
	while (num_fences--)
		WARN_ON(dma_fence_is_container(fences[num_fences]));

	return array;
}
EXPORT_SYMBOL(dma_fence_array_create);

/**
 * dma_fence_match_context - Check if all fences are from the given context
 * @fence:		[in]	fence or fence array
 * @context:		[in]	fence context to check all fences against
 *
 * Checks the provided fence or, for a fence array, all fences in the array
 * against the given context. Returns false if any fence is from a different
 * context.
 */
bool dma_fence_match_context(struct dma_fence *fence, u64 context)
{
	struct dma_fence_array *array = to_dma_fence_array(fence);
	unsigned i;

	if (!dma_fence_is_array(fence))
		return fence->context == context;

	for (i = 0; i < array->num_fences; i++) {
		if (array->fences[i]->context != context)
			return false;
	}

	return true;
}
EXPORT_SYMBOL(dma_fence_match_context);

struct dma_fence *dma_fence_array_first(struct dma_fence *head)
{
	struct dma_fence_array *array;

	if (!head)
		return NULL;

	array = to_dma_fence_array(head);
	if (!array)
		return head;

	if (!array->num_fences)
		return NULL;

	return array->fences[0];
}
EXPORT_SYMBOL(dma_fence_array_first);

struct dma_fence *dma_fence_array_next(struct dma_fence *head,
				       unsigned int index)
{
	struct dma_fence_array *array = to_dma_fence_array(head);

	if (!array || index >= array->num_fences)
		return NULL;

	return array->fences[index];
}
EXPORT_SYMBOL(dma_fence_array_next);

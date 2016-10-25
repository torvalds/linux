/*
 * Fence mechanism for dma-buf and to allow for asynchronous dma access
 *
 * Copyright (C) 2012 Canonical Ltd
 * Copyright (C) 2012 Texas Instruments
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Maarten Lankhorst <maarten.lankhorst@canonical.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/slab.h>
#include <linux/export.h>
#include <linux/atomic.h>
#include <linux/dma-fence.h>

#define CREATE_TRACE_POINTS
#include <trace/events/dma_fence.h>

EXPORT_TRACEPOINT_SYMBOL(dma_fence_annotate_wait_on);
EXPORT_TRACEPOINT_SYMBOL(dma_fence_emit);

/*
 * fence context counter: each execution context should have its own
 * fence context, this allows checking if fences belong to the same
 * context or not. One device can have multiple separate contexts,
 * and they're used if some engine can run independently of another.
 */
static atomic64_t dma_fence_context_counter = ATOMIC64_INIT(0);

/**
 * dma_fence_context_alloc - allocate an array of fence contexts
 * @num:	[in]	amount of contexts to allocate
 *
 * This function will return the first index of the number of fences allocated.
 * The fence context is used for setting fence->context to a unique number.
 */
u64 dma_fence_context_alloc(unsigned num)
{
	BUG_ON(!num);
	return atomic64_add_return(num, &dma_fence_context_counter) - num;
}
EXPORT_SYMBOL(dma_fence_context_alloc);

/**
 * dma_fence_signal_locked - signal completion of a fence
 * @fence: the fence to signal
 *
 * Signal completion for software callbacks on a fence, this will unblock
 * dma_fence_wait() calls and run all the callbacks added with
 * dma_fence_add_callback(). Can be called multiple times, but since a fence
 * can only go from unsignaled to signaled state, it will only be effective
 * the first time.
 *
 * Unlike dma_fence_signal, this function must be called with fence->lock held.
 */
int dma_fence_signal_locked(struct dma_fence *fence)
{
	struct dma_fence_cb *cur, *tmp;
	int ret = 0;

	lockdep_assert_held(fence->lock);

	if (WARN_ON(!fence))
		return -EINVAL;

	if (!ktime_to_ns(fence->timestamp)) {
		fence->timestamp = ktime_get();
		smp_mb__before_atomic();
	}

	if (test_and_set_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
		ret = -EINVAL;

		/*
		 * we might have raced with the unlocked dma_fence_signal,
		 * still run through all callbacks
		 */
	} else
		trace_dma_fence_signaled(fence);

	list_for_each_entry_safe(cur, tmp, &fence->cb_list, node) {
		list_del_init(&cur->node);
		cur->func(fence, cur);
	}
	return ret;
}
EXPORT_SYMBOL(dma_fence_signal_locked);

/**
 * dma_fence_signal - signal completion of a fence
 * @fence: the fence to signal
 *
 * Signal completion for software callbacks on a fence, this will unblock
 * dma_fence_wait() calls and run all the callbacks added with
 * dma_fence_add_callback(). Can be called multiple times, but since a fence
 * can only go from unsignaled to signaled state, it will only be effective
 * the first time.
 */
int dma_fence_signal(struct dma_fence *fence)
{
	unsigned long flags;

	if (!fence)
		return -EINVAL;

	if (!ktime_to_ns(fence->timestamp)) {
		fence->timestamp = ktime_get();
		smp_mb__before_atomic();
	}

	if (test_and_set_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return -EINVAL;

	trace_dma_fence_signaled(fence);

	if (test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &fence->flags)) {
		struct dma_fence_cb *cur, *tmp;

		spin_lock_irqsave(fence->lock, flags);
		list_for_each_entry_safe(cur, tmp, &fence->cb_list, node) {
			list_del_init(&cur->node);
			cur->func(fence, cur);
		}
		spin_unlock_irqrestore(fence->lock, flags);
	}
	return 0;
}
EXPORT_SYMBOL(dma_fence_signal);

/**
 * dma_fence_wait_timeout - sleep until the fence gets signaled
 * or until timeout elapses
 * @fence:	[in]	the fence to wait on
 * @intr:	[in]	if true, do an interruptible wait
 * @timeout:	[in]	timeout value in jiffies, or MAX_SCHEDULE_TIMEOUT
 *
 * Returns -ERESTARTSYS if interrupted, 0 if the wait timed out, or the
 * remaining timeout in jiffies on success. Other error values may be
 * returned on custom implementations.
 *
 * Performs a synchronous wait on this fence. It is assumed the caller
 * directly or indirectly (buf-mgr between reservation and committing)
 * holds a reference to the fence, otherwise the fence might be
 * freed before return, resulting in undefined behavior.
 */
signed long
dma_fence_wait_timeout(struct dma_fence *fence, bool intr, signed long timeout)
{
	signed long ret;

	if (WARN_ON(timeout < 0))
		return -EINVAL;

	if (timeout == 0)
		return dma_fence_is_signaled(fence);

	trace_dma_fence_wait_start(fence);
	ret = fence->ops->wait(fence, intr, timeout);
	trace_dma_fence_wait_end(fence);
	return ret;
}
EXPORT_SYMBOL(dma_fence_wait_timeout);

void dma_fence_release(struct kref *kref)
{
	struct dma_fence *fence =
		container_of(kref, struct dma_fence, refcount);

	trace_dma_fence_destroy(fence);

	BUG_ON(!list_empty(&fence->cb_list));

	if (fence->ops->release)
		fence->ops->release(fence);
	else
		dma_fence_free(fence);
}
EXPORT_SYMBOL(dma_fence_release);

void dma_fence_free(struct dma_fence *fence)
{
	kfree_rcu(fence, rcu);
}
EXPORT_SYMBOL(dma_fence_free);

/**
 * dma_fence_enable_sw_signaling - enable signaling on fence
 * @fence:	[in]	the fence to enable
 *
 * this will request for sw signaling to be enabled, to make the fence
 * complete as soon as possible
 */
void dma_fence_enable_sw_signaling(struct dma_fence *fence)
{
	unsigned long flags;

	if (!test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
			      &fence->flags) &&
	    !test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
		trace_dma_fence_enable_signal(fence);

		spin_lock_irqsave(fence->lock, flags);

		if (!fence->ops->enable_signaling(fence))
			dma_fence_signal_locked(fence);

		spin_unlock_irqrestore(fence->lock, flags);
	}
}
EXPORT_SYMBOL(dma_fence_enable_sw_signaling);

/**
 * dma_fence_add_callback - add a callback to be called when the fence
 * is signaled
 * @fence:	[in]	the fence to wait on
 * @cb:		[in]	the callback to register
 * @func:	[in]	the function to call
 *
 * cb will be initialized by dma_fence_add_callback, no initialization
 * by the caller is required. Any number of callbacks can be registered
 * to a fence, but a callback can only be registered to one fence at a time.
 *
 * Note that the callback can be called from an atomic context.  If
 * fence is already signaled, this function will return -ENOENT (and
 * *not* call the callback)
 *
 * Add a software callback to the fence. Same restrictions apply to
 * refcount as it does to dma_fence_wait, however the caller doesn't need to
 * keep a refcount to fence afterwards: when software access is enabled,
 * the creator of the fence is required to keep the fence alive until
 * after it signals with dma_fence_signal. The callback itself can be called
 * from irq context.
 *
 */
int dma_fence_add_callback(struct dma_fence *fence, struct dma_fence_cb *cb,
			   dma_fence_func_t func)
{
	unsigned long flags;
	int ret = 0;
	bool was_set;

	if (WARN_ON(!fence || !func))
		return -EINVAL;

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
		INIT_LIST_HEAD(&cb->node);
		return -ENOENT;
	}

	spin_lock_irqsave(fence->lock, flags);

	was_set = test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
				   &fence->flags);

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		ret = -ENOENT;
	else if (!was_set) {
		trace_dma_fence_enable_signal(fence);

		if (!fence->ops->enable_signaling(fence)) {
			dma_fence_signal_locked(fence);
			ret = -ENOENT;
		}
	}

	if (!ret) {
		cb->func = func;
		list_add_tail(&cb->node, &fence->cb_list);
	} else
		INIT_LIST_HEAD(&cb->node);
	spin_unlock_irqrestore(fence->lock, flags);

	return ret;
}
EXPORT_SYMBOL(dma_fence_add_callback);

/**
 * dma_fence_remove_callback - remove a callback from the signaling list
 * @fence:	[in]	the fence to wait on
 * @cb:		[in]	the callback to remove
 *
 * Remove a previously queued callback from the fence. This function returns
 * true if the callback is successfully removed, or false if the fence has
 * already been signaled.
 *
 * *WARNING*:
 * Cancelling a callback should only be done if you really know what you're
 * doing, since deadlocks and race conditions could occur all too easily. For
 * this reason, it should only ever be done on hardware lockup recovery,
 * with a reference held to the fence.
 */
bool
dma_fence_remove_callback(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(fence->lock, flags);

	ret = !list_empty(&cb->node);
	if (ret)
		list_del_init(&cb->node);

	spin_unlock_irqrestore(fence->lock, flags);

	return ret;
}
EXPORT_SYMBOL(dma_fence_remove_callback);

struct default_wait_cb {
	struct dma_fence_cb base;
	struct task_struct *task;
};

static void
dma_fence_default_wait_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct default_wait_cb *wait =
		container_of(cb, struct default_wait_cb, base);

	wake_up_state(wait->task, TASK_NORMAL);
}

/**
 * dma_fence_default_wait - default sleep until the fence gets signaled
 * or until timeout elapses
 * @fence:	[in]	the fence to wait on
 * @intr:	[in]	if true, do an interruptible wait
 * @timeout:	[in]	timeout value in jiffies, or MAX_SCHEDULE_TIMEOUT
 *
 * Returns -ERESTARTSYS if interrupted, 0 if the wait timed out, or the
 * remaining timeout in jiffies on success.
 */
signed long
dma_fence_default_wait(struct dma_fence *fence, bool intr, signed long timeout)
{
	struct default_wait_cb cb;
	unsigned long flags;
	signed long ret = timeout;
	bool was_set;

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return timeout;

	spin_lock_irqsave(fence->lock, flags);

	if (intr && signal_pending(current)) {
		ret = -ERESTARTSYS;
		goto out;
	}

	was_set = test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
				   &fence->flags);

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		goto out;

	if (!was_set) {
		trace_dma_fence_enable_signal(fence);

		if (!fence->ops->enable_signaling(fence)) {
			dma_fence_signal_locked(fence);
			goto out;
		}
	}

	cb.base.func = dma_fence_default_wait_cb;
	cb.task = current;
	list_add(&cb.base.node, &fence->cb_list);

	while (!test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags) && ret > 0) {
		if (intr)
			__set_current_state(TASK_INTERRUPTIBLE);
		else
			__set_current_state(TASK_UNINTERRUPTIBLE);
		spin_unlock_irqrestore(fence->lock, flags);

		ret = schedule_timeout(ret);

		spin_lock_irqsave(fence->lock, flags);
		if (ret > 0 && intr && signal_pending(current))
			ret = -ERESTARTSYS;
	}

	if (!list_empty(&cb.base.node))
		list_del(&cb.base.node);
	__set_current_state(TASK_RUNNING);

out:
	spin_unlock_irqrestore(fence->lock, flags);
	return ret;
}
EXPORT_SYMBOL(dma_fence_default_wait);

static bool
dma_fence_test_signaled_any(struct dma_fence **fences, uint32_t count)
{
	int i;

	for (i = 0; i < count; ++i) {
		struct dma_fence *fence = fences[i];
		if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
			return true;
	}
	return false;
}

/**
 * dma_fence_wait_any_timeout - sleep until any fence gets signaled
 * or until timeout elapses
 * @fences:	[in]	array of fences to wait on
 * @count:	[in]	number of fences to wait on
 * @intr:	[in]	if true, do an interruptible wait
 * @timeout:	[in]	timeout value in jiffies, or MAX_SCHEDULE_TIMEOUT
 *
 * Returns -EINVAL on custom fence wait implementation, -ERESTARTSYS if
 * interrupted, 0 if the wait timed out, or the remaining timeout in jiffies
 * on success.
 *
 * Synchronous waits for the first fence in the array to be signaled. The
 * caller needs to hold a reference to all fences in the array, otherwise a
 * fence might be freed before return, resulting in undefined behavior.
 */
signed long
dma_fence_wait_any_timeout(struct dma_fence **fences, uint32_t count,
			   bool intr, signed long timeout)
{
	struct default_wait_cb *cb;
	signed long ret = timeout;
	unsigned i;

	if (WARN_ON(!fences || !count || timeout < 0))
		return -EINVAL;

	if (timeout == 0) {
		for (i = 0; i < count; ++i)
			if (dma_fence_is_signaled(fences[i]))
				return 1;

		return 0;
	}

	cb = kcalloc(count, sizeof(struct default_wait_cb), GFP_KERNEL);
	if (cb == NULL) {
		ret = -ENOMEM;
		goto err_free_cb;
	}

	for (i = 0; i < count; ++i) {
		struct dma_fence *fence = fences[i];

		if (fence->ops->wait != dma_fence_default_wait) {
			ret = -EINVAL;
			goto fence_rm_cb;
		}

		cb[i].task = current;
		if (dma_fence_add_callback(fence, &cb[i].base,
					   dma_fence_default_wait_cb)) {
			/* This fence is already signaled */
			goto fence_rm_cb;
		}
	}

	while (ret > 0) {
		if (intr)
			set_current_state(TASK_INTERRUPTIBLE);
		else
			set_current_state(TASK_UNINTERRUPTIBLE);

		if (dma_fence_test_signaled_any(fences, count))
			break;

		ret = schedule_timeout(ret);

		if (ret > 0 && intr && signal_pending(current))
			ret = -ERESTARTSYS;
	}

	__set_current_state(TASK_RUNNING);

fence_rm_cb:
	while (i-- > 0)
		dma_fence_remove_callback(fences[i], &cb[i].base);

err_free_cb:
	kfree(cb);

	return ret;
}
EXPORT_SYMBOL(dma_fence_wait_any_timeout);

/**
 * dma_fence_init - Initialize a custom fence.
 * @fence:	[in]	the fence to initialize
 * @ops:	[in]	the dma_fence_ops for operations on this fence
 * @lock:	[in]	the irqsafe spinlock to use for locking this fence
 * @context:	[in]	the execution context this fence is run on
 * @seqno:	[in]	a linear increasing sequence number for this context
 *
 * Initializes an allocated fence, the caller doesn't have to keep its
 * refcount after committing with this fence, but it will need to hold a
 * refcount again if dma_fence_ops.enable_signaling gets called. This can
 * be used for other implementing other types of fence.
 *
 * context and seqno are used for easy comparison between fences, allowing
 * to check which fence is later by simply using dma_fence_later.
 */
void
dma_fence_init(struct dma_fence *fence, const struct dma_fence_ops *ops,
	       spinlock_t *lock, u64 context, unsigned seqno)
{
	BUG_ON(!lock);
	BUG_ON(!ops || !ops->wait || !ops->enable_signaling ||
	       !ops->get_driver_name || !ops->get_timeline_name);

	kref_init(&fence->refcount);
	fence->ops = ops;
	INIT_LIST_HEAD(&fence->cb_list);
	fence->lock = lock;
	fence->context = context;
	fence->seqno = seqno;
	fence->flags = 0UL;

	trace_dma_fence_init(fence);
}
EXPORT_SYMBOL(dma_fence_init);

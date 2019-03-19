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
#include <linux/sched/signal.h>

#define CREATE_TRACE_POINTS
#include <trace/events/dma_fence.h>

EXPORT_TRACEPOINT_SYMBOL(dma_fence_emit);
EXPORT_TRACEPOINT_SYMBOL(dma_fence_enable_signal);

static DEFINE_SPINLOCK(dma_fence_stub_lock);
static struct dma_fence dma_fence_stub;

/*
 * fence context counter: each execution context should have its own
 * fence context, this allows checking if fences belong to the same
 * context or not. One device can have multiple separate contexts,
 * and they're used if some engine can run independently of another.
 */
static atomic64_t dma_fence_context_counter = ATOMIC64_INIT(1);

/**
 * DOC: DMA fences overview
 *
 * DMA fences, represented by &struct dma_fence, are the kernel internal
 * synchronization primitive for DMA operations like GPU rendering, video
 * encoding/decoding, or displaying buffers on a screen.
 *
 * A fence is initialized using dma_fence_init() and completed using
 * dma_fence_signal(). Fences are associated with a context, allocated through
 * dma_fence_context_alloc(), and all fences on the same context are
 * fully ordered.
 *
 * Since the purposes of fences is to facilitate cross-device and
 * cross-application synchronization, there's multiple ways to use one:
 *
 * - Individual fences can be exposed as a &sync_file, accessed as a file
 *   descriptor from userspace, created by calling sync_file_create(). This is
 *   called explicit fencing, since userspace passes around explicit
 *   synchronization points.
 *
 * - Some subsystems also have their own explicit fencing primitives, like
 *   &drm_syncobj. Compared to &sync_file, a &drm_syncobj allows the underlying
 *   fence to be updated.
 *
 * - Then there's also implicit fencing, where the synchronization points are
 *   implicitly passed around as part of shared &dma_buf instances. Such
 *   implicit fences are stored in &struct reservation_object through the
 *   &dma_buf.resv pointer.
 */

static const char *dma_fence_stub_get_name(struct dma_fence *fence)
{
        return "stub";
}

static const struct dma_fence_ops dma_fence_stub_ops = {
	.get_driver_name = dma_fence_stub_get_name,
	.get_timeline_name = dma_fence_stub_get_name,
};

/**
 * dma_fence_get_stub - return a signaled fence
 *
 * Return a stub fence which is already signaled.
 */
struct dma_fence *dma_fence_get_stub(void)
{
	spin_lock(&dma_fence_stub_lock);
	if (!dma_fence_stub.ops) {
		dma_fence_init(&dma_fence_stub,
			       &dma_fence_stub_ops,
			       &dma_fence_stub_lock,
			       0, 0);
		dma_fence_signal_locked(&dma_fence_stub);
	}
	spin_unlock(&dma_fence_stub_lock);

	return dma_fence_get(&dma_fence_stub);
}
EXPORT_SYMBOL(dma_fence_get_stub);

/**
 * dma_fence_context_alloc - allocate an array of fence contexts
 * @num: amount of contexts to allocate
 *
 * This function will return the first index of the number of fence contexts
 * allocated.  The fence context is used for setting &dma_fence.context to a
 * unique number by passing the context to dma_fence_init().
 */
u64 dma_fence_context_alloc(unsigned num)
{
	WARN_ON(!num);
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
 * can only go from the unsignaled to the signaled state and not back, it will
 * only be effective the first time.
 *
 * Unlike dma_fence_signal(), this function must be called with &dma_fence.lock
 * held.
 *
 * Returns 0 on success and a negative error value when @fence has been
 * signalled already.
 */
int dma_fence_signal_locked(struct dma_fence *fence)
{
	struct dma_fence_cb *cur, *tmp;
	int ret = 0;

	lockdep_assert_held(fence->lock);

	if (WARN_ON(!fence))
		return -EINVAL;

	if (test_and_set_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
		ret = -EINVAL;

		/*
		 * we might have raced with the unlocked dma_fence_signal,
		 * still run through all callbacks
		 */
	} else {
		fence->timestamp = ktime_get();
		set_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags);
		trace_dma_fence_signaled(fence);
	}

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
 * can only go from the unsignaled to the signaled state and not back, it will
 * only be effective the first time.
 *
 * Returns 0 on success and a negative error value when @fence has been
 * signalled already.
 */
int dma_fence_signal(struct dma_fence *fence)
{
	unsigned long flags;

	if (!fence)
		return -EINVAL;

	if (test_and_set_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return -EINVAL;

	fence->timestamp = ktime_get();
	set_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags);
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
 * @fence: the fence to wait on
 * @intr: if true, do an interruptible wait
 * @timeout: timeout value in jiffies, or MAX_SCHEDULE_TIMEOUT
 *
 * Returns -ERESTARTSYS if interrupted, 0 if the wait timed out, or the
 * remaining timeout in jiffies on success. Other error values may be
 * returned on custom implementations.
 *
 * Performs a synchronous wait on this fence. It is assumed the caller
 * directly or indirectly (buf-mgr between reservation and committing)
 * holds a reference to the fence, otherwise the fence might be
 * freed before return, resulting in undefined behavior.
 *
 * See also dma_fence_wait() and dma_fence_wait_any_timeout().
 */
signed long
dma_fence_wait_timeout(struct dma_fence *fence, bool intr, signed long timeout)
{
	signed long ret;

	if (WARN_ON(timeout < 0))
		return -EINVAL;

	trace_dma_fence_wait_start(fence);
	if (fence->ops->wait)
		ret = fence->ops->wait(fence, intr, timeout);
	else
		ret = dma_fence_default_wait(fence, intr, timeout);
	trace_dma_fence_wait_end(fence);
	return ret;
}
EXPORT_SYMBOL(dma_fence_wait_timeout);

/**
 * dma_fence_release - default relese function for fences
 * @kref: &dma_fence.recfount
 *
 * This is the default release functions for &dma_fence. Drivers shouldn't call
 * this directly, but instead call dma_fence_put().
 */
void dma_fence_release(struct kref *kref)
{
	struct dma_fence *fence =
		container_of(kref, struct dma_fence, refcount);

	trace_dma_fence_destroy(fence);

	/* Failed to signal before release, could be a refcounting issue */
	WARN_ON(!list_empty(&fence->cb_list));

	if (fence->ops->release)
		fence->ops->release(fence);
	else
		dma_fence_free(fence);
}
EXPORT_SYMBOL(dma_fence_release);

/**
 * dma_fence_free - default release function for &dma_fence.
 * @fence: fence to release
 *
 * This is the default implementation for &dma_fence_ops.release. It calls
 * kfree_rcu() on @fence.
 */
void dma_fence_free(struct dma_fence *fence)
{
	kfree_rcu(fence, rcu);
}
EXPORT_SYMBOL(dma_fence_free);

/**
 * dma_fence_enable_sw_signaling - enable signaling on fence
 * @fence: the fence to enable
 *
 * This will request for sw signaling to be enabled, to make the fence
 * complete as soon as possible. This calls &dma_fence_ops.enable_signaling
 * internally.
 */
void dma_fence_enable_sw_signaling(struct dma_fence *fence)
{
	unsigned long flags;

	if (!test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
			      &fence->flags) &&
	    !test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags) &&
	    fence->ops->enable_signaling) {
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
 * @fence: the fence to wait on
 * @cb: the callback to register
 * @func: the function to call
 *
 * @cb will be initialized by dma_fence_add_callback(), no initialization
 * by the caller is required. Any number of callbacks can be registered
 * to a fence, but a callback can only be registered to one fence at a time.
 *
 * Note that the callback can be called from an atomic context.  If
 * fence is already signaled, this function will return -ENOENT (and
 * *not* call the callback).
 *
 * Add a software callback to the fence. Same restrictions apply to
 * refcount as it does to dma_fence_wait(), however the caller doesn't need to
 * keep a refcount to fence afterward dma_fence_add_callback() has returned:
 * when software access is enabled, the creator of the fence is required to keep
 * the fence alive until after it signals with dma_fence_signal(). The callback
 * itself can be called from irq context.
 *
 * Returns 0 in case of success, -ENOENT if the fence is already signaled
 * and -EINVAL in case of error.
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
	else if (!was_set && fence->ops->enable_signaling) {
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
 * dma_fence_get_status - returns the status upon completion
 * @fence: the dma_fence to query
 *
 * This wraps dma_fence_get_status_locked() to return the error status
 * condition on a signaled fence. See dma_fence_get_status_locked() for more
 * details.
 *
 * Returns 0 if the fence has not yet been signaled, 1 if the fence has
 * been signaled without an error condition, or a negative error code
 * if the fence has been completed in err.
 */
int dma_fence_get_status(struct dma_fence *fence)
{
	unsigned long flags;
	int status;

	spin_lock_irqsave(fence->lock, flags);
	status = dma_fence_get_status_locked(fence);
	spin_unlock_irqrestore(fence->lock, flags);

	return status;
}
EXPORT_SYMBOL(dma_fence_get_status);

/**
 * dma_fence_remove_callback - remove a callback from the signaling list
 * @fence: the fence to wait on
 * @cb: the callback to remove
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
 *
 * Behaviour is undefined if @cb has not been added to @fence using
 * dma_fence_add_callback() beforehand.
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
 * @fence: the fence to wait on
 * @intr: if true, do an interruptible wait
 * @timeout: timeout value in jiffies, or MAX_SCHEDULE_TIMEOUT
 *
 * Returns -ERESTARTSYS if interrupted, 0 if the wait timed out, or the
 * remaining timeout in jiffies on success. If timeout is zero the value one is
 * returned if the fence is already signaled for consistency with other
 * functions taking a jiffies timeout.
 */
signed long
dma_fence_default_wait(struct dma_fence *fence, bool intr, signed long timeout)
{
	struct default_wait_cb cb;
	unsigned long flags;
	signed long ret = timeout ? timeout : 1;
	bool was_set;

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return ret;

	spin_lock_irqsave(fence->lock, flags);

	if (intr && signal_pending(current)) {
		ret = -ERESTARTSYS;
		goto out;
	}

	was_set = test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
				   &fence->flags);

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		goto out;

	if (!was_set && fence->ops->enable_signaling) {
		trace_dma_fence_enable_signal(fence);

		if (!fence->ops->enable_signaling(fence)) {
			dma_fence_signal_locked(fence);
			goto out;
		}
	}

	if (!timeout) {
		ret = 0;
		goto out;
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
dma_fence_test_signaled_any(struct dma_fence **fences, uint32_t count,
			    uint32_t *idx)
{
	int i;

	for (i = 0; i < count; ++i) {
		struct dma_fence *fence = fences[i];
		if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
			if (idx)
				*idx = i;
			return true;
		}
	}
	return false;
}

/**
 * dma_fence_wait_any_timeout - sleep until any fence gets signaled
 * or until timeout elapses
 * @fences: array of fences to wait on
 * @count: number of fences to wait on
 * @intr: if true, do an interruptible wait
 * @timeout: timeout value in jiffies, or MAX_SCHEDULE_TIMEOUT
 * @idx: used to store the first signaled fence index, meaningful only on
 *	positive return
 *
 * Returns -EINVAL on custom fence wait implementation, -ERESTARTSYS if
 * interrupted, 0 if the wait timed out, or the remaining timeout in jiffies
 * on success.
 *
 * Synchronous waits for the first fence in the array to be signaled. The
 * caller needs to hold a reference to all fences in the array, otherwise a
 * fence might be freed before return, resulting in undefined behavior.
 *
 * See also dma_fence_wait() and dma_fence_wait_timeout().
 */
signed long
dma_fence_wait_any_timeout(struct dma_fence **fences, uint32_t count,
			   bool intr, signed long timeout, uint32_t *idx)
{
	struct default_wait_cb *cb;
	signed long ret = timeout;
	unsigned i;

	if (WARN_ON(!fences || !count || timeout < 0))
		return -EINVAL;

	if (timeout == 0) {
		for (i = 0; i < count; ++i)
			if (dma_fence_is_signaled(fences[i])) {
				if (idx)
					*idx = i;
				return 1;
			}

		return 0;
	}

	cb = kcalloc(count, sizeof(struct default_wait_cb), GFP_KERNEL);
	if (cb == NULL) {
		ret = -ENOMEM;
		goto err_free_cb;
	}

	for (i = 0; i < count; ++i) {
		struct dma_fence *fence = fences[i];

		cb[i].task = current;
		if (dma_fence_add_callback(fence, &cb[i].base,
					   dma_fence_default_wait_cb)) {
			/* This fence is already signaled */
			if (idx)
				*idx = i;
			goto fence_rm_cb;
		}
	}

	while (ret > 0) {
		if (intr)
			set_current_state(TASK_INTERRUPTIBLE);
		else
			set_current_state(TASK_UNINTERRUPTIBLE);

		if (dma_fence_test_signaled_any(fences, count, idx))
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
 * @fence: the fence to initialize
 * @ops: the dma_fence_ops for operations on this fence
 * @lock: the irqsafe spinlock to use for locking this fence
 * @context: the execution context this fence is run on
 * @seqno: a linear increasing sequence number for this context
 *
 * Initializes an allocated fence, the caller doesn't have to keep its
 * refcount after committing with this fence, but it will need to hold a
 * refcount again if &dma_fence_ops.enable_signaling gets called.
 *
 * context and seqno are used for easy comparison between fences, allowing
 * to check which fence is later by simply using dma_fence_later().
 */
void
dma_fence_init(struct dma_fence *fence, const struct dma_fence_ops *ops,
	       spinlock_t *lock, u64 context, u64 seqno)
{
	BUG_ON(!lock);
	BUG_ON(!ops || !ops->get_driver_name || !ops->get_timeline_name);

	kref_init(&fence->refcount);
	fence->ops = ops;
	INIT_LIST_HEAD(&fence->cb_list);
	fence->lock = lock;
	fence->context = context;
	fence->seqno = seqno;
	fence->flags = 0UL;
	fence->error = 0;

	trace_dma_fence_init(fence);
}
EXPORT_SYMBOL(dma_fence_init);

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Fence mechanism for dma-buf and to allow for asynchronous dma access
 *
 * Copyright (C) 2012 Canonical Ltd
 * Copyright (C) 2012 Texas Instruments
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Maarten Lankhorst <maarten.lankhorst@canonical.com>
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
EXPORT_TRACEPOINT_SYMBOL(dma_fence_signaled);

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
 *   implicit fences are stored in &struct dma_resv through the
 *   &dma_buf.resv pointer.
 */

/**
 * DOC: fence cross-driver contract
 *
 * Since &dma_fence provide a cross driver contract, all drivers must follow the
 * same rules:
 *
 * * Fences must complete in a reasonable time. Fences which represent kernels
 *   and shaders submitted by userspace, which could run forever, must be backed
 *   up by timeout and gpu hang recovery code. Minimally that code must prevent
 *   further command submission and force complete all in-flight fences, e.g.
 *   when the driver or hardware do not support gpu reset, or if the gpu reset
 *   failed for some reason. Ideally the driver supports gpu recovery which only
 *   affects the offending userspace context, and no other userspace
 *   submissions.
 *
 * * Drivers may have different ideas of what completion within a reasonable
 *   time means. Some hang recovery code uses a fixed timeout, others a mix
 *   between observing forward progress and increasingly strict timeouts.
 *   Drivers should not try to second guess timeout handling of fences from
 *   other drivers.
 *
 * * To ensure there's no deadlocks of dma_fence_wait() against other locks
 *   drivers should annotate all code required to reach dma_fence_signal(),
 *   which completes the fences, with dma_fence_begin_signalling() and
 *   dma_fence_end_signalling().
 *
 * * Drivers are allowed to call dma_fence_wait() while holding dma_resv_lock().
 *   This means any code required for fence completion cannot acquire a
 *   &dma_resv lock. Note that this also pulls in the entire established
 *   locking hierarchy around dma_resv_lock() and dma_resv_unlock().
 *
 * * Drivers are allowed to call dma_fence_wait() from their &shrinker
 *   callbacks. This means any code required for fence completion cannot
 *   allocate memory with GFP_KERNEL.
 *
 * * Drivers are allowed to call dma_fence_wait() from their &mmu_notifier
 *   respectively &mmu_interval_notifier callbacks. This means any code required
 *   for fence completeion cannot allocate memory with GFP_NOFS or GFP_NOIO.
 *   Only GFP_ATOMIC is permissible, which might fail.
 *
 * Note that only GPU drivers have a reasonable excuse for both requiring
 * &mmu_interval_notifier and &shrinker callbacks at the same time as having to
 * track asynchronous compute work using &dma_fence. No driver outside of
 * drivers/gpu should ever call dma_fence_wait() in such contexts.
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
	return atomic64_fetch_add(num, &dma_fence_context_counter);
}
EXPORT_SYMBOL(dma_fence_context_alloc);

/**
 * DOC: fence signalling annotation
 *
 * Proving correctness of all the kernel code around &dma_fence through code
 * review and testing is tricky for a few reasons:
 *
 * * It is a cross-driver contract, and therefore all drivers must follow the
 *   same rules for lock nesting order, calling contexts for various functions
 *   and anything else significant for in-kernel interfaces. But it is also
 *   impossible to test all drivers in a single machine, hence brute-force N vs.
 *   N testing of all combinations is impossible. Even just limiting to the
 *   possible combinations is infeasible.
 *
 * * There is an enormous amount of driver code involved. For render drivers
 *   there's the tail of command submission, after fences are published,
 *   scheduler code, interrupt and workers to process job completion,
 *   and timeout, gpu reset and gpu hang recovery code. Plus for integration
 *   with core mm with have &mmu_notifier, respectively &mmu_interval_notifier,
 *   and &shrinker. For modesetting drivers there's the commit tail functions
 *   between when fences for an atomic modeset are published, and when the
 *   corresponding vblank completes, including any interrupt processing and
 *   related workers. Auditing all that code, across all drivers, is not
 *   feasible.
 *
 * * Due to how many other subsystems are involved and the locking hierarchies
 *   this pulls in there is extremely thin wiggle-room for driver-specific
 *   differences. &dma_fence interacts with almost all of the core memory
 *   handling through page fault handlers via &dma_resv, dma_resv_lock() and
 *   dma_resv_unlock(). On the other side it also interacts through all
 *   allocation sites through &mmu_notifier and &shrinker.
 *
 * Furthermore lockdep does not handle cross-release dependencies, which means
 * any deadlocks between dma_fence_wait() and dma_fence_signal() can't be caught
 * at runtime with some quick testing. The simplest example is one thread
 * waiting on a &dma_fence while holding a lock::
 *
 *     lock(A);
 *     dma_fence_wait(B);
 *     unlock(A);
 *
 * while the other thread is stuck trying to acquire the same lock, which
 * prevents it from signalling the fence the previous thread is stuck waiting
 * on::
 *
 *     lock(A);
 *     unlock(A);
 *     dma_fence_signal(B);
 *
 * By manually annotating all code relevant to signalling a &dma_fence we can
 * teach lockdep about these dependencies, which also helps with the validation
 * headache since now lockdep can check all the rules for us::
 *
 *    cookie = dma_fence_begin_signalling();
 *    lock(A);
 *    unlock(A);
 *    dma_fence_signal(B);
 *    dma_fence_end_signalling(cookie);
 *
 * For using dma_fence_begin_signalling() and dma_fence_end_signalling() to
 * annotate critical sections the following rules need to be observed:
 *
 * * All code necessary to complete a &dma_fence must be annotated, from the
 *   point where a fence is accessible to other threads, to the point where
 *   dma_fence_signal() is called. Un-annotated code can contain deadlock issues,
 *   and due to the very strict rules and many corner cases it is infeasible to
 *   catch these just with review or normal stress testing.
 *
 * * &struct dma_resv deserves a special note, since the readers are only
 *   protected by rcu. This means the signalling critical section starts as soon
 *   as the new fences are installed, even before dma_resv_unlock() is called.
 *
 * * The only exception are fast paths and opportunistic signalling code, which
 *   calls dma_fence_signal() purely as an optimization, but is not required to
 *   guarantee completion of a &dma_fence. The usual example is a wait IOCTL
 *   which calls dma_fence_signal(), while the mandatory completion path goes
 *   through a hardware interrupt and possible job completion worker.
 *
 * * To aid composability of code, the annotations can be freely nested, as long
 *   as the overall locking hierarchy is consistent. The annotations also work
 *   both in interrupt and process context. Due to implementation details this
 *   requires that callers pass an opaque cookie from
 *   dma_fence_begin_signalling() to dma_fence_end_signalling().
 *
 * * Validation against the cross driver contract is implemented by priming
 *   lockdep with the relevant hierarchy at boot-up. This means even just
 *   testing with a single device is enough to validate a driver, at least as
 *   far as deadlocks with dma_fence_wait() against dma_fence_signal() are
 *   concerned.
 */
#ifdef CONFIG_LOCKDEP
static struct lockdep_map dma_fence_lockdep_map = {
	.name = "dma_fence_map"
};

/**
 * dma_fence_begin_signalling - begin a critical DMA fence signalling section
 *
 * Drivers should use this to annotate the beginning of any code section
 * required to eventually complete &dma_fence by calling dma_fence_signal().
 *
 * The end of these critical sections are annotated with
 * dma_fence_end_signalling().
 *
 * Returns:
 *
 * Opaque cookie needed by the implementation, which needs to be passed to
 * dma_fence_end_signalling().
 */
bool dma_fence_begin_signalling(void)
{
	/* explicitly nesting ... */
	if (lock_is_held_type(&dma_fence_lockdep_map, 1))
		return true;

	/* rely on might_sleep check for soft/hardirq locks */
	if (in_atomic())
		return true;

	/* ... and non-recursive readlock */
	lock_acquire(&dma_fence_lockdep_map, 0, 0, 1, 1, NULL, _RET_IP_);

	return false;
}
EXPORT_SYMBOL(dma_fence_begin_signalling);

/**
 * dma_fence_end_signalling - end a critical DMA fence signalling section
 * @cookie: opaque cookie from dma_fence_begin_signalling()
 *
 * Closes a critical section annotation opened by dma_fence_begin_signalling().
 */
void dma_fence_end_signalling(bool cookie)
{
	if (cookie)
		return;

	lock_release(&dma_fence_lockdep_map, _RET_IP_);
}
EXPORT_SYMBOL(dma_fence_end_signalling);

void __dma_fence_might_wait(void)
{
	bool tmp;

	tmp = lock_is_held_type(&dma_fence_lockdep_map, 1);
	if (tmp)
		lock_release(&dma_fence_lockdep_map, _THIS_IP_);
	lock_map_acquire(&dma_fence_lockdep_map);
	lock_map_release(&dma_fence_lockdep_map);
	if (tmp)
		lock_acquire(&dma_fence_lockdep_map, 0, 0, 1, 1, NULL, _THIS_IP_);
}
#endif


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
	struct list_head cb_list;

	lockdep_assert_held(fence->lock);

	if (unlikely(test_and_set_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
				      &fence->flags)))
		return -EINVAL;

	/* Stash the cb_list before replacing it with the timestamp */
	list_replace(&fence->cb_list, &cb_list);

	fence->timestamp = ktime_get();
	set_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags);
	trace_dma_fence_signaled(fence);

	list_for_each_entry_safe(cur, tmp, &cb_list, node) {
		INIT_LIST_HEAD(&cur->node);
		cur->func(fence, cur);
	}

	return 0;
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
	int ret;
	bool tmp;

	if (!fence)
		return -EINVAL;

	tmp = dma_fence_begin_signalling();

	spin_lock_irqsave(fence->lock, flags);
	ret = dma_fence_signal_locked(fence);
	spin_unlock_irqrestore(fence->lock, flags);

	dma_fence_end_signalling(tmp);

	return ret;
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

	might_sleep();

	__dma_fence_might_wait();

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

	if (WARN(!list_empty(&fence->cb_list) &&
		 !test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags),
		 "Fence %s:%s:%llx:%llx released with pending signals!\n",
		 fence->ops->get_driver_name(fence),
		 fence->ops->get_timeline_name(fence),
		 fence->context, fence->seqno)) {
		unsigned long flags;

		/*
		 * Failed to signal before release, likely a refcounting issue.
		 *
		 * This should never happen, but if it does make sure that we
		 * don't leave chains dangling. We set the error flag first
		 * so that the callbacks know this signal is due to an error.
		 */
		spin_lock_irqsave(fence->lock, flags);
		fence->error = -EDEADLK;
		dma_fence_signal_locked(fence);
		spin_unlock_irqrestore(fence->lock, flags);
	}

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

static bool __dma_fence_enable_signaling(struct dma_fence *fence)
{
	bool was_set;

	lockdep_assert_held(fence->lock);

	was_set = test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
				   &fence->flags);

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return false;

	if (!was_set && fence->ops->enable_signaling) {
		trace_dma_fence_enable_signal(fence);

		if (!fence->ops->enable_signaling(fence)) {
			dma_fence_signal_locked(fence);
			return false;
		}
	}

	return true;
}

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

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return;

	spin_lock_irqsave(fence->lock, flags);
	__dma_fence_enable_signaling(fence);
	spin_unlock_irqrestore(fence->lock, flags);
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

	if (WARN_ON(!fence || !func))
		return -EINVAL;

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
		INIT_LIST_HEAD(&cb->node);
		return -ENOENT;
	}

	spin_lock_irqsave(fence->lock, flags);

	if (__dma_fence_enable_signaling(fence)) {
		cb->func = func;
		list_add_tail(&cb->node, &fence->cb_list);
	} else {
		INIT_LIST_HEAD(&cb->node);
		ret = -ENOENT;
	}

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

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return ret;

	spin_lock_irqsave(fence->lock, flags);

	if (intr && signal_pending(current)) {
		ret = -ERESTARTSYS;
		goto out;
	}

	if (!__dma_fence_enable_signaling(fence))
		goto out;

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

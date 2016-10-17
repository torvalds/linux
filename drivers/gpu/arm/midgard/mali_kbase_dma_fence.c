/*
 *
 * (C) COPYRIGHT 2011-2016 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */




/* Include mali_kbase_dma_fence.h before checking for CONFIG_MALI_DMA_FENCE as
 * it will be set there.
 */
#include "mali_kbase_dma_fence.h"

#include <linux/atomic.h>
#include <linux/fence.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/reservation.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/ww_mutex.h>

#include <mali_kbase.h>


/* Spin lock protecting all Mali fences as fence->lock. */
static DEFINE_SPINLOCK(kbase_dma_fence_lock);

static void
kbase_dma_fence_work(struct work_struct *pwork);

static void
kbase_dma_fence_waiters_add(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;

	list_add_tail(&katom->queue, &kctx->dma_fence.waiting_resource);
}

void
kbase_dma_fence_waiters_remove(struct kbase_jd_atom *katom)
{
	list_del(&katom->queue);
}

static const char *
kbase_dma_fence_get_driver_name(struct fence *fence)
{
	return kbase_drv_name;
}

static const char *
kbase_dma_fence_get_timeline_name(struct fence *fence)
{
	return kbase_timeline_name;
}

static bool
kbase_dma_fence_enable_signaling(struct fence *fence)
{
	/* If in the future we need to add code here remember to
	 * to get a reference to the fence and release it when signaling
	 * as stated in fence.h
	 */
	return true;
}

static void
kbase_dma_fence_fence_value_str(struct fence *fence, char *str, int size)
{
	snprintf(str, size, "%u", fence->seqno);
}

static const struct fence_ops kbase_dma_fence_ops = {
	.get_driver_name = kbase_dma_fence_get_driver_name,
	.get_timeline_name = kbase_dma_fence_get_timeline_name,
	.enable_signaling = kbase_dma_fence_enable_signaling,
	/* Use the default wait */
	.wait = fence_default_wait,
	.fence_value_str = kbase_dma_fence_fence_value_str,
};

static struct fence *
kbase_dma_fence_new(unsigned int context, unsigned int seqno)
{
	struct fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return NULL;

	fence_init(fence,
		   &kbase_dma_fence_ops,
		   &kbase_dma_fence_lock,
		   context,
		   seqno);

	return fence;
}

static int
kbase_dma_fence_lock_reservations(struct kbase_dma_fence_resv_info *info,
				  struct ww_acquire_ctx *ctx)
{
	struct reservation_object *content_res = NULL;
	unsigned int content_res_idx = 0;
	unsigned int r;
	int err = 0;

	ww_acquire_init(ctx, &reservation_ww_class);

retry:
	for (r = 0; r < info->dma_fence_resv_count; r++) {
		if (info->resv_objs[r] == content_res) {
			content_res = NULL;
			continue;
		}

		err = ww_mutex_lock(&info->resv_objs[r]->lock, ctx);
		if (err)
			goto error;
	}

	ww_acquire_done(ctx);
	return err;

error:
	content_res_idx = r;

	/* Unlock the locked one ones */
	while (r--)
		ww_mutex_unlock(&info->resv_objs[r]->lock);

	if (content_res)
		ww_mutex_unlock(&content_res->lock);

	/* If we deadlock try with lock_slow and retry */
	if (err == -EDEADLK) {
		content_res = info->resv_objs[content_res_idx];
		ww_mutex_lock_slow(&content_res->lock, ctx);
		goto retry;
	}

	/* If we are here the function failed */
	ww_acquire_fini(ctx);
	return err;
}

static void
kbase_dma_fence_unlock_reservations(struct kbase_dma_fence_resv_info *info,
				    struct ww_acquire_ctx *ctx)
{
	unsigned int r;

	for (r = 0; r < info->dma_fence_resv_count; r++)
		ww_mutex_unlock(&info->resv_objs[r]->lock);
	ww_acquire_fini(ctx);
}

/**
 * kbase_dma_fence_queue_work() - Queue work to handle @katom
 * @katom: Pointer to atom for which to queue work
 *
 * Queue kbase_dma_fence_work() for @katom to clean up the fence callbacks and
 * submit the atom.
 */
static void
kbase_dma_fence_queue_work(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	bool ret;

	INIT_WORK(&katom->work, kbase_dma_fence_work);
	ret = queue_work(kctx->dma_fence.wq, &katom->work);
	/* Warn if work was already queued, that should not happen. */
	WARN_ON(!ret);
}

/**
 * kbase_dma_fence_free_callbacks - Free dma-fence callbacks on a katom
 * @katom: Pointer to katom
 * @queue_worker: Boolean indicating if fence worker is to be queued when
 *                dep_count reaches 0.
 *
 * This function will free all fence callbacks on the katom's list of
 * callbacks. Callbacks that have not yet been called, because their fence
 * hasn't yet signaled, will first be removed from the fence.
 *
 * Locking: katom->dma_fence.callbacks list assumes jctx.lock is held.
 */
static void
kbase_dma_fence_free_callbacks(struct kbase_jd_atom *katom, bool queue_worker)
{
	struct kbase_dma_fence_cb *cb, *tmp;

	lockdep_assert_held(&katom->kctx->jctx.lock);

	/* Clean up and free callbacks. */
	list_for_each_entry_safe(cb, tmp, &katom->dma_fence.callbacks, node) {
		bool ret;

		/* Cancel callbacks that hasn't been called yet. */
		ret = fence_remove_callback(cb->fence, &cb->fence_cb);
		if (ret) {
			int ret;

			/* Fence had not signaled, clean up after
			 * canceling.
			 */
			ret = atomic_dec_return(&katom->dma_fence.dep_count);

			if (unlikely(queue_worker && ret == 0)) {
				/*
				 * dep_count went to zero and queue_worker is
				 * true. Queue the worker to handle the
				 * completion of the katom.
				 */
				kbase_dma_fence_queue_work(katom);
			}
		}

		/*
		 * Release the reference taken in
		 * kbase_dma_fence_add_callback().
		 */
		fence_put(cb->fence);
		list_del(&cb->node);
		kfree(cb);
	}
}

/**
 * kbase_dma_fence_cancel_atom() - Cancels waiting on an atom
 * @katom:	Katom to cancel
 *
 * Locking: katom->dma_fence.callbacks list assumes jctx.lock is held.
 */
static void
kbase_dma_fence_cancel_atom(struct kbase_jd_atom *katom)
{
	lockdep_assert_held(&katom->kctx->jctx.lock);

	/* Cancel callbacks and clean up. */
	kbase_dma_fence_free_callbacks(katom, false);

	KBASE_DEBUG_ASSERT(atomic_read(&katom->dma_fence.dep_count) == 0);

	/* Mark the atom as handled in case all fences signaled just before
	 * canceling the callbacks and the worker was queued.
	 */
	atomic_set(&katom->dma_fence.dep_count, -1);

	/* Prevent job_done_nolock from being called twice on an atom when
	 * there is a race between job completion and cancellation.
	 */

	if (katom->status == KBASE_JD_ATOM_STATE_QUEUED) {
		/* Wait was cancelled - zap the atom */
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
		if (jd_done_nolock(katom, NULL))
			kbase_js_sched_all(katom->kctx->kbdev);
	}
}

/**
 * kbase_dma_fence_work() - Worker thread called when a fence is signaled
 * @pwork:	work_struct containing a pointer to a katom
 *
 * This function will clean and mark all dependencies as satisfied
 */
static void
kbase_dma_fence_work(struct work_struct *pwork)
{
	struct kbase_jd_atom *katom;
	struct kbase_jd_context *ctx;

	katom = container_of(pwork, struct kbase_jd_atom, work);
	ctx = &katom->kctx->jctx;

	mutex_lock(&ctx->lock);
	if (atomic_read(&katom->dma_fence.dep_count) != 0)
		goto out;

	atomic_set(&katom->dma_fence.dep_count, -1);

	/* Remove atom from list of dma-fence waiting atoms. */
	kbase_dma_fence_waiters_remove(katom);
	/* Cleanup callbacks. */
	kbase_dma_fence_free_callbacks(katom, false);
	/*
	 * Queue atom on GPU, unless it has already completed due to a failing
	 * dependency. Run jd_done_nolock() on the katom if it is completed.
	 */
	if (unlikely(katom->status == KBASE_JD_ATOM_STATE_COMPLETED))
		jd_done_nolock(katom, NULL);
	else
		kbase_jd_dep_clear_locked(katom);

out:
	mutex_unlock(&ctx->lock);
}

/**
 * kbase_dma_fence_add_callback() - Add callback on @fence to block @katom
 * @katom: Pointer to katom that will be blocked by @fence
 * @fence: Pointer to fence on which to set up the callback
 * @callback: Pointer to function to be called when fence is signaled
 *
 * Caller needs to hold a reference to @fence when calling this function, and
 * the caller is responsible for releasing that reference.  An additional
 * reference to @fence will be taken when the callback was successfully set up
 * and @fence needs to be kept valid until the callback has been called and
 * cleanup have been done.
 *
 * Return: 0 on success: fence was either already signalled, or callback was
 * set up. Negative error code is returned on error.
 */
static int
kbase_dma_fence_add_callback(struct kbase_jd_atom *katom,
			     struct fence *fence,
			     fence_func_t callback)
{
	int err = 0;
	struct kbase_dma_fence_cb *kbase_fence_cb;

	kbase_fence_cb = kmalloc(sizeof(*kbase_fence_cb), GFP_KERNEL);
	if (!kbase_fence_cb)
		return -ENOMEM;

	kbase_fence_cb->fence = fence;
	kbase_fence_cb->katom = katom;
	INIT_LIST_HEAD(&kbase_fence_cb->node);

	err = fence_add_callback(fence, &kbase_fence_cb->fence_cb, callback);
	if (err == -ENOENT) {
		/* Fence signaled, clear the error and return */
		err = 0;
		kbase_fence_cb->fence = NULL;
		kfree(kbase_fence_cb);
	} else if (err) {
		kfree(kbase_fence_cb);
	} else {
		/*
		 * Get reference to fence that will be kept until callback gets
		 * cleaned up in kbase_dma_fence_free_callbacks().
		 */
		fence_get(fence);
		atomic_inc(&katom->dma_fence.dep_count);
		/* Add callback to katom's list of callbacks */
		list_add(&kbase_fence_cb->node, &katom->dma_fence.callbacks);
	}

	return err;
}

static void
kbase_dma_fence_cb(struct fence *fence, struct fence_cb *cb)
{
	struct kbase_dma_fence_cb *kcb = container_of(cb,
				struct kbase_dma_fence_cb,
				fence_cb);
	struct kbase_jd_atom *katom = kcb->katom;

	/* If the atom is zapped dep_count will be forced to a negative number
	 * preventing this callback from ever scheduling work. Which in turn
	 * would reschedule the atom.
	 */
	if (atomic_dec_and_test(&katom->dma_fence.dep_count))
		kbase_dma_fence_queue_work(katom);
}

static int
kbase_dma_fence_add_reservation_callback(struct kbase_jd_atom *katom,
					 struct reservation_object *resv,
					 bool exclusive)
{
	struct fence *excl_fence = NULL;
	struct fence **shared_fences = NULL;
	unsigned int shared_count = 0;
	int err, i;

	err = reservation_object_get_fences_rcu(resv,
						&excl_fence,
						&shared_count,
						&shared_fences);
	if (err)
		return err;

	if (excl_fence) {
		err = kbase_dma_fence_add_callback(katom,
						   excl_fence,
						   kbase_dma_fence_cb);

		/* Release our reference, taken by reservation_object_get_fences_rcu(),
		 * to the fence. We have set up our callback (if that was possible),
		 * and it's the fence's owner is responsible for singling the fence
		 * before allowing it to disappear.
		 */
		fence_put(excl_fence);

		if (err)
			goto out;
	}

	if (exclusive) {
		for (i = 0; i < shared_count; i++) {
			err = kbase_dma_fence_add_callback(katom,
							   shared_fences[i],
							   kbase_dma_fence_cb);
			if (err)
				goto out;
		}
	}

	/* Release all our references to the shared fences, taken by
	 * reservation_object_get_fences_rcu(). We have set up our callback (if
	 * that was possible), and it's the fence's owner is responsible for
	 * signaling the fence before allowing it to disappear.
	 */
out:
	for (i = 0; i < shared_count; i++)
		fence_put(shared_fences[i]);
	kfree(shared_fences);

	if (err) {
		/*
		 * On error, cancel and clean up all callbacks that was set up
		 * before the error.
		 */
		kbase_dma_fence_free_callbacks(katom, false);
	}

	return err;
}

void kbase_dma_fence_add_reservation(struct reservation_object *resv,
				     struct kbase_dma_fence_resv_info *info,
				     bool exclusive)
{
	unsigned int i;

	for (i = 0; i < info->dma_fence_resv_count; i++) {
		/* Duplicate resource, ignore */
		if (info->resv_objs[i] == resv)
			return;
	}

	info->resv_objs[info->dma_fence_resv_count] = resv;
	if (exclusive)
		set_bit(info->dma_fence_resv_count,
			info->dma_fence_excl_bitmap);
	(info->dma_fence_resv_count)++;
}

int kbase_dma_fence_wait(struct kbase_jd_atom *katom,
			 struct kbase_dma_fence_resv_info *info)
{
	int err, i;
	struct fence *fence;
	struct ww_acquire_ctx ww_ctx;

	lockdep_assert_held(&katom->kctx->jctx.lock);

	fence = kbase_dma_fence_new(katom->dma_fence.context,
				    atomic_inc_return(&katom->dma_fence.seqno));
	if (!fence) {
		err = -ENOMEM;
		dev_err(katom->kctx->kbdev->dev,
			"Error %d creating fence.\n", err);
		return err;
	}

	katom->dma_fence.fence = fence;
	atomic_set(&katom->dma_fence.dep_count, 1);

	err = kbase_dma_fence_lock_reservations(info, &ww_ctx);
	if (err) {
		dev_err(katom->kctx->kbdev->dev,
			"Error %d locking reservations.\n", err);
		atomic_set(&katom->dma_fence.dep_count, -1);
		fence_put(fence);
		return err;
	}

	for (i = 0; i < info->dma_fence_resv_count; i++) {
		struct reservation_object *obj = info->resv_objs[i];

		if (!test_bit(i, info->dma_fence_excl_bitmap)) {
			err = reservation_object_reserve_shared(obj);
			if (err) {
				dev_err(katom->kctx->kbdev->dev,
					"Error %d reserving space for shared fence.\n", err);
				goto end;
			}

			err = kbase_dma_fence_add_reservation_callback(katom, obj, false);
			if (err) {
				dev_err(katom->kctx->kbdev->dev,
					"Error %d adding reservation to callback.\n", err);
				goto end;
			}

			reservation_object_add_shared_fence(obj, katom->dma_fence.fence);
		} else {
			err = kbase_dma_fence_add_reservation_callback(katom, obj, true);
			if (err) {
				dev_err(katom->kctx->kbdev->dev,
					"Error %d adding reservation to callback.\n", err);
				goto end;
			}

			reservation_object_add_excl_fence(obj, katom->dma_fence.fence);
		}
	}

end:
	kbase_dma_fence_unlock_reservations(info, &ww_ctx);

	if (likely(!err)) {
		/* Test if the callbacks are already triggered */
		if (atomic_dec_and_test(&katom->dma_fence.dep_count)) {
			atomic_set(&katom->dma_fence.dep_count, -1);
			kbase_dma_fence_free_callbacks(katom, false);
		} else {
			/* Add katom to the list of dma-buf fence waiting atoms
			 * only if it is still waiting.
			 */
			kbase_dma_fence_waiters_add(katom);
		}
	} else {
		/* There was an error, cancel callbacks, set dep_count to -1 to
		 * indicate that the atom has been handled (the caller will
		 * kill it for us), signal the fence, free callbacks and the
		 * fence.
		 */
		kbase_dma_fence_free_callbacks(katom, false);
		atomic_set(&katom->dma_fence.dep_count, -1);
		kbase_dma_fence_signal(katom);
	}

	return err;
}

void kbase_dma_fence_cancel_all_atoms(struct kbase_context *kctx)
{
	struct list_head *list = &kctx->dma_fence.waiting_resource;

	while (!list_empty(list)) {
		struct kbase_jd_atom *katom;

		katom = list_first_entry(list, struct kbase_jd_atom, queue);
		kbase_dma_fence_waiters_remove(katom);
		kbase_dma_fence_cancel_atom(katom);
	}
}

void kbase_dma_fence_cancel_callbacks(struct kbase_jd_atom *katom)
{
	/* Cancel callbacks and clean up. */
	kbase_dma_fence_free_callbacks(katom, true);
}

void kbase_dma_fence_signal(struct kbase_jd_atom *katom)
{
	if (!katom->dma_fence.fence)
		return;

	KBASE_DEBUG_ASSERT(atomic_read(&katom->dma_fence.dep_count) == -1);

	/* Signal the atom's fence. */
	fence_signal(katom->dma_fence.fence);
	fence_put(katom->dma_fence.fence);
	katom->dma_fence.fence = NULL;

	kbase_dma_fence_free_callbacks(katom, false);
}

void kbase_dma_fence_term(struct kbase_context *kctx)
{
	destroy_workqueue(kctx->dma_fence.wq);
	kctx->dma_fence.wq = NULL;
}

int kbase_dma_fence_init(struct kbase_context *kctx)
{
	INIT_LIST_HEAD(&kctx->dma_fence.waiting_resource);

	kctx->dma_fence.wq = alloc_workqueue("mali-fence-%d",
					     WQ_UNBOUND, 1, kctx->pid);
	if (!kctx->dma_fence.wq)
		return -ENOMEM;

	return 0;
}

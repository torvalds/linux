/*
 * drm_sync_helper.c: software fence and helper functions for fences and
 * reservations used for dma buffer access synchronization between drivers.
 *
 * Copyright 2014 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <drm/drm_sync_helper.h>
#include <linux/slab.h>
#include <linux/reservation.h>

static DEFINE_SPINLOCK(sw_fence_lock);

void drm_add_reservation(struct reservation_object *resv,
			 struct reservation_object **resvs,
			 unsigned long *excl_resvs_bitmap,
			 unsigned int *num_resvs, bool exclusive)
{
	unsigned int r;

	for (r = 0; r < *num_resvs; r++) {
		if (resvs[r] == resv)
			return;
	}
	resvs[*num_resvs] = resv;
	if (exclusive)
		set_bit(*num_resvs, excl_resvs_bitmap);
	(*num_resvs)++;
}
EXPORT_SYMBOL(drm_add_reservation);

int drm_lock_reservations(struct reservation_object **resvs,
			  unsigned int num_resvs, struct ww_acquire_ctx *ctx)
{
	unsigned int r;
	struct reservation_object *slow_res = NULL;

	ww_acquire_init(ctx, &reservation_ww_class);

retry:
	for (r = 0; r < num_resvs; r++) {
		int ret;
		/* skip the resv we locked with slow lock */
		if (resvs[r] == slow_res) {
			slow_res = NULL;
			continue;
		}
		ret = ww_mutex_lock(&resvs[r]->lock, ctx);
		if (ret < 0) {
			unsigned int slow_r = r;
			/*
			 * undo all the locks we already done,
			 * in reverse order
			 */
			while (r > 0) {
				r--;
				ww_mutex_unlock(&resvs[r]->lock);
			}
			if (slow_res)
				ww_mutex_unlock(&slow_res->lock);
			if (ret == -EDEADLK) {
				slow_res = resvs[slow_r];
				ww_mutex_lock_slow(&slow_res->lock, ctx);
				goto retry;
			}
			ww_acquire_fini(ctx);
			return ret;
		}
	}

	ww_acquire_done(ctx);
	return 0;
}
EXPORT_SYMBOL(drm_lock_reservations);

void drm_unlock_reservations(struct reservation_object **resvs,
			     unsigned int num_resvs,
			     struct ww_acquire_ctx *ctx)
{
	unsigned int r;

	for (r = 0; r < num_resvs; r++)
		ww_mutex_unlock(&resvs[r]->lock);

	ww_acquire_fini(ctx);
}
EXPORT_SYMBOL(drm_unlock_reservations);

static void reservation_cb_fence_cb(struct fence *fence, struct fence_cb *cb)
{
	struct drm_reservation_fence_cb *rfcb =
		container_of(cb, struct drm_reservation_fence_cb, base);
	struct drm_reservation_cb *rcb = rfcb->parent;

	if (atomic_dec_and_test(&rcb->count))
		schedule_work(&rcb->work);
}

static void
reservation_cb_cleanup(struct drm_reservation_cb *rcb)
{
	unsigned cb;

	for (cb = 0; cb < rcb->num_fence_cbs; cb++) {
		if (rcb->fence_cbs[cb]) {
			fence_remove_callback(rcb->fence_cbs[cb]->fence,
						&rcb->fence_cbs[cb]->base);
			fence_put(rcb->fence_cbs[cb]->fence);
			kfree(rcb->fence_cbs[cb]);
			rcb->fence_cbs[cb] = NULL;
		}
	}
	kfree(rcb->fence_cbs);
	rcb->fence_cbs = NULL;
	rcb->num_fence_cbs = 0;
}

static void reservation_cb_work(struct work_struct *pwork)
{
	struct drm_reservation_cb *rcb =
		container_of(pwork, struct drm_reservation_cb, work);
	/*
	 * clean up everything before calling the callback, because the callback
	 * may free structure containing rcb and work_struct
	 */
	reservation_cb_cleanup(rcb);
	rcb->func(rcb, rcb->context);
}

static int
reservation_cb_add_fence_cb(struct drm_reservation_cb *rcb, struct fence *fence)
{
	int ret = 0;
	struct drm_reservation_fence_cb *fence_cb;
	struct drm_reservation_fence_cb **new_fence_cbs;

	new_fence_cbs = krealloc(rcb->fence_cbs,
				(rcb->num_fence_cbs + 1)
				* sizeof(struct drm_reservation_fence_cb *),
				GFP_KERNEL);
	if (!new_fence_cbs)
		return -ENOMEM;
	rcb->fence_cbs = new_fence_cbs;

	fence_cb = kzalloc(sizeof(struct drm_reservation_fence_cb), GFP_KERNEL);
	if (!fence_cb)
		return -ENOMEM;

	/*
	 * do not want for fence to disappear on us while we are waiting for
	 * callback and we need it in case we want to remove callbacks
	 */
	fence_get(fence);
	fence_cb->fence = fence;
	fence_cb->parent = rcb;
	rcb->fence_cbs[rcb->num_fence_cbs] = fence_cb;
	atomic_inc(&rcb->count);
	ret = fence_add_callback(fence, &fence_cb->base,
					reservation_cb_fence_cb);
	if (ret == -ENOENT) {
		/* already signaled */
		atomic_dec(&rcb->count);
		fence_put(fence_cb->fence);
		kfree(fence_cb);
		ret = 0;
	} else if (ret < 0) {
		atomic_dec(&rcb->count);
		fence_put(fence_cb->fence);
		kfree(fence_cb);
		return ret;
	} else {
		rcb->num_fence_cbs++;
	}
	return ret;
}

void
drm_reservation_cb_init(struct drm_reservation_cb *rcb,
			drm_reservation_cb_func_t func, void *context)
{
	INIT_WORK(&rcb->work, reservation_cb_work);
	atomic_set(&rcb->count, 1);
	rcb->num_fence_cbs = 0;
	rcb->fence_cbs = NULL;
	rcb->func = func;
	rcb->context = context;
}
EXPORT_SYMBOL(drm_reservation_cb_init);

int
drm_reservation_cb_add(struct drm_reservation_cb *rcb,
			struct reservation_object *resv, bool exclusive)
{
	int ret = 0;
	struct fence *fence;
	unsigned shared_count = 0, f;
	struct fence **shared_fences = NULL;

	/* enum all the fences in the reservation and add callbacks */
	ret = reservation_object_get_fences_rcu(resv, &fence,
					&shared_count, &shared_fences);
	if (ret < 0)
		return ret;

	if (fence) {
		ret = reservation_cb_add_fence_cb(rcb, fence);
		if (ret < 0) {
			reservation_cb_cleanup(rcb);
			goto error;
		}
	}

	if (exclusive) {
		for (f = 0; f < shared_count; f++) {
			ret = reservation_cb_add_fence_cb(rcb,
						shared_fences[f]);
			if (ret < 0) {
				reservation_cb_cleanup(rcb);
				goto error;
			}
		}
	}

error:
	if (fence)
		fence_put(fence);
	if (shared_fences) {
		for (f = 0; f < shared_count; f++)
			fence_put(shared_fences[f]);
		kfree(shared_fences);
	}
	return ret;
}
EXPORT_SYMBOL(drm_reservation_cb_add);

void
drm_reservation_cb_done(struct drm_reservation_cb *rcb)
{
	/*
	 * we need to decrement from initial 1
	 * and trigger the callback in case all the
	 * fences were already triggered
	 */
	if (atomic_dec_and_test(&rcb->count)) {
		/*
		 * we could call the callback here directly but in case
		 * the callback function needs to lock the same mutex
		 * as our caller it could cause a deadlock, so it is
		 * safer to call it from a worker
		 */
		schedule_work(&rcb->work);
	}
}
EXPORT_SYMBOL(drm_reservation_cb_done);

void
drm_reservation_cb_fini(struct drm_reservation_cb *rcb)
{
	/* make sure no work will be triggered */
	atomic_set(&rcb->count, 0);
	cancel_work_sync(&rcb->work);
	reservation_cb_cleanup(rcb);
}
EXPORT_SYMBOL(drm_reservation_cb_fini);

static bool sw_fence_enable_signaling(struct fence *f)
{
	return true;
}

static const char *sw_fence_get_get_driver_name(struct fence *fence)
{
	return "drm_sync_helper";
}

static const char *sw_fence_get_timeline_name(struct fence *f)
{
	return "drm_sync.sw";
}

static const struct fence_ops sw_fence_ops = {
	.get_driver_name = sw_fence_get_get_driver_name,
	.get_timeline_name = sw_fence_get_timeline_name,
	.enable_signaling = sw_fence_enable_signaling,
	.signaled = NULL,
	.wait = fence_default_wait,
	.release = NULL
};

struct fence *drm_sw_fence_new(unsigned int context, unsigned seqno)
{
	struct fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return ERR_PTR(-ENOMEM);
	fence_init(fence,
		   &sw_fence_ops,
		   &sw_fence_lock,
		   context, seqno);

	return fence;
}
EXPORT_SYMBOL(drm_sw_fence_new);

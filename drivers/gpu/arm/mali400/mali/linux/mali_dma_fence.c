/*
 * Copyright (C) 2012-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/version.h>
#include "mali_osk.h"
#include "mali_kernel_common.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
#include "mali_dma_fence.h"
#include <linux/atomic.h>
#include <linux/workqueue.h>
#endif

static DEFINE_SPINLOCK(mali_dma_fence_lock);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
static bool mali_dma_fence_enable_signaling(struct dma_fence *fence)
{
	MALI_IGNORE(fence);
	return true;
}

static const char *mali_dma_fence_get_driver_name(struct dma_fence *fence)
{
	MALI_IGNORE(fence);
	return "mali";
}

static const char *mali_dma_fence_get_timeline_name(struct dma_fence *fence)
{
	MALI_IGNORE(fence);
	return "mali_dma_fence";
}

static const struct dma_fence_ops mali_dma_fence_ops = {
	.get_driver_name = mali_dma_fence_get_driver_name,
	.get_timeline_name = mali_dma_fence_get_timeline_name,
	.enable_signaling = mali_dma_fence_enable_signaling,
	.signaled = NULL,
	.wait = dma_fence_default_wait,
	.release = NULL
};
#else
static bool mali_dma_fence_enable_signaling(struct fence *fence)
{
	MALI_IGNORE(fence);
	return true;
}

static const char *mali_dma_fence_get_driver_name(struct fence *fence)
{
	MALI_IGNORE(fence);
	return "mali";
}

static const char *mali_dma_fence_get_timeline_name(struct fence *fence)
{
	MALI_IGNORE(fence);
	return "mali_dma_fence";
}

static const struct fence_ops mali_dma_fence_ops = {
	.get_driver_name = mali_dma_fence_get_driver_name,
	.get_timeline_name = mali_dma_fence_get_timeline_name,
	.enable_signaling = mali_dma_fence_enable_signaling,
	.signaled = NULL,
	.wait = fence_default_wait,
	.release = NULL
};
#endif

static void mali_dma_fence_context_cleanup(struct mali_dma_fence_context *dma_fence_context)
{
	u32 i;

	MALI_DEBUG_ASSERT_POINTER(dma_fence_context);

	for (i = 0; i < dma_fence_context->num_dma_fence_waiter; i++) {
		if (dma_fence_context->mali_dma_fence_waiters[i]) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
			dma_fence_remove_callback(dma_fence_context->mali_dma_fence_waiters[i]->fence,
						  &dma_fence_context->mali_dma_fence_waiters[i]->base);
			dma_fence_put(dma_fence_context->mali_dma_fence_waiters[i]->fence);

#else
			fence_remove_callback(dma_fence_context->mali_dma_fence_waiters[i]->fence,
					      &dma_fence_context->mali_dma_fence_waiters[i]->base);
			fence_put(dma_fence_context->mali_dma_fence_waiters[i]->fence);
#endif
			kfree(dma_fence_context->mali_dma_fence_waiters[i]);
			dma_fence_context->mali_dma_fence_waiters[i] = NULL;
		}
	}

	if (NULL != dma_fence_context->mali_dma_fence_waiters)
		kfree(dma_fence_context->mali_dma_fence_waiters);

	dma_fence_context->mali_dma_fence_waiters = NULL;
	dma_fence_context->num_dma_fence_waiter = 0;
}

static void mali_dma_fence_context_work_func(struct work_struct *work_handle)
{
	struct mali_dma_fence_context *dma_fence_context;

	MALI_DEBUG_ASSERT_POINTER(work_handle);

	dma_fence_context = container_of(work_handle, struct mali_dma_fence_context, work_handle);

	dma_fence_context->cb_func(dma_fence_context->pp_job_ptr);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
static void mali_dma_fence_callback(struct dma_fence *fence, struct dma_fence_cb *cb)
#else
static void mali_dma_fence_callback(struct fence *fence, struct fence_cb *cb)
#endif
{
	struct mali_dma_fence_waiter *dma_fence_waiter = NULL;
	struct mali_dma_fence_context *dma_fence_context = NULL;

	MALI_DEBUG_ASSERT_POINTER(fence);
	MALI_DEBUG_ASSERT_POINTER(cb);

	MALI_IGNORE(fence);

	dma_fence_waiter = container_of(cb, struct mali_dma_fence_waiter, base);
	dma_fence_context = dma_fence_waiter->parent;

	MALI_DEBUG_ASSERT_POINTER(dma_fence_context);

	if (atomic_dec_and_test(&dma_fence_context->count))
		schedule_work(&dma_fence_context->work_handle);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
static _mali_osk_errcode_t mali_dma_fence_add_callback(struct mali_dma_fence_context *dma_fence_context, struct dma_fence *fence)
#else
static _mali_osk_errcode_t mali_dma_fence_add_callback(struct mali_dma_fence_context *dma_fence_context, struct fence *fence)
#endif
{
	int ret = 0;
	struct mali_dma_fence_waiter *dma_fence_waiter;
	struct mali_dma_fence_waiter **dma_fence_waiters;

	MALI_DEBUG_ASSERT_POINTER(dma_fence_context);
	MALI_DEBUG_ASSERT_POINTER(fence);

	dma_fence_waiters = krealloc(dma_fence_context->mali_dma_fence_waiters,
				     (dma_fence_context->num_dma_fence_waiter + 1)
				     * sizeof(struct mali_dma_fence_waiter *),
				     GFP_KERNEL);

	if (NULL == dma_fence_waiters) {
		MALI_DEBUG_PRINT(1, ("Mali dma fence: failed to realloc the dma fence waiters.\n"));
		return _MALI_OSK_ERR_NOMEM;
	}

	dma_fence_context->mali_dma_fence_waiters = dma_fence_waiters;

	dma_fence_waiter = kzalloc(sizeof(struct mali_dma_fence_waiter), GFP_KERNEL);

	if (NULL == dma_fence_waiter) {
		MALI_DEBUG_PRINT(1, ("Mali dma fence: failed to create mali dma fence waiter.\n"));
		return _MALI_OSK_ERR_NOMEM;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	dma_fence_get(fence);
#else
	fence_get(fence);
#endif
	dma_fence_waiter->fence = fence;
	dma_fence_waiter->parent = dma_fence_context;
	atomic_inc(&dma_fence_context->count);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	ret = dma_fence_add_callback(fence, &dma_fence_waiter->base,
				     mali_dma_fence_callback);
#else
	ret = fence_add_callback(fence, &dma_fence_waiter->base,
				 mali_dma_fence_callback);
#endif
	if (0 > ret) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
		dma_fence_put(fence);
#else
		fence_put(fence);
#endif
		kfree(dma_fence_waiter);
		atomic_dec(&dma_fence_context->count);
		if (-ENOENT == ret) {
			/*-ENOENT if fence has already been signaled, return _MALI_OSK_ERR_OK*/
			return _MALI_OSK_ERR_OK;
		}
		/* Failed to add the fence callback into fence, return _MALI_OSK_ERR_FAULT*/
		MALI_DEBUG_PRINT(1, ("Mali dma fence: failed to add callback into fence.\n"));
		return _MALI_OSK_ERR_FAULT;
	}

	dma_fence_context->mali_dma_fence_waiters[dma_fence_context->num_dma_fence_waiter] = dma_fence_waiter;
	dma_fence_context->num_dma_fence_waiter++;

	return _MALI_OSK_ERR_OK;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
struct dma_fence *mali_dma_fence_new(u32  context, u32 seqno)
#else
struct fence *mali_dma_fence_new(u32  context, u32 seqno)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	struct dma_fence *fence = NULL;
	fence = kzalloc(sizeof(struct dma_fence), GFP_KERNEL);
#else
	struct fence *fence = NULL;
	fence = kzalloc(sizeof(struct fence), GFP_KERNEL);
#endif
	if (NULL == fence) {
		MALI_DEBUG_PRINT(1, ("Mali dma fence: failed to create dma fence.\n"));
		return fence;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	dma_fence_init(fence,
		       &mali_dma_fence_ops,
		       &mali_dma_fence_lock,
		       context, seqno);
#else
	fence_init(fence,
		   &mali_dma_fence_ops,
		   &mali_dma_fence_lock,
		   context, seqno);
#endif
	return fence;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
void mali_dma_fence_signal_and_put(struct dma_fence **fence)
#else
void mali_dma_fence_signal_and_put(struct fence **fence)
#endif
{
	MALI_DEBUG_ASSERT_POINTER(fence);
	MALI_DEBUG_ASSERT_POINTER(*fence);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	dma_fence_signal(*fence);
	dma_fence_put(*fence);
#else
	fence_signal(*fence);
	fence_put(*fence);
#endif
	*fence = NULL;
}

void mali_dma_fence_context_init(struct mali_dma_fence_context *dma_fence_context,
				 mali_dma_fence_context_callback_func_t  cb_func,
				 void *pp_job_ptr)
{
	MALI_DEBUG_ASSERT_POINTER(dma_fence_context);

	INIT_WORK(&dma_fence_context->work_handle, mali_dma_fence_context_work_func);
	atomic_set(&dma_fence_context->count, 1);
	dma_fence_context->num_dma_fence_waiter = 0;
	dma_fence_context->mali_dma_fence_waiters = NULL;
	dma_fence_context->cb_func = cb_func;
	dma_fence_context->pp_job_ptr = pp_job_ptr;
}

_mali_osk_errcode_t mali_dma_fence_context_add_waiters(struct mali_dma_fence_context *dma_fence_context,
		struct reservation_object *dma_reservation_object)
{
	_mali_osk_errcode_t ret = _MALI_OSK_ERR_OK;
	u32 shared_count = 0, i;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	struct dma_fence *exclusive_fence = NULL;
	struct dma_fence **shared_fences = NULL;
#else
	struct fence *exclusive_fence = NULL;
	struct fence **shared_fences = NULL;
#endif
	MALI_DEBUG_ASSERT_POINTER(dma_fence_context);
	MALI_DEBUG_ASSERT_POINTER(dma_reservation_object);

	/* Get all the shared/exclusive fences in the reservation object of dma buf*/
	ret = reservation_object_get_fences_rcu(dma_reservation_object, &exclusive_fence,
						&shared_count, &shared_fences);
	if (ret < 0) {
		MALI_DEBUG_PRINT(1, ("Mali dma fence: failed to get  shared or exclusive_fence dma fences from  the reservation object of dma buf.\n"));
		return _MALI_OSK_ERR_FAULT;
	}

	if (exclusive_fence) {
		ret = mali_dma_fence_add_callback(dma_fence_context, exclusive_fence);
		if (_MALI_OSK_ERR_OK != ret) {
			MALI_DEBUG_PRINT(1, ("Mali dma fence: failed to add callback into exclusive fence.\n"));
			mali_dma_fence_context_cleanup(dma_fence_context);
			goto ended;
		}
	}


	for (i = 0; i < shared_count; i++) {
		ret = mali_dma_fence_add_callback(dma_fence_context, shared_fences[i]);
		if (_MALI_OSK_ERR_OK != ret) {
			MALI_DEBUG_PRINT(1, ("Mali dma fence: failed to add callback into shared fence [%d].\n", i));
			mali_dma_fence_context_cleanup(dma_fence_context);
			break;
		}
	}

ended:

	if (exclusive_fence)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
		dma_fence_put(exclusive_fence);
#else
		fence_put(exclusive_fence);
#endif

	if (shared_fences) {
		for (i = 0; i < shared_count; i++) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
			dma_fence_put(shared_fences[i]);
#else
			fence_put(shared_fences[i]);
#endif
		}
		kfree(shared_fences);
	}

	return ret;
}


void mali_dma_fence_context_term(struct mali_dma_fence_context *dma_fence_context)
{
	MALI_DEBUG_ASSERT_POINTER(dma_fence_context);
	atomic_set(&dma_fence_context->count, 0);
	if (dma_fence_context->work_handle.func) {
		cancel_work_sync(&dma_fence_context->work_handle);
	}
	mali_dma_fence_context_cleanup(dma_fence_context);
}

void mali_dma_fence_context_dec_count(struct mali_dma_fence_context *dma_fence_context)
{
	MALI_DEBUG_ASSERT_POINTER(dma_fence_context);

	if (atomic_dec_and_test(&dma_fence_context->count))
		schedule_work(&dma_fence_context->work_handle);
}


void mali_dma_fence_add_reservation_object_list(struct reservation_object *dma_reservation_object,
		struct reservation_object **dma_reservation_object_list,
		u32 *num_dma_reservation_object)
{
	u32 i;

	MALI_DEBUG_ASSERT_POINTER(dma_reservation_object);
	MALI_DEBUG_ASSERT_POINTER(dma_reservation_object_list);
	MALI_DEBUG_ASSERT_POINTER(num_dma_reservation_object);

	for (i = 0; i < *num_dma_reservation_object; i++) {
		if (dma_reservation_object_list[i] == dma_reservation_object)
			return;
	}

	dma_reservation_object_list[*num_dma_reservation_object] = dma_reservation_object;
	(*num_dma_reservation_object)++;
}

int mali_dma_fence_lock_reservation_object_list(struct reservation_object **dma_reservation_object_list,
		u32 num_dma_reservation_object, struct ww_acquire_ctx *ww_actx)
{
	u32 i;

	struct reservation_object *reservation_object_to_slow_lock = NULL;

	MALI_DEBUG_ASSERT_POINTER(dma_reservation_object_list);
	MALI_DEBUG_ASSERT_POINTER(ww_actx);

	ww_acquire_init(ww_actx, &reservation_ww_class);

again:
	for (i = 0; i < num_dma_reservation_object; i++) {
		int ret;

		if (dma_reservation_object_list[i] == reservation_object_to_slow_lock) {
			reservation_object_to_slow_lock = NULL;
			continue;
		}

		ret = ww_mutex_lock(&dma_reservation_object_list[i]->lock, ww_actx);

		if (ret < 0) {
			u32  slow_lock_index = i;

			/* unlock all pre locks we have already locked.*/
			while (i > 0) {
				i--;
				ww_mutex_unlock(&dma_reservation_object_list[i]->lock);
			}

			if (NULL != reservation_object_to_slow_lock)
				ww_mutex_unlock(&reservation_object_to_slow_lock->lock);

			if (ret == -EDEADLK) {
				reservation_object_to_slow_lock = dma_reservation_object_list[slow_lock_index];
				ww_mutex_lock_slow(&reservation_object_to_slow_lock->lock, ww_actx);
				goto again;
			}
			ww_acquire_fini(ww_actx);
			MALI_DEBUG_PRINT(1, ("Mali dma fence: failed to lock all dma reservation objects.\n", i));
			return ret;
		}
	}

	ww_acquire_done(ww_actx);
	return 0;
}

void mali_dma_fence_unlock_reservation_object_list(struct reservation_object **dma_reservation_object_list,
		u32 num_dma_reservation_object, struct ww_acquire_ctx *ww_actx)
{
	u32 i;

	for (i = 0; i < num_dma_reservation_object; i++)
		ww_mutex_unlock(&dma_reservation_object_list[i]->lock);

	ww_acquire_fini(ww_actx);
}

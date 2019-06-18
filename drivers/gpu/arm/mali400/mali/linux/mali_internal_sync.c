/*
 * Copyright (C) 2012-2018 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_internal_sync.h"
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
#include <linux/ioctl.h>
#include <linux/export.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/anon_inodes.h>

#include "mali_osk.h"
#include "mali_kernel_common.h"
#if defined(DEBUG)
#include "mali_session.h"
#include "mali_timeline.h"
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
static const struct dma_fence_ops fence_ops;
#else
static const struct fence_ops fence_ops;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
static struct mali_internal_sync_point *mali_internal_fence_to_sync_pt(struct dma_fence *fence)
#else
static struct mali_internal_sync_point *mali_internal_fence_to_sync_pt(struct fence *fence)
#endif
{
	MALI_DEBUG_ASSERT_POINTER(fence);
	return container_of(fence, struct mali_internal_sync_point, base);
}

static inline struct mali_internal_sync_timeline *mali_internal_sync_pt_to_sync_timeline(struct mali_internal_sync_point *sync_pt)
{
	MALI_DEBUG_ASSERT_POINTER(sync_pt);
	return container_of(sync_pt->base.lock, struct mali_internal_sync_timeline, sync_pt_list_lock);
}

static void mali_internal_sync_timeline_free(struct kref *kref_count)
{
	struct mali_internal_sync_timeline *sync_timeline;

	MALI_DEBUG_ASSERT_POINTER(kref_count);

	sync_timeline = container_of(kref_count, struct mali_internal_sync_timeline, kref_count);

	if (sync_timeline->ops->release_obj)
		sync_timeline->ops->release_obj(sync_timeline);

	kfree(sync_timeline);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
static void mali_internal_fence_check_cb_func(struct fence *fence, struct fence_cb *cb)
#else
static void mali_internal_fence_check_cb_func(struct dma_fence *fence, struct dma_fence_cb *cb)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
	struct mali_internal_sync_fence_cb *check;
#else
	struct mali_internal_sync_fence_waiter *waiter;
#endif
	struct mali_internal_sync_fence *sync_fence;
	int ret;
	MALI_DEBUG_ASSERT_POINTER(cb);
	MALI_IGNORE(fence);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
	check = container_of(cb, struct mali_internal_sync_fence_cb, cb);
	sync_fence = check->sync_file;
#else
	waiter = container_of(cb, struct mali_internal_sync_fence_waiter, cb);
	sync_fence = (struct mali_internal_sync_fence *)waiter->work.private;
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
	ret = atomic_dec_and_test(&sync_fence->status);
	if (ret)
		wake_up_all(&sync_fence->wq);
#else
	ret = sync_fence->fence->ops->signaled(sync_fence->fence);

	if (0 > ret)
		MALI_PRINT_ERROR(("Mali internal sync:Failed to wait fence  0x%x for sync_fence 0x%x.\n", fence, sync_fence));
	if (1 == ret)
		wake_up_all(&sync_fence->wq);
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
static void mali_internal_sync_fence_add_fence(struct mali_internal_sync_fence *sync_fence, struct fence *sync_pt)
{
	int fence_num = 0;
	MALI_DEBUG_ASSERT_POINTER(sync_fence);
	MALI_DEBUG_ASSERT_POINTER(sync_pt);

	fence_num = sync_fence->num_fences;

	sync_fence->cbs[fence_num].fence = sync_pt;
	sync_fence->cbs[fence_num].sync_file = sync_fence;

	if (!fence_add_callback(sync_pt, &sync_fence->cbs[fence_num].cb, mali_internal_fence_check_cb_func)) {
		fence_get(sync_pt);
		sync_fence->num_fences++;
		atomic_inc(&sync_fence->status);
	}
}
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
static int mali_internal_sync_fence_wake_up_wq(wait_queue_entry_t *curr, unsigned mode,
		int wake_flags, void *key)
#else
static int mali_internal_sync_fence_wake_up_wq(wait_queue_t *curr, unsigned mode,
		int wake_flags, void *key)
#endif
{
	struct mali_internal_sync_fence_waiter *wait;
	MALI_IGNORE(mode);
	MALI_IGNORE(wake_flags);
	MALI_IGNORE(key);

	wait = container_of(curr, struct mali_internal_sync_fence_waiter, work);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
	list_del_init(&wait->work.entry);
#else
	list_del_init(&wait->work.task_list);
#endif
	wait->callback(wait->work.private, wait);
	return 1;
}

struct mali_internal_sync_timeline *mali_internal_sync_timeline_create(const struct mali_internal_sync_timeline_ops *ops,
		int size, const char *name)
{
	struct mali_internal_sync_timeline *sync_timeline = NULL;

	MALI_DEBUG_ASSERT_POINTER(ops);

	if (size < sizeof(struct mali_internal_sync_timeline)) {
		MALI_PRINT_ERROR(("Mali internal sync:Invalid size to create the mali internal sync timeline.\n"));
		goto err;
	}

	sync_timeline = kzalloc(size, GFP_KERNEL);
	if (NULL == sync_timeline) {
		MALI_PRINT_ERROR(("Mali internal sync:Failed to  allocate buffer  for the mali internal sync timeline.\n"));
		goto err;
	}
	kref_init(&sync_timeline->kref_count);
	sync_timeline->ops = ops;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	sync_timeline->fence_context = dma_fence_context_alloc(1);
#else
	sync_timeline->fence_context = fence_context_alloc(1);
#endif
	strlcpy(sync_timeline->name, name, sizeof(sync_timeline->name));

	INIT_LIST_HEAD(&sync_timeline->sync_pt_list_head);
	spin_lock_init(&sync_timeline->sync_pt_list_lock);

	return sync_timeline;
err:
	if (NULL != sync_timeline) {
		kfree(sync_timeline);
	}
	return NULL;
}

void mali_internal_sync_timeline_destroy(struct mali_internal_sync_timeline *sync_timeline)
{
	MALI_DEBUG_ASSERT_POINTER(sync_timeline);

	sync_timeline->destroyed = MALI_TRUE;

	smp_wmb();

	mali_internal_sync_timeline_signal(sync_timeline);
	kref_put(&sync_timeline->kref_count, mali_internal_sync_timeline_free);
}

void mali_internal_sync_timeline_signal(struct mali_internal_sync_timeline *sync_timeline)
{
	unsigned long flags;
	struct mali_internal_sync_point *sync_pt, *next;

	MALI_DEBUG_ASSERT_POINTER(sync_timeline);

	spin_lock_irqsave(&sync_timeline->sync_pt_list_lock, flags);

	list_for_each_entry_safe(sync_pt, next, &sync_timeline->sync_pt_list_head,
				 sync_pt_list) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
		if (dma_fence_is_signaled_locked(&sync_pt->base))
#else
		if (fence_is_signaled_locked(&sync_pt->base))
#endif
			list_del_init(&sync_pt->sync_pt_list);
	}

	spin_unlock_irqrestore(&sync_timeline->sync_pt_list_lock, flags);
}

struct mali_internal_sync_point *mali_internal_sync_point_create(struct mali_internal_sync_timeline *sync_timeline, int size)
{
	unsigned long flags;
	struct mali_internal_sync_point *sync_pt = NULL;

	MALI_DEBUG_ASSERT_POINTER(sync_timeline);

	if (size < sizeof(struct mali_internal_sync_point)) {
		MALI_PRINT_ERROR(("Mali internal sync:Invalid size to create the mali internal sync point.\n"));
		goto err;
	}

	sync_pt = kzalloc(size, GFP_KERNEL);
	if (NULL == sync_pt) {
		MALI_PRINT_ERROR(("Mali internal sync:Failed to  allocate buffer  for the mali internal sync point.\n"));
		goto err;
	}
	spin_lock_irqsave(&sync_timeline->sync_pt_list_lock, flags);
	kref_get(&sync_timeline->kref_count);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	dma_fence_init(&sync_pt->base, &fence_ops, &sync_timeline->sync_pt_list_lock,
		       sync_timeline->fence_context, ++sync_timeline->value);
#else
	fence_init(&sync_pt->base, &fence_ops, &sync_timeline->sync_pt_list_lock,
		   sync_timeline->fence_context, ++sync_timeline->value);
#endif
	INIT_LIST_HEAD(&sync_pt->sync_pt_list);
	spin_unlock_irqrestore(&sync_timeline->sync_pt_list_lock, flags);

	return sync_pt;
err:
	if (NULL != sync_pt) {
		kfree(sync_pt);
	}
	return NULL;
}

struct mali_internal_sync_fence *mali_internal_sync_fence_fdget(int fd)
{
	struct file *file = fget(fd);

	if (NULL == file) {
		return NULL;
	}

	return file->private_data;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
struct mali_internal_sync_fence *mali_internal_sync_fence_merge(
	struct mali_internal_sync_fence *sync_fence1, struct mali_internal_sync_fence *sync_fence2)
{
	struct mali_internal_sync_fence *new_sync_fence;
	int i, j, num_fence1, num_fence2, total_fences;
	struct fence *fence0 = NULL;

	MALI_DEBUG_ASSERT_POINTER(sync_fence1);
	MALI_DEBUG_ASSERT_POINTER(sync_fence2);

	num_fence1 = sync_fence1->num_fences;
	num_fence2 = sync_fence2->num_fences;

	total_fences = num_fence1 + num_fence2;

	i = 0;
	j = 0;

	if (num_fence1 > 0) {
		fence0 = sync_fence1->cbs[i].fence;
		i = 1;
	} else if (num_fence2 > 0) {
		fence0 = sync_fence2->cbs[i].fence;
		j = 1;
	}

	new_sync_fence = (struct mali_internal_sync_fence *)sync_file_create(fence0);
	if (NULL == new_sync_fence) {
		MALI_PRINT_ERROR(("Mali internal sync:Failed to  create the mali internal sync fence when merging sync fence.\n"));
		return NULL;
	}

	fence_remove_callback(new_sync_fence->cb[0].fence, &new_sync_fence->cb[0].cb);
	new_sync_fence->num_fences = 0;
	atomic_dec(&new_sync_fence->status);

	for (; i < num_fence1 && j < num_fence2;) {
		struct fence *fence1 = sync_fence1->cbs[i].fence;
		struct fence *fence2 = sync_fence2->cbs[j].fence;

		if (fence1->context < fence2->context) {
			mali_internal_sync_fence_add_fence(new_sync_fence, fence1);

			i++;
		} else if (fence1->context > fence2->context) {
			mali_internal_sync_fence_add_fence(new_sync_fence, fence2);

			j++;
		} else {
			if (fence1->seqno - fence2->seqno <= INT_MAX)
				mali_internal_sync_fence_add_fence(new_sync_fence, fence1);
			else
				mali_internal_sync_fence_add_fence(new_sync_fence, fence2);
			i++;
			j++;
		}
	}

	for (; i < num_fence1; i++)
		mali_internal_sync_fence_add_fence(new_sync_fence, sync_fence1->cbs[i].fence);

	for (; j < num_fence2; j++)
		mali_internal_sync_fence_add_fence(new_sync_fence, sync_fence2->cbs[j].fence);

	return new_sync_fence;
}
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
static struct fence **mali_internal_get_fences(struct mali_internal_sync_fence *sync_fence, int *num_fences)
#else
static struct dma_fence **mali_internal_get_fences(struct mali_internal_sync_fence *sync_fence, int *num_fences)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	if (sync_fence->fence->ops == &fence_array_ops) {
		struct fence_array *fence_array = container_of(sync_fence->fence, struct fence_array, base);
		*num_fences = fence_array->num_fences;
		return fence_array->fences;
	}
#else
	if (sync_fence->fence->ops == &dma_fence_array_ops) {
		struct dma_fence_array *fence_array = container_of(sync_fence->fence, struct dma_fence_array, base);
		*num_fences = fence_array->num_fences;
		return fence_array->fences;
	}
#endif
	*num_fences = 1;
	return &sync_fence->fence;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
static void mali_internal_add_fence_array(struct fence **fences, int *num_fences, struct fence *fence)
#else
static void mali_internal_add_fence_array(struct dma_fence **fences, int *num_fences, struct dma_fence *fence)
#endif
{
	fences[*num_fences] = fence;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	if (!fence_is_signaled(fence)) {
		fence_get(fence);
		(*num_fences)++;
	}
#else
	if (!dma_fence_is_signaled(fence)) {
		dma_fence_get(fence);
		(*num_fences)++;
	}
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
static int mali_internal_sync_fence_set_fence_array(struct mali_internal_sync_fence *sync_fence,
		struct fence **fences, int num_fences)
#else
static int mali_internal_sync_fence_set_fence_array(struct mali_internal_sync_fence *sync_fence,
		struct dma_fence **fences, int num_fences)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	struct fence_array *array;
#else
	struct dma_fence_array *array;
#endif
	if(num_fences == 1) {
		sync_fence->fence =fences[0];
		kfree(fences);
	} else {	
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	array = fence_array_create(num_fences, fences,
				   fence_context_alloc(1), 1, false);
#else
	array = dma_fence_array_create(num_fences, fences,
				       dma_fence_context_alloc(1), 1, false);
#endif
	if (!array){
		return -ENOMEM;
    }
	    sync_fence->fence = &array->base;
	}
	return 0;
}

struct mali_internal_sync_fence *mali_internal_sync_fence_merge(
	struct mali_internal_sync_fence *sync_fence1, struct mali_internal_sync_fence *sync_fence2)
{
	struct mali_internal_sync_fence *sync_fence;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	struct fence **fences, **nfences, **fences1, **fences2;
#else
	struct dma_fence **fences, **nfences, **fences1, **fences2;
#endif
	int real_num_fences, i, j, num_fences, num_fences1, num_fences2;

	fences1 = mali_internal_get_fences(sync_fence1, &num_fences1);
	fences2 = mali_internal_get_fences(sync_fence2, &num_fences2);

	num_fences = num_fences1 + num_fences2;

	fences = kcalloc(num_fences, sizeof(*fences), GFP_KERNEL);
	if (!fences) {
		MALI_PRINT_ERROR(("Mali internal sync:Failed to  alloc buffer for fences.\n"));
		goto fences_alloc_failed;
	}

	for (real_num_fences = i = j = 0; i < num_fences1 && j < num_fences2;) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
		struct fence *fence1 = fences1[i];
		struct fence *fence2 = fences2[j];
#else
		struct dma_fence *fence1 = fences1[i];
		struct dma_fence *fence2 = fences2[j];
#endif
		if (fence1->context < fence2->context) {
			mali_internal_add_fence_array(fences, &real_num_fences, fence1);

			i++;
		} else if (fence1->context > fence2->context) {
			mali_internal_add_fence_array(fences, &real_num_fences, fence2);

			j++;
		} else {
			if (fence1->seqno - fence2->seqno <= INT_MAX)
				mali_internal_add_fence_array(fences, &real_num_fences, fence1);
			else
				mali_internal_add_fence_array(fences, &real_num_fences, fence2);

			i++;
			j++;
		}
	}

	for (; i < num_fences1; i++)
		mali_internal_add_fence_array(fences, &real_num_fences, fences1[i]);

	for (; j < num_fences2; j++)
		mali_internal_add_fence_array(fences, &real_num_fences, fences2[j]);

	if (0 == real_num_fences)
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
		fences[real_num_fences++] = fence_get(fences1[0]);
#else
		fences[real_num_fences++] = dma_fence_get(fences1[0]);
#endif

	if (num_fences > real_num_fences) {
		nfences = krealloc(fences, real_num_fences * sizeof(*fences),
				   GFP_KERNEL);
		if (!nfences)
			goto nfences_alloc_failed;

		fences = nfences;
	}

	sync_fence = (struct mali_internal_sync_fence *)sync_file_create(fences[0]);
	if (NULL == sync_fence) {
		MALI_PRINT_ERROR(("Mali internal sync:Failed to  create the mali internal sync fence when merging sync fence.\n"));
		goto sync_fence_alloc_failed;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	fence_put(fences[0]);
#else
	dma_fence_put(fences[0]);
#endif

	if (mali_internal_sync_fence_set_fence_array(sync_fence, fences, real_num_fences) < 0) {
		MALI_PRINT_ERROR(("Mali internal sync:Failed to  set fence for sync fence.\n"));
		goto sync_fence_set_failed;
	}

	return sync_fence;

sync_fence_set_failed:
	fput(sync_fence->file);
sync_fence_alloc_failed:
	for (i = 0; i < real_num_fences; i++)
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
		fence_put(fences[i]);
#else
		dma_fence_put(fences[i]);
#endif
nfences_alloc_failed:
	kfree(fences);
fences_alloc_failed:
	return NULL;
}
#endif

void mali_internal_sync_fence_waiter_init(struct mali_internal_sync_fence_waiter *waiter,
		mali_internal_sync_callback_t callback)
{
	MALI_DEBUG_ASSERT_POINTER(waiter);
	MALI_DEBUG_ASSERT_POINTER(callback);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
	INIT_LIST_HEAD(&waiter->work.entry);
#else
	INIT_LIST_HEAD(&waiter->work.task_list);
#endif
	waiter->callback = callback;
}

int mali_internal_sync_fence_wait_async(struct mali_internal_sync_fence *sync_fence,
					struct mali_internal_sync_fence_waiter *waiter)
{
	int err;
	unsigned long flags;

	MALI_DEBUG_ASSERT_POINTER(sync_fence);
	MALI_DEBUG_ASSERT_POINTER(waiter);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
	err = atomic_read(&sync_fence->status);

	if (0 > err)
		return err;

	if (!err)
		return 1;

	init_waitqueue_func_entry(&waiter->work, mali_internal_sync_fence_wake_up_wq);
	waiter->work.private = sync_fence;

	spin_lock_irqsave(&sync_fence->wq.lock, flags);
	err = atomic_read(&sync_fence->status);

	if (0 < err)
		__add_wait_queue_tail(&sync_fence->wq, &waiter->work);
	spin_unlock_irqrestore(&sync_fence->wq.lock, flags);

	if (0 > err)
		return err;

	return !err;
#else
	if ((sync_fence->fence) && (sync_fence->fence->ops) && (sync_fence->fence->ops->signaled))
		err = sync_fence->fence->ops->signaled(sync_fence->fence);
	else
		err = -1;

	if (0 > err)
		return err;

	if (1 == err)
		return err;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	err = dma_fence_add_callback(sync_fence->fence, &waiter->cb, mali_internal_fence_check_cb_func);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
	err = fence_add_callback(sync_fence->fence, &waiter->cb, mali_internal_fence_check_cb_func);
#endif

	if (0 != err) {
		if (-ENOENT == err) 
			err = 1;
		return err;
	}
	init_waitqueue_func_entry(&waiter->work, mali_internal_sync_fence_wake_up_wq);
	waiter->work.private = sync_fence;

	spin_lock_irqsave(&sync_fence->wq.lock, flags);
	err =  sync_fence->fence->ops->signaled(sync_fence->fence);

	if (0 == err){
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
		__add_wait_queue_entry_tail(&sync_fence->wq, &waiter->work);
#else
		__add_wait_queue_tail(&sync_fence->wq, &waiter->work);
#endif
        }
	spin_unlock_irqrestore(&sync_fence->wq.lock, flags);

	return err;
#endif
}

int mali_internal_sync_fence_cancel_async(struct mali_internal_sync_fence *sync_fence,
		struct mali_internal_sync_fence_waiter *waiter)
{
	unsigned long flags;
	int ret = 0;

	MALI_DEBUG_ASSERT_POINTER(sync_fence);
	MALI_DEBUG_ASSERT_POINTER(waiter);

	spin_lock_irqsave(&sync_fence->wq.lock, flags);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
	if (!list_empty(&waiter->work.entry))
		list_del_init(&waiter->work.entry);
#else
	if (!list_empty(&waiter->work.task_list))
		list_del_init(&waiter->work.task_list);
#endif
	else
		ret = -ENOENT;
	spin_unlock_irqrestore(&sync_fence->wq.lock, flags);

	if (0 == ret) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
		dma_fence_remove_callback(sync_fence->fence, &waiter->cb);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
		fence_remove_callback(sync_fence->fence, &waiter->cb);
#endif

	}

	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
static const char *mali_internal_fence_get_driver_name(struct dma_fence *fence)
#else
static const char *mali_internal_fence_get_driver_name(struct fence *fence)
#endif
{
	struct mali_internal_sync_point *sync_pt;
	struct mali_internal_sync_timeline *parent;

	MALI_DEBUG_ASSERT_POINTER(fence);

	sync_pt = mali_internal_fence_to_sync_pt(fence);
	parent = mali_internal_sync_pt_to_sync_timeline(sync_pt);

	return parent->ops->driver_name;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
static const char *mali_internal_fence_get_timeline_name(struct dma_fence *fence)
#else
static const char *mali_internal_fence_get_timeline_name(struct fence *fence)
#endif
{
	struct mali_internal_sync_point *sync_pt;
	struct mali_internal_sync_timeline *parent;

	MALI_DEBUG_ASSERT_POINTER(fence);

	sync_pt = mali_internal_fence_to_sync_pt(fence);
	parent = mali_internal_sync_pt_to_sync_timeline(sync_pt);

	return parent->name;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
static void mali_internal_fence_release(struct dma_fence *fence)
#else
static void mali_internal_fence_release(struct fence *fence)
#endif
{
	unsigned long flags;
	struct mali_internal_sync_point *sync_pt;
	struct mali_internal_sync_timeline *parent;

	MALI_DEBUG_ASSERT_POINTER(fence);

	sync_pt = mali_internal_fence_to_sync_pt(fence);
	parent = mali_internal_sync_pt_to_sync_timeline(sync_pt);


	spin_lock_irqsave(fence->lock, flags);
	if (WARN_ON_ONCE(!list_empty(&sync_pt->sync_pt_list)))
		list_del(&sync_pt->sync_pt_list);
	spin_unlock_irqrestore(fence->lock, flags);

	if (parent->ops->free_pt)
		parent->ops->free_pt(sync_pt);

	kref_put(&parent->kref_count, mali_internal_sync_timeline_free);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	dma_fence_free(&sync_pt->base);
#else
	fence_free(&sync_pt->base);
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
static bool mali_internal_fence_signaled(struct dma_fence *fence)
#else
static bool mali_internal_fence_signaled(struct fence *fence)
#endif
{
	int ret;
	struct mali_internal_sync_point *sync_pt;
	struct mali_internal_sync_timeline *parent;

	MALI_DEBUG_ASSERT_POINTER(fence);

	sync_pt = mali_internal_fence_to_sync_pt(fence);
	parent = mali_internal_sync_pt_to_sync_timeline(sync_pt);

	ret = parent->ops->has_signaled(sync_pt);
	if (0 > ret)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0) \
                || (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0) && LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 68)))
		fence->error = ret;
#else
		fence->status = ret;
#endif
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
static bool mali_internal_fence_enable_signaling(struct dma_fence *fence)
#else
static bool mali_internal_fence_enable_signaling(struct fence *fence)
#endif
{
	struct mali_internal_sync_point *sync_pt;
	struct mali_internal_sync_timeline *parent;

	MALI_DEBUG_ASSERT_POINTER(fence);

	sync_pt = mali_internal_fence_to_sync_pt(fence);
	parent = mali_internal_sync_pt_to_sync_timeline(sync_pt);

	if (mali_internal_fence_signaled(fence))
		return false;

	list_add_tail(&sync_pt->sync_pt_list, &parent->sync_pt_list_head);
	return true;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
static void mali_internal_fence_value_str(struct dma_fence *fence, char *str, int size)
#else
static void mali_internal_fence_value_str(struct fence *fence, char *str, int size)
#endif
{
	struct mali_internal_sync_point *sync_pt;
	struct mali_internal_sync_timeline *parent;

	MALI_DEBUG_ASSERT_POINTER(fence);
	MALI_IGNORE(str);
	MALI_IGNORE(size);

	sync_pt = mali_internal_fence_to_sync_pt(fence);
	parent = mali_internal_sync_pt_to_sync_timeline(sync_pt);

	parent->ops->print_sync_pt(sync_pt);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
static const struct dma_fence_ops fence_ops = {
#else
static const struct fence_ops fence_ops = {
#endif
	.get_driver_name = mali_internal_fence_get_driver_name,
	.get_timeline_name = mali_internal_fence_get_timeline_name,
	.enable_signaling = mali_internal_fence_enable_signaling,
	.signaled = mali_internal_fence_signaled,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	.wait = dma_fence_default_wait,
#else
	.wait = fence_default_wait,
#endif
	.release = mali_internal_fence_release,
	.fence_value_str = mali_internal_fence_value_str,
};
#endif

/**************************************************************************
 *
 * Copyright (c) 2006-2008 Tungsten Graphics, Inc., Cedar Park, TX., USA
 * All Rights Reserved.
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 */

#include "psb_ttm_fence_api.h"
#include "psb_ttm_fence_driver.h"
#include <linux/wait.h>
#include <linux/sched.h>

#include <drm/drmP.h>

/*
 * Simple implementation for now.
 */

static void ttm_fence_lockup(struct ttm_fence_object *fence, uint32_t mask)
{
	struct ttm_fence_class_manager *fc = ttm_fence_fc(fence);

	printk(KERN_ERR "GPU lockup dectected on engine %u "
	       "fence type 0x%08x\n",
	       (unsigned int)fence->fence_class, (unsigned int)mask);
	/*
	 * Give engines some time to idle?
	 */

	write_lock(&fc->lock);
	ttm_fence_handler(fence->fdev, fence->fence_class,
			  fence->sequence, mask, -EBUSY);
	write_unlock(&fc->lock);
}

/*
 * Convenience function to be called by fence::wait methods that
 * need polling.
 */

int ttm_fence_wait_polling(struct ttm_fence_object *fence, bool lazy,
			   bool interruptible, uint32_t mask)
{
	struct ttm_fence_class_manager *fc = ttm_fence_fc(fence);
	const struct ttm_fence_driver *driver = ttm_fence_driver(fence);
	uint32_t count = 0;
	int ret;
	unsigned long end_jiffies = fence->timeout_jiffies;

	DECLARE_WAITQUEUE(entry, current);
	add_wait_queue(&fc->fence_queue, &entry);

	ret = 0;

	for (;;) {
		__set_current_state((interruptible) ?
				    TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE);
		if (ttm_fence_object_signaled(fence, mask))
			break;
		if (time_after_eq(jiffies, end_jiffies)) {
			if (driver->lockup)
				driver->lockup(fence, mask);
			else
				ttm_fence_lockup(fence, mask);
			continue;
		}
		if (lazy)
			schedule_timeout(1);
		else if ((++count & 0x0F) == 0) {
			__set_current_state(TASK_RUNNING);
			schedule();
			__set_current_state((interruptible) ?
					    TASK_INTERRUPTIBLE :
					    TASK_UNINTERRUPTIBLE);
		}
		if (interruptible && signal_pending(current)) {
			ret = -ERESTART;
			break;
		}
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&fc->fence_queue, &entry);
	return ret;
}

/*
 * Typically called by the IRQ handler.
 */

void ttm_fence_handler(struct ttm_fence_device *fdev, uint32_t fence_class,
		       uint32_t sequence, uint32_t type, uint32_t error)
{
	int wake = 0;
	uint32_t diff;
	uint32_t relevant_type;
	uint32_t new_type;
	struct ttm_fence_class_manager *fc = &fdev->fence_class[fence_class];
	const struct ttm_fence_driver *driver = ttm_fence_driver_from_dev(fdev);
	struct list_head *head;
	struct ttm_fence_object *fence, *next;
	bool found = false;

	if (list_empty(&fc->ring))
		return;

	list_for_each_entry(fence, &fc->ring, ring) {
		diff = (sequence - fence->sequence) & fc->sequence_mask;
		if (diff > fc->wrap_diff) {
			found = true;
			break;
		}
	}

	fc->waiting_types &= ~type;
	head = (found) ? &fence->ring : &fc->ring;

	list_for_each_entry_safe_reverse(fence, next, head, ring) {
		if (&fence->ring == &fc->ring)
			break;

		DRM_DEBUG("Fence 0x%08lx, sequence 0x%08x, type 0x%08x\n",
			  (unsigned long)fence, fence->sequence,
			  fence->fence_type);

		if (error) {
			fence->info.error = error;
			fence->info.signaled_types = fence->fence_type;
			list_del_init(&fence->ring);
			wake = 1;
			break;
		}

		relevant_type = type & fence->fence_type;
		new_type = (fence->info.signaled_types | relevant_type) ^
		    fence->info.signaled_types;

		if (new_type) {
			fence->info.signaled_types |= new_type;
			DRM_DEBUG("Fence 0x%08lx signaled 0x%08x\n",
				  (unsigned long)fence,
				  fence->info.signaled_types);

			if (unlikely(driver->signaled))
				driver->signaled(fence);

			if (driver->needed_flush)
				fc->pending_flush |=
				    driver->needed_flush(fence);

			if (new_type & fence->waiting_types)
				wake = 1;
		}

		fc->waiting_types |=
		    fence->waiting_types & ~fence->info.signaled_types;

		if (!(fence->fence_type & ~fence->info.signaled_types)) {
			DRM_DEBUG("Fence completely signaled 0x%08lx\n",
				  (unsigned long)fence);
			list_del_init(&fence->ring);
		}
	}

	/*
	 * Reinstate lost waiting types.
	 */

	if ((fc->waiting_types & type) != type) {
		head = head->prev;
		list_for_each_entry(fence, head, ring) {
			if (&fence->ring == &fc->ring)
				break;
			diff =
			    (fc->highest_waiting_sequence -
			     fence->sequence) & fc->sequence_mask;
			if (diff > fc->wrap_diff)
				break;

			fc->waiting_types |=
			    fence->waiting_types & ~fence->info.signaled_types;
		}
	}

	if (wake)
		wake_up_all(&fc->fence_queue);
}

static void ttm_fence_unring(struct ttm_fence_object *fence)
{
	struct ttm_fence_class_manager *fc = ttm_fence_fc(fence);
	unsigned long irq_flags;

	write_lock_irqsave(&fc->lock, irq_flags);
	list_del_init(&fence->ring);
	write_unlock_irqrestore(&fc->lock, irq_flags);
}

bool ttm_fence_object_signaled(struct ttm_fence_object *fence, uint32_t mask)
{
	unsigned long flags;
	bool signaled;
	const struct ttm_fence_driver *driver = ttm_fence_driver(fence);
	struct ttm_fence_class_manager *fc = ttm_fence_fc(fence);

	mask &= fence->fence_type;
	read_lock_irqsave(&fc->lock, flags);
	signaled = (mask & fence->info.signaled_types) == mask;
	read_unlock_irqrestore(&fc->lock, flags);
	if (!signaled && driver->poll) {
		write_lock_irqsave(&fc->lock, flags);
		driver->poll(fence->fdev, fence->fence_class, mask);
		signaled = (mask & fence->info.signaled_types) == mask;
		write_unlock_irqrestore(&fc->lock, flags);
	}
	return signaled;
}

int ttm_fence_object_flush(struct ttm_fence_object *fence, uint32_t type)
{
	const struct ttm_fence_driver *driver = ttm_fence_driver(fence);
	struct ttm_fence_class_manager *fc = ttm_fence_fc(fence);
	unsigned long irq_flags;
	uint32_t saved_pending_flush;
	uint32_t diff;
	bool call_flush;

	if (type & ~fence->fence_type) {
		DRM_ERROR("Flush trying to extend fence type, "
			  "0x%x, 0x%x\n", type, fence->fence_type);
		return -EINVAL;
	}

	write_lock_irqsave(&fc->lock, irq_flags);
	fence->waiting_types |= type;
	fc->waiting_types |= fence->waiting_types;
	diff = (fence->sequence - fc->highest_waiting_sequence) &
	    fc->sequence_mask;

	if (diff < fc->wrap_diff)
		fc->highest_waiting_sequence = fence->sequence;

	/*
	 * fence->waiting_types has changed. Determine whether
	 * we need to initiate some kind of flush as a result of this.
	 */

	saved_pending_flush = fc->pending_flush;
	if (driver->needed_flush)
		fc->pending_flush |= driver->needed_flush(fence);

	if (driver->poll)
		driver->poll(fence->fdev, fence->fence_class,
			     fence->waiting_types);

	call_flush = (fc->pending_flush != 0);
	write_unlock_irqrestore(&fc->lock, irq_flags);

	if (call_flush && driver->flush)
		driver->flush(fence->fdev, fence->fence_class);

	return 0;
}

/*
 * Make sure old fence objects are signaled before their fence sequences are
 * wrapped around and reused.
 */

void ttm_fence_flush_old(struct ttm_fence_device *fdev,
			 uint32_t fence_class, uint32_t sequence)
{
	struct ttm_fence_class_manager *fc = &fdev->fence_class[fence_class];
	struct ttm_fence_object *fence;
	unsigned long irq_flags;
	const struct ttm_fence_driver *driver = fdev->driver;
	bool call_flush;

	uint32_t diff;

	write_lock_irqsave(&fc->lock, irq_flags);

	list_for_each_entry_reverse(fence, &fc->ring, ring) {
		diff = (sequence - fence->sequence) & fc->sequence_mask;
		if (diff <= fc->flush_diff)
			break;

		fence->waiting_types = fence->fence_type;
		fc->waiting_types |= fence->fence_type;

		if (driver->needed_flush)
			fc->pending_flush |= driver->needed_flush(fence);
	}

	if (driver->poll)
		driver->poll(fdev, fence_class, fc->waiting_types);

	call_flush = (fc->pending_flush != 0);
	write_unlock_irqrestore(&fc->lock, irq_flags);

	if (call_flush && driver->flush)
		driver->flush(fdev, fence->fence_class);

	/*
	 * FIXME: Shold we implement a wait here for really old fences?
	 */

}

int ttm_fence_object_wait(struct ttm_fence_object *fence,
			  bool lazy, bool interruptible, uint32_t mask)
{
	const struct ttm_fence_driver *driver = ttm_fence_driver(fence);
	struct ttm_fence_class_manager *fc = ttm_fence_fc(fence);
	int ret = 0;
	unsigned long timeout;
	unsigned long cur_jiffies;
	unsigned long to_jiffies;

	if (mask & ~fence->fence_type) {
		DRM_ERROR("Wait trying to extend fence type"
			  " 0x%08x 0x%08x\n", mask, fence->fence_type);
		BUG();
		return -EINVAL;
	}

	if (driver->wait)
		return driver->wait(fence, lazy, interruptible, mask);

	ttm_fence_object_flush(fence, mask);
retry:
	if (!driver->has_irq ||
	    driver->has_irq(fence->fdev, fence->fence_class, mask)) {

		cur_jiffies = jiffies;
		to_jiffies = fence->timeout_jiffies;

		timeout = (time_after(to_jiffies, cur_jiffies)) ?
		    to_jiffies - cur_jiffies : 1;

		if (interruptible)
			ret = wait_event_interruptible_timeout
			    (fc->fence_queue,
			     ttm_fence_object_signaled(fence, mask), timeout);
		else
			ret = wait_event_timeout
			    (fc->fence_queue,
			     ttm_fence_object_signaled(fence, mask), timeout);

		if (unlikely(ret == -ERESTARTSYS))
			return -ERESTART;

		if (unlikely(ret == 0)) {
			if (driver->lockup)
				driver->lockup(fence, mask);
			else
				ttm_fence_lockup(fence, mask);
			goto retry;
		}

		return 0;
	}

	return ttm_fence_wait_polling(fence, lazy, interruptible, mask);
}

int ttm_fence_object_emit(struct ttm_fence_object *fence, uint32_t fence_flags,
			  uint32_t fence_class, uint32_t type)
{
	const struct ttm_fence_driver *driver = ttm_fence_driver(fence);
	struct ttm_fence_class_manager *fc = ttm_fence_fc(fence);
	unsigned long flags;
	uint32_t sequence;
	unsigned long timeout;
	int ret;

	ttm_fence_unring(fence);
	ret = driver->emit(fence->fdev,
			   fence_class, fence_flags, &sequence, &timeout);
	if (ret)
		return ret;

	write_lock_irqsave(&fc->lock, flags);
	fence->fence_class = fence_class;
	fence->fence_type = type;
	fence->waiting_types = 0;
	fence->info.signaled_types = 0;
	fence->info.error = 0;
	fence->sequence = sequence;
	fence->timeout_jiffies = timeout;
	if (list_empty(&fc->ring))
		fc->highest_waiting_sequence = sequence - 1;
	list_add_tail(&fence->ring, &fc->ring);
	fc->latest_queued_sequence = sequence;
	write_unlock_irqrestore(&fc->lock, flags);
	return 0;
}

int ttm_fence_object_init(struct ttm_fence_device *fdev,
			  uint32_t fence_class,
			  uint32_t type,
			  uint32_t create_flags,
			  void (*destroy) (struct ttm_fence_object *),
			  struct ttm_fence_object *fence)
{
	int ret = 0;

	kref_init(&fence->kref);
	fence->fence_class = fence_class;
	fence->fence_type = type;
	fence->info.signaled_types = 0;
	fence->waiting_types = 0;
	fence->sequence = 0;
	fence->info.error = 0;
	fence->fdev = fdev;
	fence->destroy = destroy;
	INIT_LIST_HEAD(&fence->ring);
	atomic_inc(&fdev->count);

	if (create_flags & TTM_FENCE_FLAG_EMIT) {
		ret = ttm_fence_object_emit(fence, create_flags,
					    fence->fence_class, type);
	}

	return ret;
}

int ttm_fence_object_create(struct ttm_fence_device *fdev,
			    uint32_t fence_class,
			    uint32_t type,
			    uint32_t create_flags,
			    struct ttm_fence_object **c_fence)
{
	struct ttm_fence_object *fence;
	int ret;

	ret = ttm_mem_global_alloc(fdev->mem_glob,
				   sizeof(*fence),
				   false,
				   false);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Out of memory creating fence object\n");
		return ret;
	}

	fence = kmalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence) {
		printk(KERN_ERR "Out of memory creating fence object\n");
		ttm_mem_global_free(fdev->mem_glob, sizeof(*fence));
		return -ENOMEM;
	}

	ret = ttm_fence_object_init(fdev, fence_class, type,
				    create_flags, NULL, fence);
	if (ret) {
		ttm_fence_object_unref(&fence);
		return ret;
	}
	*c_fence = fence;

	return 0;
}

static void ttm_fence_object_destroy(struct kref *kref)
{
	struct ttm_fence_object *fence =
	    container_of(kref, struct ttm_fence_object, kref);
	struct ttm_fence_class_manager *fc = ttm_fence_fc(fence);
	unsigned long irq_flags;

	write_lock_irqsave(&fc->lock, irq_flags);
	list_del_init(&fence->ring);
	write_unlock_irqrestore(&fc->lock, irq_flags);

	atomic_dec(&fence->fdev->count);
	if (fence->destroy)
		fence->destroy(fence);
	else {
		ttm_mem_global_free(fence->fdev->mem_glob,
				    sizeof(*fence));
		kfree(fence);
	}
}

void ttm_fence_device_release(struct ttm_fence_device *fdev)
{
	kfree(fdev->fence_class);
}

int
ttm_fence_device_init(int num_classes,
		      struct ttm_mem_global *mem_glob,
		      struct ttm_fence_device *fdev,
		      const struct ttm_fence_class_init *init,
		      bool replicate_init,
		      const struct ttm_fence_driver *driver)
{
	struct ttm_fence_class_manager *fc;
	const struct ttm_fence_class_init *fci;
	int i;

	fdev->mem_glob = mem_glob;
	fdev->fence_class = kzalloc(num_classes *
				    sizeof(*fdev->fence_class), GFP_KERNEL);

	if (unlikely(!fdev->fence_class))
		return -ENOMEM;

	fdev->num_classes = num_classes;
	atomic_set(&fdev->count, 0);
	fdev->driver = driver;

	for (i = 0; i < fdev->num_classes; ++i) {
		fc = &fdev->fence_class[i];
		fci = &init[(replicate_init) ? 0 : i];

		fc->wrap_diff = fci->wrap_diff;
		fc->flush_diff = fci->flush_diff;
		fc->sequence_mask = fci->sequence_mask;

		rwlock_init(&fc->lock);
		INIT_LIST_HEAD(&fc->ring);
		init_waitqueue_head(&fc->fence_queue);
	}

	return 0;
}

struct ttm_fence_info ttm_fence_get_info(struct ttm_fence_object *fence)
{
	struct ttm_fence_class_manager *fc = ttm_fence_fc(fence);
	struct ttm_fence_info tmp;
	unsigned long irq_flags;

	read_lock_irqsave(&fc->lock, irq_flags);
	tmp = fence->info;
	read_unlock_irqrestore(&fc->lock, irq_flags);

	return tmp;
}

void ttm_fence_object_unref(struct ttm_fence_object **p_fence)
{
	struct ttm_fence_object *fence = *p_fence;

	*p_fence = NULL;
	(void)kref_put(&fence->kref, &ttm_fence_object_destroy);
}

/*
 * Placement / BO sync object glue.
 */

bool ttm_fence_sync_obj_signaled(void *sync_obj, void *sync_arg)
{
	struct ttm_fence_object *fence = (struct ttm_fence_object *)sync_obj;
	uint32_t fence_types = (uint32_t) (unsigned long)sync_arg;

	return ttm_fence_object_signaled(fence, fence_types);
}

int ttm_fence_sync_obj_wait(void *sync_obj, void *sync_arg,
			    bool lazy, bool interruptible)
{
	struct ttm_fence_object *fence = (struct ttm_fence_object *)sync_obj;
	uint32_t fence_types = (uint32_t) (unsigned long)sync_arg;

	return ttm_fence_object_wait(fence, lazy, interruptible, fence_types);
}

int ttm_fence_sync_obj_flush(void *sync_obj, void *sync_arg)
{
	struct ttm_fence_object *fence = (struct ttm_fence_object *)sync_obj;
	uint32_t fence_types = (uint32_t) (unsigned long)sync_arg;

	return ttm_fence_object_flush(fence, fence_types);
}

void ttm_fence_sync_obj_unref(void **sync_obj)
{
	ttm_fence_object_unref((struct ttm_fence_object **)sync_obj);
}

void *ttm_fence_sync_obj_ref(void *sync_obj)
{
	return (void *)
	    ttm_fence_object_ref((struct ttm_fence_object *)sync_obj);
}

/*
 * drivers/base/sync.c
 *
 * Copyright (C) 2012 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/debugfs.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/anon_inodes.h>

#include "sync.h"

#define CREATE_TRACE_POINTS
#include "trace/sync.h"

static const struct fence_ops timeline_fence_ops;

struct sync_timeline *sync_timeline_create(const char *drv_name,
					   const char *name)
{
	struct sync_timeline *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return NULL;

	kref_init(&obj->kref);
	obj->context = fence_context_alloc(1);
	strlcpy(obj->name, name, sizeof(obj->name));
	strlcpy(obj->drv_name, drv_name, sizeof(obj->drv_name));

	INIT_LIST_HEAD(&obj->child_list_head);
	INIT_LIST_HEAD(&obj->active_list_head);
	spin_lock_init(&obj->child_list_lock);

	sync_timeline_debug_add(obj);

	return obj;
}
EXPORT_SYMBOL(sync_timeline_create);

static void sync_timeline_free(struct kref *kref)
{
	struct sync_timeline *obj =
		container_of(kref, struct sync_timeline, kref);

	sync_timeline_debug_remove(obj);

	kfree(obj);
}

static void sync_timeline_get(struct sync_timeline *obj)
{
	kref_get(&obj->kref);
}

static void sync_timeline_put(struct sync_timeline *obj)
{
	kref_put(&obj->kref, sync_timeline_free);
}

void sync_timeline_destroy(struct sync_timeline *obj)
{
	obj->destroyed = true;
	/*
	 * Ensure timeline is marked as destroyed before
	 * changing timeline's fences status.
	 */
	smp_wmb();

	sync_timeline_put(obj);
}
EXPORT_SYMBOL(sync_timeline_destroy);

void sync_timeline_signal(struct sync_timeline *obj, unsigned int inc)
{
	unsigned long flags;
	struct fence *fence, *next;

	trace_sync_timeline(obj);

	spin_lock_irqsave(&obj->child_list_lock, flags);

	obj->value += inc;

	list_for_each_entry_safe(fence, next, &obj->active_list_head,
				 active_list) {
		if (fence_is_signaled_locked(fence))
			list_del_init(&fence->active_list);
	}

	spin_unlock_irqrestore(&obj->child_list_lock, flags);
}
EXPORT_SYMBOL(sync_timeline_signal);

struct fence *sync_pt_create(struct sync_timeline *obj, int size,
			     unsigned int value)
{
	unsigned long flags;
	struct fence *fence;

	if (size < sizeof(*fence))
		return NULL;

	fence = kzalloc(size, GFP_KERNEL);
	if (!fence)
		return NULL;

	spin_lock_irqsave(&obj->child_list_lock, flags);
	sync_timeline_get(obj);
	fence_init(fence, &timeline_fence_ops, &obj->child_list_lock,
		   obj->context, value);
	list_add_tail(&fence->child_list, &obj->child_list_head);
	INIT_LIST_HEAD(&fence->active_list);
	spin_unlock_irqrestore(&obj->child_list_lock, flags);
	return fence;
}
EXPORT_SYMBOL(sync_pt_create);

static const char *timeline_fence_get_driver_name(struct fence *fence)
{
	struct sync_timeline *parent = fence_parent(fence);

	return parent->drv_name;
}

static const char *timeline_fence_get_timeline_name(struct fence *fence)
{
	struct sync_timeline *parent = fence_parent(fence);

	return parent->name;
}

static void timeline_fence_release(struct fence *fence)
{
	struct sync_timeline *parent = fence_parent(fence);
	unsigned long flags;

	spin_lock_irqsave(fence->lock, flags);
	list_del(&fence->child_list);
	if (WARN_ON_ONCE(!list_empty(&fence->active_list)))
		list_del(&fence->active_list);
	spin_unlock_irqrestore(fence->lock, flags);

	sync_timeline_put(parent);
	fence_free(fence);
}

static bool timeline_fence_signaled(struct fence *fence)
{
	struct sync_timeline *parent = fence_parent(fence);

	return (fence->seqno > parent->value) ? false : true;
}

static bool timeline_fence_enable_signaling(struct fence *fence)
{
	struct sync_timeline *parent = fence_parent(fence);

	if (timeline_fence_signaled(fence))
		return false;

	list_add_tail(&fence->active_list, &parent->active_list_head);
	return true;
}

static void timeline_fence_value_str(struct fence *fence,
				    char *str, int size)
{
	snprintf(str, size, "%d", fence->seqno);
}

static void timeline_fence_timeline_value_str(struct fence *fence,
					     char *str, int size)
{
	struct sync_timeline *parent = fence_parent(fence);

	snprintf(str, size, "%d", parent->value);
}

static const struct fence_ops timeline_fence_ops = {
	.get_driver_name = timeline_fence_get_driver_name,
	.get_timeline_name = timeline_fence_get_timeline_name,
	.enable_signaling = timeline_fence_enable_signaling,
	.signaled = timeline_fence_signaled,
	.wait = fence_default_wait,
	.release = timeline_fence_release,
	.fence_value_str = timeline_fence_value_str,
	.timeline_value_str = timeline_fence_timeline_value_str,
};

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/syscalls.h>
#include <linux/sync_file.h>
#include <linux/fence.h>

#include "goldfish_sync_timeline_fence.h"

/*
 * Timeline-based sync for Goldfish Sync
 * Based on "Sync File validation framework"
 * (drivers/dma-buf/sw_sync.c)
 *
 * Copyright (C) 2017 Google, Inc.
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

/**
 * struct goldfish_sync_timeline - sync object
 * @kref:		reference count on fence.
 * @name:		name of the goldfish_sync_timeline. Useful for debugging
 * @child_list_head:	list of children sync_pts for this goldfish_sync_timeline
 * @child_list_lock:	lock protecting @child_list_head and fence.status
 * @active_list_head:	list of active (unsignaled/errored) sync_pts
 */
struct goldfish_sync_timeline {
	struct kref		kref;
	char			name[32];

	/* protected by child_list_lock */
	u64			context;
	int			value;

	struct list_head	child_list_head;
	spinlock_t		child_list_lock;

	struct list_head	active_list_head;
};

static inline struct goldfish_sync_timeline *fence_parent(struct fence *fence)
{
	return container_of(fence->lock, struct goldfish_sync_timeline,
				child_list_lock);
}

static const struct fence_ops goldfish_sync_timeline_fence_ops;

static inline struct sync_pt *goldfish_sync_fence_to_sync_pt(struct fence *fence)
{
	if (fence->ops != &goldfish_sync_timeline_fence_ops)
		return NULL;
	return container_of(fence, struct sync_pt, base);
}

/**
 * goldfish_sync_timeline_create_internal() - creates a sync object
 * @name:	sync_timeline name
 *
 * Creates a new sync_timeline. Returns the sync_timeline object or NULL in
 * case of error.
 */
struct goldfish_sync_timeline
*goldfish_sync_timeline_create_internal(const char *name)
{
	struct goldfish_sync_timeline *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return NULL;

	kref_init(&obj->kref);
	obj->context = fence_context_alloc(1);
	strlcpy(obj->name, name, sizeof(obj->name));

	INIT_LIST_HEAD(&obj->child_list_head);
	INIT_LIST_HEAD(&obj->active_list_head);
	spin_lock_init(&obj->child_list_lock);

	return obj;
}

static void goldfish_sync_timeline_free_internal(struct kref *kref)
{
	struct goldfish_sync_timeline *obj =
		container_of(kref, struct goldfish_sync_timeline, kref);

	kfree(obj);
}

static void goldfish_sync_timeline_get_internal(
					struct goldfish_sync_timeline *obj)
{
	kref_get(&obj->kref);
}

void goldfish_sync_timeline_put_internal(struct goldfish_sync_timeline *obj)
{
	kref_put(&obj->kref, goldfish_sync_timeline_free_internal);
}

/**
 * goldfish_sync_timeline_signal() -
 * signal a status change on a goldfish_sync_timeline
 * @obj:	sync_timeline to signal
 * @inc:	num to increment on timeline->value
 *
 * A sync implementation should call this any time one of it's fences
 * has signaled or has an error condition.
 */
void goldfish_sync_timeline_signal_internal(struct goldfish_sync_timeline *obj,
											unsigned int inc)
{
	unsigned long flags;
	struct sync_pt *pt, *next;

	spin_lock_irqsave(&obj->child_list_lock, flags);

	obj->value += inc;

	list_for_each_entry_safe(pt, next, &obj->active_list_head,
				 active_list) {
		if (fence_is_signaled_locked(&pt->base))
			list_del_init(&pt->active_list);
	}

	spin_unlock_irqrestore(&obj->child_list_lock, flags);
}

/**
 * goldfish_sync_pt_create_internal() - creates a sync pt
 * @parent:	fence's parent sync_timeline
 * @size:	size to allocate for this pt
 * @inc:	value of the fence
 *
 * Creates a new sync_pt as a child of @parent.  @size bytes will be
 * allocated allowing for implementation specific data to be kept after
 * the generic sync_timeline struct. Returns the sync_pt object or
 * NULL in case of error.
 */
struct sync_pt *goldfish_sync_pt_create_internal(
					struct goldfish_sync_timeline *obj, int size,
				 	unsigned int value)
{
	unsigned long flags;
	struct sync_pt *pt;

	if (size < sizeof(*pt))
		return NULL;

	pt = kzalloc(size, GFP_KERNEL);
	if (!pt)
		return NULL;

	spin_lock_irqsave(&obj->child_list_lock, flags);
	goldfish_sync_timeline_get_internal(obj);
	fence_init(&pt->base, &goldfish_sync_timeline_fence_ops, &obj->child_list_lock,
		   obj->context, value);
	list_add_tail(&pt->child_list, &obj->child_list_head);
	INIT_LIST_HEAD(&pt->active_list);
	spin_unlock_irqrestore(&obj->child_list_lock, flags);
	return pt;
}

static const char *goldfish_sync_timeline_fence_get_driver_name(
						struct fence *fence)
{
	return "sw_sync";
}

static const char *goldfish_sync_timeline_fence_get_timeline_name(
						struct fence *fence)
{
	struct goldfish_sync_timeline *parent = fence_parent(fence);

	return parent->name;
}

static void goldfish_sync_timeline_fence_release(struct fence *fence)
{
	struct sync_pt *pt = goldfish_sync_fence_to_sync_pt(fence);
	struct goldfish_sync_timeline *parent = fence_parent(fence);
	unsigned long flags;

	spin_lock_irqsave(fence->lock, flags);
	list_del(&pt->child_list);
	if (!list_empty(&pt->active_list))
		list_del(&pt->active_list);
	spin_unlock_irqrestore(fence->lock, flags);

	goldfish_sync_timeline_put_internal(parent);
	fence_free(fence);
}

static bool goldfish_sync_timeline_fence_signaled(struct fence *fence)
{
	struct goldfish_sync_timeline *parent = fence_parent(fence);

	return (fence->seqno > parent->value) ? false : true;
}

static bool goldfish_sync_timeline_fence_enable_signaling(struct fence *fence)
{
	struct sync_pt *pt = goldfish_sync_fence_to_sync_pt(fence);
	struct goldfish_sync_timeline *parent = fence_parent(fence);

	if (goldfish_sync_timeline_fence_signaled(fence))
		return false;

	list_add_tail(&pt->active_list, &parent->active_list_head);
	return true;
}

static void goldfish_sync_timeline_fence_disable_signaling(struct fence *fence)
{
	struct sync_pt *pt = container_of(fence, struct sync_pt, base);

	list_del_init(&pt->active_list);
}

static void goldfish_sync_timeline_fence_value_str(struct fence *fence,
					char *str, int size)
{
	snprintf(str, size, "%d", fence->seqno);
}

static void goldfish_sync_timeline_fence_timeline_value_str(
				struct fence *fence,
				char *str, int size)
{
	struct goldfish_sync_timeline *parent = fence_parent(fence);

	snprintf(str, size, "%d", parent->value);
}

static const struct fence_ops goldfish_sync_timeline_fence_ops = {
	.get_driver_name = goldfish_sync_timeline_fence_get_driver_name,
	.get_timeline_name = goldfish_sync_timeline_fence_get_timeline_name,
	.enable_signaling = goldfish_sync_timeline_fence_enable_signaling,
	.disable_signaling = goldfish_sync_timeline_fence_disable_signaling,
	.signaled = goldfish_sync_timeline_fence_signaled,
	.wait = fence_default_wait,
	.release = goldfish_sync_timeline_fence_release,
	.fence_value_str = goldfish_sync_timeline_fence_value_str,
	.timeline_value_str = goldfish_sync_timeline_fence_timeline_value_str,
};

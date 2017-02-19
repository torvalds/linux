/*
 * Sync File validation framework and debug infomation
 *
 * Copyright (C) 2012 Google, Inc.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_SYNC_H
#define _LINUX_SYNC_H

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/dma-fence.h>

#include <linux/sync_file.h>
#include <uapi/linux/sync_file.h>

/**
 * struct sync_timeline - sync object
 * @kref:		reference count on fence.
 * @name:		name of the sync_timeline. Useful for debugging
 * @child_list_head:	list of children sync_pts for this sync_timeline
 * @child_list_lock:	lock protecting @child_list_head and fence.status
 * @active_list_head:	list of active (unsignaled/errored) sync_pts
 * @sync_timeline_list:	membership in global sync_timeline_list
 */
struct sync_timeline {
	struct kref		kref;
	char			name[32];

	/* protected by child_list_lock */
	u64			context;
	int			value;

	struct list_head	child_list_head;
	spinlock_t		child_list_lock;

	struct list_head	active_list_head;

	struct list_head	sync_timeline_list;
};

static inline struct sync_timeline *dma_fence_parent(struct dma_fence *fence)
{
	return container_of(fence->lock, struct sync_timeline, child_list_lock);
}

/**
 * struct sync_pt - sync_pt object
 * @base: base fence object
 * @child_list: sync timeline child's list
 * @active_list: sync timeline active child's list
 */
struct sync_pt {
	struct dma_fence base;
	struct list_head child_list;
	struct list_head active_list;
};

#ifdef CONFIG_SW_SYNC

extern const struct file_operations sw_sync_debugfs_fops;

void sync_timeline_debug_add(struct sync_timeline *obj);
void sync_timeline_debug_remove(struct sync_timeline *obj);
void sync_file_debug_add(struct sync_file *fence);
void sync_file_debug_remove(struct sync_file *fence);
void sync_dump(void);

#else
# define sync_timeline_debug_add(obj)
# define sync_timeline_debug_remove(obj)
# define sync_file_debug_add(fence)
# define sync_file_debug_remove(fence)
# define sync_dump()
#endif

#endif /* _LINUX_SYNC_H */

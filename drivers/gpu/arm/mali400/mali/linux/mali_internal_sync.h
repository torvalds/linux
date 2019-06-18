/*
 * Copyright (C) 2012-2015, 2017-2018 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_internal_sync.h
 *
 * Mali internal structure/interface for sync.
 */

#ifndef _MALI_INTERNAL_SYNC_H
#define _MALI_INTERNAL_SYNC_H
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
#include <linux/types.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)
#include <sync.h>
#else
#include <linux/sync_file.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
#include <linux/dma-fence.h>
#else
#include <linux/fence.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
#include <linux/fence-array.h>
#else
#include <linux/dma-fence-array.h>
#endif
#endif

struct mali_internal_sync_timeline;
struct mali_internal_sync_point;
struct mali_internal_sync_fence;

struct mali_internal_sync_timeline_ops {
	const char *driver_name;
	int (*has_signaled)(struct mali_internal_sync_point *pt);
	void (*free_pt)(struct mali_internal_sync_point *sync_pt);
	void (*release_obj)(struct mali_internal_sync_timeline *sync_timeline);
	void (*print_sync_pt)(struct mali_internal_sync_point *sync_pt);
};

struct mali_internal_sync_timeline {
	struct kref             kref_count;
	const struct mali_internal_sync_timeline_ops  *ops;
	char                    name[32];
	bool                    destroyed;
	int                     fence_context;
	int                     value;
	spinlock_t              sync_pt_list_lock;
	struct list_head        sync_pt_list_head;
};

struct mali_internal_sync_point {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	struct dma_fence base;
#else
	struct fence base;
#endif
	struct list_head        sync_pt_list;
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
struct mali_internal_sync_fence_cb {
	struct fence_cb cb;
	struct fence *fence;
	struct mali_internal_sync_fence *sync_file;
};
#endif

struct mali_internal_sync_fence {
	struct file             *file;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 13, 0)
	struct kref             kref;
#endif
	char            name[32];
#ifdef CONFIG_DEBUG_FS
	struct list_head        sync_file_list;
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
	int num_fences;
#endif
	wait_queue_head_t       wq;
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 12, 0)
	unsigned long		flags;
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
	atomic_t                status;
	struct mali_internal_sync_fence_cb    cbs[];
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	struct fence *fence;
	struct fence_cb cb;
#else
	struct dma_fence *fence;
	struct dma_fence_cb cb;
#endif
};

struct mali_internal_sync_fence_waiter;

typedef void (*mali_internal_sync_callback_t)(struct mali_internal_sync_fence *sync_fence,
		struct mali_internal_sync_fence_waiter *waiter);

struct mali_internal_sync_fence_waiter {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
	wait_queue_entry_t work;
#else
	wait_queue_t work;
#endif
	mali_internal_sync_callback_t callback;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	struct fence_cb cb;
#else
	struct dma_fence_cb cb;
#endif
#endif
};

/**
 * Create a mali internal sync timeline.
 * @param ops The implementation ops for the mali internal sync timeline
 * @param size The size to allocate
 * @param name The sync_timeline name
 * @return The new mali internal sync timeline if successful, NULL if not.
 */
struct mali_internal_sync_timeline *mali_internal_sync_timeline_create(const struct mali_internal_sync_timeline_ops *ops,
		int size, const char *name);

/**
 * Destroy one mali internal sync timeline.
 * @param sync_timeline The mali internal sync timeline to destroy.
 */
void mali_internal_sync_timeline_destroy(struct mali_internal_sync_timeline *sync_timeline);

/**
 * Signal one mali internal sync timeline.
 * @param sync_timeline The mali internal sync timeline to signal.
 */
void mali_internal_sync_timeline_signal(struct mali_internal_sync_timeline *sync_timeline);

/**
 * Create one mali internal sync point.
 * @param sync_timeline The mali internal sync timeline to add this mali internal sync point.
  * @return the new mali internal sync point if successful, NULL if not.
 */
struct mali_internal_sync_point *mali_internal_sync_point_create(struct mali_internal_sync_timeline *sync_timeline, int size);

/**
 * Merge mali internal sync fences
 * @param sync_fence1 The mali internal sync fence to merge
 * @param sync_fence2 The mali internal sync fence to merge
 * @return the new mali internal sync fence if successful, NULL if not.
 */
struct mali_internal_sync_fence *mali_internal_sync_fence_merge(struct mali_internal_sync_fence *sync_fence1,
		struct mali_internal_sync_fence *sync_fence2);

/**
 * Get the mali internal sync fence from sync fd
 * @param fd The sync handle to get the mali internal sync fence
 * @return the mali internal sync fence if successful, NULL if not.
 */
struct mali_internal_sync_fence *mali_internal_sync_fence_fdget(int fd);


void mali_internal_sync_fence_waiter_init(struct mali_internal_sync_fence_waiter *waiter,
		mali_internal_sync_callback_t callback);

int mali_internal_sync_fence_wait_async(struct mali_internal_sync_fence *sync_fence,
					struct mali_internal_sync_fence_waiter *waiter);

int mali_internal_sync_fence_cancel_async(struct mali_internal_sync_fence *sync_fence,
		struct mali_internal_sync_fence_waiter *waiter);

#endif /*LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)*/
#endif /* _MALI_INTERNAL_SYNC_H */

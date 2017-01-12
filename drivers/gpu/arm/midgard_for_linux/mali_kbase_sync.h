/*
 *
 * (C) COPYRIGHT 2012-2016 ARM Limited. All rights reserved.
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





/**
 * @file mali_kbase_sync.h
 *
 */

#ifndef MALI_KBASE_SYNC_H
#define MALI_KBASE_SYNC_H

#include "sync.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
/* For backwards compatiblility with kernels before 3.17. After 3.17
 * sync_pt_parent is included in the kernel. */
static inline struct sync_timeline *sync_pt_parent(struct sync_pt *pt)
{
	return pt->parent;
}
#endif

static inline int kbase_fence_get_status(struct sync_fence *fence)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
	return fence->status;
#else
	return atomic_read(&fence->status);
#endif
}

/*
 * Create a stream object.
 * Built on top of timeline object.
 * Exposed as a file descriptor.
 * Life-time controlled via the file descriptor:
 * - dup to add a ref
 * - close to remove a ref
 */
int kbase_stream_create(const char *name, int *const out_fd);

/*
 * Create a fence in a stream object
 */
int kbase_stream_create_fence(int tl_fd);

/*
 * Validate a fd to be a valid fence
 * No reference is taken.
 *
 * This function is only usable to catch unintentional user errors early,
 * it does not stop malicious code changing the fd after this function returns.
 */
int kbase_fence_validate(int fd);

/* Returns true if the specified timeline is allocated by Mali */
int kbase_sync_timeline_is_ours(struct sync_timeline *timeline);

/* Allocates a timeline for Mali
 *
 * One timeline should be allocated per API context.
 */
struct sync_timeline *kbase_sync_timeline_alloc(const char *name);

/* Allocates a sync point within the timeline.
 *
 * The timeline must be the one allocated by kbase_sync_timeline_alloc
 *
 * Sync points must be triggered in *exactly* the same order as they are allocated.
 */
struct sync_pt *kbase_sync_pt_alloc(struct sync_timeline *parent);

/* Signals a particular sync point
 *
 * Sync points must be triggered in *exactly* the same order as they are allocated.
 *
 * If they are signalled in the wrong order then a message will be printed in debug
 * builds and otherwise attempts to signal order sync_pts will be ignored.
 *
 * result can be negative to indicate error, any other value is interpreted as success.
 */
void kbase_sync_signal_pt(struct sync_pt *pt, int result);

#endif

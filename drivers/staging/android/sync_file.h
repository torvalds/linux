/*
 * include/linux/sync_file.h
 *
 * Copyright (C) 2012 Google, Inc.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_SYNC_FILE_H
#define _LINUX_SYNC_FILE_H

#include <linux/types.h>
#include <linux/kref.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/fence.h>

struct sync_file_cb {
	struct fence_cb cb;
	struct fence *fence;
	struct sync_file *sync_file;
};

/**
 * struct sync_file - sync file to export to the userspace
 * @file:		file representing this fence
 * @kref:		reference count on fence.
 * @name:		name of sync_file.  Useful for debugging
 * @sync_file_list:	membership in global file list
 * @num_fences:		number of sync_pts in the fence
 * @wq:			wait queue for fence signaling
 * @status:		0: signaled, >0:active, <0: error
 * @cbs:		sync_pts callback information
 */
struct sync_file {
	struct file		*file;
	struct kref		kref;
	char			name[32];
#ifdef CONFIG_DEBUG_FS
	struct list_head	sync_file_list;
#endif
	int num_fences;

	wait_queue_head_t	wq;
	atomic_t		status;

	struct sync_file_cb	cbs[];
};

struct sync_file *sync_file_create(struct fence *fence);

#endif /* _LINUX_SYNC_H */

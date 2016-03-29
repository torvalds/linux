/*
 * include/linux/sync.h
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

#include <linux/types.h>
#include <linux/kref.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/fence.h>

#include "uapi/sync.h"

struct sync_timeline;
struct sync_file;

/**
 * struct sync_timeline_ops - sync object implementation ops
 * @driver_name:	name of the implementation
 * @has_signaled:	returns:
 *			  1 if pt has signaled
 *			  0 if pt has not signaled
 *			 <0 on error
 * @timeline_value_str: fill str with the value of the sync_timeline's counter
 * @fence_value_str:	fill str with the value of the fence
 */
struct sync_timeline_ops {
	const char *driver_name;

	/* required */
	int (*has_signaled)(struct fence *fence);

	/* optional */
	void (*timeline_value_str)(struct sync_timeline *timeline, char *str,
				   int size);

	/* optional */
	void (*fence_value_str)(struct fence *fence, char *str, int size);
};

/**
 * struct sync_timeline - sync object
 * @kref:		reference count on fence.
 * @ops:		ops that define the implementation of the sync_timeline
 * @name:		name of the sync_timeline. Useful for debugging
 * @destroyed:		set when sync_timeline is destroyed
 * @child_list_head:	list of children sync_pts for this sync_timeline
 * @child_list_lock:	lock protecting @child_list_head, destroyed, and
 *			fence.status
 * @active_list_head:	list of active (unsignaled/errored) sync_pts
 * @sync_timeline_list:	membership in global sync_timeline_list
 */
struct sync_timeline {
	struct kref		kref;
	const struct sync_timeline_ops	*ops;
	char			name[32];

	/* protected by child_list_lock */
	bool			destroyed;
	int			context, value;

	struct list_head	child_list_head;
	spinlock_t		child_list_lock;

	struct list_head	active_list_head;

#ifdef CONFIG_DEBUG_FS
	struct list_head	sync_timeline_list;
#endif
};

static inline struct sync_timeline *fence_parent(struct fence *fence)
{
	return container_of(fence->lock, struct sync_timeline,
			    child_list_lock);
}

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
 * @num_fences		number of sync_pts in the fence
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

/*
 * API for sync_timeline implementers
 */

/**
 * sync_timeline_create() - creates a sync object
 * @ops:	specifies the implementation ops for the object
 * @size:	size to allocate for this obj
 * @name:	sync_timeline name
 *
 * Creates a new sync_timeline which will use the implementation specified by
 * @ops.  @size bytes will be allocated allowing for implementation specific
 * data to be kept after the generic sync_timeline struct. Returns the
 * sync_timeline object or NULL in case of error.
 */
struct sync_timeline *sync_timeline_create(const struct sync_timeline_ops *ops,
					   int size, const char *name);

/**
 * sync_timeline_destroy() - destroys a sync object
 * @obj:	sync_timeline to destroy
 *
 * A sync implementation should call this when the @obj is going away
 * (i.e. module unload.)  @obj won't actually be freed until all its children
 * fences are freed.
 */
void sync_timeline_destroy(struct sync_timeline *obj);

/**
 * sync_timeline_signal() - signal a status change on a sync_timeline
 * @obj:	sync_timeline to signal
 *
 * A sync implementation should call this any time one of it's fences
 * has signaled or has an error condition.
 */
void sync_timeline_signal(struct sync_timeline *obj);

/**
 * sync_pt_create() - creates a sync pt
 * @parent:	fence's parent sync_timeline
 * @size:	size to allocate for this pt
 *
 * Creates a new fence as a child of @parent.  @size bytes will be
 * allocated allowing for implementation specific data to be kept after
 * the generic sync_timeline struct. Returns the fence object or
 * NULL in case of error.
 */
struct fence *sync_pt_create(struct sync_timeline *parent, int size);

/**
 * sync_fence_create() - creates a sync fence
 * @name:	name of fence to create
 * @fence:	fence to add to the sync_fence
 *
 * Creates a sync_file containg @fence. Once this is called, the sync_file
 * takes ownership of @fence.
 */
struct sync_file *sync_file_create(const char *name, struct fence *fence);

/*
 * API for sync_file consumers
 */

/**
 * sync_file_merge() - merge two sync_files
 * @name:	name of new fence
 * @a:		sync_file a
 * @b:		sync_file b
 *
 * Creates a new sync_file which contains copies of all the fences in both
 * @a and @b.  @a and @b remain valid, independent sync_file. Returns the
 * new merged sync_file or NULL in case of error.
 */
struct sync_file *sync_file_merge(const char *name,
				    struct sync_file *a, struct sync_file *b);

/**
 * sync_file_fdget() - get a sync_file from an fd
 * @fd:		fd referencing a fence
 *
 * Ensures @fd references a valid sync_file, increments the refcount of the
 * backing file. Returns the sync_file or NULL in case of error.
 */
struct sync_file *sync_file_fdget(int fd);

/**
 * sync_file_put() - puts a reference of a sync_file
 * @sync_file:	sync_file to put
 *
 * Puts a reference on @sync_fence.  If this is the last reference, the
 * sync_fil and all it's sync_pts will be freed
 */
void sync_file_put(struct sync_file *sync_file);

/**
 * sync_file_install() - installs a sync_file into a file descriptor
 * @sync_file:	sync_file to install
 * @fd:		file descriptor in which to install the fence
 *
 * Installs @sync_file into @fd.  @fd's should be acquired through
 * get_unused_fd_flags(O_CLOEXEC).
 */
void sync_file_install(struct sync_file *sync_file, int fd);

#ifdef CONFIG_DEBUG_FS

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

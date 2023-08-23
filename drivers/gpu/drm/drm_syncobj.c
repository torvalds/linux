/*
 * Copyright 2017 Red Hat
 * Parts ported from amdgpu (fence wait code).
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *
 */

/**
 * DOC: Overview
 *
 * DRM synchronisation objects (syncobj, see struct &drm_syncobj) provide a
 * container for a synchronization primitive which can be used by userspace
 * to explicitly synchronize GPU commands, can be shared between userspace
 * processes, and can be shared between different DRM drivers.
 * Their primary use-case is to implement Vulkan fences and semaphores.
 * The syncobj userspace API provides ioctls for several operations:
 *
 *  - Creation and destruction of syncobjs
 *  - Import and export of syncobjs to/from a syncobj file descriptor
 *  - Import and export a syncobj's underlying fence to/from a sync file
 *  - Reset a syncobj (set its fence to NULL)
 *  - Signal a syncobj (set a trivially signaled fence)
 *  - Wait for a syncobj's fence to appear and be signaled
 *
 * The syncobj userspace API also provides operations to manipulate a syncobj
 * in terms of a timeline of struct &dma_fence_chain rather than a single
 * struct &dma_fence, through the following operations:
 *
 *   - Signal a given point on the timeline
 *   - Wait for a given point to appear and/or be signaled
 *   - Import and export from/to a given point of a timeline
 *
 * At it's core, a syncobj is simply a wrapper around a pointer to a struct
 * &dma_fence which may be NULL.
 * When a syncobj is first created, its pointer is either NULL or a pointer
 * to an already signaled fence depending on whether the
 * &DRM_SYNCOBJ_CREATE_SIGNALED flag is passed to
 * &DRM_IOCTL_SYNCOBJ_CREATE.
 *
 * If the syncobj is considered as a binary (its state is either signaled or
 * unsignaled) primitive, when GPU work is enqueued in a DRM driver to signal
 * the syncobj, the syncobj's fence is replaced with a fence which will be
 * signaled by the completion of that work.
 * If the syncobj is considered as a timeline primitive, when GPU work is
 * enqueued in a DRM driver to signal the a given point of the syncobj, a new
 * struct &dma_fence_chain pointing to the DRM driver's fence and also
 * pointing to the previous fence that was in the syncobj. The new struct
 * &dma_fence_chain fence replace the syncobj's fence and will be signaled by
 * completion of the DRM driver's work and also any work associated with the
 * fence previously in the syncobj.
 *
 * When GPU work which waits on a syncobj is enqueued in a DRM driver, at the
 * time the work is enqueued, it waits on the syncobj's fence before
 * submitting the work to hardware. That fence is either :
 *
 *    - The syncobj's current fence if the syncobj is considered as a binary
 *      primitive.
 *    - The struct &dma_fence associated with a given point if the syncobj is
 *      considered as a timeline primitive.
 *
 * If the syncobj's fence is NULL or not present in the syncobj's timeline,
 * the enqueue operation is expected to fail.
 *
 * With binary syncobj, all manipulation of the syncobjs's fence happens in
 * terms of the current fence at the time the ioctl is called by userspace
 * regardless of whether that operation is an immediate host-side operation
 * (signal or reset) or or an operation which is enqueued in some driver
 * queue. &DRM_IOCTL_SYNCOBJ_RESET and &DRM_IOCTL_SYNCOBJ_SIGNAL can be used
 * to manipulate a syncobj from the host by resetting its pointer to NULL or
 * setting its pointer to a fence which is already signaled.
 *
 * With a timeline syncobj, all manipulation of the synobj's fence happens in
 * terms of a u64 value referring to point in the timeline. See
 * dma_fence_chain_find_seqno() to see how a given point is found in the
 * timeline.
 *
 * Note that applications should be careful to always use timeline set of
 * ioctl() when dealing with syncobj considered as timeline. Using a binary
 * set of ioctl() with a syncobj considered as timeline could result incorrect
 * synchronization. The use of binary syncobj is supported through the
 * timeline set of ioctl() by using a point value of 0, this will reproduce
 * the behavior of the binary set of ioctl() (for example replace the
 * syncobj's fence when signaling).
 *
 *
 * Host-side wait on syncobjs
 * --------------------------
 *
 * &DRM_IOCTL_SYNCOBJ_WAIT takes an array of syncobj handles and does a
 * host-side wait on all of the syncobj fences simultaneously.
 * If &DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL is set, the wait ioctl will wait on
 * all of the syncobj fences to be signaled before it returns.
 * Otherwise, it returns once at least one syncobj fence has been signaled
 * and the index of a signaled fence is written back to the client.
 *
 * Unlike the enqueued GPU work dependencies which fail if they see a NULL
 * fence in a syncobj, if &DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT is set,
 * the host-side wait will first wait for the syncobj to receive a non-NULL
 * fence and then wait on that fence.
 * If &DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT is not set and any one of the
 * syncobjs in the array has a NULL fence, -EINVAL will be returned.
 * Assuming the syncobj starts off with a NULL fence, this allows a client
 * to do a host wait in one thread (or process) which waits on GPU work
 * submitted in another thread (or process) without having to manually
 * synchronize between the two.
 * This requirement is inherited from the Vulkan fence API.
 *
 * Similarly, &DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT takes an array of syncobj
 * handles as well as an array of u64 points and does a host-side wait on all
 * of syncobj fences at the given points simultaneously.
 *
 * &DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT also adds the ability to wait for a given
 * fence to materialize on the timeline without waiting for the fence to be
 * signaled by using the &DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE flag. This
 * requirement is inherited from the wait-before-signal behavior required by
 * the Vulkan timeline semaphore API.
 *
 *
 * Import/export of syncobjs
 * -------------------------
 *
 * &DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE and &DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD
 * provide two mechanisms for import/export of syncobjs.
 *
 * The first lets the client import or export an entire syncobj to a file
 * descriptor.
 * These fd's are opaque and have no other use case, except passing the
 * syncobj between processes.
 * All exported file descriptors and any syncobj handles created as a
 * result of importing those file descriptors own a reference to the
 * same underlying struct &drm_syncobj and the syncobj can be used
 * persistently across all the processes with which it is shared.
 * The syncobj is freed only once the last reference is dropped.
 * Unlike dma-buf, importing a syncobj creates a new handle (with its own
 * reference) for every import instead of de-duplicating.
 * The primary use-case of this persistent import/export is for shared
 * Vulkan fences and semaphores.
 *
 * The second import/export mechanism, which is indicated by
 * &DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE or
 * &DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE lets the client
 * import/export the syncobj's current fence from/to a &sync_file.
 * When a syncobj is exported to a sync file, that sync file wraps the
 * sycnobj's fence at the time of export and any later signal or reset
 * operations on the syncobj will not affect the exported sync file.
 * When a sync file is imported into a syncobj, the syncobj's fence is set
 * to the fence wrapped by that sync file.
 * Because sync files are immutable, resetting or signaling the syncobj
 * will not affect any sync files whose fences have been imported into the
 * syncobj.
 *
 *
 * Import/export of timeline points in timeline syncobjs
 * -----------------------------------------------------
 *
 * &DRM_IOCTL_SYNCOBJ_TRANSFER provides a mechanism to transfer a struct
 * &dma_fence_chain of a syncobj at a given u64 point to another u64 point
 * into another syncobj.
 *
 * Note that if you want to transfer a struct &dma_fence_chain from a given
 * point on a timeline syncobj from/into a binary syncobj, you can use the
 * point 0 to mean take/replace the fence in the syncobj.
 */

#include <linux/anon_inodes.h>
#include <linux/dma-fence-unwrap.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/sched/signal.h>
#include <linux/sync_file.h>
#include <linux/uaccess.h>

#include <drm/drm.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_print.h>
#include <drm/drm_syncobj.h>
#include <drm/drm_utils.h>

#include "drm_internal.h"

struct syncobj_wait_entry {
	struct list_head node;
	struct task_struct *task;
	struct dma_fence *fence;
	struct dma_fence_cb fence_cb;
	u64    point;
};

static void syncobj_wait_syncobj_func(struct drm_syncobj *syncobj,
				      struct syncobj_wait_entry *wait);

/**
 * drm_syncobj_find - lookup and reference a sync object.
 * @file_private: drm file private pointer
 * @handle: sync object handle to lookup.
 *
 * Returns a reference to the syncobj pointed to by handle or NULL. The
 * reference must be released by calling drm_syncobj_put().
 */
struct drm_syncobj *drm_syncobj_find(struct drm_file *file_private,
				     u32 handle)
{
	struct drm_syncobj *syncobj;

	spin_lock(&file_private->syncobj_table_lock);

	/* Check if we currently have a reference on the object */
	syncobj = idr_find(&file_private->syncobj_idr, handle);
	if (syncobj)
		drm_syncobj_get(syncobj);

	spin_unlock(&file_private->syncobj_table_lock);

	return syncobj;
}
EXPORT_SYMBOL(drm_syncobj_find);

static void drm_syncobj_fence_add_wait(struct drm_syncobj *syncobj,
				       struct syncobj_wait_entry *wait)
{
	struct dma_fence *fence;

	if (wait->fence)
		return;

	spin_lock(&syncobj->lock);
	/* We've already tried once to get a fence and failed.  Now that we
	 * have the lock, try one more time just to be sure we don't add a
	 * callback when a fence has already been set.
	 */
	fence = dma_fence_get(rcu_dereference_protected(syncobj->fence, 1));
	if (!fence || dma_fence_chain_find_seqno(&fence, wait->point)) {
		dma_fence_put(fence);
		list_add_tail(&wait->node, &syncobj->cb_list);
	} else if (!fence) {
		wait->fence = dma_fence_get_stub();
	} else {
		wait->fence = fence;
	}
	spin_unlock(&syncobj->lock);
}

static void drm_syncobj_remove_wait(struct drm_syncobj *syncobj,
				    struct syncobj_wait_entry *wait)
{
	if (!wait->node.next)
		return;

	spin_lock(&syncobj->lock);
	list_del_init(&wait->node);
	spin_unlock(&syncobj->lock);
}

/**
 * drm_syncobj_add_point - add new timeline point to the syncobj
 * @syncobj: sync object to add timeline point do
 * @chain: chain node to use to add the point
 * @fence: fence to encapsulate in the chain node
 * @point: sequence number to use for the point
 *
 * Add the chain node as new timeline point to the syncobj.
 */
void drm_syncobj_add_point(struct drm_syncobj *syncobj,
			   struct dma_fence_chain *chain,
			   struct dma_fence *fence,
			   uint64_t point)
{
	struct syncobj_wait_entry *cur, *tmp;
	struct dma_fence *prev;

	dma_fence_get(fence);

	spin_lock(&syncobj->lock);

	prev = drm_syncobj_fence_get(syncobj);
	/* You are adding an unorder point to timeline, which could cause payload returned from query_ioctl is 0! */
	if (prev && prev->seqno >= point)
		DRM_DEBUG("You are adding an unorder point to timeline!\n");
	dma_fence_chain_init(chain, prev, fence, point);
	rcu_assign_pointer(syncobj->fence, &chain->base);

	list_for_each_entry_safe(cur, tmp, &syncobj->cb_list, node)
		syncobj_wait_syncobj_func(syncobj, cur);
	spin_unlock(&syncobj->lock);

	/* Walk the chain once to trigger garbage collection */
	dma_fence_chain_for_each(fence, prev);
	dma_fence_put(prev);
}
EXPORT_SYMBOL(drm_syncobj_add_point);

/**
 * drm_syncobj_replace_fence - replace fence in a sync object.
 * @syncobj: Sync object to replace fence in
 * @fence: fence to install in sync file.
 *
 * This replaces the fence on a sync object.
 */
void drm_syncobj_replace_fence(struct drm_syncobj *syncobj,
			       struct dma_fence *fence)
{
	struct dma_fence *old_fence;
	struct syncobj_wait_entry *cur, *tmp;

	if (fence)
		dma_fence_get(fence);

	spin_lock(&syncobj->lock);

	old_fence = rcu_dereference_protected(syncobj->fence,
					      lockdep_is_held(&syncobj->lock));
	rcu_assign_pointer(syncobj->fence, fence);

	if (fence != old_fence) {
		list_for_each_entry_safe(cur, tmp, &syncobj->cb_list, node)
			syncobj_wait_syncobj_func(syncobj, cur);
	}

	spin_unlock(&syncobj->lock);

	dma_fence_put(old_fence);
}
EXPORT_SYMBOL(drm_syncobj_replace_fence);

/**
 * drm_syncobj_assign_null_handle - assign a stub fence to the sync object
 * @syncobj: sync object to assign the fence on
 *
 * Assign a already signaled stub fence to the sync object.
 */
static int drm_syncobj_assign_null_handle(struct drm_syncobj *syncobj)
{
	struct dma_fence *fence = dma_fence_allocate_private_stub(ktime_get());

	if (!fence)
		return -ENOMEM;

	drm_syncobj_replace_fence(syncobj, fence);
	dma_fence_put(fence);
	return 0;
}

/* 5s default for wait submission */
#define DRM_SYNCOBJ_WAIT_FOR_SUBMIT_TIMEOUT 5000000000ULL
/**
 * drm_syncobj_find_fence - lookup and reference the fence in a sync object
 * @file_private: drm file private pointer
 * @handle: sync object handle to lookup.
 * @point: timeline point
 * @flags: DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT or not
 * @fence: out parameter for the fence
 *
 * This is just a convenience function that combines drm_syncobj_find() and
 * drm_syncobj_fence_get().
 *
 * Returns 0 on success or a negative error value on failure. On success @fence
 * contains a reference to the fence, which must be released by calling
 * dma_fence_put().
 */
int drm_syncobj_find_fence(struct drm_file *file_private,
			   u32 handle, u64 point, u64 flags,
			   struct dma_fence **fence)
{
	struct drm_syncobj *syncobj = drm_syncobj_find(file_private, handle);
	struct syncobj_wait_entry wait;
	u64 timeout = nsecs_to_jiffies64(DRM_SYNCOBJ_WAIT_FOR_SUBMIT_TIMEOUT);
	int ret;

	if (!syncobj)
		return -ENOENT;

	/* Waiting for userspace with locks help is illegal cause that can
	 * trivial deadlock with page faults for example. Make lockdep complain
	 * about it early on.
	 */
	if (flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT) {
		might_sleep();
		lockdep_assert_none_held_once();
	}

	*fence = drm_syncobj_fence_get(syncobj);

	if (*fence) {
		ret = dma_fence_chain_find_seqno(fence, point);
		if (!ret) {
			/* If the requested seqno is already signaled
			 * drm_syncobj_find_fence may return a NULL
			 * fence. To make sure the recipient gets
			 * signalled, use a new fence instead.
			 */
			if (!*fence)
				*fence = dma_fence_get_stub();

			goto out;
		}
		dma_fence_put(*fence);
	} else {
		ret = -EINVAL;
	}

	if (!(flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT))
		goto out;

	memset(&wait, 0, sizeof(wait));
	wait.task = current;
	wait.point = point;
	drm_syncobj_fence_add_wait(syncobj, &wait);

	do {
		set_current_state(TASK_INTERRUPTIBLE);
		if (wait.fence) {
			ret = 0;
			break;
		}
                if (timeout == 0) {
                        ret = -ETIME;
                        break;
                }

		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

                timeout = schedule_timeout(timeout);
	} while (1);

	__set_current_state(TASK_RUNNING);
	*fence = wait.fence;

	if (wait.node.next)
		drm_syncobj_remove_wait(syncobj, &wait);

out:
	drm_syncobj_put(syncobj);

	return ret;
}
EXPORT_SYMBOL(drm_syncobj_find_fence);

/**
 * drm_syncobj_free - free a sync object.
 * @kref: kref to free.
 *
 * Only to be called from kref_put in drm_syncobj_put.
 */
void drm_syncobj_free(struct kref *kref)
{
	struct drm_syncobj *syncobj = container_of(kref,
						   struct drm_syncobj,
						   refcount);
	drm_syncobj_replace_fence(syncobj, NULL);
	kfree(syncobj);
}
EXPORT_SYMBOL(drm_syncobj_free);

/**
 * drm_syncobj_create - create a new syncobj
 * @out_syncobj: returned syncobj
 * @flags: DRM_SYNCOBJ_* flags
 * @fence: if non-NULL, the syncobj will represent this fence
 *
 * This is the first function to create a sync object. After creating, drivers
 * probably want to make it available to userspace, either through
 * drm_syncobj_get_handle() or drm_syncobj_get_fd().
 *
 * Returns 0 on success or a negative error value on failure.
 */
int drm_syncobj_create(struct drm_syncobj **out_syncobj, uint32_t flags,
		       struct dma_fence *fence)
{
	int ret;
	struct drm_syncobj *syncobj;

	syncobj = kzalloc(sizeof(struct drm_syncobj), GFP_KERNEL);
	if (!syncobj)
		return -ENOMEM;

	kref_init(&syncobj->refcount);
	INIT_LIST_HEAD(&syncobj->cb_list);
	spin_lock_init(&syncobj->lock);

	if (flags & DRM_SYNCOBJ_CREATE_SIGNALED) {
		ret = drm_syncobj_assign_null_handle(syncobj);
		if (ret < 0) {
			drm_syncobj_put(syncobj);
			return ret;
		}
	}

	if (fence)
		drm_syncobj_replace_fence(syncobj, fence);

	*out_syncobj = syncobj;
	return 0;
}
EXPORT_SYMBOL(drm_syncobj_create);

/**
 * drm_syncobj_get_handle - get a handle from a syncobj
 * @file_private: drm file private pointer
 * @syncobj: Sync object to export
 * @handle: out parameter with the new handle
 *
 * Exports a sync object created with drm_syncobj_create() as a handle on
 * @file_private to userspace.
 *
 * Returns 0 on success or a negative error value on failure.
 */
int drm_syncobj_get_handle(struct drm_file *file_private,
			   struct drm_syncobj *syncobj, u32 *handle)
{
	int ret;

	/* take a reference to put in the idr */
	drm_syncobj_get(syncobj);

	idr_preload(GFP_KERNEL);
	spin_lock(&file_private->syncobj_table_lock);
	ret = idr_alloc(&file_private->syncobj_idr, syncobj, 1, 0, GFP_NOWAIT);
	spin_unlock(&file_private->syncobj_table_lock);

	idr_preload_end();

	if (ret < 0) {
		drm_syncobj_put(syncobj);
		return ret;
	}

	*handle = ret;
	return 0;
}
EXPORT_SYMBOL(drm_syncobj_get_handle);

static int drm_syncobj_create_as_handle(struct drm_file *file_private,
					u32 *handle, uint32_t flags)
{
	int ret;
	struct drm_syncobj *syncobj;

	ret = drm_syncobj_create(&syncobj, flags, NULL);
	if (ret)
		return ret;

	ret = drm_syncobj_get_handle(file_private, syncobj, handle);
	drm_syncobj_put(syncobj);
	return ret;
}

static int drm_syncobj_destroy(struct drm_file *file_private,
			       u32 handle)
{
	struct drm_syncobj *syncobj;

	spin_lock(&file_private->syncobj_table_lock);
	syncobj = idr_remove(&file_private->syncobj_idr, handle);
	spin_unlock(&file_private->syncobj_table_lock);

	if (!syncobj)
		return -EINVAL;

	drm_syncobj_put(syncobj);
	return 0;
}

static int drm_syncobj_file_release(struct inode *inode, struct file *file)
{
	struct drm_syncobj *syncobj = file->private_data;

	drm_syncobj_put(syncobj);
	return 0;
}

static const struct file_operations drm_syncobj_file_fops = {
	.release = drm_syncobj_file_release,
};

/**
 * drm_syncobj_get_fd - get a file descriptor from a syncobj
 * @syncobj: Sync object to export
 * @p_fd: out parameter with the new file descriptor
 *
 * Exports a sync object created with drm_syncobj_create() as a file descriptor.
 *
 * Returns 0 on success or a negative error value on failure.
 */
int drm_syncobj_get_fd(struct drm_syncobj *syncobj, int *p_fd)
{
	struct file *file;
	int fd;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;

	file = anon_inode_getfile("syncobj_file",
				  &drm_syncobj_file_fops,
				  syncobj, 0);
	if (IS_ERR(file)) {
		put_unused_fd(fd);
		return PTR_ERR(file);
	}

	drm_syncobj_get(syncobj);
	fd_install(fd, file);

	*p_fd = fd;
	return 0;
}
EXPORT_SYMBOL(drm_syncobj_get_fd);

static int drm_syncobj_handle_to_fd(struct drm_file *file_private,
				    u32 handle, int *p_fd)
{
	struct drm_syncobj *syncobj = drm_syncobj_find(file_private, handle);
	int ret;

	if (!syncobj)
		return -EINVAL;

	ret = drm_syncobj_get_fd(syncobj, p_fd);
	drm_syncobj_put(syncobj);
	return ret;
}

static int drm_syncobj_fd_to_handle(struct drm_file *file_private,
				    int fd, u32 *handle)
{
	struct drm_syncobj *syncobj;
	struct fd f = fdget(fd);
	int ret;

	if (!f.file)
		return -EINVAL;

	if (f.file->f_op != &drm_syncobj_file_fops) {
		fdput(f);
		return -EINVAL;
	}

	/* take a reference to put in the idr */
	syncobj = f.file->private_data;
	drm_syncobj_get(syncobj);

	idr_preload(GFP_KERNEL);
	spin_lock(&file_private->syncobj_table_lock);
	ret = idr_alloc(&file_private->syncobj_idr, syncobj, 1, 0, GFP_NOWAIT);
	spin_unlock(&file_private->syncobj_table_lock);
	idr_preload_end();

	if (ret > 0) {
		*handle = ret;
		ret = 0;
	} else
		drm_syncobj_put(syncobj);

	fdput(f);
	return ret;
}

static int drm_syncobj_import_sync_file_fence(struct drm_file *file_private,
					      int fd, int handle)
{
	struct dma_fence *fence = sync_file_get_fence(fd);
	struct drm_syncobj *syncobj;

	if (!fence)
		return -EINVAL;

	syncobj = drm_syncobj_find(file_private, handle);
	if (!syncobj) {
		dma_fence_put(fence);
		return -ENOENT;
	}

	drm_syncobj_replace_fence(syncobj, fence);
	dma_fence_put(fence);
	drm_syncobj_put(syncobj);
	return 0;
}

static int drm_syncobj_export_sync_file(struct drm_file *file_private,
					int handle, int *p_fd)
{
	int ret;
	struct dma_fence *fence;
	struct sync_file *sync_file;
	int fd = get_unused_fd_flags(O_CLOEXEC);

	if (fd < 0)
		return fd;

	ret = drm_syncobj_find_fence(file_private, handle, 0, 0, &fence);
	if (ret)
		goto err_put_fd;

	sync_file = sync_file_create(fence);

	dma_fence_put(fence);

	if (!sync_file) {
		ret = -EINVAL;
		goto err_put_fd;
	}

	fd_install(fd, sync_file->file);

	*p_fd = fd;
	return 0;
err_put_fd:
	put_unused_fd(fd);
	return ret;
}
/**
 * drm_syncobj_open - initializes syncobj file-private structures at devnode open time
 * @file_private: drm file-private structure to set up
 *
 * Called at device open time, sets up the structure for handling refcounting
 * of sync objects.
 */
void
drm_syncobj_open(struct drm_file *file_private)
{
	idr_init_base(&file_private->syncobj_idr, 1);
	spin_lock_init(&file_private->syncobj_table_lock);
}

static int
drm_syncobj_release_handle(int id, void *ptr, void *data)
{
	struct drm_syncobj *syncobj = ptr;

	drm_syncobj_put(syncobj);
	return 0;
}

/**
 * drm_syncobj_release - release file-private sync object resources
 * @file_private: drm file-private structure to clean up
 *
 * Called at close time when the filp is going away.
 *
 * Releases any remaining references on objects by this filp.
 */
void
drm_syncobj_release(struct drm_file *file_private)
{
	idr_for_each(&file_private->syncobj_idr,
		     &drm_syncobj_release_handle, file_private);
	idr_destroy(&file_private->syncobj_idr);
}

int
drm_syncobj_create_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_private)
{
	struct drm_syncobj_create *args = data;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		return -EOPNOTSUPP;

	/* no valid flags yet */
	if (args->flags & ~DRM_SYNCOBJ_CREATE_SIGNALED)
		return -EINVAL;

	return drm_syncobj_create_as_handle(file_private,
					    &args->handle, args->flags);
}

int
drm_syncobj_destroy_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_private)
{
	struct drm_syncobj_destroy *args = data;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		return -EOPNOTSUPP;

	/* make sure padding is empty */
	if (args->pad)
		return -EINVAL;
	return drm_syncobj_destroy(file_private, args->handle);
}

int
drm_syncobj_handle_to_fd_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file_private)
{
	struct drm_syncobj_handle *args = data;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		return -EOPNOTSUPP;

	if (args->pad)
		return -EINVAL;

	if (args->flags != 0 &&
	    args->flags != DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE)
		return -EINVAL;

	if (args->flags & DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE)
		return drm_syncobj_export_sync_file(file_private, args->handle,
						    &args->fd);

	return drm_syncobj_handle_to_fd(file_private, args->handle,
					&args->fd);
}

int
drm_syncobj_fd_to_handle_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file_private)
{
	struct drm_syncobj_handle *args = data;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		return -EOPNOTSUPP;

	if (args->pad)
		return -EINVAL;

	if (args->flags != 0 &&
	    args->flags != DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE)
		return -EINVAL;

	if (args->flags & DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE)
		return drm_syncobj_import_sync_file_fence(file_private,
							  args->fd,
							  args->handle);

	return drm_syncobj_fd_to_handle(file_private, args->fd,
					&args->handle);
}

static int drm_syncobj_transfer_to_timeline(struct drm_file *file_private,
					    struct drm_syncobj_transfer *args)
{
	struct drm_syncobj *timeline_syncobj = NULL;
	struct dma_fence *fence, *tmp;
	struct dma_fence_chain *chain;
	int ret;

	timeline_syncobj = drm_syncobj_find(file_private, args->dst_handle);
	if (!timeline_syncobj) {
		return -ENOENT;
	}
	ret = drm_syncobj_find_fence(file_private, args->src_handle,
				     args->src_point, args->flags,
				     &tmp);
	if (ret)
		goto err_put_timeline;

	fence = dma_fence_unwrap_merge(tmp);
	dma_fence_put(tmp);
	if (!fence) {
		ret = -ENOMEM;
		goto err_put_timeline;
	}

	chain = dma_fence_chain_alloc();
	if (!chain) {
		ret = -ENOMEM;
		goto err_free_fence;
	}

	drm_syncobj_add_point(timeline_syncobj, chain, fence, args->dst_point);
err_free_fence:
	dma_fence_put(fence);
err_put_timeline:
	drm_syncobj_put(timeline_syncobj);

	return ret;
}

static int
drm_syncobj_transfer_to_binary(struct drm_file *file_private,
			       struct drm_syncobj_transfer *args)
{
	struct drm_syncobj *binary_syncobj = NULL;
	struct dma_fence *fence;
	int ret;

	binary_syncobj = drm_syncobj_find(file_private, args->dst_handle);
	if (!binary_syncobj)
		return -ENOENT;
	ret = drm_syncobj_find_fence(file_private, args->src_handle,
				     args->src_point, args->flags, &fence);
	if (ret)
		goto err;
	drm_syncobj_replace_fence(binary_syncobj, fence);
	dma_fence_put(fence);
err:
	drm_syncobj_put(binary_syncobj);

	return ret;
}
int
drm_syncobj_transfer_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_private)
{
	struct drm_syncobj_transfer *args = data;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ_TIMELINE))
		return -EOPNOTSUPP;

	if (args->pad)
		return -EINVAL;

	if (args->dst_point)
		ret = drm_syncobj_transfer_to_timeline(file_private, args);
	else
		ret = drm_syncobj_transfer_to_binary(file_private, args);

	return ret;
}

static void syncobj_wait_fence_func(struct dma_fence *fence,
				    struct dma_fence_cb *cb)
{
	struct syncobj_wait_entry *wait =
		container_of(cb, struct syncobj_wait_entry, fence_cb);

	wake_up_process(wait->task);
}

static void syncobj_wait_syncobj_func(struct drm_syncobj *syncobj,
				      struct syncobj_wait_entry *wait)
{
	struct dma_fence *fence;

	/* This happens inside the syncobj lock */
	fence = rcu_dereference_protected(syncobj->fence,
					  lockdep_is_held(&syncobj->lock));
	dma_fence_get(fence);
	if (!fence || dma_fence_chain_find_seqno(&fence, wait->point)) {
		dma_fence_put(fence);
		return;
	} else if (!fence) {
		wait->fence = dma_fence_get_stub();
	} else {
		wait->fence = fence;
	}

	wake_up_process(wait->task);
	list_del_init(&wait->node);
}

static signed long drm_syncobj_array_wait_timeout(struct drm_syncobj **syncobjs,
						  void __user *user_points,
						  uint32_t count,
						  uint32_t flags,
						  signed long timeout,
						  uint32_t *idx)
{
	struct syncobj_wait_entry *entries;
	struct dma_fence *fence;
	uint64_t *points;
	uint32_t signaled_count, i;

	if (flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT)
		lockdep_assert_none_held_once();

	points = kmalloc_array(count, sizeof(*points), GFP_KERNEL);
	if (points == NULL)
		return -ENOMEM;

	if (!user_points) {
		memset(points, 0, count * sizeof(uint64_t));

	} else if (copy_from_user(points, user_points,
				  sizeof(uint64_t) * count)) {
		timeout = -EFAULT;
		goto err_free_points;
	}

	entries = kcalloc(count, sizeof(*entries), GFP_KERNEL);
	if (!entries) {
		timeout = -ENOMEM;
		goto err_free_points;
	}
	/* Walk the list of sync objects and initialize entries.  We do
	 * this up-front so that we can properly return -EINVAL if there is
	 * a syncobj with a missing fence and then never have the chance of
	 * returning -EINVAL again.
	 */
	signaled_count = 0;
	for (i = 0; i < count; ++i) {
		struct dma_fence *fence;

		entries[i].task = current;
		entries[i].point = points[i];
		fence = drm_syncobj_fence_get(syncobjs[i]);
		if (!fence || dma_fence_chain_find_seqno(&fence, points[i])) {
			dma_fence_put(fence);
			if (flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT) {
				continue;
			} else {
				timeout = -EINVAL;
				goto cleanup_entries;
			}
		}

		if (fence)
			entries[i].fence = fence;
		else
			entries[i].fence = dma_fence_get_stub();

		if ((flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE) ||
		    dma_fence_is_signaled(entries[i].fence)) {
			if (signaled_count == 0 && idx)
				*idx = i;
			signaled_count++;
		}
	}

	if (signaled_count == count ||
	    (signaled_count > 0 &&
	     !(flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL)))
		goto cleanup_entries;

	/* There's a very annoying laxness in the dma_fence API here, in
	 * that backends are not required to automatically report when a
	 * fence is signaled prior to fence->ops->enable_signaling() being
	 * called.  So here if we fail to match signaled_count, we need to
	 * fallthough and try a 0 timeout wait!
	 */

	if (flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT) {
		for (i = 0; i < count; ++i)
			drm_syncobj_fence_add_wait(syncobjs[i], &entries[i]);
	}

	do {
		set_current_state(TASK_INTERRUPTIBLE);

		signaled_count = 0;
		for (i = 0; i < count; ++i) {
			fence = entries[i].fence;
			if (!fence)
				continue;

			if ((flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE) ||
			    dma_fence_is_signaled(fence) ||
			    (!entries[i].fence_cb.func &&
			     dma_fence_add_callback(fence,
						    &entries[i].fence_cb,
						    syncobj_wait_fence_func))) {
				/* The fence has been signaled */
				if (flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL) {
					signaled_count++;
				} else {
					if (idx)
						*idx = i;
					goto done_waiting;
				}
			}
		}

		if (signaled_count == count)
			goto done_waiting;

		if (timeout == 0) {
			timeout = -ETIME;
			goto done_waiting;
		}

		if (signal_pending(current)) {
			timeout = -ERESTARTSYS;
			goto done_waiting;
		}

		timeout = schedule_timeout(timeout);
	} while (1);

done_waiting:
	__set_current_state(TASK_RUNNING);

cleanup_entries:
	for (i = 0; i < count; ++i) {
		drm_syncobj_remove_wait(syncobjs[i], &entries[i]);
		if (entries[i].fence_cb.func)
			dma_fence_remove_callback(entries[i].fence,
						  &entries[i].fence_cb);
		dma_fence_put(entries[i].fence);
	}
	kfree(entries);

err_free_points:
	kfree(points);

	return timeout;
}

/**
 * drm_timeout_abs_to_jiffies - calculate jiffies timeout from absolute value
 *
 * @timeout_nsec: timeout nsec component in ns, 0 for poll
 *
 * Calculate the timeout in jiffies from an absolute time in sec/nsec.
 */
signed long drm_timeout_abs_to_jiffies(int64_t timeout_nsec)
{
	ktime_t abs_timeout, now;
	u64 timeout_ns, timeout_jiffies64;

	/* make 0 timeout means poll - absolute 0 doesn't seem valid */
	if (timeout_nsec == 0)
		return 0;

	abs_timeout = ns_to_ktime(timeout_nsec);
	now = ktime_get();

	if (!ktime_after(abs_timeout, now))
		return 0;

	timeout_ns = ktime_to_ns(ktime_sub(abs_timeout, now));

	timeout_jiffies64 = nsecs_to_jiffies64(timeout_ns);
	/*  clamp timeout to avoid infinite timeout */
	if (timeout_jiffies64 >= MAX_SCHEDULE_TIMEOUT - 1)
		return MAX_SCHEDULE_TIMEOUT - 1;

	return timeout_jiffies64 + 1;
}
EXPORT_SYMBOL(drm_timeout_abs_to_jiffies);

static int drm_syncobj_array_wait(struct drm_device *dev,
				  struct drm_file *file_private,
				  struct drm_syncobj_wait *wait,
				  struct drm_syncobj_timeline_wait *timeline_wait,
				  struct drm_syncobj **syncobjs, bool timeline)
{
	signed long timeout = 0;
	uint32_t first = ~0;

	if (!timeline) {
		timeout = drm_timeout_abs_to_jiffies(wait->timeout_nsec);
		timeout = drm_syncobj_array_wait_timeout(syncobjs,
							 NULL,
							 wait->count_handles,
							 wait->flags,
							 timeout, &first);
		if (timeout < 0)
			return timeout;
		wait->first_signaled = first;
	} else {
		timeout = drm_timeout_abs_to_jiffies(timeline_wait->timeout_nsec);
		timeout = drm_syncobj_array_wait_timeout(syncobjs,
							 u64_to_user_ptr(timeline_wait->points),
							 timeline_wait->count_handles,
							 timeline_wait->flags,
							 timeout, &first);
		if (timeout < 0)
			return timeout;
		timeline_wait->first_signaled = first;
	}
	return 0;
}

static int drm_syncobj_array_find(struct drm_file *file_private,
				  void __user *user_handles,
				  uint32_t count_handles,
				  struct drm_syncobj ***syncobjs_out)
{
	uint32_t i, *handles;
	struct drm_syncobj **syncobjs;
	int ret;

	handles = kmalloc_array(count_handles, sizeof(*handles), GFP_KERNEL);
	if (handles == NULL)
		return -ENOMEM;

	if (copy_from_user(handles, user_handles,
			   sizeof(uint32_t) * count_handles)) {
		ret = -EFAULT;
		goto err_free_handles;
	}

	syncobjs = kmalloc_array(count_handles, sizeof(*syncobjs), GFP_KERNEL);
	if (syncobjs == NULL) {
		ret = -ENOMEM;
		goto err_free_handles;
	}

	for (i = 0; i < count_handles; i++) {
		syncobjs[i] = drm_syncobj_find(file_private, handles[i]);
		if (!syncobjs[i]) {
			ret = -ENOENT;
			goto err_put_syncobjs;
		}
	}

	kfree(handles);
	*syncobjs_out = syncobjs;
	return 0;

err_put_syncobjs:
	while (i-- > 0)
		drm_syncobj_put(syncobjs[i]);
	kfree(syncobjs);
err_free_handles:
	kfree(handles);

	return ret;
}

static void drm_syncobj_array_free(struct drm_syncobj **syncobjs,
				   uint32_t count)
{
	uint32_t i;

	for (i = 0; i < count; i++)
		drm_syncobj_put(syncobjs[i]);
	kfree(syncobjs);
}

int
drm_syncobj_wait_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_private)
{
	struct drm_syncobj_wait *args = data;
	struct drm_syncobj **syncobjs;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		return -EOPNOTSUPP;

	if (args->flags & ~(DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
			    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT))
		return -EINVAL;

	if (args->count_handles == 0)
		return -EINVAL;

	ret = drm_syncobj_array_find(file_private,
				     u64_to_user_ptr(args->handles),
				     args->count_handles,
				     &syncobjs);
	if (ret < 0)
		return ret;

	ret = drm_syncobj_array_wait(dev, file_private,
				     args, NULL, syncobjs, false);

	drm_syncobj_array_free(syncobjs, args->count_handles);

	return ret;
}

int
drm_syncobj_timeline_wait_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_private)
{
	struct drm_syncobj_timeline_wait *args = data;
	struct drm_syncobj **syncobjs;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ_TIMELINE))
		return -EOPNOTSUPP;

	if (args->flags & ~(DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
			    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT |
			    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE))
		return -EINVAL;

	if (args->count_handles == 0)
		return -EINVAL;

	ret = drm_syncobj_array_find(file_private,
				     u64_to_user_ptr(args->handles),
				     args->count_handles,
				     &syncobjs);
	if (ret < 0)
		return ret;

	ret = drm_syncobj_array_wait(dev, file_private,
				     NULL, args, syncobjs, true);

	drm_syncobj_array_free(syncobjs, args->count_handles);

	return ret;
}


int
drm_syncobj_reset_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_private)
{
	struct drm_syncobj_array *args = data;
	struct drm_syncobj **syncobjs;
	uint32_t i;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		return -EOPNOTSUPP;

	if (args->pad != 0)
		return -EINVAL;

	if (args->count_handles == 0)
		return -EINVAL;

	ret = drm_syncobj_array_find(file_private,
				     u64_to_user_ptr(args->handles),
				     args->count_handles,
				     &syncobjs);
	if (ret < 0)
		return ret;

	for (i = 0; i < args->count_handles; i++)
		drm_syncobj_replace_fence(syncobjs[i], NULL);

	drm_syncobj_array_free(syncobjs, args->count_handles);

	return 0;
}

int
drm_syncobj_signal_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_private)
{
	struct drm_syncobj_array *args = data;
	struct drm_syncobj **syncobjs;
	uint32_t i;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		return -EOPNOTSUPP;

	if (args->pad != 0)
		return -EINVAL;

	if (args->count_handles == 0)
		return -EINVAL;

	ret = drm_syncobj_array_find(file_private,
				     u64_to_user_ptr(args->handles),
				     args->count_handles,
				     &syncobjs);
	if (ret < 0)
		return ret;

	for (i = 0; i < args->count_handles; i++) {
		ret = drm_syncobj_assign_null_handle(syncobjs[i]);
		if (ret < 0)
			break;
	}

	drm_syncobj_array_free(syncobjs, args->count_handles);

	return ret;
}

int
drm_syncobj_timeline_signal_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_private)
{
	struct drm_syncobj_timeline_array *args = data;
	struct drm_syncobj **syncobjs;
	struct dma_fence_chain **chains;
	uint64_t *points;
	uint32_t i, j;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ_TIMELINE))
		return -EOPNOTSUPP;

	if (args->flags != 0)
		return -EINVAL;

	if (args->count_handles == 0)
		return -EINVAL;

	ret = drm_syncobj_array_find(file_private,
				     u64_to_user_ptr(args->handles),
				     args->count_handles,
				     &syncobjs);
	if (ret < 0)
		return ret;

	points = kmalloc_array(args->count_handles, sizeof(*points),
			       GFP_KERNEL);
	if (!points) {
		ret = -ENOMEM;
		goto out;
	}
	if (!u64_to_user_ptr(args->points)) {
		memset(points, 0, args->count_handles * sizeof(uint64_t));
	} else if (copy_from_user(points, u64_to_user_ptr(args->points),
				  sizeof(uint64_t) * args->count_handles)) {
		ret = -EFAULT;
		goto err_points;
	}

	chains = kmalloc_array(args->count_handles, sizeof(void *), GFP_KERNEL);
	if (!chains) {
		ret = -ENOMEM;
		goto err_points;
	}
	for (i = 0; i < args->count_handles; i++) {
		chains[i] = dma_fence_chain_alloc();
		if (!chains[i]) {
			for (j = 0; j < i; j++)
				dma_fence_chain_free(chains[j]);
			ret = -ENOMEM;
			goto err_chains;
		}
	}

	for (i = 0; i < args->count_handles; i++) {
		struct dma_fence *fence = dma_fence_get_stub();

		drm_syncobj_add_point(syncobjs[i], chains[i],
				      fence, points[i]);
		dma_fence_put(fence);
	}
err_chains:
	kfree(chains);
err_points:
	kfree(points);
out:
	drm_syncobj_array_free(syncobjs, args->count_handles);

	return ret;
}

int drm_syncobj_query_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_private)
{
	struct drm_syncobj_timeline_array *args = data;
	struct drm_syncobj **syncobjs;
	uint64_t __user *points = u64_to_user_ptr(args->points);
	uint32_t i;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ_TIMELINE))
		return -EOPNOTSUPP;

	if (args->flags & ~DRM_SYNCOBJ_QUERY_FLAGS_LAST_SUBMITTED)
		return -EINVAL;

	if (args->count_handles == 0)
		return -EINVAL;

	ret = drm_syncobj_array_find(file_private,
				     u64_to_user_ptr(args->handles),
				     args->count_handles,
				     &syncobjs);
	if (ret < 0)
		return ret;

	for (i = 0; i < args->count_handles; i++) {
		struct dma_fence_chain *chain;
		struct dma_fence *fence;
		uint64_t point;

		fence = drm_syncobj_fence_get(syncobjs[i]);
		chain = to_dma_fence_chain(fence);
		if (chain) {
			struct dma_fence *iter, *last_signaled =
				dma_fence_get(fence);

			if (args->flags &
			    DRM_SYNCOBJ_QUERY_FLAGS_LAST_SUBMITTED) {
				point = fence->seqno;
			} else {
				dma_fence_chain_for_each(iter, fence) {
					if (iter->context != fence->context) {
						dma_fence_put(iter);
						/* It is most likely that timeline has
						* unorder points. */
						break;
					}
					dma_fence_put(last_signaled);
					last_signaled = dma_fence_get(iter);
				}
				point = dma_fence_is_signaled(last_signaled) ?
					last_signaled->seqno :
					to_dma_fence_chain(last_signaled)->prev_seqno;
			}
			dma_fence_put(last_signaled);
		} else {
			point = 0;
		}
		dma_fence_put(fence);
		ret = copy_to_user(&points[i], &point, sizeof(uint64_t));
		ret = ret ? -EFAULT : 0;
		if (ret)
			break;
	}
	drm_syncobj_array_free(syncobjs, args->count_handles);

	return ret;
}

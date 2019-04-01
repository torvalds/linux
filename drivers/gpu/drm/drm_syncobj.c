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
 * DRM synchronisation objects (syncobj, see struct &drm_syncobj) are
 * persistent objects that contain an optional fence. The fence can be updated
 * with a new fence, or be NULL.
 *
 * syncobj's can be waited upon, where it will wait for the underlying
 * fence.
 *
 * syncobj's can be export to fd's and back, these fd's are opaque and
 * have no other use case, except passing the syncobj between processes.
 *
 * Their primary use-case is to implement Vulkan fences and semaphores.
 *
 * syncobj have a kref reference count, but also have an optional file.
 * The file is only created once the syncobj is exported.
 * The file takes a reference on the kref.
 */

#include <drm/drmP.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/sync_file.h>
#include <linux/sched/signal.h>

#include "drm_internal.h"
#include <drm/drm_syncobj.h>

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
		DRM_ERROR("You are adding an unorder point to timeline!\n");
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
static void drm_syncobj_assign_null_handle(struct drm_syncobj *syncobj)
{
	struct dma_fence *fence = dma_fence_get_stub();

	drm_syncobj_replace_fence(syncobj, fence);
	dma_fence_put(fence);
}

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
	int ret = 0;

	if (!syncobj)
		return -ENOENT;

	*fence = drm_syncobj_fence_get(syncobj);
	if (!*fence) {
		ret = -EINVAL;
	}
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
	struct drm_syncobj *syncobj;

	syncobj = kzalloc(sizeof(struct drm_syncobj), GFP_KERNEL);
	if (!syncobj)
		return -ENOMEM;

	kref_init(&syncobj->refcount);
	INIT_LIST_HEAD(&syncobj->cb_list);
	spin_lock_init(&syncobj->lock);

	if (flags & DRM_SYNCOBJ_CREATE_SIGNALED)
		drm_syncobj_assign_null_handle(syncobj);

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
	struct file *file;
	int ret;

	file = fget(fd);
	if (!file)
		return -EINVAL;

	if (file->f_op != &drm_syncobj_file_fops) {
		fput(file);
		return -EINVAL;
	}

	/* take a reference to put in the idr */
	syncobj = file->private_data;
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

	fput(file);
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
 * drm_syncobj_open - initalizes syncobj file-private structures at devnode open time
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

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		return -ENODEV;

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

	for (i = 0; i < args->count_handles; i++)
		drm_syncobj_assign_null_handle(syncobjs[i]);

	drm_syncobj_array_free(syncobjs, args->count_handles);

	return ret;
}

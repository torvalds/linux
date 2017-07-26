/*
 * Copyright 2017 Red Hat
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
 * DRM synchronisation objects (syncobj) are a persistent objects,
 * that contain an optional fence. The fence can be updated with a new
 * fence, or be NULL.
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

#include "drm_internal.h"
#include <drm/drm_syncobj.h>

/**
 * drm_syncobj_find - lookup and reference a sync object.
 * @file_private: drm file private pointer
 * @handle: sync object handle to lookup.
 *
 * Returns a reference to the syncobj pointed to by handle or NULL.
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

	if (fence)
		dma_fence_get(fence);
	old_fence = xchg(&syncobj->fence, fence);

	dma_fence_put(old_fence);
}
EXPORT_SYMBOL(drm_syncobj_replace_fence);

int drm_syncobj_fence_get(struct drm_file *file_private,
			  u32 handle,
			  struct dma_fence **fence)
{
	struct drm_syncobj *syncobj = drm_syncobj_find(file_private, handle);
	int ret = 0;

	if (!syncobj)
		return -ENOENT;

	*fence = dma_fence_get(syncobj->fence);
	if (!*fence) {
		ret = -EINVAL;
	}
	drm_syncobj_put(syncobj);
	return ret;
}
EXPORT_SYMBOL(drm_syncobj_fence_get);

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
	dma_fence_put(syncobj->fence);
	kfree(syncobj);
}
EXPORT_SYMBOL(drm_syncobj_free);

static int drm_syncobj_create(struct drm_file *file_private,
			      u32 *handle)
{
	int ret;
	struct drm_syncobj *syncobj;

	syncobj = kzalloc(sizeof(struct drm_syncobj), GFP_KERNEL);
	if (!syncobj)
		return -ENOMEM;

	kref_init(&syncobj->refcount);

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

static int drm_syncobj_alloc_file(struct drm_syncobj *syncobj)
{
	struct file *file = anon_inode_getfile("syncobj_file",
					       &drm_syncobj_file_fops,
					       syncobj, 0);
	if (IS_ERR(file))
		return PTR_ERR(file);

	drm_syncobj_get(syncobj);
	if (cmpxchg(&syncobj->file, NULL, file)) {
		/* lost the race */
		fput(file);
	}

	return 0;
}

static int drm_syncobj_handle_to_fd(struct drm_file *file_private,
				    u32 handle, int *p_fd)
{
	struct drm_syncobj *syncobj = drm_syncobj_find(file_private, handle);
	int ret;
	int fd;

	if (!syncobj)
		return -EINVAL;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		drm_syncobj_put(syncobj);
		return fd;
	}

	if (!syncobj->file) {
		ret = drm_syncobj_alloc_file(syncobj);
		if (ret)
			goto out_put_fd;
	}
	fd_install(fd, syncobj->file);
	drm_syncobj_put(syncobj);
	*p_fd = fd;
	return 0;
out_put_fd:
	put_unused_fd(fd);
	drm_syncobj_put(syncobj);
	return ret;
}

static struct drm_syncobj *drm_syncobj_fdget(int fd)
{
	struct file *file = fget(fd);

	if (!file)
		return NULL;
	if (file->f_op != &drm_syncobj_file_fops)
		goto err;

	return file->private_data;
err:
	fput(file);
	return NULL;
};

static int drm_syncobj_fd_to_handle(struct drm_file *file_private,
				    int fd, u32 *handle)
{
	struct drm_syncobj *syncobj = drm_syncobj_fdget(fd);
	int ret;

	if (!syncobj)
		return -EINVAL;

	/* take a reference to put in the idr */
	drm_syncobj_get(syncobj);

	idr_preload(GFP_KERNEL);
	spin_lock(&file_private->syncobj_table_lock);
	ret = idr_alloc(&file_private->syncobj_idr, syncobj, 1, 0, GFP_NOWAIT);
	spin_unlock(&file_private->syncobj_table_lock);
	idr_preload_end();

	if (ret < 0) {
		fput(syncobj->file);
		return ret;
	}
	*handle = ret;
	return 0;
}

int drm_syncobj_import_sync_file_fence(struct drm_file *file_private,
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

int drm_syncobj_export_sync_file(struct drm_file *file_private,
				 int handle, int *p_fd)
{
	int ret;
	struct dma_fence *fence;
	struct sync_file *sync_file;
	int fd = get_unused_fd_flags(O_CLOEXEC);

	if (fd < 0)
		return fd;

	ret = drm_syncobj_fence_get(file_private, handle, &fence);
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
	idr_init(&file_private->syncobj_idr);
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
		return -ENODEV;

	/* no valid flags yet */
	if (args->flags)
		return -EINVAL;

	return drm_syncobj_create(file_private,
				  &args->handle);
}

int
drm_syncobj_destroy_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_private)
{
	struct drm_syncobj_destroy *args = data;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		return -ENODEV;

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
		return -ENODEV;

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
		return -ENODEV;

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

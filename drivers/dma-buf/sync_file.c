// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/dma-buf/sync_file.c
 *
 * Copyright (C) 2012 Google, Inc.
 */

#include <linux/dma-fence-unwrap.h>
#include <linux/export.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/anon_inodes.h>
#include <linux/sync_file.h>
#include <uapi/linux/sync_file.h>

static const struct file_operations sync_file_fops;

static struct sync_file *sync_file_alloc(void)
{
	struct sync_file *sync_file;

	sync_file = kzalloc(sizeof(*sync_file), GFP_KERNEL);
	if (!sync_file)
		return NULL;

	sync_file->file = anon_inode_getfile("sync_file", &sync_file_fops,
					     sync_file, 0);
	if (IS_ERR(sync_file->file))
		goto err;

	init_waitqueue_head(&sync_file->wq);

	INIT_LIST_HEAD(&sync_file->cb.node);

	return sync_file;

err:
	kfree(sync_file);
	return NULL;
}

static void fence_check_cb_func(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct sync_file *sync_file;

	sync_file = container_of(cb, struct sync_file, cb);

	wake_up_all(&sync_file->wq);
}

/**
 * sync_file_create() - creates a sync file
 * @fence:	fence to add to the sync_fence
 *
 * Creates a sync_file containg @fence. This function acquires and additional
 * reference of @fence for the newly-created &sync_file, if it succeeds. The
 * sync_file can be released with fput(sync_file->file). Returns the
 * sync_file or NULL in case of error.
 */
struct sync_file *sync_file_create(struct dma_fence *fence)
{
	struct sync_file *sync_file;

	sync_file = sync_file_alloc();
	if (!sync_file)
		return NULL;

	sync_file->fence = dma_fence_get(fence);

	return sync_file;
}
EXPORT_SYMBOL(sync_file_create);

static struct sync_file *sync_file_fdget(int fd)
{
	struct file *file = fget(fd);

	if (!file)
		return NULL;

	if (file->f_op != &sync_file_fops)
		goto err;

	return file->private_data;

err:
	fput(file);
	return NULL;
}

/**
 * sync_file_get_fence - get the fence related to the sync_file fd
 * @fd:		sync_file fd to get the fence from
 *
 * Ensures @fd references a valid sync_file and returns a fence that
 * represents all fence in the sync_file. On error NULL is returned.
 */
struct dma_fence *sync_file_get_fence(int fd)
{
	struct sync_file *sync_file;
	struct dma_fence *fence;

	sync_file = sync_file_fdget(fd);
	if (!sync_file)
		return NULL;

	fence = dma_fence_get(sync_file->fence);
	fput(sync_file->file);

	return fence;
}
EXPORT_SYMBOL(sync_file_get_fence);

/**
 * sync_file_get_name - get the name of the sync_file
 * @sync_file:		sync_file to get the fence from
 * @buf:		destination buffer to copy sync_file name into
 * @len:		available size of destination buffer.
 *
 * Each sync_file may have a name assigned either by the user (when merging
 * sync_files together) or created from the fence it contains. In the latter
 * case construction of the name is deferred until use, and so requires
 * sync_file_get_name().
 *
 * Returns: a string representing the name.
 */
char *sync_file_get_name(struct sync_file *sync_file, char *buf, int len)
{
	if (sync_file->user_name[0]) {
		strlcpy(buf, sync_file->user_name, len);
	} else {
		struct dma_fence *fence = sync_file->fence;

		snprintf(buf, len, "%s-%s%llu-%lld",
			 fence->ops->get_driver_name(fence),
			 fence->ops->get_timeline_name(fence),
			 fence->context,
			 fence->seqno);
	}

	return buf;
}

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
static struct sync_file *sync_file_merge(const char *name, struct sync_file *a,
					 struct sync_file *b)
{
	struct sync_file *sync_file;
	struct dma_fence *fence;

	sync_file = sync_file_alloc();
	if (!sync_file)
		return NULL;

	fence = dma_fence_unwrap_merge(a->fence, b->fence);
	if (!fence) {
		fput(sync_file->file);
		return NULL;
	}
	sync_file->fence = fence;
	strlcpy(sync_file->user_name, name, sizeof(sync_file->user_name));
	return sync_file;
}

static int sync_file_release(struct inode *inode, struct file *file)
{
	struct sync_file *sync_file = file->private_data;

	if (test_bit(POLL_ENABLED, &sync_file->flags))
		dma_fence_remove_callback(sync_file->fence, &sync_file->cb);
	dma_fence_put(sync_file->fence);
	kfree(sync_file);

	return 0;
}

static __poll_t sync_file_poll(struct file *file, poll_table *wait)
{
	struct sync_file *sync_file = file->private_data;

	poll_wait(file, &sync_file->wq, wait);

	if (list_empty(&sync_file->cb.node) &&
	    !test_and_set_bit(POLL_ENABLED, &sync_file->flags)) {
		if (dma_fence_add_callback(sync_file->fence, &sync_file->cb,
					   fence_check_cb_func) < 0)
			wake_up_all(&sync_file->wq);
	}

	return dma_fence_is_signaled(sync_file->fence) ? EPOLLIN : 0;
}

static long sync_file_ioctl_merge(struct sync_file *sync_file,
				  unsigned long arg)
{
	int fd = get_unused_fd_flags(O_CLOEXEC);
	int err;
	struct sync_file *fence2, *fence3;
	struct sync_merge_data data;

	if (fd < 0)
		return fd;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		err = -EFAULT;
		goto err_put_fd;
	}

	if (data.flags || data.pad) {
		err = -EINVAL;
		goto err_put_fd;
	}

	fence2 = sync_file_fdget(data.fd2);
	if (!fence2) {
		err = -ENOENT;
		goto err_put_fd;
	}

	data.name[sizeof(data.name) - 1] = '\0';
	fence3 = sync_file_merge(data.name, sync_file, fence2);
	if (!fence3) {
		err = -ENOMEM;
		goto err_put_fence2;
	}

	data.fence = fd;
	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		err = -EFAULT;
		goto err_put_fence3;
	}

	fd_install(fd, fence3->file);
	fput(fence2->file);
	return 0;

err_put_fence3:
	fput(fence3->file);

err_put_fence2:
	fput(fence2->file);

err_put_fd:
	put_unused_fd(fd);
	return err;
}

static int sync_fill_fence_info(struct dma_fence *fence,
				 struct sync_fence_info *info)
{
	strlcpy(info->obj_name, fence->ops->get_timeline_name(fence),
		sizeof(info->obj_name));
	strlcpy(info->driver_name, fence->ops->get_driver_name(fence),
		sizeof(info->driver_name));

	info->status = dma_fence_get_status(fence);
	while (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags) &&
	       !test_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags))
		cpu_relax();
	info->timestamp_ns =
		test_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags) ?
		ktime_to_ns(fence->timestamp) :
		ktime_set(0, 0);

	return info->status;
}

static long sync_file_ioctl_fence_info(struct sync_file *sync_file,
				       unsigned long arg)
{
	struct sync_fence_info *fence_info = NULL;
	struct dma_fence_unwrap iter;
	struct sync_file_info info;
	unsigned int num_fences;
	struct dma_fence *fence;
	int ret;
	__u32 size;

	if (copy_from_user(&info, (void __user *)arg, sizeof(info)))
		return -EFAULT;

	if (info.flags || info.pad)
		return -EINVAL;

	num_fences = 0;
	dma_fence_unwrap_for_each(fence, &iter, sync_file->fence)
		++num_fences;

	/*
	 * Passing num_fences = 0 means that userspace doesn't want to
	 * retrieve any sync_fence_info. If num_fences = 0 we skip filling
	 * sync_fence_info and return the actual number of fences on
	 * info->num_fences.
	 */
	if (!info.num_fences) {
		info.status = dma_fence_get_status(sync_file->fence);
		goto no_fences;
	} else {
		info.status = 1;
	}

	if (info.num_fences < num_fences)
		return -EINVAL;

	size = num_fences * sizeof(*fence_info);
	fence_info = kzalloc(size, GFP_KERNEL);
	if (!fence_info)
		return -ENOMEM;

	num_fences = 0;
	dma_fence_unwrap_for_each(fence, &iter, sync_file->fence) {
		int status;

		status = sync_fill_fence_info(fence, &fence_info[num_fences++]);
		info.status = info.status <= 0 ? info.status : status;
	}

	if (copy_to_user(u64_to_user_ptr(info.sync_fence_info), fence_info,
			 size)) {
		ret = -EFAULT;
		goto out;
	}

no_fences:
	sync_file_get_name(sync_file, info.name, sizeof(info.name));
	info.num_fences = num_fences;

	if (copy_to_user((void __user *)arg, &info, sizeof(info)))
		ret = -EFAULT;
	else
		ret = 0;

out:
	kfree(fence_info);

	return ret;
}

static long sync_file_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct sync_file *sync_file = file->private_data;

	switch (cmd) {
	case SYNC_IOC_MERGE:
		return sync_file_ioctl_merge(sync_file, arg);

	case SYNC_IOC_FILE_INFO:
		return sync_file_ioctl_fence_info(sync_file, arg);

	default:
		return -ENOTTY;
	}
}

static const struct file_operations sync_file_fops = {
	.release = sync_file_release,
	.poll = sync_file_poll,
	.unlocked_ioctl = sync_file_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

/*
 * drivers/dma-buf/sync_file.c
 *
 * Copyright (C) 2012 Google, Inc.
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

static struct sync_file *sync_file_alloc(int size)
{
	struct sync_file *sync_file;

	sync_file = kzalloc(size, GFP_KERNEL);
	if (!sync_file)
		return NULL;

	sync_file->file = anon_inode_getfile("sync_file", &sync_file_fops,
					     sync_file, 0);
	if (IS_ERR(sync_file->file))
		goto err;

	kref_init(&sync_file->kref);

	init_waitqueue_head(&sync_file->wq);

	return sync_file;

err:
	kfree(sync_file);
	return NULL;
}

static void fence_check_cb_func(struct fence *f, struct fence_cb *cb)
{
	struct sync_file_cb *check;
	struct sync_file *sync_file;

	check = container_of(cb, struct sync_file_cb, cb);
	sync_file = check->sync_file;

	if (atomic_dec_and_test(&sync_file->status))
		wake_up_all(&sync_file->wq);
}

/**
 * sync_file_create() - creates a sync file
 * @fence:	fence to add to the sync_fence
 *
 * Creates a sync_file containg @fence. Once this is called, the sync_file
 * takes ownership of @fence. The sync_file can be released with
 * fput(sync_file->file). Returns the sync_file or NULL in case of error.
 */
struct sync_file *sync_file_create(struct fence *fence)
{
	struct sync_file *sync_file;

	sync_file = sync_file_alloc(offsetof(struct sync_file, cbs[1]));
	if (!sync_file)
		return NULL;

	sync_file->num_fences = 1;
	atomic_set(&sync_file->status, 1);
	snprintf(sync_file->name, sizeof(sync_file->name), "%s-%s%llu-%d",
		 fence->ops->get_driver_name(fence),
		 fence->ops->get_timeline_name(fence), fence->context,
		 fence->seqno);

	sync_file->cbs[0].fence = fence;
	sync_file->cbs[0].sync_file = sync_file;
	if (fence_add_callback(fence, &sync_file->cbs[0].cb,
			       fence_check_cb_func))
		atomic_dec(&sync_file->status);

	return sync_file;
}
EXPORT_SYMBOL(sync_file_create);

/**
 * sync_file_fdget() - get a sync_file from an fd
 * @fd:		fd referencing a fence
 *
 * Ensures @fd references a valid sync_file, increments the refcount of the
 * backing file. Returns the sync_file or NULL in case of error.
 */
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

static void sync_file_add_pt(struct sync_file *sync_file, int *i,
			     struct fence *fence)
{
	sync_file->cbs[*i].fence = fence;
	sync_file->cbs[*i].sync_file = sync_file;

	if (!fence_add_callback(fence, &sync_file->cbs[*i].cb,
				fence_check_cb_func)) {
		fence_get(fence);
		(*i)++;
	}
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
	int num_fences = a->num_fences + b->num_fences;
	struct sync_file *sync_file;
	int i, i_a, i_b;
	unsigned long size = offsetof(struct sync_file, cbs[num_fences]);

	sync_file = sync_file_alloc(size);
	if (!sync_file)
		return NULL;

	atomic_set(&sync_file->status, num_fences);

	/*
	 * Assume sync_file a and b are both ordered and have no
	 * duplicates with the same context.
	 *
	 * If a sync_file can only be created with sync_file_merge
	 * and sync_file_create, this is a reasonable assumption.
	 */
	for (i = i_a = i_b = 0; i_a < a->num_fences && i_b < b->num_fences; ) {
		struct fence *pt_a = a->cbs[i_a].fence;
		struct fence *pt_b = b->cbs[i_b].fence;

		if (pt_a->context < pt_b->context) {
			sync_file_add_pt(sync_file, &i, pt_a);

			i_a++;
		} else if (pt_a->context > pt_b->context) {
			sync_file_add_pt(sync_file, &i, pt_b);

			i_b++;
		} else {
			if (pt_a->seqno - pt_b->seqno <= INT_MAX)
				sync_file_add_pt(sync_file, &i, pt_a);
			else
				sync_file_add_pt(sync_file, &i, pt_b);

			i_a++;
			i_b++;
		}
	}

	for (; i_a < a->num_fences; i_a++)
		sync_file_add_pt(sync_file, &i, a->cbs[i_a].fence);

	for (; i_b < b->num_fences; i_b++)
		sync_file_add_pt(sync_file, &i, b->cbs[i_b].fence);

	if (num_fences > i)
		atomic_sub(num_fences - i, &sync_file->status);
	sync_file->num_fences = i;

	strlcpy(sync_file->name, name, sizeof(sync_file->name));
	return sync_file;
}

static void sync_file_free(struct kref *kref)
{
	struct sync_file *sync_file = container_of(kref, struct sync_file,
						     kref);
	int i;

	for (i = 0; i < sync_file->num_fences; ++i) {
		fence_remove_callback(sync_file->cbs[i].fence,
				      &sync_file->cbs[i].cb);
		fence_put(sync_file->cbs[i].fence);
	}

	kfree(sync_file);
}

static int sync_file_release(struct inode *inode, struct file *file)
{
	struct sync_file *sync_file = file->private_data;

	kref_put(&sync_file->kref, sync_file_free);
	return 0;
}

static unsigned int sync_file_poll(struct file *file, poll_table *wait)
{
	struct sync_file *sync_file = file->private_data;
	int status;

	poll_wait(file, &sync_file->wq, wait);

	status = atomic_read(&sync_file->status);

	if (!status)
		return POLLIN;
	if (status < 0)
		return POLLERR;
	return 0;
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

static void sync_fill_fence_info(struct fence *fence,
				 struct sync_fence_info *info)
{
	strlcpy(info->obj_name, fence->ops->get_timeline_name(fence),
		sizeof(info->obj_name));
	strlcpy(info->driver_name, fence->ops->get_driver_name(fence),
		sizeof(info->driver_name));
	if (fence_is_signaled(fence))
		info->status = fence->status >= 0 ? 1 : fence->status;
	else
		info->status = 0;
	info->timestamp_ns = ktime_to_ns(fence->timestamp);
}

static long sync_file_ioctl_fence_info(struct sync_file *sync_file,
				       unsigned long arg)
{
	struct sync_file_info info;
	struct sync_fence_info *fence_info = NULL;
	__u32 size;
	int ret, i;

	if (copy_from_user(&info, (void __user *)arg, sizeof(info)))
		return -EFAULT;

	if (info.flags || info.pad)
		return -EINVAL;

	/*
	 * Passing num_fences = 0 means that userspace doesn't want to
	 * retrieve any sync_fence_info. If num_fences = 0 we skip filling
	 * sync_fence_info and return the actual number of fences on
	 * info->num_fences.
	 */
	if (!info.num_fences)
		goto no_fences;

	if (info.num_fences < sync_file->num_fences)
		return -EINVAL;

	size = sync_file->num_fences * sizeof(*fence_info);
	fence_info = kzalloc(size, GFP_KERNEL);
	if (!fence_info)
		return -ENOMEM;

	for (i = 0; i < sync_file->num_fences; ++i)
		sync_fill_fence_info(sync_file->cbs[i].fence, &fence_info[i]);

	if (copy_to_user(u64_to_user_ptr(info.sync_fence_info), fence_info,
			 size)) {
		ret = -EFAULT;
		goto out;
	}

no_fences:
	strlcpy(info.name, sync_file->name, sizeof(info.name));
	info.status = atomic_read(&sync_file->status);
	if (info.status >= 0)
		info.status = !info.status;

	info.num_fences = sync_file->num_fences;

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
	.compat_ioctl = sync_file_ioctl,
};


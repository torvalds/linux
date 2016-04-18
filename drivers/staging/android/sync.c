/*
 * drivers/base/sync.c
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

#include <linux/debugfs.h>
#include <linux/export.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/anon_inodes.h>

#include "sync.h"

#define CREATE_TRACE_POINTS
#include "trace/sync.h"

static const struct fence_ops android_fence_ops;
static const struct file_operations sync_file_fops;

struct sync_timeline *sync_timeline_create(const struct sync_timeline_ops *ops,
					   int size, const char *name)
{
	struct sync_timeline *obj;

	if (size < sizeof(struct sync_timeline))
		return NULL;

	obj = kzalloc(size, GFP_KERNEL);
	if (!obj)
		return NULL;

	kref_init(&obj->kref);
	obj->ops = ops;
	obj->context = fence_context_alloc(1);
	strlcpy(obj->name, name, sizeof(obj->name));

	INIT_LIST_HEAD(&obj->child_list_head);
	INIT_LIST_HEAD(&obj->active_list_head);
	spin_lock_init(&obj->child_list_lock);

	sync_timeline_debug_add(obj);

	return obj;
}
EXPORT_SYMBOL(sync_timeline_create);

static void sync_timeline_free(struct kref *kref)
{
	struct sync_timeline *obj =
		container_of(kref, struct sync_timeline, kref);

	sync_timeline_debug_remove(obj);

	kfree(obj);
}

static void sync_timeline_get(struct sync_timeline *obj)
{
	kref_get(&obj->kref);
}

static void sync_timeline_put(struct sync_timeline *obj)
{
	kref_put(&obj->kref, sync_timeline_free);
}

void sync_timeline_destroy(struct sync_timeline *obj)
{
	obj->destroyed = true;
	/*
	 * Ensure timeline is marked as destroyed before
	 * changing timeline's fences status.
	 */
	smp_wmb();

	sync_timeline_put(obj);
}
EXPORT_SYMBOL(sync_timeline_destroy);

void sync_timeline_signal(struct sync_timeline *obj)
{
	unsigned long flags;
	struct fence *fence, *next;

	trace_sync_timeline(obj);

	spin_lock_irqsave(&obj->child_list_lock, flags);

	list_for_each_entry_safe(fence, next, &obj->active_list_head,
				 active_list) {
		if (fence_is_signaled_locked(fence))
			list_del_init(&fence->active_list);
	}

	spin_unlock_irqrestore(&obj->child_list_lock, flags);
}
EXPORT_SYMBOL(sync_timeline_signal);

struct fence *sync_pt_create(struct sync_timeline *obj, int size)
{
	unsigned long flags;
	struct fence *fence;

	if (size < sizeof(*fence))
		return NULL;

	fence = kzalloc(size, GFP_KERNEL);
	if (!fence)
		return NULL;

	spin_lock_irqsave(&obj->child_list_lock, flags);
	sync_timeline_get(obj);
	fence_init(fence, &android_fence_ops, &obj->child_list_lock,
		   obj->context, ++obj->value);
	list_add_tail(&fence->child_list, &obj->child_list_head);
	INIT_LIST_HEAD(&fence->active_list);
	spin_unlock_irqrestore(&obj->child_list_lock, flags);
	return fence;
}
EXPORT_SYMBOL(sync_pt_create);

static struct sync_file *sync_file_alloc(int size, const char *name)
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
	strlcpy(sync_file->name, name, sizeof(sync_file->name));

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

/* TODO: implement a create which takes more that one fence */
struct sync_file *sync_file_create(const char *name, struct fence *fence)
{
	struct sync_file *sync_file;

	sync_file = sync_file_alloc(offsetof(struct sync_file, cbs[1]),
				    name);
	if (!sync_file)
		return NULL;

	sync_file->num_fences = 1;
	atomic_set(&sync_file->status, 1);

	sync_file->cbs[0].fence = fence;
	sync_file->cbs[0].sync_file = sync_file;
	if (fence_add_callback(fence, &sync_file->cbs[0].cb,
			       fence_check_cb_func))
		atomic_dec(&sync_file->status);

	sync_file_debug_add(sync_file);

	return sync_file;
}
EXPORT_SYMBOL(sync_file_create);

struct sync_file *sync_file_fdget(int fd)
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
EXPORT_SYMBOL(sync_file_fdget);

void sync_file_put(struct sync_file *sync_file)
{
	fput(sync_file->file);
}
EXPORT_SYMBOL(sync_file_put);

void sync_file_install(struct sync_file *sync_file, int fd)
{
	fd_install(fd, sync_file->file);
}
EXPORT_SYMBOL(sync_file_install);

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

struct sync_file *sync_file_merge(const char *name,
				  struct sync_file *a, struct sync_file *b)
{
	int num_fences = a->num_fences + b->num_fences;
	struct sync_file *sync_file;
	int i, i_a, i_b;
	unsigned long size = offsetof(struct sync_file, cbs[num_fences]);

	sync_file = sync_file_alloc(size, name);
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

	sync_file_debug_add(sync_file);
	return sync_file;
}
EXPORT_SYMBOL(sync_file_merge);

static const char *android_fence_get_driver_name(struct fence *fence)
{
	struct sync_timeline *parent = fence_parent(fence);

	return parent->ops->driver_name;
}

static const char *android_fence_get_timeline_name(struct fence *fence)
{
	struct sync_timeline *parent = fence_parent(fence);

	return parent->name;
}

static void android_fence_release(struct fence *fence)
{
	struct sync_timeline *parent = fence_parent(fence);
	unsigned long flags;

	spin_lock_irqsave(fence->lock, flags);
	list_del(&fence->child_list);
	if (WARN_ON_ONCE(!list_empty(&fence->active_list)))
		list_del(&fence->active_list);
	spin_unlock_irqrestore(fence->lock, flags);

	sync_timeline_put(parent);
	fence_free(fence);
}

static bool android_fence_signaled(struct fence *fence)
{
	struct sync_timeline *parent = fence_parent(fence);
	int ret;

	ret = parent->ops->has_signaled(fence);
	if (ret < 0)
		fence->status = ret;
	return ret;
}

static bool android_fence_enable_signaling(struct fence *fence)
{
	struct sync_timeline *parent = fence_parent(fence);

	if (android_fence_signaled(fence))
		return false;

	list_add_tail(&fence->active_list, &parent->active_list_head);
	return true;
}

static void android_fence_value_str(struct fence *fence,
				    char *str, int size)
{
	struct sync_timeline *parent = fence_parent(fence);

	if (!parent->ops->fence_value_str) {
		if (size)
			*str = 0;
		return;
	}
	parent->ops->fence_value_str(fence, str, size);
}

static void android_fence_timeline_value_str(struct fence *fence,
					     char *str, int size)
{
	struct sync_timeline *parent = fence_parent(fence);

	if (!parent->ops->timeline_value_str) {
		if (size)
			*str = 0;
		return;
	}
	parent->ops->timeline_value_str(parent, str, size);
}

static const struct fence_ops android_fence_ops = {
	.get_driver_name = android_fence_get_driver_name,
	.get_timeline_name = android_fence_get_timeline_name,
	.enable_signaling = android_fence_enable_signaling,
	.signaled = android_fence_signaled,
	.wait = fence_default_wait,
	.release = android_fence_release,
	.fence_value_str = android_fence_value_str,
	.timeline_value_str = android_fence_timeline_value_str,
};

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

	sync_file_debug_remove(sync_file);

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

	sync_file_install(fence3, fd);
	sync_file_put(fence2);
	return 0;

err_put_fence3:
	sync_file_put(fence3);

err_put_fence2:
	sync_file_put(fence2);

err_put_fd:
	put_unused_fd(fd);
	return err;
}

static int sync_fill_fence_info(struct fence *fence, void *data, int size)
{
	struct sync_fence_info *info = data;

	if (size < sizeof(*info))
		return -ENOMEM;

	strlcpy(info->obj_name, fence->ops->get_timeline_name(fence),
		sizeof(info->obj_name));
	strlcpy(info->driver_name, fence->ops->get_driver_name(fence),
		sizeof(info->driver_name));
	if (fence_is_signaled(fence))
		info->status = fence->status >= 0 ? 1 : fence->status;
	else
		info->status = 0;
	info->timestamp_ns = ktime_to_ns(fence->timestamp);

	return sizeof(*info);
}

static long sync_file_ioctl_fence_info(struct sync_file *sync_file,
					unsigned long arg)
{
	struct sync_file_info *info;
	__u32 size;
	__u32 len = 0;
	int ret, i;

	if (copy_from_user(&size, (void __user *)arg, sizeof(size)))
		return -EFAULT;

	if (size < sizeof(struct sync_file_info))
		return -EINVAL;

	if (size > 4096)
		size = 4096;

	info = kzalloc(size, GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	strlcpy(info->name, sync_file->name, sizeof(info->name));
	info->status = atomic_read(&sync_file->status);
	if (info->status >= 0)
		info->status = !info->status;

	len = sizeof(struct sync_file_info);

	for (i = 0; i < sync_file->num_fences; ++i) {
		struct fence *fence = sync_file->cbs[i].fence;

		ret = sync_fill_fence_info(fence, (u8 *)info + len, size - len);

		if (ret < 0)
			goto out;

		len += ret;
	}

	info->len = len;

	if (copy_to_user((void __user *)arg, info, len))
		ret = -EFAULT;
	else
		ret = 0;

out:
	kfree(info);

	return ret;
}

static long sync_file_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	struct sync_file *sync_file = file->private_data;

	switch (cmd) {
	case SYNC_IOC_MERGE:
		return sync_file_ioctl_merge(sync_file, arg);

	case SYNC_IOC_FENCE_INFO:
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


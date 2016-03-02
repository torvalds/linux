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
static const struct file_operations sync_fence_fops;

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

	if (obj->ops->release_obj)
		obj->ops->release_obj(obj);

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

	/*
	 * signal any children that their parent is going away.
	 */
	sync_timeline_signal(obj);
	sync_timeline_put(obj);
}
EXPORT_SYMBOL(sync_timeline_destroy);

void sync_timeline_signal(struct sync_timeline *obj)
{
	unsigned long flags;
	LIST_HEAD(signaled_pts);
	struct sync_pt *pt, *next;

	trace_sync_timeline(obj);

	spin_lock_irqsave(&obj->child_list_lock, flags);

	list_for_each_entry_safe(pt, next, &obj->active_list_head,
				 active_list) {
		if (fence_is_signaled_locked(&pt->base))
			list_del_init(&pt->active_list);
	}

	spin_unlock_irqrestore(&obj->child_list_lock, flags);
}
EXPORT_SYMBOL(sync_timeline_signal);

struct sync_pt *sync_pt_create(struct sync_timeline *obj, int size)
{
	unsigned long flags;
	struct sync_pt *pt;

	if (size < sizeof(struct sync_pt))
		return NULL;

	pt = kzalloc(size, GFP_KERNEL);
	if (!pt)
		return NULL;

	spin_lock_irqsave(&obj->child_list_lock, flags);
	sync_timeline_get(obj);
	fence_init(&pt->base, &android_fence_ops, &obj->child_list_lock,
		   obj->context, ++obj->value);
	list_add_tail(&pt->child_list, &obj->child_list_head);
	INIT_LIST_HEAD(&pt->active_list);
	spin_unlock_irqrestore(&obj->child_list_lock, flags);
	return pt;
}
EXPORT_SYMBOL(sync_pt_create);

void sync_pt_free(struct sync_pt *pt)
{
	fence_put(&pt->base);
}
EXPORT_SYMBOL(sync_pt_free);

static struct sync_fence *sync_fence_alloc(int size, const char *name)
{
	struct sync_fence *fence;

	fence = kzalloc(size, GFP_KERNEL);
	if (!fence)
		return NULL;

	fence->file = anon_inode_getfile("sync_fence", &sync_fence_fops,
					 fence, 0);
	if (IS_ERR(fence->file))
		goto err;

	kref_init(&fence->kref);
	strlcpy(fence->name, name, sizeof(fence->name));

	init_waitqueue_head(&fence->wq);

	return fence;

err:
	kfree(fence);
	return NULL;
}

static void fence_check_cb_func(struct fence *f, struct fence_cb *cb)
{
	struct sync_fence_cb *check;
	struct sync_fence *fence;

	check = container_of(cb, struct sync_fence_cb, cb);
	fence = check->fence;

	if (atomic_dec_and_test(&fence->status))
		wake_up_all(&fence->wq);
}

/* TODO: implement a create which takes more that one sync_pt */
struct sync_fence *sync_fence_create_dma(const char *name, struct fence *pt)
{
	struct sync_fence *fence;

	fence = sync_fence_alloc(offsetof(struct sync_fence, cbs[1]), name);
	if (!fence)
		return NULL;

	fence->num_fences = 1;
	atomic_set(&fence->status, 1);

	fence->cbs[0].sync_pt = pt;
	fence->cbs[0].fence = fence;
	if (fence_add_callback(pt, &fence->cbs[0].cb, fence_check_cb_func))
		atomic_dec(&fence->status);

	sync_fence_debug_add(fence);

	return fence;
}
EXPORT_SYMBOL(sync_fence_create_dma);

struct sync_fence *sync_fence_create(const char *name, struct sync_pt *pt)
{
	return sync_fence_create_dma(name, &pt->base);
}
EXPORT_SYMBOL(sync_fence_create);

struct sync_fence *sync_fence_fdget(int fd)
{
	struct file *file = fget(fd);

	if (!file)
		return NULL;

	if (file->f_op != &sync_fence_fops)
		goto err;

	return file->private_data;

err:
	fput(file);
	return NULL;
}
EXPORT_SYMBOL(sync_fence_fdget);

void sync_fence_put(struct sync_fence *fence)
{
	fput(fence->file);
}
EXPORT_SYMBOL(sync_fence_put);

void sync_fence_install(struct sync_fence *fence, int fd)
{
	fd_install(fd, fence->file);
}
EXPORT_SYMBOL(sync_fence_install);

static void sync_fence_add_pt(struct sync_fence *fence,
			      int *i, struct fence *pt)
{
	fence->cbs[*i].sync_pt = pt;
	fence->cbs[*i].fence = fence;

	if (!fence_add_callback(pt, &fence->cbs[*i].cb, fence_check_cb_func)) {
		fence_get(pt);
		(*i)++;
	}
}

struct sync_fence *sync_fence_merge(const char *name,
				    struct sync_fence *a, struct sync_fence *b)
{
	int num_fences = a->num_fences + b->num_fences;
	struct sync_fence *fence;
	int i, i_a, i_b;
	unsigned long size = offsetof(struct sync_fence, cbs[num_fences]);

	fence = sync_fence_alloc(size, name);
	if (!fence)
		return NULL;

	atomic_set(&fence->status, num_fences);

	/*
	 * Assume sync_fence a and b are both ordered and have no
	 * duplicates with the same context.
	 *
	 * If a sync_fence can only be created with sync_fence_merge
	 * and sync_fence_create, this is a reasonable assumption.
	 */
	for (i = i_a = i_b = 0; i_a < a->num_fences && i_b < b->num_fences; ) {
		struct fence *pt_a = a->cbs[i_a].sync_pt;
		struct fence *pt_b = b->cbs[i_b].sync_pt;

		if (pt_a->context < pt_b->context) {
			sync_fence_add_pt(fence, &i, pt_a);

			i_a++;
		} else if (pt_a->context > pt_b->context) {
			sync_fence_add_pt(fence, &i, pt_b);

			i_b++;
		} else {
			if (pt_a->seqno - pt_b->seqno <= INT_MAX)
				sync_fence_add_pt(fence, &i, pt_a);
			else
				sync_fence_add_pt(fence, &i, pt_b);

			i_a++;
			i_b++;
		}
	}

	for (; i_a < a->num_fences; i_a++)
		sync_fence_add_pt(fence, &i, a->cbs[i_a].sync_pt);

	for (; i_b < b->num_fences; i_b++)
		sync_fence_add_pt(fence, &i, b->cbs[i_b].sync_pt);

	if (num_fences > i)
		atomic_sub(num_fences - i, &fence->status);
	fence->num_fences = i;

	sync_fence_debug_add(fence);
	return fence;
}
EXPORT_SYMBOL(sync_fence_merge);

int sync_fence_wake_up_wq(wait_queue_t *curr, unsigned mode,
			  int wake_flags, void *key)
{
	struct sync_fence_waiter *wait;

	wait = container_of(curr, struct sync_fence_waiter, work);
	list_del_init(&wait->work.task_list);

	wait->callback(wait->work.private, wait);
	return 1;
}

int sync_fence_wait_async(struct sync_fence *fence,
			  struct sync_fence_waiter *waiter)
{
	int err = atomic_read(&fence->status);
	unsigned long flags;

	if (err < 0)
		return err;

	if (!err)
		return 1;

	init_waitqueue_func_entry(&waiter->work, sync_fence_wake_up_wq);
	waiter->work.private = fence;

	spin_lock_irqsave(&fence->wq.lock, flags);
	err = atomic_read(&fence->status);
	if (err > 0)
		__add_wait_queue_tail(&fence->wq, &waiter->work);
	spin_unlock_irqrestore(&fence->wq.lock, flags);

	if (err < 0)
		return err;

	return !err;
}
EXPORT_SYMBOL(sync_fence_wait_async);

int sync_fence_cancel_async(struct sync_fence *fence,
			    struct sync_fence_waiter *waiter)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&fence->wq.lock, flags);
	if (!list_empty(&waiter->work.task_list))
		list_del_init(&waiter->work.task_list);
	else
		ret = -ENOENT;
	spin_unlock_irqrestore(&fence->wq.lock, flags);
	return ret;
}
EXPORT_SYMBOL(sync_fence_cancel_async);

int sync_fence_wait(struct sync_fence *fence, long timeout)
{
	long ret;
	int i;

	if (timeout < 0)
		timeout = MAX_SCHEDULE_TIMEOUT;
	else
		timeout = msecs_to_jiffies(timeout);

	trace_sync_wait(fence, 1);
	for (i = 0; i < fence->num_fences; ++i)
		trace_sync_pt(fence->cbs[i].sync_pt);
	ret = wait_event_interruptible_timeout(fence->wq,
					       atomic_read(&fence->status) <= 0,
					       timeout);
	trace_sync_wait(fence, 0);

	if (ret < 0) {
		return ret;
	} else if (ret == 0) {
		if (timeout) {
			pr_info("fence timeout on [%p] after %dms\n", fence,
				jiffies_to_msecs(timeout));
			sync_dump();
		}
		return -ETIME;
	}

	ret = atomic_read(&fence->status);
	if (ret) {
		pr_info("fence error %ld on [%p]\n", ret, fence);
		sync_dump();
	}
	return ret;
}
EXPORT_SYMBOL(sync_fence_wait);

static const char *android_fence_get_driver_name(struct fence *fence)
{
	struct sync_pt *pt = container_of(fence, struct sync_pt, base);
	struct sync_timeline *parent = sync_pt_parent(pt);

	return parent->ops->driver_name;
}

static const char *android_fence_get_timeline_name(struct fence *fence)
{
	struct sync_pt *pt = container_of(fence, struct sync_pt, base);
	struct sync_timeline *parent = sync_pt_parent(pt);

	return parent->name;
}

static void android_fence_release(struct fence *fence)
{
	struct sync_pt *pt = container_of(fence, struct sync_pt, base);
	struct sync_timeline *parent = sync_pt_parent(pt);
	unsigned long flags;

	spin_lock_irqsave(fence->lock, flags);
	list_del(&pt->child_list);
	if (WARN_ON_ONCE(!list_empty(&pt->active_list)))
		list_del(&pt->active_list);
	spin_unlock_irqrestore(fence->lock, flags);

	if (parent->ops->free_pt)
		parent->ops->free_pt(pt);

	sync_timeline_put(parent);
	fence_free(&pt->base);
}

static bool android_fence_signaled(struct fence *fence)
{
	struct sync_pt *pt = container_of(fence, struct sync_pt, base);
	struct sync_timeline *parent = sync_pt_parent(pt);
	int ret;

	ret = parent->ops->has_signaled(pt);
	if (ret < 0)
		fence->status = ret;
	return ret;
}

static bool android_fence_enable_signaling(struct fence *fence)
{
	struct sync_pt *pt = container_of(fence, struct sync_pt, base);
	struct sync_timeline *parent = sync_pt_parent(pt);

	if (android_fence_signaled(fence))
		return false;

	list_add_tail(&pt->active_list, &parent->active_list_head);
	return true;
}

static int android_fence_fill_driver_data(struct fence *fence,
					  void *data, int size)
{
	struct sync_pt *pt = container_of(fence, struct sync_pt, base);
	struct sync_timeline *parent = sync_pt_parent(pt);

	if (!parent->ops->fill_driver_data)
		return 0;
	return parent->ops->fill_driver_data(pt, data, size);
}

static void android_fence_value_str(struct fence *fence,
				    char *str, int size)
{
	struct sync_pt *pt = container_of(fence, struct sync_pt, base);
	struct sync_timeline *parent = sync_pt_parent(pt);

	if (!parent->ops->pt_value_str) {
		if (size)
			*str = 0;
		return;
	}
	parent->ops->pt_value_str(pt, str, size);
}

static void android_fence_timeline_value_str(struct fence *fence,
					     char *str, int size)
{
	struct sync_pt *pt = container_of(fence, struct sync_pt, base);
	struct sync_timeline *parent = sync_pt_parent(pt);

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
	.fill_driver_data = android_fence_fill_driver_data,
	.fence_value_str = android_fence_value_str,
	.timeline_value_str = android_fence_timeline_value_str,
};

static void sync_fence_free(struct kref *kref)
{
	struct sync_fence *fence = container_of(kref, struct sync_fence, kref);
	int i;

	for (i = 0; i < fence->num_fences; ++i) {
		fence_remove_callback(fence->cbs[i].sync_pt, &fence->cbs[i].cb);
		fence_put(fence->cbs[i].sync_pt);
	}

	kfree(fence);
}

static int sync_fence_release(struct inode *inode, struct file *file)
{
	struct sync_fence *fence = file->private_data;

	sync_fence_debug_remove(fence);

	kref_put(&fence->kref, sync_fence_free);
	return 0;
}

static unsigned int sync_fence_poll(struct file *file, poll_table *wait)
{
	struct sync_fence *fence = file->private_data;
	int status;

	poll_wait(file, &fence->wq, wait);

	status = atomic_read(&fence->status);

	if (!status)
		return POLLIN;
	else if (status < 0)
		return POLLERR;
	return 0;
}

static long sync_fence_ioctl_wait(struct sync_fence *fence, unsigned long arg)
{
	__s32 value;

	if (copy_from_user(&value, (void __user *)arg, sizeof(value)))
		return -EFAULT;

	return sync_fence_wait(fence, value);
}

static long sync_fence_ioctl_merge(struct sync_fence *fence, unsigned long arg)
{
	int fd = get_unused_fd_flags(O_CLOEXEC);
	int err;
	struct sync_fence *fence2, *fence3;
	struct sync_merge_data data;

	if (fd < 0)
		return fd;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		err = -EFAULT;
		goto err_put_fd;
	}

	fence2 = sync_fence_fdget(data.fd2);
	if (!fence2) {
		err = -ENOENT;
		goto err_put_fd;
	}

	data.name[sizeof(data.name) - 1] = '\0';
	fence3 = sync_fence_merge(data.name, fence, fence2);
	if (!fence3) {
		err = -ENOMEM;
		goto err_put_fence2;
	}

	data.fence = fd;
	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		err = -EFAULT;
		goto err_put_fence3;
	}

	sync_fence_install(fence3, fd);
	sync_fence_put(fence2);
	return 0;

err_put_fence3:
	sync_fence_put(fence3);

err_put_fence2:
	sync_fence_put(fence2);

err_put_fd:
	put_unused_fd(fd);
	return err;
}

static int sync_fill_pt_info(struct fence *fence, void *data, int size)
{
	struct sync_pt_info *info = data;
	int ret;

	if (size < sizeof(struct sync_pt_info))
		return -ENOMEM;

	info->len = sizeof(struct sync_pt_info);

	if (fence->ops->fill_driver_data) {
		ret = fence->ops->fill_driver_data(fence, info->driver_data,
						   size - sizeof(*info));
		if (ret < 0)
			return ret;

		info->len += ret;
	}

	strlcpy(info->obj_name, fence->ops->get_timeline_name(fence),
		sizeof(info->obj_name));
	strlcpy(info->driver_name, fence->ops->get_driver_name(fence),
		sizeof(info->driver_name));
	if (fence_is_signaled(fence))
		info->status = fence->status >= 0 ? 1 : fence->status;
	else
		info->status = 0;
	info->timestamp_ns = ktime_to_ns(fence->timestamp);

	return info->len;
}

static long sync_fence_ioctl_fence_info(struct sync_fence *fence,
					unsigned long arg)
{
	struct sync_fence_info_data *data;
	__u32 size;
	__u32 len = 0;
	int ret, i;

	if (copy_from_user(&size, (void __user *)arg, sizeof(size)))
		return -EFAULT;

	if (size < sizeof(struct sync_fence_info_data))
		return -EINVAL;

	if (size > 4096)
		size = 4096;

	data = kzalloc(size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	strlcpy(data->name, fence->name, sizeof(data->name));
	data->status = atomic_read(&fence->status);
	if (data->status >= 0)
		data->status = !data->status;

	len = sizeof(struct sync_fence_info_data);

	for (i = 0; i < fence->num_fences; ++i) {
		struct fence *pt = fence->cbs[i].sync_pt;

		ret = sync_fill_pt_info(pt, (u8 *)data + len, size - len);

		if (ret < 0)
			goto out;

		len += ret;
	}

	data->len = len;

	if (copy_to_user((void __user *)arg, data, len))
		ret = -EFAULT;
	else
		ret = 0;

out:
	kfree(data);

	return ret;
}

static long sync_fence_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	struct sync_fence *fence = file->private_data;

	switch (cmd) {
	case SYNC_IOC_WAIT:
		return sync_fence_ioctl_wait(fence, arg);

	case SYNC_IOC_MERGE:
		return sync_fence_ioctl_merge(fence, arg);

	case SYNC_IOC_FENCE_INFO:
		return sync_fence_ioctl_fence_info(fence, arg);

	default:
		return -ENOTTY;
	}
}

static const struct file_operations sync_fence_fops = {
	.release = sync_fence_release,
	.poll = sync_fence_poll,
	.unlocked_ioctl = sync_fence_ioctl,
	.compat_ioctl = sync_fence_ioctl,
};


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

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/anon_inodes.h>

#include "sync.h"

static void sync_fence_signal_pt(struct sync_pt *pt);
static int _sync_pt_has_signaled(struct sync_pt *pt);

struct sync_timeline *sync_timeline_create(const struct sync_timeline_ops *ops,
					   int size, const char *name)
{
	struct sync_timeline *obj;

	if (size < sizeof(struct sync_timeline))
		return NULL;

	obj = kzalloc(size, GFP_KERNEL);
	if (obj == NULL)
		return NULL;

	obj->ops = ops;
	strlcpy(obj->name, name, sizeof(obj->name));

	INIT_LIST_HEAD(&obj->child_list_head);
	spin_lock_init(&obj->child_list_lock);

	INIT_LIST_HEAD(&obj->active_list_head);
	spin_lock_init(&obj->active_list_lock);

	return obj;
}

void sync_timeline_destroy(struct sync_timeline *obj)
{
	unsigned long flags;
	bool needs_freeing;

	spin_lock_irqsave(&obj->child_list_lock, flags);
	obj->destroyed = true;
	needs_freeing = list_empty(&obj->child_list_head);
	spin_unlock_irqrestore(&obj->child_list_lock, flags);

	if (needs_freeing)
		kfree(obj);
	else
		sync_timeline_signal(obj);
}

static void sync_timeline_add_pt(struct sync_timeline *obj, struct sync_pt *pt)
{
	unsigned long flags;

	pt->parent = obj;

	spin_lock_irqsave(&obj->child_list_lock, flags);
	list_add_tail(&pt->child_list, &obj->child_list_head);
	spin_unlock_irqrestore(&obj->child_list_lock, flags);
}

static void sync_timeline_remove_pt(struct sync_pt *pt)
{
	struct sync_timeline *obj = pt->parent;
	unsigned long flags;
	bool needs_freeing;

	spin_lock_irqsave(&obj->active_list_lock, flags);
	if (!list_empty(&pt->active_list))
		list_del_init(&pt->active_list);
	spin_unlock_irqrestore(&obj->active_list_lock, flags);

	spin_lock_irqsave(&obj->child_list_lock, flags);
	list_del(&pt->child_list);
	needs_freeing = obj->destroyed && list_empty(&obj->child_list_head);
	spin_unlock_irqrestore(&obj->child_list_lock, flags);

	if (needs_freeing)
		kfree(obj);
}

void sync_timeline_signal(struct sync_timeline *obj)
{
	unsigned long flags;
	LIST_HEAD(signaled_pts);
	struct list_head *pos, *n;

	spin_lock_irqsave(&obj->active_list_lock, flags);

	list_for_each_safe(pos, n, &obj->active_list_head) {
		struct sync_pt *pt =
			container_of(pos, struct sync_pt, active_list);

		if (_sync_pt_has_signaled(pt))
			list_move(pos, &signaled_pts);
	}

	spin_unlock_irqrestore(&obj->active_list_lock, flags);

	list_for_each_safe(pos, n, &signaled_pts) {
		struct sync_pt *pt =
			container_of(pos, struct sync_pt, active_list);

		list_del_init(pos);
		sync_fence_signal_pt(pt);
	}
}

struct sync_pt *sync_pt_create(struct sync_timeline *parent, int size)
{
	struct sync_pt *pt;

	if (size < sizeof(struct sync_pt))
		return NULL;

	pt = kzalloc(size, GFP_KERNEL);
	if (pt == NULL)
		return NULL;

	INIT_LIST_HEAD(&pt->active_list);
	sync_timeline_add_pt(parent, pt);

	return pt;
}

void sync_pt_free(struct sync_pt *pt)
{
	if (pt->parent->ops->free_pt)
		pt->parent->ops->free_pt(pt);

	sync_timeline_remove_pt(pt);

	kfree(pt);
}

/* call with pt->parent->active_list_lock held */
static int _sync_pt_has_signaled(struct sync_pt *pt)
{
	if (!pt->status)
		pt->status = pt->parent->ops->has_signaled(pt);

	if (!pt->status && pt->parent->destroyed)
		pt->status = -ENOENT;

	return pt->status;
}

static struct sync_pt *sync_pt_dup(struct sync_pt *pt)
{
	return pt->parent->ops->dup(pt);
}

/* Adds a sync pt to the active queue.  Called when added to a fence */
static void sync_pt_activate(struct sync_pt *pt)
{
	struct sync_timeline *obj = pt->parent;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&obj->active_list_lock, flags);

	err = _sync_pt_has_signaled(pt);
	if (err != 0)
		goto out;

	list_add_tail(&pt->active_list, &obj->active_list_head);

out:
	spin_unlock_irqrestore(&obj->active_list_lock, flags);
}

static int sync_fence_release(struct inode *inode, struct file *file);
static long sync_fence_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg);


static const struct file_operations sync_fence_fops = {
	.release = sync_fence_release,
	.unlocked_ioctl = sync_fence_ioctl,
};

static struct sync_fence *sync_fence_alloc(const char *name)
{
	struct sync_fence *fence;

	fence = kzalloc(sizeof(struct sync_fence), GFP_KERNEL);
	if (fence == NULL)
		return NULL;

	fence->file = anon_inode_getfile("sync_fence", &sync_fence_fops,
					 fence, 0);
	if (fence->file == NULL)
		goto err;

	strlcpy(fence->name, name, sizeof(fence->name));

	INIT_LIST_HEAD(&fence->pt_list_head);
	INIT_LIST_HEAD(&fence->waiter_list_head);
	spin_lock_init(&fence->waiter_list_lock);

	init_waitqueue_head(&fence->wq);
	return fence;

err:
	kfree(fence);
	return NULL;
}

/* TODO: implement a create which takes more that one sync_pt */
struct sync_fence *sync_fence_create(const char *name, struct sync_pt *pt)
{
	struct sync_fence *fence;

	if (pt->fence)
		return NULL;

	fence = sync_fence_alloc(name);
	if (fence == NULL)
		return NULL;

	pt->fence = fence;
	list_add(&pt->pt_list, &fence->pt_list_head);
	sync_pt_activate(pt);

	return fence;
}

static int sync_fence_copy_pts(struct sync_fence *dst, struct sync_fence *src)
{
	struct list_head *pos;

	list_for_each(pos, &src->pt_list_head) {
		struct sync_pt *orig_pt =
			container_of(pos, struct sync_pt, pt_list);
		struct sync_pt *new_pt = sync_pt_dup(orig_pt);

		if (new_pt == NULL)
			return -ENOMEM;

		new_pt->fence = dst;
		list_add(&new_pt->pt_list, &dst->pt_list_head);
		sync_pt_activate(new_pt);
	}

	return 0;
}

static void sync_fence_free_pts(struct sync_fence *fence)
{
	struct list_head *pos, *n;

	list_for_each_safe(pos, n, &fence->pt_list_head) {
		struct sync_pt *pt = container_of(pos, struct sync_pt, pt_list);
		sync_pt_free(pt);
	}
}

struct sync_fence *sync_fence_fdget(int fd)
{
	struct file *file = fget(fd);

	if (file == NULL)
		return NULL;

	if (file->f_op != &sync_fence_fops)
		goto err;

	return file->private_data;

err:
	fput(file);
	return NULL;
}

void sync_fence_put(struct sync_fence *fence)
{
	fput(fence->file);
}

void sync_fence_install(struct sync_fence *fence, int fd)
{
	fd_install(fd, fence->file);
}

static int sync_fence_get_status(struct sync_fence *fence)
{
	struct list_head *pos;
	int status = 1;

	list_for_each(pos, &fence->pt_list_head) {
		struct sync_pt *pt = container_of(pos, struct sync_pt, pt_list);
		int pt_status = pt->status;

		if (pt_status < 0) {
			status = pt_status;
			break;
		} else if (status == 1) {
			status = pt_status;
		}
	}

	return status;
}

struct sync_fence *sync_fence_merge(const char *name,
				    struct sync_fence *a, struct sync_fence *b)
{
	struct sync_fence *fence;
	int err;

	fence = sync_fence_alloc(name);
	if (fence == NULL)
		return NULL;

	err = sync_fence_copy_pts(fence, a);
	if (err < 0)
		goto err;

	err = sync_fence_copy_pts(fence, b);
	if (err < 0)
		goto err;

	fence->status = sync_fence_get_status(fence);

	return fence;
err:
	sync_fence_free_pts(fence);
	kfree(fence);
	return NULL;
}

static void sync_fence_signal_pt(struct sync_pt *pt)
{
	LIST_HEAD(signaled_waiters);
	struct sync_fence *fence = pt->fence;
	struct list_head *pos;
	struct list_head *n;
	unsigned long flags;
	int status;

	status = sync_fence_get_status(fence);

	spin_lock_irqsave(&fence->waiter_list_lock, flags);
	/*
	 * this should protect against two threads racing on the signaled
	 * false -> true transition
	 */
	if (status && !fence->status) {
		list_for_each_safe(pos, n, &fence->waiter_list_head)
			list_move(pos, &signaled_waiters);

		fence->status = status;
	} else {
		status = 0;
	}
	spin_unlock_irqrestore(&fence->waiter_list_lock, flags);

	if (status) {
		list_for_each_safe(pos, n, &signaled_waiters) {
			struct sync_fence_waiter *waiter =
				container_of(pos, struct sync_fence_waiter,
					     waiter_list);

			waiter->callback(fence, waiter->callback_data);
			list_del(pos);
			kfree(waiter);
		}
		wake_up(&fence->wq);
	}
}

int sync_fence_wait_async(struct sync_fence *fence,
			  void (*callback)(struct sync_fence *, void *data),
			  void *callback_data)
{
	struct sync_fence_waiter *waiter;
	unsigned long flags;
	int err = 0;

	waiter = kzalloc(sizeof(struct sync_fence_waiter), GFP_KERNEL);
	if (waiter == NULL)
		return -ENOMEM;

	waiter->callback = callback;
	waiter->callback_data = callback_data;

	spin_lock_irqsave(&fence->waiter_list_lock, flags);

	if (fence->status) {
		kfree(waiter);
		err = fence->status;
		goto out;
	}

	list_add_tail(&waiter->waiter_list, &fence->waiter_list_head);
out:
	spin_unlock_irqrestore(&fence->waiter_list_lock, flags);

	return err;
}

int sync_fence_wait(struct sync_fence *fence, long timeout)
{
	int err;

	if (timeout) {
		timeout = msecs_to_jiffies(timeout);
		err = wait_event_interruptible_timeout(fence->wq,
						       fence->status != 0,
						       timeout);
	} else {
		err = wait_event_interruptible(fence->wq, fence->status != 0);
	}

	if (err < 0)
		return err;

	if (fence->status < 0)
		return fence->status;

	if (fence->status == 0)
		return -ETIME;

	return 0;
}

static int sync_fence_release(struct inode *inode, struct file *file)
{
	struct sync_fence *fence = file->private_data;

	sync_fence_free_pts(fence);
	kfree(fence);

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
	int fd = get_unused_fd();
	int err;
	struct sync_fence *fence2, *fence3;
	struct sync_merge_data data;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data)))
		return -EFAULT;

	fence2 = sync_fence_fdget(data.fd2);
	if (fence2 == NULL) {
		err = -ENOENT;
		goto err_put_fd;
	}

	data.name[sizeof(data.name) - 1] = '\0';
	fence3 = sync_fence_merge(data.name, fence, fence2);
	if (fence3 == NULL) {
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


static long sync_fence_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	struct sync_fence *fence = file->private_data;
	switch (cmd) {
	case SYNC_IOC_WAIT:
		return sync_fence_ioctl_wait(fence, arg);

	case SYNC_IOC_MERGE:
		return sync_fence_ioctl_merge(fence, arg);
	default:
		return -ENOTTY;
	}
}


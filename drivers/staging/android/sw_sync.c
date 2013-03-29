/*
 * drivers/base/sw_sync.c
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

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#include "sw_sync.h"

static int sw_sync_cmp(u32 a, u32 b)
{
	if (a == b)
		return 0;

	return ((s32)a - (s32)b) < 0 ? -1 : 1;
}

struct sync_pt *sw_sync_pt_create(struct sw_sync_timeline *obj, u32 value)
{
	struct sw_sync_pt *pt;

	pt = (struct sw_sync_pt *)
		sync_pt_create(&obj->obj, sizeof(struct sw_sync_pt));

	pt->value = value;

	return (struct sync_pt *)pt;
}
EXPORT_SYMBOL(sw_sync_pt_create);

static struct sync_pt *sw_sync_pt_dup(struct sync_pt *sync_pt)
{
	struct sw_sync_pt *pt = (struct sw_sync_pt *) sync_pt;
	struct sw_sync_timeline *obj =
		(struct sw_sync_timeline *)sync_pt->parent;

	return (struct sync_pt *) sw_sync_pt_create(obj, pt->value);
}

static int sw_sync_pt_has_signaled(struct sync_pt *sync_pt)
{
	struct sw_sync_pt *pt = (struct sw_sync_pt *)sync_pt;
	struct sw_sync_timeline *obj =
		(struct sw_sync_timeline *)sync_pt->parent;

	return sw_sync_cmp(obj->value, pt->value) >= 0;
}

static int sw_sync_pt_compare(struct sync_pt *a, struct sync_pt *b)
{
	struct sw_sync_pt *pt_a = (struct sw_sync_pt *)a;
	struct sw_sync_pt *pt_b = (struct sw_sync_pt *)b;

	return sw_sync_cmp(pt_a->value, pt_b->value);
}

static int sw_sync_fill_driver_data(struct sync_pt *sync_pt,
				    void *data, int size)
{
	struct sw_sync_pt *pt = (struct sw_sync_pt *)sync_pt;

	if (size < sizeof(pt->value))
		return -ENOMEM;

	memcpy(data, &pt->value, sizeof(pt->value));

	return sizeof(pt->value);
}

static void sw_sync_timeline_value_str(struct sync_timeline *sync_timeline,
				       char *str, int size)
{
	struct sw_sync_timeline *timeline =
		(struct sw_sync_timeline *)sync_timeline;
	snprintf(str, size, "%d", timeline->value);
}

static void sw_sync_pt_value_str(struct sync_pt *sync_pt,
				       char *str, int size)
{
	struct sw_sync_pt *pt = (struct sw_sync_pt *)sync_pt;
	snprintf(str, size, "%d", pt->value);
}

static struct sync_timeline_ops sw_sync_timeline_ops = {
	.driver_name = "sw_sync",
	.dup = sw_sync_pt_dup,
	.has_signaled = sw_sync_pt_has_signaled,
	.compare = sw_sync_pt_compare,
	.fill_driver_data = sw_sync_fill_driver_data,
	.timeline_value_str = sw_sync_timeline_value_str,
	.pt_value_str = sw_sync_pt_value_str,
};


struct sw_sync_timeline *sw_sync_timeline_create(const char *name)
{
	struct sw_sync_timeline *obj = (struct sw_sync_timeline *)
		sync_timeline_create(&sw_sync_timeline_ops,
				     sizeof(struct sw_sync_timeline),
				     name);

	return obj;
}
EXPORT_SYMBOL(sw_sync_timeline_create);

void sw_sync_timeline_inc(struct sw_sync_timeline *obj, u32 inc)
{
	obj->value += inc;

	sync_timeline_signal(&obj->obj);
}
EXPORT_SYMBOL(sw_sync_timeline_inc);

#ifdef CONFIG_SW_SYNC_USER
/* *WARNING*
 *
 * improper use of this can result in deadlocking kernel drivers from userspace.
 */

/* opening sw_sync create a new sync obj */
static int sw_sync_open(struct inode *inode, struct file *file)
{
	struct sw_sync_timeline *obj;
	char task_comm[TASK_COMM_LEN];

	get_task_comm(task_comm, current);

	obj = sw_sync_timeline_create(task_comm);
	if (obj == NULL)
		return -ENOMEM;

	file->private_data = obj;

	return 0;
}

static int sw_sync_release(struct inode *inode, struct file *file)
{
	struct sw_sync_timeline *obj = file->private_data;
	sync_timeline_destroy(&obj->obj);
	return 0;
}

static long sw_sync_ioctl_create_fence(struct sw_sync_timeline *obj, unsigned long arg)
{
	int fd = get_unused_fd();
	int err;
	struct sync_pt *pt;
	struct sync_fence *fence;
	struct sw_sync_create_fence_data data;

	if (fd < 0)
		return fd;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		err = -EFAULT;
		goto err;
	}

	pt = sw_sync_pt_create(obj, data.value);
	if (pt == NULL) {
		err = -ENOMEM;
		goto err;
	}

	data.name[sizeof(data.name) - 1] = '\0';
	fence = sync_fence_create(data.name, pt);
	if (fence == NULL) {
		sync_pt_free(pt);
		err = -ENOMEM;
		goto err;
	}

	data.fence = fd;
	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		sync_fence_put(fence);
		err = -EFAULT;
		goto err;
	}

	sync_fence_install(fence, fd);

	return 0;

err:
	put_unused_fd(fd);
	return err;
}

static long sw_sync_ioctl_inc(struct sw_sync_timeline *obj, unsigned long arg)
{
	u32 value;

	if (copy_from_user(&value, (void __user *)arg, sizeof(value)))
		return -EFAULT;

	sw_sync_timeline_inc(obj, value);

	return 0;
}

static long sw_sync_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct sw_sync_timeline *obj = file->private_data;

	switch (cmd) {
	case SW_SYNC_IOC_CREATE_FENCE:
		return sw_sync_ioctl_create_fence(obj, arg);

	case SW_SYNC_IOC_INC:
		return sw_sync_ioctl_inc(obj, arg);

	default:
		return -ENOTTY;
	}
}

static const struct file_operations sw_sync_fops = {
	.owner = THIS_MODULE,
	.open = sw_sync_open,
	.release = sw_sync_release,
	.unlocked_ioctl = sw_sync_ioctl,
	.compat_ioctl = sw_sync_ioctl,
};

static struct miscdevice sw_sync_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "sw_sync",
	.fops	= &sw_sync_fops,
};

static int __init sw_sync_device_init(void)
{
	return misc_register(&sw_sync_dev);
}

static void __exit sw_sync_device_remove(void)
{
	misc_deregister(&sw_sync_dev);
}

module_init(sw_sync_device_init);
module_exit(sw_sync_device_remove);

#endif /* CONFIG_SW_SYNC_USER */

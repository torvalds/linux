/*
 * drivers/dma-buf/sw_sync.c
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
#include <linux/uaccess.h>
#include <linux/sync_file.h>

#include "uapi/sw_sync.h"
#include "sync.h"

/*
 * *WARNING*
 *
 * improper use of this can result in deadlocking kernel drivers from userspace.
 */

/* opening sw_sync create a new sync obj */
static int sw_sync_debugfs_open(struct inode *inode, struct file *file)
{
	struct sync_timeline *obj;
	char task_comm[TASK_COMM_LEN];

	get_task_comm(task_comm, current);

	obj = sync_timeline_create("sw_sync", task_comm);
	if (!obj)
		return -ENOMEM;

	file->private_data = obj;

	return 0;
}

static int sw_sync_debugfs_release(struct inode *inode, struct file *file)
{
	struct sync_timeline *obj = file->private_data;

	sync_timeline_destroy(obj);
	return 0;
}

static long sw_sync_ioctl_create_fence(struct sync_timeline *obj,
				       unsigned long arg)
{
	int fd = get_unused_fd_flags(O_CLOEXEC);
	int err;
	struct sync_pt *pt;
	struct sync_file *sync_file;
	struct sw_sync_create_fence_data data;

	if (fd < 0)
		return fd;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		err = -EFAULT;
		goto err;
	}

	pt = sync_pt_create(obj, sizeof(*pt), data.value);
	if (!pt) {
		err = -ENOMEM;
		goto err;
	}

	sync_file = sync_file_create(&pt->base);
	if (!sync_file) {
		fence_put(&pt->base);
		err = -ENOMEM;
		goto err;
	}

	data.fence = fd;
	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		fput(sync_file->file);
		err = -EFAULT;
		goto err;
	}

	fd_install(fd, sync_file->file);

	return 0;

err:
	put_unused_fd(fd);
	return err;
}

static long sw_sync_ioctl_inc(struct sync_timeline *obj, unsigned long arg)
{
	u32 value;

	if (copy_from_user(&value, (void __user *)arg, sizeof(value)))
		return -EFAULT;

	sync_timeline_signal(obj, value);

	return 0;
}

static long sw_sync_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct sync_timeline *obj = file->private_data;

	switch (cmd) {
	case SW_SYNC_IOC_CREATE_FENCE:
		return sw_sync_ioctl_create_fence(obj, arg);

	case SW_SYNC_IOC_INC:
		return sw_sync_ioctl_inc(obj, arg);

	default:
		return -ENOTTY;
	}
}

const struct file_operations sw_sync_debugfs_fops = {
	.open           = sw_sync_debugfs_open,
	.release        = sw_sync_debugfs_release,
	.unlocked_ioctl = sw_sync_ioctl,
	.compat_ioctl	= sw_sync_ioctl,
};

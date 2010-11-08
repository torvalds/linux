/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *   Dima Zavin <dima@android.com>
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

#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/tegra_sema.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "trpc_sema.h"

struct trpc_sema {
	wait_queue_head_t	wq;
	spinlock_t		lock;
	int			count;
};

static int rpc_sema_minor = -1;

static inline bool is_trpc_sema_file(struct file *file)
{
	dev_t rdev = file->f_dentry->d_inode->i_rdev;

	if (MAJOR(rdev) == MISC_MAJOR && MINOR(rdev) == rpc_sema_minor)
		return true;
	return false;
}

struct file *trpc_sema_get_from_fd(int fd)
{
	struct file *file;

	file = fget(fd);
	if (unlikely(file == NULL)) {
		pr_err("%s: fd %d is invalid\n", __func__, fd);
		return ERR_PTR(-EINVAL);
	}

	if (!is_trpc_sema_file(file)) {
		pr_err("%s: fd (%d) is not a trpc_sema file\n", __func__, fd);
		fput(file);
		return ERR_PTR(-EINVAL);
	}

	return file;
}

int trpc_sema_signal(struct file *file)
{
	struct trpc_sema *info = file->private_data;
	unsigned long flags;

	if (!info)
		return -EINVAL;

	spin_lock_irqsave(&info->lock, flags);
	info->count++;
	wake_up_interruptible_all(&info->wq);
	spin_unlock_irqrestore(&info->lock, flags);
	return 0;
}

static int trpc_sema_wait(struct trpc_sema *info, long *timeleft)
{
	unsigned long flags;
	int ret = 0;
	unsigned long endtime;
	long timeout = *timeleft;

	*timeleft = 0;
	if (timeout < 0) {
		timeout = MAX_SCHEDULE_TIMEOUT;
	} else if (timeout > 0) {
		timeout = msecs_to_jiffies(timeout);
		endtime = jiffies + timeout;
	}

again:
	if (timeout)
		ret = wait_event_interruptible_timeout(info->wq,
						       info->count > 0,
						       timeout);
	spin_lock_irqsave(&info->lock, flags);
	if (info->count > 0) {
		info->count--;
		ret = 0;
	} else if (ret == 0 || timeout == 0) {
		ret = -ETIMEDOUT;
	} else if (ret < 0) {
		ret = -EINTR;
		if (timeout != MAX_SCHEDULE_TIMEOUT &&
		    time_before(jiffies, endtime))
			*timeleft = jiffies_to_msecs(endtime - jiffies);
		else
			*timeleft = 0;
	} else {
		/* we woke up but someone else got the semaphore and we have
		 * time left, try again */
		timeout = ret;
		spin_unlock_irqrestore(&info->lock, flags);
		goto again;
	}
	spin_unlock_irqrestore(&info->lock, flags);
	return ret;
}

static int trpc_sema_open(struct inode *inode, struct file *file)
{
	struct trpc_sema *info;

	info = kzalloc(sizeof(struct trpc_sema), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	nonseekable_open(inode, file);
	init_waitqueue_head(&info->wq);
	spin_lock_init(&info->lock);
	file->private_data = info;
	return 0;
}

static int trpc_sema_release(struct inode *inode, struct file *file)
{
	struct trpc_sema *info = file->private_data;

	file->private_data = NULL;
	kfree(info);
	return 0;
}

static long trpc_sema_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct trpc_sema *info = file->private_data;
	int ret;
	long timeout;

	if (_IOC_TYPE(cmd) != TEGRA_SEMA_IOCTL_MAGIC ||
	    _IOC_NR(cmd) < TEGRA_SEMA_IOCTL_MIN_NR ||
	    _IOC_NR(cmd) > TEGRA_SEMA_IOCTL_MAX_NR)
		return -ENOTTY;
	else if (!info)
		return -EINVAL;

	switch (cmd) {
	case TEGRA_SEMA_IOCTL_WAIT:
		if (copy_from_user(&timeout, (void __user *)arg, sizeof(long)))
			return -EFAULT;
		ret = trpc_sema_wait(info, &timeout);
		if (ret != -EINTR)
			break;
		if (copy_to_user((void __user *)arg, &timeout, sizeof(long)))
			ret = -EFAULT;
		break;
	case TEGRA_SEMA_IOCTL_SIGNAL:
		ret = trpc_sema_signal(file);
		break;
	default:
		pr_err("%s: Unknown tegra_sema ioctl 0x%x\n", __func__,
		       _IOC_NR(cmd));
		ret = -ENOTTY;
		break;
	}
	return ret;
}

static const struct file_operations trpc_sema_misc_fops = {
	.owner		= THIS_MODULE,
	.open		= trpc_sema_open,
	.release	= trpc_sema_release,
	.unlocked_ioctl	= trpc_sema_ioctl,
};

static struct miscdevice trpc_sema_misc_device = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "tegra_sema",
	.fops	= &trpc_sema_misc_fops,
};

int __init trpc_sema_init(void)
{
	int ret;

	if (rpc_sema_minor >= 0) {
		pr_err("%s: trpc_sema already registered\n", __func__);
		return -EBUSY;
	}

	ret = misc_register(&trpc_sema_misc_device);
	if (ret) {
		pr_err("%s: can't register misc device\n", __func__);
		return ret;
	}

	rpc_sema_minor = trpc_sema_misc_device.minor;
	pr_info("%s: registered misc dev %d:%d\n", __func__, MISC_MAJOR,
		rpc_sema_minor);

	return 0;
}

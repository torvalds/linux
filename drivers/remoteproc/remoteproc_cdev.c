// SPDX-License-Identifier: GPL-2.0-only
/*
 * Character device interface driver for Remoteproc framework.
 *
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/remoteproc.h>
#include <linux/uaccess.h>
#include <uapi/linux/remoteproc_cdev.h>

#include "remoteproc_internal.h"

#define NUM_RPROC_DEVICES	64
static dev_t rproc_major;

static ssize_t rproc_cdev_write(struct file *filp, const char __user *buf, size_t len, loff_t *pos)
{
	struct rproc *rproc = container_of(filp->f_inode->i_cdev, struct rproc, cdev);
	int ret = 0;
	char cmd[10];

	if (!len || len > sizeof(cmd))
		return -EINVAL;

	ret = copy_from_user(cmd, buf, len);
	if (ret)
		return -EFAULT;

	if (!strncmp(cmd, "start", len)) {
		if (rproc->state == RPROC_RUNNING)
			return -EBUSY;

		ret = rproc_boot(rproc);
	} else if (!strncmp(cmd, "stop", len)) {
		if (rproc->state != RPROC_RUNNING)
			return -EINVAL;

		rproc_shutdown(rproc);
	} else {
		dev_err(&rproc->dev, "Unrecognized option\n");
		ret = -EINVAL;
	}

	return ret ? ret : len;
}

static long rproc_device_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	struct rproc *rproc = container_of(filp->f_inode->i_cdev, struct rproc, cdev);
	void __user *argp = (void __user *)arg;
	s32 param;

	switch (ioctl) {
	case RPROC_SET_SHUTDOWN_ON_RELEASE:
		if (copy_from_user(&param, argp, sizeof(s32)))
			return -EFAULT;

		rproc->cdev_put_on_release = !!param;
		break;
	case RPROC_GET_SHUTDOWN_ON_RELEASE:
		param = (s32)rproc->cdev_put_on_release;
		if (copy_to_user(argp, &param, sizeof(s32)))
			return -EFAULT;

		break;
	default:
		dev_err(&rproc->dev, "Unsupported ioctl\n");
		return -EINVAL;
	}

	return 0;
}

static int rproc_cdev_release(struct inode *inode, struct file *filp)
{
	struct rproc *rproc = container_of(inode->i_cdev, struct rproc, cdev);

	if (rproc->cdev_put_on_release && rproc->state == RPROC_RUNNING)
		rproc_shutdown(rproc);

	return 0;
}

static const struct file_operations rproc_fops = {
	.write = rproc_cdev_write,
	.unlocked_ioctl = rproc_device_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.release = rproc_cdev_release,
};

int rproc_char_device_add(struct rproc *rproc)
{
	int ret;

	cdev_init(&rproc->cdev, &rproc_fops);
	rproc->cdev.owner = THIS_MODULE;

	rproc->dev.devt = MKDEV(MAJOR(rproc_major), rproc->index);
	cdev_set_parent(&rproc->cdev, &rproc->dev.kobj);
	ret = cdev_add(&rproc->cdev, rproc->dev.devt, 1);
	if (ret < 0)
		dev_err(&rproc->dev, "Failed to add char dev for %s\n", rproc->name);

	return ret;
}

void rproc_char_device_remove(struct rproc *rproc)
{
	__unregister_chrdev(MAJOR(rproc->dev.devt), rproc->index, 1, "remoteproc");
}

void __init rproc_init_cdev(void)
{
	int ret;

	ret = alloc_chrdev_region(&rproc_major, 0, NUM_RPROC_DEVICES, "remoteproc");
	if (ret < 0)
		pr_err("Failed to alloc rproc_cdev region, err %d\n", ret);
}

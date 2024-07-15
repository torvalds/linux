// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022, STMicroelectronics
 * Copyright (c) 2016, Linaro Ltd.
 * Copyright (c) 2012, Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2012, PetaLogix
 * Copyright (c) 2011, Texas Instruments, Inc.
 * Copyright (c) 2011, Google, Inc.
 *
 * Based on rpmsg performance statistics driver by Michal Simek, which in turn
 * was based on TI & Google OMX rpmsg driver.
 */

#define pr_fmt(fmt)		KBUILD_MODNAME ": " fmt

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <uapi/linux/rpmsg.h>

#include "rpmsg_char.h"
#include "rpmsg_internal.h"

#define RPMSG_DEV_MAX	(MINORMASK + 1)

static dev_t rpmsg_major;

static DEFINE_IDA(rpmsg_ctrl_ida);
static DEFINE_IDA(rpmsg_minor_ida);

#define dev_to_ctrldev(dev) container_of(dev, struct rpmsg_ctrldev, dev)
#define cdev_to_ctrldev(i_cdev) container_of(i_cdev, struct rpmsg_ctrldev, cdev)

/**
 * struct rpmsg_ctrldev - control device for instantiating endpoint devices
 * @rpdev:	underlaying rpmsg device
 * @cdev:	cdev for the ctrl device
 * @dev:	device for the ctrl device
 * @ctrl_lock:	serialize the ioctrls.
 */
struct rpmsg_ctrldev {
	struct rpmsg_device *rpdev;
	struct cdev cdev;
	struct device dev;
	struct mutex ctrl_lock;
};

static int rpmsg_ctrldev_open(struct inode *inode, struct file *filp)
{
	struct rpmsg_ctrldev *ctrldev = cdev_to_ctrldev(inode->i_cdev);

	get_device(&ctrldev->dev);
	filp->private_data = ctrldev;

	return 0;
}

static int rpmsg_ctrldev_release(struct inode *inode, struct file *filp)
{
	struct rpmsg_ctrldev *ctrldev = cdev_to_ctrldev(inode->i_cdev);

	put_device(&ctrldev->dev);

	return 0;
}

static long rpmsg_ctrldev_ioctl(struct file *fp, unsigned int cmd,
				unsigned long arg)
{
	struct rpmsg_ctrldev *ctrldev = fp->private_data;
	void __user *argp = (void __user *)arg;
	struct rpmsg_endpoint_info eptinfo;
	struct rpmsg_channel_info chinfo;
	struct rpmsg_device *rpdev;
	int ret = 0;

	if (copy_from_user(&eptinfo, argp, sizeof(eptinfo)))
		return -EFAULT;

	memcpy(chinfo.name, eptinfo.name, RPMSG_NAME_SIZE);
	chinfo.name[RPMSG_NAME_SIZE - 1] = '\0';
	chinfo.src = eptinfo.src;
	chinfo.dst = eptinfo.dst;

	mutex_lock(&ctrldev->ctrl_lock);
	switch (cmd) {
	case RPMSG_CREATE_EPT_IOCTL:
		ret = rpmsg_chrdev_eptdev_create(ctrldev->rpdev, &ctrldev->dev, chinfo);
		break;

	case RPMSG_CREATE_DEV_IOCTL:
		rpdev = rpmsg_create_channel(ctrldev->rpdev, &chinfo);
		if (!rpdev) {
			dev_err(&ctrldev->dev, "failed to create %s channel\n", chinfo.name);
			ret = -ENXIO;
		}
		break;

	case RPMSG_RELEASE_DEV_IOCTL:
		ret = rpmsg_release_channel(ctrldev->rpdev, &chinfo);
		if (ret)
			dev_err(&ctrldev->dev, "failed to release %s channel (%d)\n",
				chinfo.name, ret);
		break;

	default:
		ret = -EINVAL;
	}
	mutex_unlock(&ctrldev->ctrl_lock);

	return ret;
};

static const struct file_operations rpmsg_ctrldev_fops = {
	.owner = THIS_MODULE,
	.open = rpmsg_ctrldev_open,
	.release = rpmsg_ctrldev_release,
	.unlocked_ioctl = rpmsg_ctrldev_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static void rpmsg_ctrldev_release_device(struct device *dev)
{
	struct rpmsg_ctrldev *ctrldev = dev_to_ctrldev(dev);

	ida_free(&rpmsg_ctrl_ida, dev->id);
	ida_free(&rpmsg_minor_ida, MINOR(dev->devt));
	kfree(ctrldev);
}

static int rpmsg_ctrldev_probe(struct rpmsg_device *rpdev)
{
	struct rpmsg_ctrldev *ctrldev;
	struct device *dev;
	int ret;

	ctrldev = kzalloc(sizeof(*ctrldev), GFP_KERNEL);
	if (!ctrldev)
		return -ENOMEM;

	ctrldev->rpdev = rpdev;

	dev = &ctrldev->dev;
	device_initialize(dev);
	dev->parent = &rpdev->dev;
	dev->class = &rpmsg_class;

	mutex_init(&ctrldev->ctrl_lock);
	cdev_init(&ctrldev->cdev, &rpmsg_ctrldev_fops);
	ctrldev->cdev.owner = THIS_MODULE;

	ret = ida_alloc_max(&rpmsg_minor_ida, RPMSG_DEV_MAX - 1, GFP_KERNEL);
	if (ret < 0)
		goto free_ctrldev;
	dev->devt = MKDEV(MAJOR(rpmsg_major), ret);

	ret = ida_alloc(&rpmsg_ctrl_ida, GFP_KERNEL);
	if (ret < 0)
		goto free_minor_ida;
	dev->id = ret;
	dev_set_name(&ctrldev->dev, "rpmsg_ctrl%d", ret);

	ret = cdev_device_add(&ctrldev->cdev, &ctrldev->dev);
	if (ret)
		goto free_ctrl_ida;

	/* We can now rely on the release function for cleanup */
	dev->release = rpmsg_ctrldev_release_device;

	dev_set_drvdata(&rpdev->dev, ctrldev);

	return ret;

free_ctrl_ida:
	ida_free(&rpmsg_ctrl_ida, dev->id);
free_minor_ida:
	ida_free(&rpmsg_minor_ida, MINOR(dev->devt));
free_ctrldev:
	put_device(dev);
	kfree(ctrldev);

	return ret;
}

static void rpmsg_ctrldev_remove(struct rpmsg_device *rpdev)
{
	struct rpmsg_ctrldev *ctrldev = dev_get_drvdata(&rpdev->dev);
	int ret;

	mutex_lock(&ctrldev->ctrl_lock);
	/* Destroy all endpoints */
	ret = device_for_each_child(&ctrldev->dev, NULL, rpmsg_chrdev_eptdev_destroy);
	if (ret)
		dev_warn(&rpdev->dev, "failed to nuke endpoints: %d\n", ret);
	mutex_unlock(&ctrldev->ctrl_lock);

	cdev_device_del(&ctrldev->cdev, &ctrldev->dev);
	put_device(&ctrldev->dev);
}

static struct rpmsg_driver rpmsg_ctrldev_driver = {
	.probe = rpmsg_ctrldev_probe,
	.remove = rpmsg_ctrldev_remove,
	.drv = {
		.name = "rpmsg_ctrl",
	},
};

static int rpmsg_ctrldev_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&rpmsg_major, 0, RPMSG_DEV_MAX, "rpmsg_ctrl");
	if (ret < 0) {
		pr_err("failed to allocate char dev region\n");
		return ret;
	}

	ret = register_rpmsg_driver(&rpmsg_ctrldev_driver);
	if (ret < 0) {
		pr_err("failed to register rpmsg driver\n");
		unregister_chrdev_region(rpmsg_major, RPMSG_DEV_MAX);
	}

	return ret;
}
postcore_initcall(rpmsg_ctrldev_init);

static void rpmsg_ctrldev_exit(void)
{
	unregister_rpmsg_driver(&rpmsg_ctrldev_driver);
	unregister_chrdev_region(rpmsg_major, RPMSG_DEV_MAX);
}
module_exit(rpmsg_ctrldev_exit);

MODULE_DESCRIPTION("rpmsg control interface");
MODULE_ALIAS("rpmsg:" KBUILD_MODNAME);
MODULE_LICENSE("GPL v2");

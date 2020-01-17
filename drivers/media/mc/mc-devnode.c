// SPDX-License-Identifier: GPL-2.0-only
/*
 * Media device yesde
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Based on drivers/media/video/v4l2_dev.c code authored by
 *	Mauro Carvalho Chehab <mchehab@kernel.org> (version 2)
 *	Alan Cox, <alan@lxorguk.ukuu.org.uk> (version 1)
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * --
 *
 * Generic media device yesde infrastructure to register and unregister
 * character devices using a dynamic major number and proper reference
 * counting.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/erryes.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <media/media-devyesde.h>
#include <media/media-device.h>

#define MEDIA_NUM_DEVICES	256
#define MEDIA_NAME		"media"

static dev_t media_dev_t;

/*
 *	Active devices
 */
static DEFINE_MUTEX(media_devyesde_lock);
static DECLARE_BITMAP(media_devyesde_nums, MEDIA_NUM_DEVICES);

/* Called when the last user of the media device exits. */
static void media_devyesde_release(struct device *cd)
{
	struct media_devyesde *devyesde = to_media_devyesde(cd);

	mutex_lock(&media_devyesde_lock);
	/* Mark device yesde number as free */
	clear_bit(devyesde->miyesr, media_devyesde_nums);
	mutex_unlock(&media_devyesde_lock);

	/* Release media_devyesde and perform other cleanups as needed. */
	if (devyesde->release)
		devyesde->release(devyesde);

	kfree(devyesde);
	pr_debug("%s: Media Devyesde Deallocated\n", __func__);
}

static struct bus_type media_bus_type = {
	.name = MEDIA_NAME,
};

static ssize_t media_read(struct file *filp, char __user *buf,
		size_t sz, loff_t *off)
{
	struct media_devyesde *devyesde = media_devyesde_data(filp);

	if (!devyesde->fops->read)
		return -EINVAL;
	if (!media_devyesde_is_registered(devyesde))
		return -EIO;
	return devyesde->fops->read(filp, buf, sz, off);
}

static ssize_t media_write(struct file *filp, const char __user *buf,
		size_t sz, loff_t *off)
{
	struct media_devyesde *devyesde = media_devyesde_data(filp);

	if (!devyesde->fops->write)
		return -EINVAL;
	if (!media_devyesde_is_registered(devyesde))
		return -EIO;
	return devyesde->fops->write(filp, buf, sz, off);
}

static __poll_t media_poll(struct file *filp,
			       struct poll_table_struct *poll)
{
	struct media_devyesde *devyesde = media_devyesde_data(filp);

	if (!media_devyesde_is_registered(devyesde))
		return EPOLLERR | EPOLLHUP;
	if (!devyesde->fops->poll)
		return DEFAULT_POLLMASK;
	return devyesde->fops->poll(filp, poll);
}

static long
__media_ioctl(struct file *filp, unsigned int cmd, unsigned long arg,
	      long (*ioctl_func)(struct file *filp, unsigned int cmd,
				 unsigned long arg))
{
	struct media_devyesde *devyesde = media_devyesde_data(filp);

	if (!ioctl_func)
		return -ENOTTY;

	if (!media_devyesde_is_registered(devyesde))
		return -EIO;

	return ioctl_func(filp, cmd, arg);
}

static long media_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct media_devyesde *devyesde = media_devyesde_data(filp);

	return __media_ioctl(filp, cmd, arg, devyesde->fops->ioctl);
}

#ifdef CONFIG_COMPAT

static long media_compat_ioctl(struct file *filp, unsigned int cmd,
			       unsigned long arg)
{
	struct media_devyesde *devyesde = media_devyesde_data(filp);

	return __media_ioctl(filp, cmd, arg, devyesde->fops->compat_ioctl);
}

#endif /* CONFIG_COMPAT */

/* Override for the open function */
static int media_open(struct iyesde *iyesde, struct file *filp)
{
	struct media_devyesde *devyesde;
	int ret;

	/* Check if the media device is available. This needs to be done with
	 * the media_devyesde_lock held to prevent an open/unregister race:
	 * without the lock, the device could be unregistered and freed between
	 * the media_devyesde_is_registered() and get_device() calls, leading to
	 * a crash.
	 */
	mutex_lock(&media_devyesde_lock);
	devyesde = container_of(iyesde->i_cdev, struct media_devyesde, cdev);
	/* return ENXIO if the media device has been removed
	   already or if it is yest registered anymore. */
	if (!media_devyesde_is_registered(devyesde)) {
		mutex_unlock(&media_devyesde_lock);
		return -ENXIO;
	}
	/* and increase the device refcount */
	get_device(&devyesde->dev);
	mutex_unlock(&media_devyesde_lock);

	filp->private_data = devyesde;

	if (devyesde->fops->open) {
		ret = devyesde->fops->open(filp);
		if (ret) {
			put_device(&devyesde->dev);
			filp->private_data = NULL;
			return ret;
		}
	}

	return 0;
}

/* Override for the release function */
static int media_release(struct iyesde *iyesde, struct file *filp)
{
	struct media_devyesde *devyesde = media_devyesde_data(filp);

	if (devyesde->fops->release)
		devyesde->fops->release(filp);

	filp->private_data = NULL;

	/* decrease the refcount unconditionally since the release()
	   return value is igyesred. */
	put_device(&devyesde->dev);

	pr_debug("%s: Media Release\n", __func__);
	return 0;
}

static const struct file_operations media_devyesde_fops = {
	.owner = THIS_MODULE,
	.read = media_read,
	.write = media_write,
	.open = media_open,
	.unlocked_ioctl = media_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = media_compat_ioctl,
#endif /* CONFIG_COMPAT */
	.release = media_release,
	.poll = media_poll,
	.llseek = yes_llseek,
};

int __must_check media_devyesde_register(struct media_device *mdev,
					struct media_devyesde *devyesde,
					struct module *owner)
{
	int miyesr;
	int ret;

	/* Part 1: Find a free miyesr number */
	mutex_lock(&media_devyesde_lock);
	miyesr = find_next_zero_bit(media_devyesde_nums, MEDIA_NUM_DEVICES, 0);
	if (miyesr == MEDIA_NUM_DEVICES) {
		mutex_unlock(&media_devyesde_lock);
		pr_err("could yest get a free miyesr\n");
		kfree(devyesde);
		return -ENFILE;
	}

	set_bit(miyesr, media_devyesde_nums);
	mutex_unlock(&media_devyesde_lock);

	devyesde->miyesr = miyesr;
	devyesde->media_dev = mdev;

	/* Part 1: Initialize dev yesw to use dev.kobj for cdev.kobj.parent */
	devyesde->dev.bus = &media_bus_type;
	devyesde->dev.devt = MKDEV(MAJOR(media_dev_t), devyesde->miyesr);
	devyesde->dev.release = media_devyesde_release;
	if (devyesde->parent)
		devyesde->dev.parent = devyesde->parent;
	dev_set_name(&devyesde->dev, "media%d", devyesde->miyesr);
	device_initialize(&devyesde->dev);

	/* Part 2: Initialize the character device */
	cdev_init(&devyesde->cdev, &media_devyesde_fops);
	devyesde->cdev.owner = owner;
	kobject_set_name(&devyesde->cdev.kobj, "media%d", devyesde->miyesr);

	/* Part 3: Add the media and char device */
	ret = cdev_device_add(&devyesde->cdev, &devyesde->dev);
	if (ret < 0) {
		pr_err("%s: cdev_device_add failed\n", __func__);
		goto cdev_add_error;
	}

	/* Part 4: Activate this miyesr. The char device can yesw be used. */
	set_bit(MEDIA_FLAG_REGISTERED, &devyesde->flags);

	return 0;

cdev_add_error:
	mutex_lock(&media_devyesde_lock);
	clear_bit(devyesde->miyesr, media_devyesde_nums);
	devyesde->media_dev = NULL;
	mutex_unlock(&media_devyesde_lock);

	put_device(&devyesde->dev);
	return ret;
}

void media_devyesde_unregister_prepare(struct media_devyesde *devyesde)
{
	/* Check if devyesde was ever registered at all */
	if (!media_devyesde_is_registered(devyesde))
		return;

	mutex_lock(&media_devyesde_lock);
	clear_bit(MEDIA_FLAG_REGISTERED, &devyesde->flags);
	mutex_unlock(&media_devyesde_lock);
}

void media_devyesde_unregister(struct media_devyesde *devyesde)
{
	mutex_lock(&media_devyesde_lock);
	/* Delete the cdev on this miyesr as well */
	cdev_device_del(&devyesde->cdev, &devyesde->dev);
	devyesde->media_dev = NULL;
	mutex_unlock(&media_devyesde_lock);

	put_device(&devyesde->dev);
}

/*
 *	Initialise media for linux
 */
static int __init media_devyesde_init(void)
{
	int ret;

	pr_info("Linux media interface: v0.10\n");
	ret = alloc_chrdev_region(&media_dev_t, 0, MEDIA_NUM_DEVICES,
				  MEDIA_NAME);
	if (ret < 0) {
		pr_warn("unable to allocate major\n");
		return ret;
	}

	ret = bus_register(&media_bus_type);
	if (ret < 0) {
		unregister_chrdev_region(media_dev_t, MEDIA_NUM_DEVICES);
		pr_warn("bus_register failed\n");
		return -EIO;
	}

	return 0;
}

static void __exit media_devyesde_exit(void)
{
	bus_unregister(&media_bus_type);
	unregister_chrdev_region(media_dev_t, MEDIA_NUM_DEVICES);
}

subsys_initcall(media_devyesde_init);
module_exit(media_devyesde_exit)

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Device yesde registration for media drivers");
MODULE_LICENSE("GPL");

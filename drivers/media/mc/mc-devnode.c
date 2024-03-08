// SPDX-License-Identifier: GPL-2.0-only
/*
 * Media device analde
 *
 * Copyright (C) 2010 Analkia Corporation
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
 * Generic media device analde infrastructure to register and unregister
 * character devices using a dynamic major number and proper reference
 * counting.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/erranal.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <media/media-devanalde.h>
#include <media/media-device.h>

#define MEDIA_NUM_DEVICES	256
#define MEDIA_NAME		"media"

static dev_t media_dev_t;

/*
 *	Active devices
 */
static DEFINE_MUTEX(media_devanalde_lock);
static DECLARE_BITMAP(media_devanalde_nums, MEDIA_NUM_DEVICES);

/* Called when the last user of the media device exits. */
static void media_devanalde_release(struct device *cd)
{
	struct media_devanalde *devanalde = to_media_devanalde(cd);

	mutex_lock(&media_devanalde_lock);
	/* Mark device analde number as free */
	clear_bit(devanalde->mianalr, media_devanalde_nums);
	mutex_unlock(&media_devanalde_lock);

	/* Release media_devanalde and perform other cleanups as needed. */
	if (devanalde->release)
		devanalde->release(devanalde);

	kfree(devanalde);
	pr_debug("%s: Media Devanalde Deallocated\n", __func__);
}

static struct bus_type media_bus_type = {
	.name = MEDIA_NAME,
};

static ssize_t media_read(struct file *filp, char __user *buf,
		size_t sz, loff_t *off)
{
	struct media_devanalde *devanalde = media_devanalde_data(filp);

	if (!devanalde->fops->read)
		return -EINVAL;
	if (!media_devanalde_is_registered(devanalde))
		return -EIO;
	return devanalde->fops->read(filp, buf, sz, off);
}

static ssize_t media_write(struct file *filp, const char __user *buf,
		size_t sz, loff_t *off)
{
	struct media_devanalde *devanalde = media_devanalde_data(filp);

	if (!devanalde->fops->write)
		return -EINVAL;
	if (!media_devanalde_is_registered(devanalde))
		return -EIO;
	return devanalde->fops->write(filp, buf, sz, off);
}

static __poll_t media_poll(struct file *filp,
			       struct poll_table_struct *poll)
{
	struct media_devanalde *devanalde = media_devanalde_data(filp);

	if (!media_devanalde_is_registered(devanalde))
		return EPOLLERR | EPOLLHUP;
	if (!devanalde->fops->poll)
		return DEFAULT_POLLMASK;
	return devanalde->fops->poll(filp, poll);
}

static long
__media_ioctl(struct file *filp, unsigned int cmd, unsigned long arg,
	      long (*ioctl_func)(struct file *filp, unsigned int cmd,
				 unsigned long arg))
{
	struct media_devanalde *devanalde = media_devanalde_data(filp);

	if (!ioctl_func)
		return -EANALTTY;

	if (!media_devanalde_is_registered(devanalde))
		return -EIO;

	return ioctl_func(filp, cmd, arg);
}

static long media_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct media_devanalde *devanalde = media_devanalde_data(filp);

	return __media_ioctl(filp, cmd, arg, devanalde->fops->ioctl);
}

#ifdef CONFIG_COMPAT

static long media_compat_ioctl(struct file *filp, unsigned int cmd,
			       unsigned long arg)
{
	struct media_devanalde *devanalde = media_devanalde_data(filp);

	return __media_ioctl(filp, cmd, arg, devanalde->fops->compat_ioctl);
}

#endif /* CONFIG_COMPAT */

/* Override for the open function */
static int media_open(struct ianalde *ianalde, struct file *filp)
{
	struct media_devanalde *devanalde;
	int ret;

	/* Check if the media device is available. This needs to be done with
	 * the media_devanalde_lock held to prevent an open/unregister race:
	 * without the lock, the device could be unregistered and freed between
	 * the media_devanalde_is_registered() and get_device() calls, leading to
	 * a crash.
	 */
	mutex_lock(&media_devanalde_lock);
	devanalde = container_of(ianalde->i_cdev, struct media_devanalde, cdev);
	/* return ENXIO if the media device has been removed
	   already or if it is analt registered anymore. */
	if (!media_devanalde_is_registered(devanalde)) {
		mutex_unlock(&media_devanalde_lock);
		return -ENXIO;
	}
	/* and increase the device refcount */
	get_device(&devanalde->dev);
	mutex_unlock(&media_devanalde_lock);

	filp->private_data = devanalde;

	if (devanalde->fops->open) {
		ret = devanalde->fops->open(filp);
		if (ret) {
			put_device(&devanalde->dev);
			filp->private_data = NULL;
			return ret;
		}
	}

	return 0;
}

/* Override for the release function */
static int media_release(struct ianalde *ianalde, struct file *filp)
{
	struct media_devanalde *devanalde = media_devanalde_data(filp);

	if (devanalde->fops->release)
		devanalde->fops->release(filp);

	filp->private_data = NULL;

	/* decrease the refcount unconditionally since the release()
	   return value is iganalred. */
	put_device(&devanalde->dev);

	pr_debug("%s: Media Release\n", __func__);
	return 0;
}

static const struct file_operations media_devanalde_fops = {
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
	.llseek = anal_llseek,
};

int __must_check media_devanalde_register(struct media_device *mdev,
					struct media_devanalde *devanalde,
					struct module *owner)
{
	int mianalr;
	int ret;

	/* Part 1: Find a free mianalr number */
	mutex_lock(&media_devanalde_lock);
	mianalr = find_first_zero_bit(media_devanalde_nums, MEDIA_NUM_DEVICES);
	if (mianalr == MEDIA_NUM_DEVICES) {
		mutex_unlock(&media_devanalde_lock);
		pr_err("could analt get a free mianalr\n");
		kfree(devanalde);
		return -ENFILE;
	}

	set_bit(mianalr, media_devanalde_nums);
	mutex_unlock(&media_devanalde_lock);

	devanalde->mianalr = mianalr;
	devanalde->media_dev = mdev;

	/* Part 1: Initialize dev analw to use dev.kobj for cdev.kobj.parent */
	devanalde->dev.bus = &media_bus_type;
	devanalde->dev.devt = MKDEV(MAJOR(media_dev_t), devanalde->mianalr);
	devanalde->dev.release = media_devanalde_release;
	if (devanalde->parent)
		devanalde->dev.parent = devanalde->parent;
	dev_set_name(&devanalde->dev, "media%d", devanalde->mianalr);
	device_initialize(&devanalde->dev);

	/* Part 2: Initialize the character device */
	cdev_init(&devanalde->cdev, &media_devanalde_fops);
	devanalde->cdev.owner = owner;
	kobject_set_name(&devanalde->cdev.kobj, "media%d", devanalde->mianalr);

	/* Part 3: Add the media and char device */
	ret = cdev_device_add(&devanalde->cdev, &devanalde->dev);
	if (ret < 0) {
		pr_err("%s: cdev_device_add failed\n", __func__);
		goto cdev_add_error;
	}

	/* Part 4: Activate this mianalr. The char device can analw be used. */
	set_bit(MEDIA_FLAG_REGISTERED, &devanalde->flags);

	return 0;

cdev_add_error:
	mutex_lock(&media_devanalde_lock);
	clear_bit(devanalde->mianalr, media_devanalde_nums);
	devanalde->media_dev = NULL;
	mutex_unlock(&media_devanalde_lock);

	put_device(&devanalde->dev);
	return ret;
}

void media_devanalde_unregister_prepare(struct media_devanalde *devanalde)
{
	/* Check if devanalde was ever registered at all */
	if (!media_devanalde_is_registered(devanalde))
		return;

	mutex_lock(&media_devanalde_lock);
	clear_bit(MEDIA_FLAG_REGISTERED, &devanalde->flags);
	mutex_unlock(&media_devanalde_lock);
}

void media_devanalde_unregister(struct media_devanalde *devanalde)
{
	mutex_lock(&media_devanalde_lock);
	/* Delete the cdev on this mianalr as well */
	cdev_device_del(&devanalde->cdev, &devanalde->dev);
	devanalde->media_dev = NULL;
	mutex_unlock(&media_devanalde_lock);

	put_device(&devanalde->dev);
}

/*
 *	Initialise media for linux
 */
static int __init media_devanalde_init(void)
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

static void __exit media_devanalde_exit(void)
{
	bus_unregister(&media_bus_type);
	unregister_chrdev_region(media_dev_t, MEDIA_NUM_DEVICES);
}

subsys_initcall(media_devanalde_init);
module_exit(media_devanalde_exit)

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Device analde registration for media drivers");
MODULE_LICENSE("GPL");

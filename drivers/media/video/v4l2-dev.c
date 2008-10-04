/*
 * Video capture interface for Linux version 2
 *
 *	A generic video device interface for the LINUX operating system
 *	using a set of device structures/vectors for low level operations.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * Authors:	Alan Cox, <alan@redhat.com> (version 1)
 *              Mauro Carvalho Chehab <mchehab@infradead.org> (version 2)
 *
 * Fixes:	20000516  Claudio Matsuoka <claudio@conectiva.com>
 *		- Added procfs support
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#include <media/v4l2-common.h>

#define VIDEO_NUM_DEVICES	256
#define VIDEO_NAME              "video4linux"

/*
 *	sysfs stuff
 */

static ssize_t show_index(struct device *cd,
			 struct device_attribute *attr, char *buf)
{
	struct video_device *vfd = container_of(cd, struct video_device, dev);

	return sprintf(buf, "%i\n", vfd->index);
}

static ssize_t show_name(struct device *cd,
			 struct device_attribute *attr, char *buf)
{
	struct video_device *vfd = container_of(cd, struct video_device, dev);

	return sprintf(buf, "%.*s\n", (int)sizeof(vfd->name), vfd->name);
}

static struct device_attribute video_device_attrs[] = {
	__ATTR(name, S_IRUGO, show_name, NULL),
	__ATTR(index, S_IRUGO, show_index, NULL),
	__ATTR_NULL
};

/*
 *	Active devices
 */
static struct video_device *video_device[VIDEO_NUM_DEVICES];
static DEFINE_MUTEX(videodev_lock);
static DECLARE_BITMAP(video_nums[VFL_TYPE_MAX], VIDEO_NUM_DEVICES);

struct video_device *video_device_alloc(void)
{
	return kzalloc(sizeof(struct video_device), GFP_KERNEL);
}
EXPORT_SYMBOL(video_device_alloc);

void video_device_release(struct video_device *vfd)
{
	kfree(vfd);
}
EXPORT_SYMBOL(video_device_release);

void video_device_release_empty(struct video_device *vfd)
{
	/* Do nothing */
	/* Only valid when the video_device struct is a static. */
}
EXPORT_SYMBOL(video_device_release_empty);

/* Called when the last user of the character device is gone. */
static void v4l2_chardev_release(struct kobject *kobj)
{
	struct video_device *vfd = container_of(kobj, struct video_device, cdev.kobj);

	mutex_lock(&videodev_lock);
	if (video_device[vfd->minor] != vfd) {
		mutex_unlock(&videodev_lock);
		BUG();
		return;
	}

	/* Free up this device for reuse */
	video_device[vfd->minor] = NULL;
	clear_bit(vfd->num, video_nums[vfd->vfl_type]);
	mutex_unlock(&videodev_lock);

	/* Release the character device */
	vfd->cdev_release(kobj);
	/* Release video_device and perform other
	   cleanups as needed. */
	if (vfd->release)
		vfd->release(vfd);
}

/* The new kobj_type for the character device */
static struct kobj_type v4l2_ktype_cdev_default = {
	.release = v4l2_chardev_release,
};

static void video_release(struct device *cd)
{
	struct video_device *vfd = container_of(cd, struct video_device, dev);

	/* It's now safe to delete the char device.
	   This will either trigger the v4l2_chardev_release immediately (if
	   the refcount goes to 0) or later when the last user of the
	   character device closes it. */
	cdev_del(&vfd->cdev);
}

static struct class video_class = {
	.name = VIDEO_NAME,
	.dev_attrs = video_device_attrs,
	.dev_release = video_release,
};

struct video_device *video_devdata(struct file *file)
{
	return video_device[iminor(file->f_path.dentry->d_inode)];
}
EXPORT_SYMBOL(video_devdata);

/**
 * get_index - assign stream number based on parent device
 * @vdev: video_device to assign index number to, vdev->dev should be assigned
 * @num: -1 if auto assign, requested number otherwise
 *
 *
 * returns -ENFILE if num is already in use, a free index number if
 * successful.
 */
static int get_index(struct video_device *vdev, int num)
{
	u32 used = 0;
	const int max_index = sizeof(used) * 8 - 1;
	int i;

	/* Currently a single v4l driver instance cannot create more than
	   32 devices.
	   Increase to u64 or an array of u32 if more are needed. */
	if (num > max_index) {
		printk(KERN_ERR "videodev: %s num is too large\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < VIDEO_NUM_DEVICES; i++) {
		if (video_device[i] != NULL &&
		    video_device[i] != vdev &&
		    video_device[i]->parent == vdev->parent) {
			used |= 1 << video_device[i]->index;
		}
	}

	if (num >= 0) {
		if (used & (1 << num))
			return -ENFILE;
		return num;
	}

	i = ffz(used);
	return i > max_index ? -ENFILE : i;
}

static const struct file_operations video_fops;

int video_register_device(struct video_device *vfd, int type, int nr)
{
	return video_register_device_index(vfd, type, nr, -1);
}
EXPORT_SYMBOL(video_register_device);

/**
 *	video_register_device_index - register video4linux devices
 *	@vfd:  video device structure we want to register
 *	@type: type of device to register
 *	@nr:   which device number (0 == /dev/video0, 1 == /dev/video1, ...
 *             -1 == first free)
 *	@index: stream number based on parent device;
 *		-1 if auto assign, requested number otherwise
 *
 *	The registration code assigns minor numbers based on the type
 *	requested. -ENFILE is returned in all the device slots for this
 *	category are full. If not then the minor field is set and the
 *	driver initialize function is called (if non %NULL).
 *
 *	Zero is returned on success.
 *
 *	Valid types are
 *
 *	%VFL_TYPE_GRABBER - A frame grabber
 *
 *	%VFL_TYPE_VTX - A teletext device
 *
 *	%VFL_TYPE_VBI - Vertical blank data (undecoded)
 *
 *	%VFL_TYPE_RADIO - A radio card
 */

int video_register_device_index(struct video_device *vfd, int type, int nr,
					int index)
{
	int i = 0;
	int ret;
	int minor_offset = 0;
	int minor_cnt = VIDEO_NUM_DEVICES;
	const char *name_base;
	void *priv = video_get_drvdata(vfd);

	/* the release callback MUST be present */
	BUG_ON(!vfd->release);

	if (vfd == NULL)
		return -EINVAL;

	switch (type) {
	case VFL_TYPE_GRABBER:
		name_base = "video";
		break;
	case VFL_TYPE_VTX:
		name_base = "vtx";
		break;
	case VFL_TYPE_VBI:
		name_base = "vbi";
		break;
	case VFL_TYPE_RADIO:
		name_base = "radio";
		break;
	default:
		printk(KERN_ERR "%s called with unknown type: %d\n",
		       __func__, type);
		return -EINVAL;
	}

	vfd->vfl_type = type;

#ifdef CONFIG_VIDEO_FIXED_MINOR_RANGES
	/* Keep the ranges for the first four types for historical
	 * reasons.
	 * Newer devices (not yet in place) should use the range
	 * of 128-191 and just pick the first free minor there
	 * (new style). */
	switch (type) {
	case VFL_TYPE_GRABBER:
		minor_offset = 0;
		minor_cnt = 64;
		break;
	case VFL_TYPE_RADIO:
		minor_offset = 64;
		minor_cnt = 64;
		break;
	case VFL_TYPE_VTX:
		minor_offset = 192;
		minor_cnt = 32;
		break;
	case VFL_TYPE_VBI:
		minor_offset = 224;
		minor_cnt = 32;
		break;
	default:
		minor_offset = 128;
		minor_cnt = 64;
		break;
	}
#endif

	/* Initialize the character device */
	cdev_init(&vfd->cdev, vfd->fops);
	vfd->cdev.owner = vfd->fops->owner;
	/* pick a minor number */
	mutex_lock(&videodev_lock);
	nr = find_next_zero_bit(video_nums[type], minor_cnt, nr == -1 ? 0 : nr);
	if (nr == minor_cnt)
		nr = find_first_zero_bit(video_nums[type], minor_cnt);
	if (nr == minor_cnt) {
		printk(KERN_ERR "could not get a free kernel number\n");
		mutex_unlock(&videodev_lock);
		return -ENFILE;
	}
#ifdef CONFIG_VIDEO_FIXED_MINOR_RANGES
	/* 1-on-1 mapping of kernel number to minor number */
	i = nr;
#else
	/* The kernel number and minor numbers are independent */
	for (i = 0; i < VIDEO_NUM_DEVICES; i++)
		if (video_device[i] == NULL)
			break;
	if (i == VIDEO_NUM_DEVICES) {
		mutex_unlock(&videodev_lock);
		printk(KERN_ERR "could not get a free minor\n");
		return -ENFILE;
	}
#endif
	vfd->minor = i + minor_offset;
	vfd->num = nr;
	set_bit(nr, video_nums[type]);
	BUG_ON(video_device[vfd->minor]);
	video_device[vfd->minor] = vfd;

	ret = get_index(vfd, index);
	vfd->index = ret;

	mutex_unlock(&videodev_lock);

	if (ret < 0) {
		printk(KERN_ERR "%s: get_index failed\n", __func__);
		goto fail_minor;
	}

	ret = cdev_add(&vfd->cdev, MKDEV(VIDEO_MAJOR, vfd->minor), 1);
	if (ret < 0) {
		printk(KERN_ERR "%s: cdev_add failed\n", __func__);
		goto fail_minor;
	}
	/* sysfs class */
	memset(&vfd->dev, 0, sizeof(vfd->dev));
	/* The memset above cleared the device's drvdata, so
	   put back the copy we made earlier. */
	video_set_drvdata(vfd, priv);
	vfd->dev.class = &video_class;
	vfd->dev.devt = MKDEV(VIDEO_MAJOR, vfd->minor);
	if (vfd->parent)
		vfd->dev.parent = vfd->parent;
	sprintf(vfd->dev.bus_id, "%s%d", name_base, nr);
	ret = device_register(&vfd->dev);
	if (ret < 0) {
		printk(KERN_ERR "%s: device_register failed\n", __func__);
		goto del_cdev;
	}
	/* Remember the cdev's release function */
	vfd->cdev_release = vfd->cdev.kobj.ktype->release;
	/* Install our own */
	vfd->cdev.kobj.ktype = &v4l2_ktype_cdev_default;
	return 0;

del_cdev:
	cdev_del(&vfd->cdev);

fail_minor:
	mutex_lock(&videodev_lock);
	video_device[vfd->minor] = NULL;
	clear_bit(vfd->num, video_nums[type]);
	mutex_unlock(&videodev_lock);
	vfd->minor = -1;
	return ret;
}
EXPORT_SYMBOL(video_register_device_index);

/**
 *	video_unregister_device - unregister a video4linux device
 *	@vfd: the device to unregister
 *
 *	This unregisters the passed device and deassigns the minor
 *	number. Future open calls will be met with errors.
 */

void video_unregister_device(struct video_device *vfd)
{
	device_unregister(&vfd->dev);
}
EXPORT_SYMBOL(video_unregister_device);

/*
 *	Initialise video for linux
 */
static int __init videodev_init(void)
{
	dev_t dev = MKDEV(VIDEO_MAJOR, 0);
	int ret;

	printk(KERN_INFO "Linux video capture interface: v2.00\n");
	ret = register_chrdev_region(dev, VIDEO_NUM_DEVICES, VIDEO_NAME);
	if (ret < 0) {
		printk(KERN_WARNING "videodev: unable to get major %d\n",
				VIDEO_MAJOR);
		return ret;
	}

	ret = class_register(&video_class);
	if (ret < 0) {
		unregister_chrdev_region(dev, VIDEO_NUM_DEVICES);
		printk(KERN_WARNING "video_dev: class_register failed\n");
		return -EIO;
	}

	return 0;
}

static void __exit videodev_exit(void)
{
	dev_t dev = MKDEV(VIDEO_MAJOR, 0);

	class_unregister(&video_class);
	unregister_chrdev_region(dev, VIDEO_NUM_DEVICES);
}

module_init(videodev_init)
module_exit(videodev_exit)

MODULE_AUTHOR("Alan Cox, Mauro Carvalho Chehab <mchehab@infradead.org>");
MODULE_DESCRIPTION("Device registrar for Video4Linux drivers v2");
MODULE_LICENSE("GPL");


/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

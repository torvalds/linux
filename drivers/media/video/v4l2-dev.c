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

struct video_device *video_device_alloc(void)
{
	struct video_device *vfd;

	vfd = kzalloc(sizeof(*vfd), GFP_KERNEL);
	return vfd;
}
EXPORT_SYMBOL(video_device_alloc);

void video_device_release(struct video_device *vfd)
{
	kfree(vfd);
}
EXPORT_SYMBOL(video_device_release);

static void video_release(struct device *cd)
{
	struct video_device *vfd = container_of(cd, struct video_device, dev);

#if 1
	/* needed until all drivers are fixed */
	if (!vfd->release)
		return;
#endif
	vfd->release(vfd);
}

static struct class video_class = {
	.name = VIDEO_NAME,
	.dev_attrs = video_device_attrs,
	.dev_release = video_release,
};

/*
 *	Active devices
 */

static struct video_device *video_device[VIDEO_NUM_DEVICES];
static DEFINE_MUTEX(videodev_lock);

struct video_device *video_devdata(struct file *file)
{
	return video_device[iminor(file->f_path.dentry->d_inode)];
}
EXPORT_SYMBOL(video_devdata);

/*
 *	Open a video device - FIXME: Obsoleted
 */
static int video_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	int err = 0;
	struct video_device *vfl;
	const struct file_operations *old_fops;

	if (minor >= VIDEO_NUM_DEVICES)
		return -ENODEV;
	lock_kernel();
	mutex_lock(&videodev_lock);
	vfl = video_device[minor];
	if (vfl == NULL) {
		mutex_unlock(&videodev_lock);
		request_module("char-major-%d-%d", VIDEO_MAJOR, minor);
		mutex_lock(&videodev_lock);
		vfl = video_device[minor];
		if (vfl == NULL) {
			mutex_unlock(&videodev_lock);
			unlock_kernel();
			return -ENODEV;
		}
	}
	old_fops = file->f_op;
	file->f_op = fops_get(vfl->fops);
	if (file->f_op->open)
		err = file->f_op->open(inode, file);
	if (err) {
		fops_put(file->f_op);
		file->f_op = fops_get(old_fops);
	}
	fops_put(old_fops);
	mutex_unlock(&videodev_lock);
	unlock_kernel();
	return err;
}

/*
 * open/release helper functions -- handle exclusive opens
 * Should be removed soon
 */
int video_exclusive_open(struct inode *inode, struct file *file)
{
	struct video_device *vfl = video_devdata(file);
	int retval = 0;

	mutex_lock(&vfl->lock);
	if (vfl->users)
		retval = -EBUSY;
	else
		vfl->users++;
	mutex_unlock(&vfl->lock);
	return retval;
}
EXPORT_SYMBOL(video_exclusive_open);

int video_exclusive_release(struct inode *inode, struct file *file)
{
	struct video_device *vfl = video_devdata(file);

	vfl->users--;
	return 0;
}
EXPORT_SYMBOL(video_exclusive_release);

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
	int base;
	int end;
	int ret;
	char *name_base;

	switch (type) {
	case VFL_TYPE_GRABBER:
		base = MINOR_VFL_TYPE_GRABBER_MIN;
		end = MINOR_VFL_TYPE_GRABBER_MAX+1;
		name_base = "video";
		break;
	case VFL_TYPE_VTX:
		base = MINOR_VFL_TYPE_VTX_MIN;
		end = MINOR_VFL_TYPE_VTX_MAX+1;
		name_base = "vtx";
		break;
	case VFL_TYPE_VBI:
		base = MINOR_VFL_TYPE_VBI_MIN;
		end = MINOR_VFL_TYPE_VBI_MAX+1;
		name_base = "vbi";
		break;
	case VFL_TYPE_RADIO:
		base = MINOR_VFL_TYPE_RADIO_MIN;
		end = MINOR_VFL_TYPE_RADIO_MAX+1;
		name_base = "radio";
		break;
	default:
		printk(KERN_ERR "%s called with unknown type: %d\n",
		       __func__, type);
		return -EINVAL;
	}

	/* pick a minor number */
	mutex_lock(&videodev_lock);
	if (nr >= 0  &&  nr < end-base) {
		/* use the one the driver asked for */
		i = base + nr;
		if (NULL != video_device[i]) {
			mutex_unlock(&videodev_lock);
			return -ENFILE;
		}
	} else {
		/* use first free */
		for (i = base; i < end; i++)
			if (NULL == video_device[i])
				break;
		if (i == end) {
			mutex_unlock(&videodev_lock);
			return -ENFILE;
		}
	}
	video_device[i] = vfd;
	vfd->vfl_type = type;
	vfd->minor = i;

	ret = get_index(vfd, index);
	vfd->index = ret;

	mutex_unlock(&videodev_lock);

	if (ret < 0) {
		printk(KERN_ERR "%s: get_index failed\n", __func__);
		goto fail_minor;
	}

	mutex_init(&vfd->lock);

	/* sysfs class */
	memset(&vfd->dev, 0x00, sizeof(vfd->dev));
	vfd->dev.class = &video_class;
	vfd->dev.devt = MKDEV(VIDEO_MAJOR, vfd->minor);
	if (vfd->parent)
		vfd->dev.parent = vfd->parent;
	sprintf(vfd->dev.bus_id, "%s%d", name_base, i - base);
	ret = device_register(&vfd->dev);
	if (ret < 0) {
		printk(KERN_ERR "%s: device_register failed\n", __func__);
		goto fail_minor;
	}

#if 1
	/* needed until all drivers are fixed */
	if (!vfd->release)
		printk(KERN_WARNING "videodev: \"%s\" has no release callback. "
		       "Please fix your driver for proper sysfs support, see "
		       "http://lwn.net/Articles/36850/\n", vfd->name);
#endif
	return 0;

fail_minor:
	mutex_lock(&videodev_lock);
	video_device[vfd->minor] = NULL;
	vfd->minor = -1;
	mutex_unlock(&videodev_lock);
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
	mutex_lock(&videodev_lock);
	if (video_device[vfd->minor] != vfd)
		panic("videodev: bad unregister");

	video_device[vfd->minor] = NULL;
	device_unregister(&vfd->dev);
	mutex_unlock(&videodev_lock);
}
EXPORT_SYMBOL(video_unregister_device);

/*
 * Video fs operations
 */
static const struct file_operations video_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.open		= video_open,
};

/*
 *	Initialise video for linux
 */

static int __init videodev_init(void)
{
	int ret;

	printk(KERN_INFO "Linux video capture interface: v2.00\n");
	if (register_chrdev(VIDEO_MAJOR, VIDEO_NAME, &video_fops)) {
		printk(KERN_WARNING "video_dev: unable to get major %d\n", VIDEO_MAJOR);
		return -EIO;
	}

	ret = class_register(&video_class);
	if (ret < 0) {
		unregister_chrdev(VIDEO_MAJOR, VIDEO_NAME);
		printk(KERN_WARNING "video_dev: class_register failed\n");
		return -EIO;
	}

	return 0;
}

static void __exit videodev_exit(void)
{
	class_unregister(&video_class);
	unregister_chrdev(VIDEO_MAJOR, VIDEO_NAME);
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

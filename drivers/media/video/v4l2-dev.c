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
 * Authors:	Alan Cox, <alan@lxorguk.ukuu.org.uk> (version 1)
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
#include <asm/uaccess.h>
#include <asm/system.h>

#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

#define VIDEO_NUM_DEVICES	256
#define VIDEO_NAME              "video4linux"

/*
 *	sysfs stuff
 */

static ssize_t show_index(struct device *cd,
			 struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(cd);

	return sprintf(buf, "%i\n", vdev->index);
}

static ssize_t show_name(struct device *cd,
			 struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(cd);

	return sprintf(buf, "%.*s\n", (int)sizeof(vdev->name), vdev->name);
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
static DECLARE_BITMAP(devnode_nums[VFL_TYPE_MAX], VIDEO_NUM_DEVICES);

/* Device node utility functions */

/* Note: these utility functions all assume that vfl_type is in the range
   [0, VFL_TYPE_MAX-1]. */

#ifdef CONFIG_VIDEO_FIXED_MINOR_RANGES
/* Return the bitmap corresponding to vfl_type. */
static inline unsigned long *devnode_bits(int vfl_type)
{
	/* Any types not assigned to fixed minor ranges must be mapped to
	   one single bitmap for the purposes of finding a free node number
	   since all those unassigned types use the same minor range. */
	int idx = (vfl_type > VFL_TYPE_RADIO) ? VFL_TYPE_MAX - 1 : vfl_type;

	return devnode_nums[idx];
}
#else
/* Return the bitmap corresponding to vfl_type. */
static inline unsigned long *devnode_bits(int vfl_type)
{
	return devnode_nums[vfl_type];
}
#endif

/* Mark device node number vdev->num as used */
static inline void devnode_set(struct video_device *vdev)
{
	set_bit(vdev->num, devnode_bits(vdev->vfl_type));
}

/* Mark device node number vdev->num as unused */
static inline void devnode_clear(struct video_device *vdev)
{
	clear_bit(vdev->num, devnode_bits(vdev->vfl_type));
}

/* Try to find a free device node number in the range [from, to> */
static inline int devnode_find(struct video_device *vdev, int from, int to)
{
	return find_next_zero_bit(devnode_bits(vdev->vfl_type), to, from);
}

struct video_device *video_device_alloc(void)
{
	return kzalloc(sizeof(struct video_device), GFP_KERNEL);
}
EXPORT_SYMBOL(video_device_alloc);

void video_device_release(struct video_device *vdev)
{
	kfree(vdev);
}
EXPORT_SYMBOL(video_device_release);

void video_device_release_empty(struct video_device *vdev)
{
	/* Do nothing */
	/* Only valid when the video_device struct is a static. */
}
EXPORT_SYMBOL(video_device_release_empty);

static inline void video_get(struct video_device *vdev)
{
	get_device(&vdev->dev);
}

static inline void video_put(struct video_device *vdev)
{
	put_device(&vdev->dev);
}

/* Called when the last user of the video device exits. */
static void v4l2_device_release(struct device *cd)
{
	struct video_device *vdev = to_video_device(cd);
	struct v4l2_device *v4l2_dev = vdev->v4l2_dev;

	mutex_lock(&videodev_lock);
	if (video_device[vdev->minor] != vdev) {
		mutex_unlock(&videodev_lock);
		/* should not happen */
		WARN_ON(1);
		return;
	}

	/* Free up this device for reuse */
	video_device[vdev->minor] = NULL;

	/* Delete the cdev on this minor as well */
	cdev_del(vdev->cdev);
	/* Just in case some driver tries to access this from
	   the release() callback. */
	vdev->cdev = NULL;

	/* Mark device node number as free */
	devnode_clear(vdev);

	mutex_unlock(&videodev_lock);

#if defined(CONFIG_MEDIA_CONTROLLER)
	if (vdev->v4l2_dev && vdev->v4l2_dev->mdev &&
	    vdev->vfl_type != VFL_TYPE_SUBDEV)
		media_device_unregister_entity(&vdev->entity);
#endif

	/* Release video_device and perform other
	   cleanups as needed. */
	vdev->release(vdev);

	/* Decrease v4l2_device refcount */
	if (v4l2_dev)
		v4l2_device_put(v4l2_dev);
}

static struct class video_class = {
	.name = VIDEO_NAME,
	.dev_attrs = video_device_attrs,
};

struct video_device *video_devdata(struct file *file)
{
	return video_device[iminor(file->f_path.dentry->d_inode)];
}
EXPORT_SYMBOL(video_devdata);


/* Priority handling */

static inline bool prio_is_valid(enum v4l2_priority prio)
{
	return prio == V4L2_PRIORITY_BACKGROUND ||
	       prio == V4L2_PRIORITY_INTERACTIVE ||
	       prio == V4L2_PRIORITY_RECORD;
}

void v4l2_prio_init(struct v4l2_prio_state *global)
{
	memset(global, 0, sizeof(*global));
}
EXPORT_SYMBOL(v4l2_prio_init);

int v4l2_prio_change(struct v4l2_prio_state *global, enum v4l2_priority *local,
		     enum v4l2_priority new)
{
	if (!prio_is_valid(new))
		return -EINVAL;
	if (*local == new)
		return 0;

	atomic_inc(&global->prios[new]);
	if (prio_is_valid(*local))
		atomic_dec(&global->prios[*local]);
	*local = new;
	return 0;
}
EXPORT_SYMBOL(v4l2_prio_change);

void v4l2_prio_open(struct v4l2_prio_state *global, enum v4l2_priority *local)
{
	v4l2_prio_change(global, local, V4L2_PRIORITY_DEFAULT);
}
EXPORT_SYMBOL(v4l2_prio_open);

void v4l2_prio_close(struct v4l2_prio_state *global, enum v4l2_priority local)
{
	if (prio_is_valid(local))
		atomic_dec(&global->prios[local]);
}
EXPORT_SYMBOL(v4l2_prio_close);

enum v4l2_priority v4l2_prio_max(struct v4l2_prio_state *global)
{
	if (atomic_read(&global->prios[V4L2_PRIORITY_RECORD]) > 0)
		return V4L2_PRIORITY_RECORD;
	if (atomic_read(&global->prios[V4L2_PRIORITY_INTERACTIVE]) > 0)
		return V4L2_PRIORITY_INTERACTIVE;
	if (atomic_read(&global->prios[V4L2_PRIORITY_BACKGROUND]) > 0)
		return V4L2_PRIORITY_BACKGROUND;
	return V4L2_PRIORITY_UNSET;
}
EXPORT_SYMBOL(v4l2_prio_max);

int v4l2_prio_check(struct v4l2_prio_state *global, enum v4l2_priority local)
{
	return (local < v4l2_prio_max(global)) ? -EBUSY : 0;
}
EXPORT_SYMBOL(v4l2_prio_check);


static ssize_t v4l2_read(struct file *filp, char __user *buf,
		size_t sz, loff_t *off)
{
	struct video_device *vdev = video_devdata(filp);
	int ret = -ENODEV;

	if (!vdev->fops->read)
		return -EINVAL;
	if (vdev->lock && mutex_lock_interruptible(vdev->lock))
		return -ERESTARTSYS;
	if (video_is_registered(vdev))
		ret = vdev->fops->read(filp, buf, sz, off);
	if (vdev->lock)
		mutex_unlock(vdev->lock);
	return ret;
}

static ssize_t v4l2_write(struct file *filp, const char __user *buf,
		size_t sz, loff_t *off)
{
	struct video_device *vdev = video_devdata(filp);
	int ret = -ENODEV;

	if (!vdev->fops->write)
		return -EINVAL;
	if (vdev->lock && mutex_lock_interruptible(vdev->lock))
		return -ERESTARTSYS;
	if (video_is_registered(vdev))
		ret = vdev->fops->write(filp, buf, sz, off);
	if (vdev->lock)
		mutex_unlock(vdev->lock);
	return ret;
}

static unsigned int v4l2_poll(struct file *filp, struct poll_table_struct *poll)
{
	struct video_device *vdev = video_devdata(filp);
	int ret = POLLERR | POLLHUP;

	if (!vdev->fops->poll)
		return DEFAULT_POLLMASK;
	if (vdev->lock)
		mutex_lock(vdev->lock);
	if (video_is_registered(vdev))
		ret = vdev->fops->poll(filp, poll);
	if (vdev->lock)
		mutex_unlock(vdev->lock);
	return ret;
}

static long v4l2_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct video_device *vdev = video_devdata(filp);
	int ret = -ENODEV;

	if (vdev->fops->unlocked_ioctl) {
		if (vdev->lock && mutex_lock_interruptible(vdev->lock))
			return -ERESTARTSYS;
		if (video_is_registered(vdev))
			ret = vdev->fops->unlocked_ioctl(filp, cmd, arg);
		if (vdev->lock)
			mutex_unlock(vdev->lock);
	} else if (vdev->fops->ioctl) {
		/* This code path is a replacement for the BKL. It is a major
		 * hack but it will have to do for those drivers that are not
		 * yet converted to use unlocked_ioctl.
		 *
		 * There are two options: if the driver implements struct
		 * v4l2_device, then the lock defined there is used to
		 * serialize the ioctls. Otherwise the v4l2 core lock defined
		 * below is used. This lock is really bad since it serializes
		 * completely independent devices.
		 *
		 * Both variants suffer from the same problem: if the driver
		 * sleeps, then it blocks all ioctls since the lock is still
		 * held. This is very common for VIDIOC_DQBUF since that
		 * normally waits for a frame to arrive. As a result any other
		 * ioctl calls will proceed very, very slowly since each call
		 * will have to wait for the VIDIOC_QBUF to finish. Things that
		 * should take 0.01s may now take 10-20 seconds.
		 *
		 * The workaround is to *not* take the lock for VIDIOC_DQBUF.
		 * This actually works OK for videobuf-based drivers, since
		 * videobuf will take its own internal lock.
		 */
		static DEFINE_MUTEX(v4l2_ioctl_mutex);
		struct mutex *m = vdev->v4l2_dev ?
			&vdev->v4l2_dev->ioctl_lock : &v4l2_ioctl_mutex;

		if (cmd != VIDIOC_DQBUF && mutex_lock_interruptible(m))
			return -ERESTARTSYS;
		if (video_is_registered(vdev))
			ret = vdev->fops->ioctl(filp, cmd, arg);
		if (cmd != VIDIOC_DQBUF)
			mutex_unlock(m);
	} else
		ret = -ENOTTY;

	return ret;
}

#ifdef CONFIG_MMU
#define v4l2_get_unmapped_area NULL
#else
static unsigned long v4l2_get_unmapped_area(struct file *filp,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	struct video_device *vdev = video_devdata(filp);

	if (!vdev->fops->get_unmapped_area)
		return -ENOSYS;
	if (!video_is_registered(vdev))
		return -ENODEV;
	return vdev->fops->get_unmapped_area(filp, addr, len, pgoff, flags);
}
#endif

static int v4l2_mmap(struct file *filp, struct vm_area_struct *vm)
{
	struct video_device *vdev = video_devdata(filp);
	int ret = -ENODEV;

	if (!vdev->fops->mmap)
		return ret;
	if (vdev->lock && mutex_lock_interruptible(vdev->lock))
		return -ERESTARTSYS;
	if (video_is_registered(vdev))
		ret = vdev->fops->mmap(filp, vm);
	if (vdev->lock)
		mutex_unlock(vdev->lock);
	return ret;
}

/* Override for the open function */
static int v4l2_open(struct inode *inode, struct file *filp)
{
	struct video_device *vdev;
	int ret = 0;

	/* Check if the video device is available */
	mutex_lock(&videodev_lock);
	vdev = video_devdata(filp);
	/* return ENODEV if the video device has already been removed. */
	if (vdev == NULL || !video_is_registered(vdev)) {
		mutex_unlock(&videodev_lock);
		return -ENODEV;
	}
	/* and increase the device refcount */
	video_get(vdev);
	mutex_unlock(&videodev_lock);
	if (vdev->fops->open) {
		if (vdev->lock && mutex_lock_interruptible(vdev->lock)) {
			ret = -ERESTARTSYS;
			goto err;
		}
		if (video_is_registered(vdev))
			ret = vdev->fops->open(filp);
		else
			ret = -ENODEV;
		if (vdev->lock)
			mutex_unlock(vdev->lock);
	}

err:
	/* decrease the refcount in case of an error */
	if (ret)
		video_put(vdev);
	return ret;
}

/* Override for the release function */
static int v4l2_release(struct inode *inode, struct file *filp)
{
	struct video_device *vdev = video_devdata(filp);
	int ret = 0;

	if (vdev->fops->release) {
		if (vdev->lock)
			mutex_lock(vdev->lock);
		vdev->fops->release(filp);
		if (vdev->lock)
			mutex_unlock(vdev->lock);
	}
	/* decrease the refcount unconditionally since the release()
	   return value is ignored. */
	video_put(vdev);
	return ret;
}

static const struct file_operations v4l2_fops = {
	.owner = THIS_MODULE,
	.read = v4l2_read,
	.write = v4l2_write,
	.open = v4l2_open,
	.get_unmapped_area = v4l2_get_unmapped_area,
	.mmap = v4l2_mmap,
	.unlocked_ioctl = v4l2_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = v4l2_compat_ioctl32,
#endif
	.release = v4l2_release,
	.poll = v4l2_poll,
	.llseek = no_llseek,
};

/**
 * get_index - assign stream index number based on parent device
 * @vdev: video_device to assign index number to, vdev->parent should be assigned
 *
 * Note that when this is called the new device has not yet been registered
 * in the video_device array, but it was able to obtain a minor number.
 *
 * This means that we can always obtain a free stream index number since
 * the worst case scenario is that there are VIDEO_NUM_DEVICES - 1 slots in
 * use of the video_device array.
 *
 * Returns a free index number.
 */
static int get_index(struct video_device *vdev)
{
	/* This can be static since this function is called with the global
	   videodev_lock held. */
	static DECLARE_BITMAP(used, VIDEO_NUM_DEVICES);
	int i;

	/* Some drivers do not set the parent. In that case always return 0. */
	if (vdev->parent == NULL)
		return 0;

	bitmap_zero(used, VIDEO_NUM_DEVICES);

	for (i = 0; i < VIDEO_NUM_DEVICES; i++) {
		if (video_device[i] != NULL &&
		    video_device[i]->parent == vdev->parent) {
			set_bit(video_device[i]->index, used);
		}
	}

	return find_first_zero_bit(used, VIDEO_NUM_DEVICES);
}

/**
 *	__video_register_device - register video4linux devices
 *	@vdev: video device structure we want to register
 *	@type: type of device to register
 *	@nr:   which device node number (0 == /dev/video0, 1 == /dev/video1, ...
 *             -1 == first free)
 *	@warn_if_nr_in_use: warn if the desired device node number
 *	       was already in use and another number was chosen instead.
 *	@owner: module that owns the video device node
 *
 *	The registration code assigns minor numbers and device node numbers
 *	based on the requested type and registers the new device node with
 *	the kernel.
 *
 *	This function assumes that struct video_device was zeroed when it
 *	was allocated and does not contain any stale date.
 *
 *	An error is returned if no free minor or device node number could be
 *	found, or if the registration of the device node failed.
 *
 *	Zero is returned on success.
 *
 *	Valid types are
 *
 *	%VFL_TYPE_GRABBER - A frame grabber
 *
 *	%VFL_TYPE_VBI - Vertical blank data (undecoded)
 *
 *	%VFL_TYPE_RADIO - A radio card
 *
 *	%VFL_TYPE_SUBDEV - A subdevice
 */
int __video_register_device(struct video_device *vdev, int type, int nr,
		int warn_if_nr_in_use, struct module *owner)
{
	int i = 0;
	int ret;
	int minor_offset = 0;
	int minor_cnt = VIDEO_NUM_DEVICES;
	const char *name_base;

	/* A minor value of -1 marks this video device as never
	   having been registered */
	vdev->minor = -1;

	/* the release callback MUST be present */
	WARN_ON(!vdev->release);
	if (!vdev->release)
		return -EINVAL;

	/* v4l2_fh support */
	spin_lock_init(&vdev->fh_lock);
	INIT_LIST_HEAD(&vdev->fh_list);

	/* Part 1: check device type */
	switch (type) {
	case VFL_TYPE_GRABBER:
		name_base = "video";
		break;
	case VFL_TYPE_VBI:
		name_base = "vbi";
		break;
	case VFL_TYPE_RADIO:
		name_base = "radio";
		break;
	case VFL_TYPE_SUBDEV:
		name_base = "v4l-subdev";
		break;
	default:
		printk(KERN_ERR "%s called with unknown type: %d\n",
		       __func__, type);
		return -EINVAL;
	}

	vdev->vfl_type = type;
	vdev->cdev = NULL;
	if (vdev->v4l2_dev) {
		if (vdev->v4l2_dev->dev)
			vdev->parent = vdev->v4l2_dev->dev;
		if (vdev->ctrl_handler == NULL)
			vdev->ctrl_handler = vdev->v4l2_dev->ctrl_handler;
		/* If the prio state pointer is NULL, then use the v4l2_device
		   prio state. */
		if (vdev->prio == NULL)
			vdev->prio = &vdev->v4l2_dev->prio;
	}

	/* Part 2: find a free minor, device node number and device index. */
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

	/* Pick a device node number */
	mutex_lock(&videodev_lock);
	nr = devnode_find(vdev, nr == -1 ? 0 : nr, minor_cnt);
	if (nr == minor_cnt)
		nr = devnode_find(vdev, 0, minor_cnt);
	if (nr == minor_cnt) {
		printk(KERN_ERR "could not get a free device node number\n");
		mutex_unlock(&videodev_lock);
		return -ENFILE;
	}
#ifdef CONFIG_VIDEO_FIXED_MINOR_RANGES
	/* 1-on-1 mapping of device node number to minor number */
	i = nr;
#else
	/* The device node number and minor numbers are independent, so
	   we just find the first free minor number. */
	for (i = 0; i < VIDEO_NUM_DEVICES; i++)
		if (video_device[i] == NULL)
			break;
	if (i == VIDEO_NUM_DEVICES) {
		mutex_unlock(&videodev_lock);
		printk(KERN_ERR "could not get a free minor\n");
		return -ENFILE;
	}
#endif
	vdev->minor = i + minor_offset;
	vdev->num = nr;
	devnode_set(vdev);

	/* Should not happen since we thought this minor was free */
	WARN_ON(video_device[vdev->minor] != NULL);
	vdev->index = get_index(vdev);
	mutex_unlock(&videodev_lock);

	/* Part 3: Initialize the character device */
	vdev->cdev = cdev_alloc();
	if (vdev->cdev == NULL) {
		ret = -ENOMEM;
		goto cleanup;
	}
	vdev->cdev->ops = &v4l2_fops;
	vdev->cdev->owner = owner;
	ret = cdev_add(vdev->cdev, MKDEV(VIDEO_MAJOR, vdev->minor), 1);
	if (ret < 0) {
		printk(KERN_ERR "%s: cdev_add failed\n", __func__);
		kfree(vdev->cdev);
		vdev->cdev = NULL;
		goto cleanup;
	}

	/* Part 4: register the device with sysfs */
	vdev->dev.class = &video_class;
	vdev->dev.devt = MKDEV(VIDEO_MAJOR, vdev->minor);
	if (vdev->parent)
		vdev->dev.parent = vdev->parent;
	dev_set_name(&vdev->dev, "%s%d", name_base, vdev->num);
	ret = device_register(&vdev->dev);
	if (ret < 0) {
		printk(KERN_ERR "%s: device_register failed\n", __func__);
		goto cleanup;
	}
	/* Register the release callback that will be called when the last
	   reference to the device goes away. */
	vdev->dev.release = v4l2_device_release;

	if (nr != -1 && nr != vdev->num && warn_if_nr_in_use)
		printk(KERN_WARNING "%s: requested %s%d, got %s\n", __func__,
			name_base, nr, video_device_node_name(vdev));

	/* Increase v4l2_device refcount */
	if (vdev->v4l2_dev)
		v4l2_device_get(vdev->v4l2_dev);

#if defined(CONFIG_MEDIA_CONTROLLER)
	/* Part 5: Register the entity. */
	if (vdev->v4l2_dev && vdev->v4l2_dev->mdev &&
	    vdev->vfl_type != VFL_TYPE_SUBDEV) {
		vdev->entity.type = MEDIA_ENT_T_DEVNODE_V4L;
		vdev->entity.name = vdev->name;
		vdev->entity.v4l.major = VIDEO_MAJOR;
		vdev->entity.v4l.minor = vdev->minor;
		ret = media_device_register_entity(vdev->v4l2_dev->mdev,
			&vdev->entity);
		if (ret < 0)
			printk(KERN_WARNING
			       "%s: media_device_register_entity failed\n",
			       __func__);
	}
#endif
	/* Part 6: Activate this minor. The char device can now be used. */
	set_bit(V4L2_FL_REGISTERED, &vdev->flags);
	mutex_lock(&videodev_lock);
	video_device[vdev->minor] = vdev;
	mutex_unlock(&videodev_lock);

	return 0;

cleanup:
	mutex_lock(&videodev_lock);
	if (vdev->cdev)
		cdev_del(vdev->cdev);
	devnode_clear(vdev);
	mutex_unlock(&videodev_lock);
	/* Mark this video device as never having been registered. */
	vdev->minor = -1;
	return ret;
}
EXPORT_SYMBOL(__video_register_device);

/**
 *	video_unregister_device - unregister a video4linux device
 *	@vdev: the device to unregister
 *
 *	This unregisters the passed device. Future open calls will
 *	be met with errors.
 */
void video_unregister_device(struct video_device *vdev)
{
	/* Check if vdev was ever registered at all */
	if (!vdev || !video_is_registered(vdev))
		return;

	mutex_lock(&videodev_lock);
	/* This must be in a critical section to prevent a race with v4l2_open.
	 * Once this bit has been cleared video_get may never be called again.
	 */
	clear_bit(V4L2_FL_REGISTERED, &vdev->flags);
	mutex_unlock(&videodev_lock);
	device_unregister(&vdev->dev);
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
MODULE_ALIAS_CHARDEV_MAJOR(VIDEO_MAJOR);


/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

/*
 * Video capture interface for Linux
 *
 *		A generic video device interface for the LINUX operating system
 *		using a set of device structures/vectors for low level operations.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Author:	Alan Cox, <alan@redhat.com>
 *
 * Fixes:	20000516  Claudio Matsuoka <claudio@conectiva.com>
 *		- Added procfs support
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/devfs_fs_kernel.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/videodev.h>

#define VIDEO_NUM_DEVICES	256
#define VIDEO_NAME              "video4linux"

/*
 *	sysfs stuff
 */

static ssize_t show_name(struct class_device *cd, char *buf)
{
	struct video_device *vfd = container_of(cd, struct video_device, class_dev);
	return sprintf(buf,"%.*s\n",(int)sizeof(vfd->name),vfd->name);
}

static CLASS_DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

struct video_device *video_device_alloc(void)
{
	struct video_device *vfd;

	vfd = kzalloc(sizeof(*vfd),GFP_KERNEL);
	return vfd;
}

void video_device_release(struct video_device *vfd)
{
	kfree(vfd);
}

static void video_release(struct class_device *cd)
{
	struct video_device *vfd = container_of(cd, struct video_device, class_dev);

#if 1
	/* needed until all drivers are fixed */
	if (!vfd->release)
		return;
#endif
	vfd->release(vfd);
}

static struct class video_class = {
	.name    = VIDEO_NAME,
	.release = video_release,
};

/*
 *	Active devices
 */

static struct video_device *video_device[VIDEO_NUM_DEVICES];
static DEFINE_MUTEX(videodev_lock);

struct video_device* video_devdata(struct file *file)
{
	return video_device[iminor(file->f_dentry->d_inode)];
}

/*
 *	Open a video device.
 */
static int video_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	int err = 0;
	struct video_device *vfl;
	struct file_operations *old_fops;

	if(minor>=VIDEO_NUM_DEVICES)
		return -ENODEV;
	mutex_lock(&videodev_lock);
	vfl=video_device[minor];
	if(vfl==NULL) {
		mutex_unlock(&videodev_lock);
		request_module("char-major-%d-%d", VIDEO_MAJOR, minor);
		mutex_lock(&videodev_lock);
		vfl=video_device[minor];
		if (vfl==NULL) {
			mutex_unlock(&videodev_lock);
			return -ENODEV;
		}
	}
	old_fops = file->f_op;
	file->f_op = fops_get(vfl->fops);
	if(file->f_op->open)
		err = file->f_op->open(inode,file);
	if (err) {
		fops_put(file->f_op);
		file->f_op = fops_get(old_fops);
	}
	fops_put(old_fops);
	mutex_unlock(&videodev_lock);
	return err;
}

/*
 * helper function -- handles userspace copying for ioctl arguments
 */

static unsigned int
video_fix_command(unsigned int cmd)
{
	switch (cmd) {
	case VIDIOC_OVERLAY_OLD:
		cmd = VIDIOC_OVERLAY;
		break;
	case VIDIOC_S_PARM_OLD:
		cmd = VIDIOC_S_PARM;
		break;
	case VIDIOC_S_CTRL_OLD:
		cmd = VIDIOC_S_CTRL;
		break;
	case VIDIOC_G_AUDIO_OLD:
		cmd = VIDIOC_G_AUDIO;
		break;
	case VIDIOC_G_AUDOUT_OLD:
		cmd = VIDIOC_G_AUDOUT;
		break;
	case VIDIOC_CROPCAP_OLD:
		cmd = VIDIOC_CROPCAP;
		break;
	}
	return cmd;
}

int
video_usercopy(struct inode *inode, struct file *file,
	       unsigned int cmd, unsigned long arg,
	       int (*func)(struct inode *inode, struct file *file,
			   unsigned int cmd, void *arg))
{
	char	sbuf[128];
	void    *mbuf = NULL;
	void	*parg = NULL;
	int	err  = -EINVAL;

	cmd = video_fix_command(cmd);

	/*  Copy arguments into temp kernel buffer  */
	switch (_IOC_DIR(cmd)) {
	case _IOC_NONE:
		parg = NULL;
		break;
	case _IOC_READ:
	case _IOC_WRITE:
	case (_IOC_WRITE | _IOC_READ):
		if (_IOC_SIZE(cmd) <= sizeof(sbuf)) {
			parg = sbuf;
		} else {
			/* too big to allocate from stack */
			mbuf = kmalloc(_IOC_SIZE(cmd),GFP_KERNEL);
			if (NULL == mbuf)
				return -ENOMEM;
			parg = mbuf;
		}

		err = -EFAULT;
		if (_IOC_DIR(cmd) & _IOC_WRITE)
			if (copy_from_user(parg, (void __user *)arg, _IOC_SIZE(cmd)))
				goto out;
		break;
	}

	/* call driver */
	err = func(inode, file, cmd, parg);
	if (err == -ENOIOCTLCMD)
		err = -EINVAL;
	if (err < 0)
		goto out;

	/*  Copy results into user buffer  */
	switch (_IOC_DIR(cmd))
	{
	case _IOC_READ:
	case (_IOC_WRITE | _IOC_READ):
		if (copy_to_user((void __user *)arg, parg, _IOC_SIZE(cmd)))
			err = -EFAULT;
		break;
	}

out:
	kfree(mbuf);
	return err;
}

/*
 * open/release helper functions -- handle exclusive opens
 */
int video_exclusive_open(struct inode *inode, struct file *file)
{
	struct  video_device *vfl = video_devdata(file);
	int retval = 0;

	mutex_lock(&vfl->lock);
	if (vfl->users) {
		retval = -EBUSY;
	} else {
		vfl->users++;
	}
	mutex_unlock(&vfl->lock);
	return retval;
}

int video_exclusive_release(struct inode *inode, struct file *file)
{
	struct  video_device *vfl = video_devdata(file);

	vfl->users--;
	return 0;
}

static struct file_operations video_fops;

/**
 *	video_register_device - register video4linux devices
 *	@vfd:  video device structure we want to register
 *	@type: type of device to register
 *	@nr:   which device number (0 == /dev/video0, 1 == /dev/video1, ...
 *             -1 == first free)
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

int video_register_device(struct video_device *vfd, int type, int nr)
{
	int i=0;
	int base;
	int end;
	char *name_base;

	switch(type)
	{
		case VFL_TYPE_GRABBER:
			base=MINOR_VFL_TYPE_GRABBER_MIN;
			end=MINOR_VFL_TYPE_GRABBER_MAX+1;
			name_base = "video";
			break;
		case VFL_TYPE_VTX:
			base=MINOR_VFL_TYPE_VTX_MIN;
			end=MINOR_VFL_TYPE_VTX_MAX+1;
			name_base = "vtx";
			break;
		case VFL_TYPE_VBI:
			base=MINOR_VFL_TYPE_VBI_MIN;
			end=MINOR_VFL_TYPE_VBI_MAX+1;
			name_base = "vbi";
			break;
		case VFL_TYPE_RADIO:
			base=MINOR_VFL_TYPE_RADIO_MIN;
			end=MINOR_VFL_TYPE_RADIO_MAX+1;
			name_base = "radio";
			break;
		default:
			return -1;
	}

	/* pick a minor number */
	mutex_lock(&videodev_lock);
	if (nr >= 0  &&  nr < end-base) {
		/* use the one the driver asked for */
		i = base+nr;
		if (NULL != video_device[i]) {
			mutex_unlock(&videodev_lock);
			return -ENFILE;
		}
	} else {
		/* use first free */
		for(i=base;i<end;i++)
			if (NULL == video_device[i])
				break;
		if (i == end) {
			mutex_unlock(&videodev_lock);
			return -ENFILE;
		}
	}
	video_device[i]=vfd;
	vfd->minor=i;
	mutex_unlock(&videodev_lock);

	sprintf(vfd->devfs_name, "v4l/%s%d", name_base, i - base);
	devfs_mk_cdev(MKDEV(VIDEO_MAJOR, vfd->minor),
			S_IFCHR | S_IRUSR | S_IWUSR, vfd->devfs_name);
	mutex_init(&vfd->lock);

	/* sysfs class */
	memset(&vfd->class_dev, 0x00, sizeof(vfd->class_dev));
	if (vfd->dev)
		vfd->class_dev.dev = vfd->dev;
	vfd->class_dev.class       = &video_class;
	vfd->class_dev.devt        = MKDEV(VIDEO_MAJOR, vfd->minor);
	strlcpy(vfd->class_dev.class_id, vfd->devfs_name + 4, BUS_ID_SIZE);
	class_device_register(&vfd->class_dev);
	class_device_create_file(&vfd->class_dev,
				&class_device_attr_name);

#if 1
	/* needed until all drivers are fixed */
	if (!vfd->release)
		printk(KERN_WARNING "videodev: \"%s\" has no release callback. "
		       "Please fix your driver for proper sysfs support, see "
		       "http://lwn.net/Articles/36850/\n", vfd->name);
#endif
	return 0;
}

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
	if(video_device[vfd->minor]!=vfd)
		panic("videodev: bad unregister");

	devfs_remove(vfd->devfs_name);
	video_device[vfd->minor]=NULL;
	class_device_unregister(&vfd->class_dev);
	mutex_unlock(&videodev_lock);
}


static struct file_operations video_fops=
{
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

	printk(KERN_INFO "Linux video capture interface: v1.00\n");
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

EXPORT_SYMBOL(video_register_device);
EXPORT_SYMBOL(video_unregister_device);
EXPORT_SYMBOL(video_devdata);
EXPORT_SYMBOL(video_usercopy);
EXPORT_SYMBOL(video_exclusive_open);
EXPORT_SYMBOL(video_exclusive_release);
EXPORT_SYMBOL(video_device_alloc);
EXPORT_SYMBOL(video_device_release);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("Device registrar for Video4Linux drivers");
MODULE_LICENSE("GPL");


/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

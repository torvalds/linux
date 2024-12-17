// SPDX-License-Identifier: GPL-2.0
/*
 *    HMC Drive CD/DVD Device
 *
 *    Copyright IBM Corp. 2013
 *    Author(s): Ralf Hoppe (rhoppe@de.ibm.com)
 *
 *    This file provides a Linux "misc" character device for access to an
 *    assigned HMC drive CD/DVD-ROM. It works as follows: First create the
 *    device by calling hmcdrv_dev_init(). After open() a lseek(fd, 0,
 *    SEEK_END) indicates that a new FTP command follows (not needed on the
 *    first command after open). Then write() the FTP command ASCII string
 *    to it, e.g. "dir /" or "nls <directory>" or "get <filename>". At the
 *    end read() the response.
 */

#define KMSG_COMPONENT "hmcdrv"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/capability.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

#include "hmcdrv_dev.h"
#include "hmcdrv_ftp.h"

/* If the following macro is defined, then the HMC device creates it's own
 * separated device class (and dynamically assigns a major number). If not
 * defined then the HMC device is assigned to the "misc" class devices.
 *
#define HMCDRV_DEV_CLASS "hmcftp"
 */

#define HMCDRV_DEV_NAME  "hmcdrv"
#define HMCDRV_DEV_BUSY_DELAY	 500 /* delay between -EBUSY trials in ms */
#define HMCDRV_DEV_BUSY_RETRIES  3   /* number of retries on -EBUSY */

struct hmcdrv_dev_node {

#ifdef HMCDRV_DEV_CLASS
	struct cdev dev; /* character device structure */
	umode_t mode;	 /* mode of device node (unused, zero) */
#else
	struct miscdevice dev; /* "misc" device structure */
#endif

};

static int hmcdrv_dev_open(struct inode *inode, struct file *fp);
static int hmcdrv_dev_release(struct inode *inode, struct file *fp);
static loff_t hmcdrv_dev_seek(struct file *fp, loff_t pos, int whence);
static ssize_t hmcdrv_dev_read(struct file *fp, char __user *ubuf,
			       size_t len, loff_t *pos);
static ssize_t hmcdrv_dev_write(struct file *fp, const char __user *ubuf,
				size_t len, loff_t *pos);
static ssize_t hmcdrv_dev_transfer(char __kernel *cmd, loff_t offset,
				   char __user *buf, size_t len);

/*
 * device operations
 */
static const struct file_operations hmcdrv_dev_fops = {
	.open = hmcdrv_dev_open,
	.llseek = hmcdrv_dev_seek,
	.release = hmcdrv_dev_release,
	.read = hmcdrv_dev_read,
	.write = hmcdrv_dev_write,
};

static struct hmcdrv_dev_node hmcdrv_dev; /* HMC device struct (static) */

#ifdef HMCDRV_DEV_CLASS

static struct class *hmcdrv_dev_class; /* device class pointer */
static dev_t hmcdrv_dev_no; /* device number (major/minor) */

/**
 * hmcdrv_dev_name() - provides a naming hint for a device node in /dev
 * @dev: device for which the naming/mode hint is
 * @mode: file mode for device node created in /dev
 *
 * See: devtmpfs.c, function devtmpfs_create_node()
 *
 * Return: recommended device file name in /dev
 */
static char *hmcdrv_dev_name(const struct device *dev, umode_t *mode)
{
	char *nodename = NULL;
	const char *devname = dev_name(dev); /* kernel device name */

	if (devname)
		nodename = kasprintf(GFP_KERNEL, "%s", devname);

	/* on device destroy (rmmod) the mode pointer may be NULL
	 */
	if (mode)
		*mode = hmcdrv_dev.mode;

	return nodename;
}

#endif	/* HMCDRV_DEV_CLASS */

/*
 * open()
 */
static int hmcdrv_dev_open(struct inode *inode, struct file *fp)
{
	int rc;

	/* check for non-blocking access, which is really unsupported
	 */
	if (fp->f_flags & O_NONBLOCK)
		return -EINVAL;

	/* Because it makes no sense to open this device read-only (then a
	 * FTP command cannot be emitted), we respond with an error.
	 */
	if ((fp->f_flags & O_ACCMODE) == O_RDONLY)
		return -EINVAL;

	/* prevent unloading this module as long as anyone holds the
	 * device file open - so increment the reference count here
	 */
	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	fp->private_data = NULL; /* no command yet */
	rc = hmcdrv_ftp_startup();
	if (rc)
		module_put(THIS_MODULE);

	pr_debug("open file '/dev/%pD' with return code %d\n", fp, rc);
	return rc;
}

/*
 * release()
 */
static int hmcdrv_dev_release(struct inode *inode, struct file *fp)
{
	pr_debug("closing file '/dev/%pD'\n", fp);
	kfree(fp->private_data);
	fp->private_data = NULL;
	hmcdrv_ftp_shutdown();
	module_put(THIS_MODULE);
	return 0;
}

/*
 * lseek()
 */
static loff_t hmcdrv_dev_seek(struct file *fp, loff_t pos, int whence)
{
	switch (whence) {
	case SEEK_CUR: /* relative to current file position */
		pos += fp->f_pos; /* new position stored in 'pos' */
		break;

	case SEEK_SET: /* absolute (relative to beginning of file) */
		break; /* SEEK_SET */

		/* We use SEEK_END as a special indicator for a SEEK_SET
		 * (set absolute position), combined with a FTP command
		 * clear.
		 */
	case SEEK_END:
		if (fp->private_data) {
			kfree(fp->private_data);
			fp->private_data = NULL;
		}

		break; /* SEEK_END */

	default: /* SEEK_DATA, SEEK_HOLE: unsupported */
		return -EINVAL;
	}

	if (pos < 0)
		return -EINVAL;

	fp->f_pos = pos;
	return pos;
}

/*
 * transfer (helper function)
 */
static ssize_t hmcdrv_dev_transfer(char __kernel *cmd, loff_t offset,
				   char __user *buf, size_t len)
{
	ssize_t retlen;
	unsigned trials = HMCDRV_DEV_BUSY_RETRIES;

	do {
		retlen = hmcdrv_ftp_cmd(cmd, offset, buf, len);

		if (retlen != -EBUSY)
			break;

		msleep(HMCDRV_DEV_BUSY_DELAY);

	} while (--trials > 0);

	return retlen;
}

/*
 * read()
 */
static ssize_t hmcdrv_dev_read(struct file *fp, char __user *ubuf,
			       size_t len, loff_t *pos)
{
	ssize_t retlen;

	if (((fp->f_flags & O_ACCMODE) == O_WRONLY) ||
	    (fp->private_data == NULL)) { /* no FTP cmd defined ? */
		return -EBADF;
	}

	retlen = hmcdrv_dev_transfer((char *) fp->private_data,
				     *pos, ubuf, len);

	pr_debug("read from file '/dev/%pD' at %lld returns %zd/%zu\n",
		 fp, (long long) *pos, retlen, len);

	if (retlen > 0)
		*pos += retlen;

	return retlen;
}

/*
 * write()
 */
static ssize_t hmcdrv_dev_write(struct file *fp, const char __user *ubuf,
				size_t len, loff_t *pos)
{
	ssize_t retlen;

	pr_debug("writing file '/dev/%pD' at pos. %lld with length %zd\n",
		 fp, (long long) *pos, len);

	if (!fp->private_data) { /* first expect a cmd write */
		fp->private_data = kmalloc(len + 1, GFP_KERNEL);

		if (!fp->private_data)
			return -ENOMEM;

		if (!copy_from_user(fp->private_data, ubuf, len)) {
			((char *)fp->private_data)[len] = '\0';
			return len;
		}

		kfree(fp->private_data);
		fp->private_data = NULL;
		return -EFAULT;
	}

	retlen = hmcdrv_dev_transfer((char *) fp->private_data,
				     *pos, (char __user *) ubuf, len);
	if (retlen > 0)
		*pos += retlen;

	pr_debug("write to file '/dev/%pD' returned %zd\n", fp, retlen);

	return retlen;
}

/**
 * hmcdrv_dev_init() - creates a HMC drive CD/DVD device
 *
 * This function creates a HMC drive CD/DVD kernel device and an associated
 * device under /dev, using a dynamically allocated major number.
 *
 * Return: 0 on success, else an error code.
 */
int hmcdrv_dev_init(void)
{
	int rc;

#ifdef HMCDRV_DEV_CLASS
	struct device *dev;

	rc = alloc_chrdev_region(&hmcdrv_dev_no, 0, 1, HMCDRV_DEV_NAME);

	if (rc)
		goto out_err;

	cdev_init(&hmcdrv_dev.dev, &hmcdrv_dev_fops);
	hmcdrv_dev.dev.owner = THIS_MODULE;
	rc = cdev_add(&hmcdrv_dev.dev, hmcdrv_dev_no, 1);

	if (rc)
		goto out_unreg;

	/* At this point the character device exists in the kernel (see
	 * /proc/devices), but not under /dev nor /sys/devices/virtual. So
	 * we have to create an associated class (see /sys/class).
	 */
	hmcdrv_dev_class = class_create(HMCDRV_DEV_CLASS);

	if (IS_ERR(hmcdrv_dev_class)) {
		rc = PTR_ERR(hmcdrv_dev_class);
		goto out_devdel;
	}

	/* Finally a device node in /dev has to be established (as 'mkdev'
	 * does from the command line). Notice that assignment of a device
	 * node name/mode function is optional (only for mode != 0600).
	 */
	hmcdrv_dev.mode = 0; /* "unset" */
	hmcdrv_dev_class->devnode = hmcdrv_dev_name;

	dev = device_create(hmcdrv_dev_class, NULL, hmcdrv_dev_no, NULL,
			    "%s", HMCDRV_DEV_NAME);
	if (!IS_ERR(dev))
		return 0;

	rc = PTR_ERR(dev);
	class_destroy(hmcdrv_dev_class);
	hmcdrv_dev_class = NULL;

out_devdel:
	cdev_del(&hmcdrv_dev.dev);

out_unreg:
	unregister_chrdev_region(hmcdrv_dev_no, 1);

out_err:

#else  /* !HMCDRV_DEV_CLASS */
	hmcdrv_dev.dev.minor = MISC_DYNAMIC_MINOR;
	hmcdrv_dev.dev.name = HMCDRV_DEV_NAME;
	hmcdrv_dev.dev.fops = &hmcdrv_dev_fops;
	hmcdrv_dev.dev.mode = 0; /* finally produces 0600 */
	rc = misc_register(&hmcdrv_dev.dev);
#endif	/* HMCDRV_DEV_CLASS */

	return rc;
}

/**
 * hmcdrv_dev_exit() - destroys a HMC drive CD/DVD device
 */
void hmcdrv_dev_exit(void)
{
#ifdef HMCDRV_DEV_CLASS
	if (!IS_ERR_OR_NULL(hmcdrv_dev_class)) {
		device_destroy(hmcdrv_dev_class, hmcdrv_dev_no);
		class_destroy(hmcdrv_dev_class);
	}

	cdev_del(&hmcdrv_dev.dev);
	unregister_chrdev_region(hmcdrv_dev_no, 1);
#else  /* !HMCDRV_DEV_CLASS */
	misc_deregister(&hmcdrv_dev.dev);
#endif	/* HMCDRV_DEV_CLASS */
}

/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * SPI Flash ROM driver
 *
 * This source code is derived from code provided in "Linux Device
 * Drivers, Third Edition", by Jonathan Corbet, Alessandro Rubini, and
 * Greg Kroah-Hartman, published by O'Reilly Media, Inc.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/aio.h>
#include <linux/pagemap.h>
#include <linux/hugetlb.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <hv/hypervisor.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <hv/drv_srom_intf.h>

/*
 * Size of our hypervisor I/O requests.  We break up large transfers
 * so that we don't spend large uninterrupted spans of time in the
 * hypervisor.  Erasing an SROM sector takes a significant fraction of
 * a second, so if we allowed the user to, say, do one I/O to write the
 * entire ROM, we'd get soft lockup timeouts, or worse.
 */
#define SROM_CHUNK_SIZE ((size_t)4096)

/*
 * When hypervisor is busy (e.g. erasing), poll the status periodically.
 */

/*
 * Interval to poll the state in msec
 */
#define SROM_WAIT_TRY_INTERVAL 20

/*
 * Maximum times to poll the state
 */
#define SROM_MAX_WAIT_TRY_TIMES 1000

struct srom_dev {
	int hv_devhdl;			/* Handle for hypervisor device */
	u32 total_size;			/* Size of this device */
	u32 sector_size;		/* Size of a sector */
	u32 page_size;			/* Size of a page */
	struct mutex lock;		/* Allow only one accessor at a time */
};

static int srom_major;			/* Dynamic major by default */
module_param(srom_major, int, 0);
MODULE_AUTHOR("Tilera Corporation");
MODULE_LICENSE("GPL");

static int srom_devs;			/* Number of SROM partitions */
static struct cdev srom_cdev;
static struct platform_device *srom_parent;
static struct class *srom_class;
static struct srom_dev *srom_devices;

/*
 * Handle calling the hypervisor and managing EAGAIN/EBUSY.
 */

static ssize_t _srom_read(int hv_devhdl, void *buf,
			  loff_t off, size_t count)
{
	int retval, retries = SROM_MAX_WAIT_TRY_TIMES;
	for (;;) {
		retval = hv_dev_pread(hv_devhdl, 0, (HV_VirtAddr)buf,
				      count, off);
		if (retval >= 0)
			return retval;
		if (retval == HV_EAGAIN)
			continue;
		if (retval == HV_EBUSY && --retries > 0) {
			msleep(SROM_WAIT_TRY_INTERVAL);
			continue;
		}
		pr_err("_srom_read: error %d\n", retval);
		return -EIO;
	}
}

static ssize_t _srom_write(int hv_devhdl, const void *buf,
			   loff_t off, size_t count)
{
	int retval, retries = SROM_MAX_WAIT_TRY_TIMES;
	for (;;) {
		retval = hv_dev_pwrite(hv_devhdl, 0, (HV_VirtAddr)buf,
				       count, off);
		if (retval >= 0)
			return retval;
		if (retval == HV_EAGAIN)
			continue;
		if (retval == HV_EBUSY && --retries > 0) {
			msleep(SROM_WAIT_TRY_INTERVAL);
			continue;
		}
		pr_err("_srom_write: error %d\n", retval);
		return -EIO;
	}
}

/**
 * srom_open() - Device open routine.
 * @inode: Inode for this device.
 * @filp: File for this specific open of the device.
 *
 * Returns zero, or an error code.
 */
static int srom_open(struct inode *inode, struct file *filp)
{
	filp->private_data = &srom_devices[iminor(inode)];
	return 0;
}


/**
 * srom_release() - Device release routine.
 * @inode: Inode for this device.
 * @filp: File for this specific open of the device.
 *
 * Returns zero, or an error code.
 */
static int srom_release(struct inode *inode, struct file *filp)
{
	struct srom_dev *srom = filp->private_data;
	char dummy;

	/* Make sure we've flushed anything written to the ROM. */
	mutex_lock(&srom->lock);
	if (srom->hv_devhdl >= 0)
		_srom_write(srom->hv_devhdl, &dummy, SROM_FLUSH_OFF, 1);
	mutex_unlock(&srom->lock);

	filp->private_data = NULL;

	return 0;
}


/**
 * srom_read() - Read data from the device.
 * @filp: File for this specific open of the device.
 * @buf: User's data buffer.
 * @count: Number of bytes requested.
 * @f_pos: File position.
 *
 * Returns number of bytes read, or an error code.
 */
static ssize_t srom_read(struct file *filp, char __user *buf,
			 size_t count, loff_t *f_pos)
{
	int retval = 0;
	void *kernbuf;
	struct srom_dev *srom = filp->private_data;

	kernbuf = kmalloc(SROM_CHUNK_SIZE, GFP_KERNEL);
	if (!kernbuf)
		return -ENOMEM;

	if (mutex_lock_interruptible(&srom->lock)) {
		retval = -ERESTARTSYS;
		kfree(kernbuf);
		return retval;
	}

	while (count) {
		int hv_retval;
		int bytes_this_pass = min(count, SROM_CHUNK_SIZE);

		hv_retval = _srom_read(srom->hv_devhdl, kernbuf,
				       *f_pos, bytes_this_pass);
		if (hv_retval <= 0) {
			if (retval == 0)
				retval = hv_retval;
			break;
		}

		if (copy_to_user(buf, kernbuf, hv_retval) != 0) {
			retval = -EFAULT;
			break;
		}

		retval += hv_retval;
		*f_pos += hv_retval;
		buf += hv_retval;
		count -= hv_retval;
	}

	mutex_unlock(&srom->lock);
	kfree(kernbuf);

	return retval;
}

/**
 * srom_write() - Write data to the device.
 * @filp: File for this specific open of the device.
 * @buf: User's data buffer.
 * @count: Number of bytes requested.
 * @f_pos: File position.
 *
 * Returns number of bytes written, or an error code.
 */
static ssize_t srom_write(struct file *filp, const char __user *buf,
			  size_t count, loff_t *f_pos)
{
	int retval = 0;
	void *kernbuf;
	struct srom_dev *srom = filp->private_data;

	kernbuf = kmalloc(SROM_CHUNK_SIZE, GFP_KERNEL);
	if (!kernbuf)
		return -ENOMEM;

	if (mutex_lock_interruptible(&srom->lock)) {
		retval = -ERESTARTSYS;
		kfree(kernbuf);
		return retval;
	}

	while (count) {
		int hv_retval;
		int bytes_this_pass = min(count, SROM_CHUNK_SIZE);

		if (copy_from_user(kernbuf, buf, bytes_this_pass) != 0) {
			retval = -EFAULT;
			break;
		}

		hv_retval = _srom_write(srom->hv_devhdl, kernbuf,
					*f_pos, bytes_this_pass);
		if (hv_retval <= 0) {
			if (retval == 0)
				retval = hv_retval;
			break;
		}

		retval += hv_retval;
		*f_pos += hv_retval;
		buf += hv_retval;
		count -= hv_retval;
	}

	mutex_unlock(&srom->lock);
	kfree(kernbuf);

	return retval;
}

/* Provide our own implementation so we can use srom->total_size. */
loff_t srom_llseek(struct file *file, loff_t offset, int origin)
{
	struct srom_dev *srom = file->private_data;
	return fixed_size_llseek(file, offset, origin, srom->total_size);
}

static ssize_t total_size_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct srom_dev *srom = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", srom->total_size);
}
static DEVICE_ATTR_RO(total_size);

static ssize_t sector_size_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct srom_dev *srom = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", srom->sector_size);
}
static DEVICE_ATTR_RO(sector_size);

static ssize_t page_size_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct srom_dev *srom = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", srom->page_size);
}
static DEVICE_ATTR_RO(page_size);

static struct attribute *srom_dev_attrs[] = {
	&dev_attr_total_size.attr,
	&dev_attr_sector_size.attr,
	&dev_attr_page_size.attr,
	NULL,
};
ATTRIBUTE_GROUPS(srom_dev);

static char *srom_devnode(struct device *dev, umode_t *mode)
{
	*mode = S_IRUGO | S_IWUSR;
	return kasprintf(GFP_KERNEL, "srom/%s", dev_name(dev));
}

/*
 * The fops
 */
static const struct file_operations srom_fops = {
	.owner =     THIS_MODULE,
	.llseek =    srom_llseek,
	.read =	     srom_read,
	.write =     srom_write,
	.open =	     srom_open,
	.release =   srom_release,
};

/**
 * srom_setup_minor() - Initialize per-minor information.
 * @srom: Per-device SROM state.
 * @index: Device to set up.
 */
static int srom_setup_minor(struct srom_dev *srom, int index)
{
	struct device *dev;
	int devhdl = srom->hv_devhdl;

	mutex_init(&srom->lock);

	if (_srom_read(devhdl, &srom->total_size,
		       SROM_TOTAL_SIZE_OFF, sizeof(srom->total_size)) < 0)
		return -EIO;
	if (_srom_read(devhdl, &srom->sector_size,
		       SROM_SECTOR_SIZE_OFF, sizeof(srom->sector_size)) < 0)
		return -EIO;
	if (_srom_read(devhdl, &srom->page_size,
		       SROM_PAGE_SIZE_OFF, sizeof(srom->page_size)) < 0)
		return -EIO;

	dev = device_create(srom_class, &srom_parent->dev,
			    MKDEV(srom_major, index), srom, "%d", index);
	return PTR_ERR_OR_ZERO(dev);
}

/** srom_init() - Initialize the driver's module. */
static int srom_init(void)
{
	int result, i;
	dev_t dev = MKDEV(srom_major, 0);

	/*
	 * Start with a plausible number of partitions; the krealloc() call
	 * below will yield about log(srom_devs) additional allocations.
	 */
	srom_devices = kzalloc(4 * sizeof(struct srom_dev), GFP_KERNEL);

	/* Discover the number of srom partitions. */
	for (i = 0; ; i++) {
		int devhdl;
		char buf[20];
		struct srom_dev *new_srom_devices =
			krealloc(srom_devices, (i+1) * sizeof(struct srom_dev),
				 GFP_KERNEL | __GFP_ZERO);
		if (!new_srom_devices) {
			result = -ENOMEM;
			goto fail_mem;
		}
		srom_devices = new_srom_devices;
		sprintf(buf, "srom/0/%d", i);
		devhdl = hv_dev_open((HV_VirtAddr)buf, 0);
		if (devhdl < 0) {
			if (devhdl != HV_ENODEV)
				pr_notice("srom/%d: hv_dev_open failed: %d.\n",
					  i, devhdl);
			break;
		}
		srom_devices[i].hv_devhdl = devhdl;
	}
	srom_devs = i;

	/* Bail out early if we have no partitions at all. */
	if (srom_devs == 0) {
		result = -ENODEV;
		goto fail_mem;
	}

	/* Register our major, and accept a dynamic number. */
	if (srom_major)
		result = register_chrdev_region(dev, srom_devs, "srom");
	else {
		result = alloc_chrdev_region(&dev, 0, srom_devs, "srom");
		srom_major = MAJOR(dev);
	}
	if (result < 0)
		goto fail_mem;

	/* Register a character device. */
	cdev_init(&srom_cdev, &srom_fops);
	srom_cdev.owner = THIS_MODULE;
	srom_cdev.ops = &srom_fops;
	result = cdev_add(&srom_cdev, dev, srom_devs);
	if (result < 0)
		goto fail_chrdev;

	/* Create a parent device */
	srom_parent = platform_device_register_simple("srom", -1, NULL, 0);
	if (IS_ERR(srom_parent)) {
		result = PTR_ERR(srom_parent);
		goto fail_pdev;
	}

	/* Create a sysfs class. */
	srom_class = class_create(THIS_MODULE, "srom");
	if (IS_ERR(srom_class)) {
		result = PTR_ERR(srom_class);
		goto fail_cdev;
	}
	srom_class->dev_groups = srom_dev_groups;
	srom_class->devnode = srom_devnode;

	/* Do per-partition initialization */
	for (i = 0; i < srom_devs; i++) {
		result = srom_setup_minor(srom_devices + i, i);
		if (result < 0)
			goto fail_class;
	}

	return 0;

fail_class:
	for (i = 0; i < srom_devs; i++)
		device_destroy(srom_class, MKDEV(srom_major, i));
	class_destroy(srom_class);
fail_cdev:
	platform_device_unregister(srom_parent);
fail_pdev:
	cdev_del(&srom_cdev);
fail_chrdev:
	unregister_chrdev_region(dev, srom_devs);
fail_mem:
	kfree(srom_devices);
	return result;
}

/** srom_cleanup() - Clean up the driver's module. */
static void srom_cleanup(void)
{
	int i;
	for (i = 0; i < srom_devs; i++)
		device_destroy(srom_class, MKDEV(srom_major, i));
	class_destroy(srom_class);
	cdev_del(&srom_cdev);
	platform_device_unregister(srom_parent);
	unregister_chrdev_region(MKDEV(srom_major, 0), srom_devs);
	kfree(srom_devices);
}

module_init(srom_init);
module_exit(srom_cleanup);

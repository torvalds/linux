/*
 * VMEbus User access driver
 *
 * Author: Martyn Welch <martyn.welch@gefanuc.com>
 * Copyright 2008 GE Fanuc Intelligent Platforms Embedded Systems, Inc.
 *
 * Based on work by:
 *   Tom Armistead and Ajit Prem
 *     Copyright 2004 Motorola Inc.
 *
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/pci.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/version.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include "../vme.h"
#include "vme_user.h"

/* Currently Documentation/devices.txt defines the following for VME:
 *
 * 221 char	VME bus
 * 		  0 = /dev/bus/vme/m0		First master image
 * 		  1 = /dev/bus/vme/m1		Second master image
 * 		  2 = /dev/bus/vme/m2		Third master image
 * 		  3 = /dev/bus/vme/m3		Fourth master image
 * 		  4 = /dev/bus/vme/s0		First slave image
 * 		  5 = /dev/bus/vme/s1		Second slave image
 * 		  6 = /dev/bus/vme/s2		Third slave image
 * 		  7 = /dev/bus/vme/s3		Fourth slave image
 * 		  8 = /dev/bus/vme/ctl		Control
 *
 * 		It is expected that all VME bus drivers will use the
 * 		same interface.  For interface documentation see
 * 		http://www.vmelinux.org/.
 *
 * However the VME driver at http://www.vmelinux.org/ is rather old and doesn't
 * even support the tsi148 chipset (which has 8 master and 8 slave windows).
 * We'll run with this or now as far as possible, however it probably makes
 * sense to get rid of the old mappings and just do everything dynamically.
 *
 * So for now, we'll restrict the driver to providing 4 masters and 4 slaves as
 * defined above and try to support at least some of the interface from
 * http://www.vmelinux.org/ as an alternative drive can be written providing a
 * saner interface later.
 */
#define VME_MAJOR	221	/* VME Major Device Number */
#define VME_DEVS	9	/* Number of dev entries */

#define MASTER_MINOR	0
#define MASTER_MAX	3
#define SLAVE_MINOR	4
#define SLAVE_MAX	7
#define CONTROL_MINOR	8

#define PCI_BUF_SIZE  0x20000	/* Size of one slave image buffer */

/*
 * Structure to handle image related parameters.
 */
typedef struct {
	void __iomem *kern_buf;	/* Buffer address in kernel space */
	dma_addr_t pci_buf;	/* Buffer address in PCI address space */
	unsigned long long size_buf;	/* Buffer size */
	struct semaphore sem;	/* Semaphore for locking image */
	struct device *device;	/* Sysfs device */
	struct vme_resource *resource;	/* VME resource */
	int users;		/* Number of current users */
} image_desc_t;
static image_desc_t image[VME_DEVS];

typedef struct {
	unsigned long reads;
	unsigned long writes;
	unsigned long ioctls;
	unsigned long irqs;
	unsigned long berrs;
	unsigned long dmaErrors;
	unsigned long timeouts;
	unsigned long external;
} driver_stats_t;
static driver_stats_t statistics;

struct cdev *vme_user_cdev;		/* Character device */
struct class *vme_user_sysfs_class;	/* Sysfs class */
struct device *vme_user_bridge;		/* Pointer to the bridge device */

static char driver_name[] = "vme_user";

static const int type[VME_DEVS] = {	MASTER_MINOR,	MASTER_MINOR,
					MASTER_MINOR,	MASTER_MINOR,
					SLAVE_MINOR,	SLAVE_MINOR,
					SLAVE_MINOR,	SLAVE_MINOR,
					CONTROL_MINOR
				};


static int vme_user_open(struct inode *, struct file *);
static int vme_user_release(struct inode *, struct file *);
static ssize_t vme_user_read(struct file *, char *, size_t, loff_t *);
static ssize_t vme_user_write(struct file *, const char *, size_t, loff_t *);
static loff_t vme_user_llseek(struct file *, loff_t, int);
static int vme_user_ioctl(struct inode *, struct file *, unsigned int,
	unsigned long);

static int __init vme_user_probe(struct device *dev);

static struct file_operations vme_user_fops = {
        .open = vme_user_open,
        .release = vme_user_release,
        .read = vme_user_read,
        .write = vme_user_write,
        .llseek = vme_user_llseek,
        .ioctl = vme_user_ioctl,
};


/*
 * Reset all the statistic counters
 */
static void reset_counters(void)
{
        statistics.reads = 0;
        statistics.writes = 0;
        statistics.ioctls = 0;
        statistics.irqs = 0;
        statistics.berrs = 0;
        statistics.dmaErrors = 0;
        statistics.timeouts = 0;
}

void lmcall(int monitor)
{
	printk("Caught Location Monitor %d access\n", monitor);
}

static void tests(void)
{
	struct vme_resource *dma_res;
	struct vme_dma_list *dma_list;
	struct vme_dma_attr *pattern_attr, *vme_attr;

	int retval;
	unsigned int data;

	printk("Running VME DMA test\n");
	dma_res = vme_request_dma(vme_user_bridge);
	dma_list = vme_new_dma_list(dma_res);
	pattern_attr = vme_dma_pattern_attribute(0x0,
		VME_DMA_PATTERN_WORD |
			VME_DMA_PATTERN_INCREMENT);
	vme_attr = vme_dma_vme_attribute(0x10000, VME_A32,
		VME_SCT, VME_D32);
	retval = vme_dma_list_add(dma_list, pattern_attr,
		vme_attr, 0x10000);
#if 0
	vme_dma_free_attribute(vme_attr);
	vme_attr = vme_dma_vme_attribute(0x20000, VME_A32,
		VME_SCT, VME_D32);
	retval = vme_dma_list_add(dma_list, pattern_attr,
		vme_attr, 0x10000);
#endif
	retval = vme_dma_list_exec(dma_list);
	vme_dma_free_attribute(pattern_attr);
	vme_dma_free_attribute(vme_attr);
	vme_dma_list_free(dma_list);
#if 0
	printk("Generating a VME interrupt\n");
	vme_generate_irq(dma_res, 0x3, 0xaa);
	printk("Interrupt returned\n");
#endif
	vme_dma_free(dma_res);

	/* Attempt RMW */
	data = vme_master_rmw(image[0].resource, 0x80000000, 0x00000000,
		0x80000000, 0);
	printk("RMW returned 0x%8.8x\n", data);


	/* Location Monitor */
	printk("vme_lm_set:%d\n", vme_lm_set(vme_user_bridge, 0x60000, VME_A32, VME_SCT | VME_USER | VME_DATA));
	printk("vme_lm_attach:%d\n", vme_lm_attach(vme_user_bridge, 0, lmcall));

	printk("Board in VME slot:%d\n", vme_slot_get(vme_user_bridge));
}

static int vme_user_open(struct inode *inode, struct file *file)
{
	int err;
	unsigned int minor = MINOR(inode->i_rdev);

	down(&image[minor].sem);
	/* Only allow device to be opened if a resource is allocated */
	if (image[minor].resource == NULL) {
		printk(KERN_ERR "No resources allocated for device\n");
		err = -EINVAL;
		goto err_res;
	}

	/* Increment user count */
	image[minor].users++;

	up(&image[minor].sem);

	return 0;

err_res:
	up(&image[minor].sem);

	return err;
}

static int vme_user_release(struct inode *inode, struct file *file)
{
	unsigned int minor = MINOR(inode->i_rdev);

	down(&image[minor].sem);

	/* Decrement user count */
	image[minor].users--;

	up(&image[minor].sem);

	return 0;
}

/*
 * We are going ot alloc a page during init per window for small transfers.
 * Small transfers will go VME -> buffer -> user space. Larger (more than a
 * page) transfers will lock the user space buffer into memory and then
 * transfer the data directly into the user space buffers.
 */
static ssize_t resource_to_user(int minor, char __user *buf, size_t count,
	loff_t *ppos)
{
	ssize_t retval;
	ssize_t copied = 0;

	if (count <= image[minor].size_buf) {
		/* We copy to kernel buffer */
		copied = vme_master_read(image[minor].resource,
			image[minor].kern_buf, count, *ppos);
		if (copied < 0) {
			return (int)copied;
		}

		retval = __copy_to_user(buf, image[minor].kern_buf,
			(unsigned long)copied);
		if (retval != 0) {
			copied = (copied - retval);
			printk("User copy failed\n");
			return -EINVAL;
		}

	} else {
		/* XXX Need to write this */
		printk("Currently don't support large transfers\n");
		/* Map in pages from userspace */

		/* Call vme_master_read to do the transfer */
		return -EINVAL;
	}

	return copied;
}

/*
 * We are going ot alloc a page during init per window for small transfers.
 * Small transfers will go user space -> buffer -> VME. Larger (more than a
 * page) transfers will lock the user space buffer into memory and then
 * transfer the data directly from the user space buffers out to VME.
 */
static ssize_t resource_from_user(unsigned int minor, const char *buf,
	size_t count, loff_t *ppos)
{
	ssize_t retval;
	ssize_t copied = 0;

	if (count <= image[minor].size_buf) {
		retval = __copy_from_user(image[minor].kern_buf, buf,
			(unsigned long)count);
		if (retval != 0)
			copied = (copied - retval);
		else
			copied = count;

		copied = vme_master_write(image[minor].resource,
			image[minor].kern_buf, copied, *ppos);
	} else {
		/* XXX Need to write this */
		printk("Currently don't support large transfers\n");
		/* Map in pages from userspace */

		/* Call vme_master_write to do the transfer */
		return -EINVAL;
	}

	return copied;
}

static ssize_t buffer_to_user(unsigned int minor, char __user *buf,
	size_t count, loff_t *ppos)
{
	void __iomem *image_ptr;
	ssize_t retval;

	image_ptr = image[minor].kern_buf + *ppos;

	retval = __copy_to_user(buf, image_ptr, (unsigned long)count);
	if (retval != 0) {
		retval = (count - retval);
		printk(KERN_WARNING "Partial copy to userspace\n");
	} else
		retval = count;

	/* Return number of bytes successfully read */
	return retval;
}

static ssize_t buffer_from_user(unsigned int minor, const char *buf,
	size_t count, loff_t *ppos)
{
	void __iomem *image_ptr;
	size_t retval;

	image_ptr = image[minor].kern_buf + *ppos;

	retval = __copy_from_user(image_ptr, buf, (unsigned long)count);
	if (retval != 0) {
		retval = (count - retval);
		printk(KERN_WARNING "Partial copy to userspace\n");
	} else
		retval = count;

	/* Return number of bytes successfully read */
	return retval;
}

static ssize_t vme_user_read(struct file *file, char *buf, size_t count,
			loff_t * ppos)
{
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
	ssize_t retval;
	size_t image_size;
	size_t okcount;

	down(&image[minor].sem);

	/* XXX Do we *really* want this helper - we can use vme_*_get ? */
	image_size = vme_get_size(image[minor].resource);

	/* Ensure we are starting at a valid location */
	if ((*ppos < 0) || (*ppos > (image_size - 1))) {
		up(&image[minor].sem);
		return 0;
	}

	/* Ensure not reading past end of the image */
	if (*ppos + count > image_size)
		okcount = image_size - *ppos;
	else
		okcount = count;

	switch (type[minor]){
	case MASTER_MINOR:
		retval = resource_to_user(minor, buf, okcount, ppos);
		break;
	case SLAVE_MINOR:
		retval = buffer_to_user(minor, buf, okcount, ppos);
		break;
	default:
		retval = -EINVAL;
	}

	up(&image[minor].sem);

	if (retval > 0)
		*ppos += retval;

	return retval;
}

static ssize_t vme_user_write(struct file *file, const char *buf, size_t count,
			 loff_t *ppos)
{
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
	ssize_t retval;
	size_t image_size;
	size_t okcount;

	down(&image[minor].sem);

	image_size = vme_get_size(image[minor].resource);

	/* Ensure we are starting at a valid location */
	if ((*ppos < 0) || (*ppos > (image_size - 1))) {
		up(&image[minor].sem);
		return 0;
	}

	/* Ensure not reading past end of the image */
	if (*ppos + count > image_size)
		okcount = image_size - *ppos;
	else
		okcount = count;

	switch (type[minor]){
	case MASTER_MINOR:
		retval = resource_from_user(minor, buf, okcount, ppos);
		break;
	case SLAVE_MINOR:
		retval = buffer_from_user(minor, buf, okcount, ppos);
		break;
	default:
		retval = -EINVAL;
	}

	up(&image[minor].sem);

	if (retval > 0)
		*ppos += retval;

	return retval;
}

static loff_t vme_user_llseek(struct file *file, loff_t off, int whence)
{
	printk(KERN_ERR "Llseek currently incomplete\n");
	return -EINVAL;
}

static int vme_user_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	unsigned int minor = MINOR(inode->i_rdev);
#if 0
	int ret_val;
#endif
	unsigned long copyRet;
	vme_slave_t slave;

	statistics.ioctls++;
	switch (type[minor]) {
	case CONTROL_MINOR:
		break;
	case MASTER_MINOR:
		break;
	case SLAVE_MINOR:
		switch (cmd) {
		case VME_SET_SLAVE:

			copyRet = copy_from_user(&slave, (char *)arg,
				sizeof(slave));
			if (copyRet != 0) {
				printk(KERN_WARNING "Partial copy from "
					"userspace\n");
				return -EFAULT;
			}

			return vme_slave_set(image[minor].resource,
				slave.enable, slave.vme_addr, slave.size,
				image[minor].pci_buf, slave.aspace,
				slave.cycle);

			break;
#if 0
		case VME_GET_SLAVE:
			vme_slave_t slave;

			ret_val = vme_slave_get(minor, &iRegs);

			copyRet = copy_to_user((char *)arg, &slave,
				sizeof(slave));
			if (copyRet != 0) {
				printk(KERN_WARNING "Partial copy to "
					"userspace\n");
				return -EFAULT;
			}

			return ret_val;
			break;
#endif
		}
		break;
	}

	return -EINVAL;
}


/*
 * Unallocate a previously allocated buffer
 */
static void buf_unalloc (int num)
{
	if (image[num].kern_buf) {
#ifdef VME_DEBUG
		printk(KERN_DEBUG "UniverseII:Releasing buffer at %p\n",
			image[num].pci_buf);
#endif

		vme_free_consistent(image[num].resource, image[num].size_buf,
			image[num].kern_buf, image[num].pci_buf);

		image[num].kern_buf = NULL;
		image[num].pci_buf = 0;
		image[num].size_buf = 0;

#ifdef VME_DEBUG
	} else {
		printk(KERN_DEBUG "UniverseII: Buffer not allocated\n");
#endif
	}
}

static struct vme_driver vme_user_driver = {
        .name = driver_name,
        .probe = vme_user_probe,
};


/*
 * In this simple access driver, the old behaviour is being preserved as much
 * as practical. We will therefore reserve the buffers and request the images
 * here so that we don't have to do it later.
 */
static int __init vme_bridge_init(void)
{
	int retval;
	printk(KERN_INFO "VME User Space Access Driver\n");
	printk("vme_user_driver:%p\n", &vme_user_driver);
	retval = vme_register_driver(&vme_user_driver);
	printk("vme_register_driver returned %d\n", retval);
	return retval;
}

/*
 * This structure gets passed a device, this should be the device created at
 * registration.
 */
static int __init vme_user_probe(struct device *dev)
{
	int i, err;
	char name[8];

	printk("Running vme_user_probe()\n");

	/* Pointer to the bridge device */
	vme_user_bridge = dev;

	/* Initialise descriptors */
	for (i = 0; i < VME_DEVS; i++) {
		image[i].kern_buf = NULL;
		image[i].pci_buf = 0;
		init_MUTEX(&(image[i].sem));
		image[i].device = NULL;
		image[i].resource = NULL;
		image[i].users = 0;
	}

	/* Initialise statistics counters */
	reset_counters();

	/* Assign major and minor numbers for the driver */
	err = register_chrdev_region(MKDEV(VME_MAJOR, 0), VME_DEVS,
		driver_name);
	if (err) {
		printk(KERN_WARNING "%s: Error getting Major Number %d for "
		"driver.\n", driver_name, VME_MAJOR);
		goto err_region;
	}

	/* Register the driver as a char device */
	vme_user_cdev = cdev_alloc();
	vme_user_cdev->ops = &vme_user_fops;
	vme_user_cdev->owner = THIS_MODULE;
	err = cdev_add(vme_user_cdev, MKDEV(VME_MAJOR, 0), VME_DEVS);
	if (err) {
		printk(KERN_WARNING "%s: cdev_all failed\n", driver_name);
		goto err_char;
	}

	/* Request slave resources and allocate buffers (128kB wide) */
	for (i = SLAVE_MINOR; i < (SLAVE_MAX + 1); i++) {
		/* XXX Need to properly request attributes */
		image[i].resource = vme_slave_request(vme_user_bridge,
			VME_A16, VME_SCT);
		if (image[i].resource == NULL) {
			printk(KERN_WARNING "Unable to allocate slave "
				"resource\n");
			goto err_buf;
		}
		image[i].size_buf = PCI_BUF_SIZE;
		image[i].kern_buf = vme_alloc_consistent(image[i].resource,
			image[i].size_buf, &(image[i].pci_buf));
		if (image[i].kern_buf == NULL) {
			printk(KERN_WARNING "Unable to allocate memory for "
				"buffer\n");
			image[i].pci_buf = 0;
			vme_slave_free(image[i].resource);
			err = -ENOMEM;
			goto err_buf;
		}
	}

	/*
	 * Request master resources allocate page sized buffers for small
	 * reads and writes
	 */
	for (i = MASTER_MINOR; i < (MASTER_MAX + 1); i++) {
		/* XXX Need to properly request attributes */
		image[i].resource = vme_master_request(vme_user_bridge,
			VME_A32, VME_SCT, VME_D32);
		if (image[i].resource == NULL) {
			printk(KERN_WARNING "Unable to allocate master "
				"resource\n");
			goto err_buf;
		}
		image[i].size_buf = PAGE_SIZE;
		image[i].kern_buf = vme_alloc_consistent(image[i].resource,
			image[i].size_buf, &(image[i].pci_buf));
		if (image[i].kern_buf == NULL) {
			printk(KERN_WARNING "Unable to allocate memory for "
				"buffer\n");
			image[i].pci_buf = 0;
			vme_master_free(image[i].resource);
			err = -ENOMEM;
			goto err_buf;
		}
	}

	/* Setup some debug windows */
	for (i = SLAVE_MINOR; i < (SLAVE_MAX + 1); i++) {
		err = vme_slave_set(image[i].resource, 1, 0x4000*(i-4),
			0x4000, image[i].pci_buf, VME_A16,
			VME_SCT | VME_SUPER | VME_USER | VME_PROG | VME_DATA);
		if (err != 0) {
			printk(KERN_WARNING "Failed to configure window\n");
			goto err_buf;
		}
	}
	for (i = MASTER_MINOR; i < (MASTER_MAX + 1); i++) {
		err = vme_master_set(image[i].resource, 1,
			(0x10000 + (0x10000*i)), 0x10000,
			VME_A32, VME_SCT | VME_USER | VME_DATA, VME_D32);
		if (err != 0) {
			printk(KERN_WARNING "Failed to configure window\n");
			goto err_buf;
		}
	}

	/* Create sysfs entries - on udev systems this creates the dev files */
	vme_user_sysfs_class = class_create(THIS_MODULE, driver_name);
	if (IS_ERR(vme_user_sysfs_class)) {
		printk(KERN_ERR "Error creating vme_user class.\n");
		err = PTR_ERR(vme_user_sysfs_class);
		goto err_class;
	}

	/* Add sysfs Entries */
	for (i=0; i<VME_DEVS; i++) {
		switch (type[i]) {
		case MASTER_MINOR:
			sprintf(name,"bus/vme/m%%d");
			break;
		case CONTROL_MINOR:
			sprintf(name,"bus/vme/ctl");
			break;
		case SLAVE_MINOR:
			sprintf(name,"bus/vme/s%%d");
			break;
		default:
			err = -EINVAL;
			goto err_sysfs;
			break;
		}

		image[i].device =
			device_create(vme_user_sysfs_class, NULL,
				MKDEV(VME_MAJOR, i), NULL, name,
				(type[i] == SLAVE_MINOR)? i - (MASTER_MAX + 1) : i);
		if (IS_ERR(image[i].device)) {
			printk("%s: Error creating sysfs device\n",
				driver_name);
			err = PTR_ERR(image[i].device);
			goto err_sysfs;
		}
	}

	/* XXX Run tests */
	/*
	tests();
	*/

	return 0;

	/* Ensure counter set correcty to destroy all sysfs devices */
	i = VME_DEVS;
err_sysfs:
	while (i > 0){
		i--;
		device_destroy(vme_user_sysfs_class, MKDEV(VME_MAJOR, i));
	}
	class_destroy(vme_user_sysfs_class);

	/* Ensure counter set correcty to unalloc all slave buffers */
	i = SLAVE_MAX + 1;
err_buf:
	while (i > SLAVE_MINOR){
		i--;
		vme_slave_set(image[i].resource, 0, 0, 0, 0, VME_A32, 0);
		vme_slave_free(image[i].resource);
		buf_unalloc(i);
	}
err_class:
	cdev_del(vme_user_cdev);
err_char:
	unregister_chrdev_region(MKDEV(VME_MAJOR, 0), VME_DEVS);
err_region:
	return err;
}

static void __exit vme_bridge_exit(void)
{
	int i;

	/* Remove sysfs Entries */
	for(i=0; i<VME_DEVS; i++) {
		device_destroy(vme_user_sysfs_class, MKDEV(VME_MAJOR, i));
	}
	class_destroy(vme_user_sysfs_class);

	for (i = SLAVE_MINOR; i < (SLAVE_MAX + 1); i++) {
		buf_unalloc(i);
	}

	/* Unregister device driver */
	cdev_del(vme_user_cdev);

	/* Unregiser the major and minor device numbers */
	unregister_chrdev_region(MKDEV(VME_MAJOR, 0), VME_DEVS);
}

MODULE_DESCRIPTION("VME User Space Access Driver");
MODULE_AUTHOR("Martyn Welch <martyn.welch@gefanuc.com");
MODULE_LICENSE("GPL");

module_init(vme_bridge_init);
module_exit(vme_bridge_exit);

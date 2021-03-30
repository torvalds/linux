// SPDX-License-Identifier: GPL-2.0+
/*
 * VMEbus User access driver
 *
 * Author: Martyn Welch <martyn.welch@ge.com>
 * Copyright 2008 GE Intelligent Platforms Embedded Systems, Inc.
 *
 * Based on work by:
 *   Tom Armistead and Ajit Prem
 *     Copyright 2004 Motorola Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/refcount.h>
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
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/syscalls.h>
#include <linux/types.h>

#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/vme.h>

#include "vme_user.h"

static const char driver_name[] = "vme_user";

static int bus[VME_USER_BUS_MAX];
static unsigned int bus_num;

/* Currently Documentation/admin-guide/devices.rst defines the
 * following for VME:
 *
 * 221 char	VME bus
 *		  0 = /dev/bus/vme/m0		First master image
 *		  1 = /dev/bus/vme/m1		Second master image
 *		  2 = /dev/bus/vme/m2		Third master image
 *		  3 = /dev/bus/vme/m3		Fourth master image
 *		  4 = /dev/bus/vme/s0		First slave image
 *		  5 = /dev/bus/vme/s1		Second slave image
 *		  6 = /dev/bus/vme/s2		Third slave image
 *		  7 = /dev/bus/vme/s3		Fourth slave image
 *		  8 = /dev/bus/vme/ctl		Control
 *
 *		It is expected that all VME bus drivers will use the
 *		same interface.  For interface documentation see
 *		http://www.vmelinux.org/.
 *
 * However the VME driver at http://www.vmelinux.org/ is rather old and doesn't
 * even support the tsi148 chipset (which has 8 master and 8 slave windows).
 * We'll run with this for now as far as possible, however it probably makes
 * sense to get rid of the old mappings and just do everything dynamically.
 *
 * So for now, we'll restrict the driver to providing 4 masters and 4 slaves as
 * defined above and try to support at least some of the interface from
 * http://www.vmelinux.org/ as an alternative the driver can be written
 * providing a saner interface later.
 *
 * The vmelinux.org driver never supported slave images, the devices reserved
 * for slaves were repurposed to support all 8 master images on the UniverseII!
 * We shall support 4 masters and 4 slaves with this driver.
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
struct image_desc {
	void *kern_buf;	/* Buffer address in kernel space */
	dma_addr_t pci_buf;	/* Buffer address in PCI address space */
	unsigned long long size_buf;	/* Buffer size */
	struct mutex mutex;	/* Mutex for locking image */
	struct device *device;	/* Sysfs device */
	struct vme_resource *resource;	/* VME resource */
	int mmap_count;		/* Number of current mmap's */
};

static struct image_desc image[VME_DEVS];

static struct cdev *vme_user_cdev;		/* Character device */
static struct class *vme_user_sysfs_class;	/* Sysfs class */
static struct vme_dev *vme_user_bridge;		/* Pointer to user device */

static const int type[VME_DEVS] = {	MASTER_MINOR,	MASTER_MINOR,
					MASTER_MINOR,	MASTER_MINOR,
					SLAVE_MINOR,	SLAVE_MINOR,
					SLAVE_MINOR,	SLAVE_MINOR,
					CONTROL_MINOR
				};

struct vme_user_vma_priv {
	unsigned int minor;
	refcount_t refcnt;
};

static ssize_t resource_to_user(int minor, char __user *buf, size_t count,
				loff_t *ppos)
{
	ssize_t copied = 0;

	if (count > image[minor].size_buf)
		count = image[minor].size_buf;

	copied = vme_master_read(image[minor].resource, image[minor].kern_buf,
				 count, *ppos);
	if (copied < 0)
		return (int)copied;

	if (copy_to_user(buf, image[minor].kern_buf, (unsigned long)copied))
		return -EFAULT;

	return copied;
}

static ssize_t resource_from_user(unsigned int minor, const char __user *buf,
				  size_t count, loff_t *ppos)
{
	if (count > image[minor].size_buf)
		count = image[minor].size_buf;

	if (copy_from_user(image[minor].kern_buf, buf, (unsigned long)count))
		return -EFAULT;

	return vme_master_write(image[minor].resource, image[minor].kern_buf,
				count, *ppos);
}

static ssize_t buffer_to_user(unsigned int minor, char __user *buf,
			      size_t count, loff_t *ppos)
{
	void *image_ptr;

	image_ptr = image[minor].kern_buf + *ppos;
	if (copy_to_user(buf, image_ptr, (unsigned long)count))
		return -EFAULT;

	return count;
}

static ssize_t buffer_from_user(unsigned int minor, const char __user *buf,
				size_t count, loff_t *ppos)
{
	void *image_ptr;

	image_ptr = image[minor].kern_buf + *ppos;
	if (copy_from_user(image_ptr, buf, (unsigned long)count))
		return -EFAULT;

	return count;
}

static ssize_t vme_user_read(struct file *file, char __user *buf, size_t count,
			     loff_t *ppos)
{
	unsigned int minor = iminor(file_inode(file));
	ssize_t retval;
	size_t image_size;

	if (minor == CONTROL_MINOR)
		return 0;

	mutex_lock(&image[minor].mutex);

	/* XXX Do we *really* want this helper - we can use vme_*_get ? */
	image_size = vme_get_size(image[minor].resource);

	/* Ensure we are starting at a valid location */
	if ((*ppos < 0) || (*ppos > (image_size - 1))) {
		mutex_unlock(&image[minor].mutex);
		return 0;
	}

	/* Ensure not reading past end of the image */
	if (*ppos + count > image_size)
		count = image_size - *ppos;

	switch (type[minor]) {
	case MASTER_MINOR:
		retval = resource_to_user(minor, buf, count, ppos);
		break;
	case SLAVE_MINOR:
		retval = buffer_to_user(minor, buf, count, ppos);
		break;
	default:
		retval = -EINVAL;
	}

	mutex_unlock(&image[minor].mutex);
	if (retval > 0)
		*ppos += retval;

	return retval;
}

static ssize_t vme_user_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	unsigned int minor = iminor(file_inode(file));
	ssize_t retval;
	size_t image_size;

	if (minor == CONTROL_MINOR)
		return 0;

	mutex_lock(&image[minor].mutex);

	image_size = vme_get_size(image[minor].resource);

	/* Ensure we are starting at a valid location */
	if ((*ppos < 0) || (*ppos > (image_size - 1))) {
		mutex_unlock(&image[minor].mutex);
		return 0;
	}

	/* Ensure not reading past end of the image */
	if (*ppos + count > image_size)
		count = image_size - *ppos;

	switch (type[minor]) {
	case MASTER_MINOR:
		retval = resource_from_user(minor, buf, count, ppos);
		break;
	case SLAVE_MINOR:
		retval = buffer_from_user(minor, buf, count, ppos);
		break;
	default:
		retval = -EINVAL;
	}

	mutex_unlock(&image[minor].mutex);

	if (retval > 0)
		*ppos += retval;

	return retval;
}

static loff_t vme_user_llseek(struct file *file, loff_t off, int whence)
{
	unsigned int minor = iminor(file_inode(file));
	size_t image_size;
	loff_t res;

	switch (type[minor]) {
	case MASTER_MINOR:
	case SLAVE_MINOR:
		mutex_lock(&image[minor].mutex);
		image_size = vme_get_size(image[minor].resource);
		res = fixed_size_llseek(file, off, whence, image_size);
		mutex_unlock(&image[minor].mutex);
		return res;
	}

	return -EINVAL;
}

/*
 * The ioctls provided by the old VME access method (the one at vmelinux.org)
 * are most certainly wrong as the effectively push the registers layout
 * through to user space. Given that the VME core can handle multiple bridges,
 * with different register layouts this is most certainly not the way to go.
 *
 * We aren't using the structures defined in the Motorola driver either - these
 * are also quite low level, however we should use the definitions that have
 * already been defined.
 */
static int vme_user_ioctl(struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct vme_master master;
	struct vme_slave slave;
	struct vme_irq_id irq_req;
	unsigned long copied;
	unsigned int minor = iminor(inode);
	int retval;
	dma_addr_t pci_addr;
	void __user *argp = (void __user *)arg;

	switch (type[minor]) {
	case CONTROL_MINOR:
		switch (cmd) {
		case VME_IRQ_GEN:
			copied = copy_from_user(&irq_req, argp,
						sizeof(irq_req));
			if (copied) {
				pr_warn("Partial copy from userspace\n");
				return -EFAULT;
			}

			return vme_irq_generate(vme_user_bridge,
						  irq_req.level,
						  irq_req.statid);
		}
		break;
	case MASTER_MINOR:
		switch (cmd) {
		case VME_GET_MASTER:
			memset(&master, 0, sizeof(master));

			/* XXX	We do not want to push aspace, cycle and width
			 *	to userspace as they are
			 */
			retval = vme_master_get(image[minor].resource,
						&master.enable,
						&master.vme_addr,
						&master.size, &master.aspace,
						&master.cycle, &master.dwidth);

			copied = copy_to_user(argp, &master,
					      sizeof(master));
			if (copied) {
				pr_warn("Partial copy to userspace\n");
				return -EFAULT;
			}

			return retval;

		case VME_SET_MASTER:

			if (image[minor].mmap_count != 0) {
				pr_warn("Can't adjust mapped window\n");
				return -EPERM;
			}

			copied = copy_from_user(&master, argp, sizeof(master));
			if (copied) {
				pr_warn("Partial copy from userspace\n");
				return -EFAULT;
			}

			/* XXX	We do not want to push aspace, cycle and width
			 *	to userspace as they are
			 */
			return vme_master_set(image[minor].resource,
				master.enable, master.vme_addr, master.size,
				master.aspace, master.cycle, master.dwidth);

			break;
		}
		break;
	case SLAVE_MINOR:
		switch (cmd) {
		case VME_GET_SLAVE:
			memset(&slave, 0, sizeof(slave));

			/* XXX	We do not want to push aspace, cycle and width
			 *	to userspace as they are
			 */
			retval = vme_slave_get(image[minor].resource,
					       &slave.enable, &slave.vme_addr,
					       &slave.size, &pci_addr,
					       &slave.aspace, &slave.cycle);

			copied = copy_to_user(argp, &slave,
					      sizeof(slave));
			if (copied) {
				pr_warn("Partial copy to userspace\n");
				return -EFAULT;
			}

			return retval;

		case VME_SET_SLAVE:

			copied = copy_from_user(&slave, argp, sizeof(slave));
			if (copied) {
				pr_warn("Partial copy from userspace\n");
				return -EFAULT;
			}

			/* XXX	We do not want to push aspace, cycle and width
			 *	to userspace as they are
			 */
			return vme_slave_set(image[minor].resource,
				slave.enable, slave.vme_addr, slave.size,
				image[minor].pci_buf, slave.aspace,
				slave.cycle);

			break;
		}
		break;
	}

	return -EINVAL;
}

static long
vme_user_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret;
	struct inode *inode = file_inode(file);
	unsigned int minor = iminor(inode);

	mutex_lock(&image[minor].mutex);
	ret = vme_user_ioctl(inode, file, cmd, arg);
	mutex_unlock(&image[minor].mutex);

	return ret;
}

static void vme_user_vm_open(struct vm_area_struct *vma)
{
	struct vme_user_vma_priv *vma_priv = vma->vm_private_data;

	refcount_inc(&vma_priv->refcnt);
}

static void vme_user_vm_close(struct vm_area_struct *vma)
{
	struct vme_user_vma_priv *vma_priv = vma->vm_private_data;
	unsigned int minor = vma_priv->minor;

	if (!refcount_dec_and_test(&vma_priv->refcnt))
		return;

	mutex_lock(&image[minor].mutex);
	image[minor].mmap_count--;
	mutex_unlock(&image[minor].mutex);

	kfree(vma_priv);
}

static const struct vm_operations_struct vme_user_vm_ops = {
	.open = vme_user_vm_open,
	.close = vme_user_vm_close,
};

static int vme_user_master_mmap(unsigned int minor, struct vm_area_struct *vma)
{
	int err;
	struct vme_user_vma_priv *vma_priv;

	mutex_lock(&image[minor].mutex);

	err = vme_master_mmap(image[minor].resource, vma);
	if (err) {
		mutex_unlock(&image[minor].mutex);
		return err;
	}

	vma_priv = kmalloc(sizeof(*vma_priv), GFP_KERNEL);
	if (!vma_priv) {
		mutex_unlock(&image[minor].mutex);
		return -ENOMEM;
	}

	vma_priv->minor = minor;
	refcount_set(&vma_priv->refcnt, 1);
	vma->vm_ops = &vme_user_vm_ops;
	vma->vm_private_data = vma_priv;

	image[minor].mmap_count++;

	mutex_unlock(&image[minor].mutex);

	return 0;
}

static int vme_user_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned int minor = iminor(file_inode(file));

	if (type[minor] == MASTER_MINOR)
		return vme_user_master_mmap(minor, vma);

	return -ENODEV;
}

static const struct file_operations vme_user_fops = {
	.read = vme_user_read,
	.write = vme_user_write,
	.llseek = vme_user_llseek,
	.unlocked_ioctl = vme_user_unlocked_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.mmap = vme_user_mmap,
};

static int vme_user_match(struct vme_dev *vdev)
{
	int i;

	int cur_bus = vme_bus_num(vdev);
	int cur_slot = vme_slot_num(vdev);

	for (i = 0; i < bus_num; i++)
		if ((cur_bus == bus[i]) && (cur_slot == vdev->num))
			return 1;

	return 0;
}

/*
 * In this simple access driver, the old behaviour is being preserved as much
 * as practical. We will therefore reserve the buffers and request the images
 * here so that we don't have to do it later.
 */
static int vme_user_probe(struct vme_dev *vdev)
{
	int i, err;
	char *name;

	/* Save pointer to the bridge device */
	if (vme_user_bridge) {
		dev_err(&vdev->dev, "Driver can only be loaded for 1 device\n");
		err = -EINVAL;
		goto err_dev;
	}
	vme_user_bridge = vdev;

	/* Initialise descriptors */
	for (i = 0; i < VME_DEVS; i++) {
		image[i].kern_buf = NULL;
		image[i].pci_buf = 0;
		mutex_init(&image[i].mutex);
		image[i].device = NULL;
		image[i].resource = NULL;
	}

	/* Assign major and minor numbers for the driver */
	err = register_chrdev_region(MKDEV(VME_MAJOR, 0), VME_DEVS,
				     driver_name);
	if (err) {
		dev_warn(&vdev->dev, "Error getting Major Number %d for driver.\n",
			 VME_MAJOR);
		goto err_region;
	}

	/* Register the driver as a char device */
	vme_user_cdev = cdev_alloc();
	if (!vme_user_cdev) {
		err = -ENOMEM;
		goto err_char;
	}
	vme_user_cdev->ops = &vme_user_fops;
	vme_user_cdev->owner = THIS_MODULE;
	err = cdev_add(vme_user_cdev, MKDEV(VME_MAJOR, 0), VME_DEVS);
	if (err)
		goto err_class;

	/* Request slave resources and allocate buffers (128kB wide) */
	for (i = SLAVE_MINOR; i < (SLAVE_MAX + 1); i++) {
		/* XXX Need to properly request attributes */
		/* For ca91cx42 bridge there are only two slave windows
		 * supporting A16 addressing, so we request A24 supported
		 * by all windows.
		 */
		image[i].resource = vme_slave_request(vme_user_bridge,
						      VME_A24, VME_SCT);
		if (!image[i].resource) {
			dev_warn(&vdev->dev,
				 "Unable to allocate slave resource\n");
			err = -ENOMEM;
			goto err_slave;
		}
		image[i].size_buf = PCI_BUF_SIZE;
		image[i].kern_buf = vme_alloc_consistent(image[i].resource,
							 image[i].size_buf,
							 &image[i].pci_buf);
		if (!image[i].kern_buf) {
			dev_warn(&vdev->dev,
				 "Unable to allocate memory for buffer\n");
			image[i].pci_buf = 0;
			vme_slave_free(image[i].resource);
			err = -ENOMEM;
			goto err_slave;
		}
	}

	/*
	 * Request master resources allocate page sized buffers for small
	 * reads and writes
	 */
	for (i = MASTER_MINOR; i < (MASTER_MAX + 1); i++) {
		/* XXX Need to properly request attributes */
		image[i].resource = vme_master_request(vme_user_bridge,
						       VME_A32, VME_SCT,
						       VME_D32);
		if (!image[i].resource) {
			dev_warn(&vdev->dev,
				 "Unable to allocate master resource\n");
			err = -ENOMEM;
			goto err_master;
		}
		image[i].size_buf = PCI_BUF_SIZE;
		image[i].kern_buf = kmalloc(image[i].size_buf, GFP_KERNEL);
		if (!image[i].kern_buf) {
			err = -ENOMEM;
			vme_master_free(image[i].resource);
			goto err_master;
		}
	}

	/* Create sysfs entries - on udev systems this creates the dev files */
	vme_user_sysfs_class = class_create(THIS_MODULE, driver_name);
	if (IS_ERR(vme_user_sysfs_class)) {
		dev_err(&vdev->dev, "Error creating vme_user class.\n");
		err = PTR_ERR(vme_user_sysfs_class);
		goto err_master;
	}

	/* Add sysfs Entries */
	for (i = 0; i < VME_DEVS; i++) {
		int num;

		switch (type[i]) {
		case MASTER_MINOR:
			name = "bus/vme/m%d";
			break;
		case CONTROL_MINOR:
			name = "bus/vme/ctl";
			break;
		case SLAVE_MINOR:
			name = "bus/vme/s%d";
			break;
		default:
			err = -EINVAL;
			goto err_sysfs;
		}

		num = (type[i] == SLAVE_MINOR) ? i - (MASTER_MAX + 1) : i;
		image[i].device = device_create(vme_user_sysfs_class, NULL,
						MKDEV(VME_MAJOR, i), NULL,
						name, num);
		if (IS_ERR(image[i].device)) {
			dev_info(&vdev->dev, "Error creating sysfs device\n");
			err = PTR_ERR(image[i].device);
			goto err_sysfs;
		}
	}

	return 0;

err_sysfs:
	while (i > 0) {
		i--;
		device_destroy(vme_user_sysfs_class, MKDEV(VME_MAJOR, i));
	}
	class_destroy(vme_user_sysfs_class);

	/* Ensure counter set correctly to unalloc all master windows */
	i = MASTER_MAX + 1;
err_master:
	while (i > MASTER_MINOR) {
		i--;
		kfree(image[i].kern_buf);
		vme_master_free(image[i].resource);
	}

	/*
	 * Ensure counter set correctly to unalloc all slave windows and buffers
	 */
	i = SLAVE_MAX + 1;
err_slave:
	while (i > SLAVE_MINOR) {
		i--;
		vme_free_consistent(image[i].resource, image[i].size_buf,
				    image[i].kern_buf, image[i].pci_buf);
		vme_slave_free(image[i].resource);
	}
err_class:
	cdev_del(vme_user_cdev);
err_char:
	unregister_chrdev_region(MKDEV(VME_MAJOR, 0), VME_DEVS);
err_region:
err_dev:
	return err;
}

static void vme_user_remove(struct vme_dev *dev)
{
	int i;

	/* Remove sysfs Entries */
	for (i = 0; i < VME_DEVS; i++) {
		mutex_destroy(&image[i].mutex);
		device_destroy(vme_user_sysfs_class, MKDEV(VME_MAJOR, i));
	}
	class_destroy(vme_user_sysfs_class);

	for (i = MASTER_MINOR; i < (MASTER_MAX + 1); i++) {
		kfree(image[i].kern_buf);
		vme_master_free(image[i].resource);
	}

	for (i = SLAVE_MINOR; i < (SLAVE_MAX + 1); i++) {
		vme_slave_set(image[i].resource, 0, 0, 0, 0, VME_A32, 0);
		vme_free_consistent(image[i].resource, image[i].size_buf,
				    image[i].kern_buf, image[i].pci_buf);
		vme_slave_free(image[i].resource);
	}

	/* Unregister device driver */
	cdev_del(vme_user_cdev);

	/* Unregister the major and minor device numbers */
	unregister_chrdev_region(MKDEV(VME_MAJOR, 0), VME_DEVS);
}

static struct vme_driver vme_user_driver = {
	.name = driver_name,
	.match = vme_user_match,
	.probe = vme_user_probe,
	.remove = vme_user_remove,
};

static int __init vme_user_init(void)
{
	int retval = 0;

	pr_info("VME User Space Access Driver\n");

	if (bus_num == 0) {
		pr_err("No cards, skipping registration\n");
		retval = -ENODEV;
		goto err_nocard;
	}

	/* Let's start by supporting one bus, we can support more than one
	 * in future revisions if that ever becomes necessary.
	 */
	if (bus_num > VME_USER_BUS_MAX) {
		pr_err("Driver only able to handle %d buses\n",
		       VME_USER_BUS_MAX);
		bus_num = VME_USER_BUS_MAX;
	}

	/*
	 * Here we just register the maximum number of devices we can and
	 * leave vme_user_match() to allow only 1 to go through to probe().
	 * This way, if we later want to allow multiple user access devices,
	 * we just change the code in vme_user_match().
	 */
	retval = vme_register_driver(&vme_user_driver, VME_MAX_SLOTS);
	if (retval)
		goto err_reg;

	return retval;

err_reg:
err_nocard:
	return retval;
}

static void __exit vme_user_exit(void)
{
	vme_unregister_driver(&vme_user_driver);
}

MODULE_PARM_DESC(bus, "Enumeration of VMEbus to which the driver is connected");
module_param_array(bus, int, &bus_num, 0000);

MODULE_DESCRIPTION("VME User Space Access Driver");
MODULE_AUTHOR("Martyn Welch <martyn.welch@ge.com");
MODULE_LICENSE("GPL");

module_init(vme_user_init);
module_exit(vme_user_exit);

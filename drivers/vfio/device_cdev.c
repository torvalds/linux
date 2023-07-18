// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Intel Corporation.
 */
#include <linux/vfio.h>

#include "vfio.h"

static dev_t device_devt;

void vfio_init_device_cdev(struct vfio_device *device)
{
	device->device.devt = MKDEV(MAJOR(device_devt), device->index);
	cdev_init(&device->cdev, &vfio_device_fops);
	device->cdev.owner = THIS_MODULE;
}

/*
 * device access via the fd opened by this function is blocked until
 * .open_device() is called successfully during BIND_IOMMUFD.
 */
int vfio_device_fops_cdev_open(struct inode *inode, struct file *filep)
{
	struct vfio_device *device = container_of(inode->i_cdev,
						  struct vfio_device, cdev);
	struct vfio_device_file *df;
	int ret;

	/* Paired with the put in vfio_device_fops_release() */
	if (!vfio_device_try_get_registration(device))
		return -ENODEV;

	df = vfio_allocate_device_file(device);
	if (IS_ERR(df)) {
		ret = PTR_ERR(df);
		goto err_put_registration;
	}

	filep->private_data = df;

	return 0;

err_put_registration:
	vfio_device_put_registration(device);
	return ret;
}

static char *vfio_device_devnode(const struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "vfio/devices/%s", dev_name(dev));
}

int vfio_cdev_init(struct class *device_class)
{
	device_class->devnode = vfio_device_devnode;
	return alloc_chrdev_region(&device_devt, 0,
				   MINORMASK + 1, "vfio-dev");
}

void vfio_cdev_cleanup(void)
{
	unregister_chrdev_region(device_devt, MINORMASK + 1);
}

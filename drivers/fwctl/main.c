// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES
 */
#define pr_fmt(fmt) "fwctl: " fmt
#include <linux/fwctl.h>

#include <linux/container_of.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/slab.h>

enum {
	FWCTL_MAX_DEVICES = 4096,
};
static_assert(FWCTL_MAX_DEVICES < (1U << MINORBITS));

static dev_t fwctl_dev;
static DEFINE_IDA(fwctl_ida);

static int fwctl_fops_open(struct inode *inode, struct file *filp)
{
	struct fwctl_device *fwctl =
		container_of(inode->i_cdev, struct fwctl_device, cdev);

	get_device(&fwctl->dev);
	filp->private_data = fwctl;
	return 0;
}

static int fwctl_fops_release(struct inode *inode, struct file *filp)
{
	struct fwctl_device *fwctl = filp->private_data;

	fwctl_put(fwctl);
	return 0;
}

static const struct file_operations fwctl_fops = {
	.owner = THIS_MODULE,
	.open = fwctl_fops_open,
	.release = fwctl_fops_release,
};

static void fwctl_device_release(struct device *device)
{
	struct fwctl_device *fwctl =
		container_of(device, struct fwctl_device, dev);

	ida_free(&fwctl_ida, fwctl->dev.devt - fwctl_dev);
	kfree(fwctl);
}

static char *fwctl_devnode(const struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "fwctl/%s", dev_name(dev));
}

static struct class fwctl_class = {
	.name = "fwctl",
	.dev_release = fwctl_device_release,
	.devnode = fwctl_devnode,
};

static struct fwctl_device *
_alloc_device(struct device *parent, const struct fwctl_ops *ops, size_t size)
{
	struct fwctl_device *fwctl __free(kfree) = kzalloc(size, GFP_KERNEL);
	int devnum;

	if (!fwctl)
		return NULL;

	fwctl->dev.class = &fwctl_class;
	fwctl->dev.parent = parent;

	devnum = ida_alloc_max(&fwctl_ida, FWCTL_MAX_DEVICES - 1, GFP_KERNEL);
	if (devnum < 0)
		return NULL;

	fwctl->dev.devt = fwctl_dev + devnum;
	fwctl->dev.class = &fwctl_class;
	fwctl->dev.parent = parent;

	device_initialize(&fwctl->dev);
	return_ptr(fwctl);
}

/* Drivers use the fwctl_alloc_device() wrapper */
struct fwctl_device *_fwctl_alloc_device(struct device *parent,
					 const struct fwctl_ops *ops,
					 size_t size)
{
	struct fwctl_device *fwctl __free(fwctl) =
		_alloc_device(parent, ops, size);

	if (!fwctl)
		return NULL;

	cdev_init(&fwctl->cdev, &fwctl_fops);
	/*
	 * The driver module is protected by fwctl_register/unregister(),
	 * unregister won't complete until we are done with the driver's module.
	 */
	fwctl->cdev.owner = THIS_MODULE;

	if (dev_set_name(&fwctl->dev, "fwctl%d", fwctl->dev.devt - fwctl_dev))
		return NULL;

	fwctl->ops = ops;
	return_ptr(fwctl);
}
EXPORT_SYMBOL_NS_GPL(_fwctl_alloc_device, "FWCTL");

/**
 * fwctl_register - Register a new device to the subsystem
 * @fwctl: Previously allocated fwctl_device
 *
 * On return the device is visible through sysfs and /dev, driver ops may be
 * called.
 */
int fwctl_register(struct fwctl_device *fwctl)
{
	return cdev_device_add(&fwctl->cdev, &fwctl->dev);
}
EXPORT_SYMBOL_NS_GPL(fwctl_register, "FWCTL");

/**
 * fwctl_unregister - Unregister a device from the subsystem
 * @fwctl: Previously allocated and registered fwctl_device
 *
 * Undoes fwctl_register(). On return no driver ops will be called. The
 * caller must still call fwctl_put() to free the fwctl.
 *
 * The design of fwctl allows this sort of disassociation of the driver from the
 * subsystem primarily by keeping memory allocations owned by the core subsytem.
 * The fwctl_device and fwctl_uctx can both be freed without requiring a driver
 * callback. This allows the module to remain unlocked while FDs are open.
 */
void fwctl_unregister(struct fwctl_device *fwctl)
{
	cdev_device_del(&fwctl->cdev, &fwctl->dev);
}
EXPORT_SYMBOL_NS_GPL(fwctl_unregister, "FWCTL");

static int __init fwctl_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&fwctl_dev, 0, FWCTL_MAX_DEVICES, "fwctl");
	if (ret)
		return ret;

	ret = class_register(&fwctl_class);
	if (ret)
		goto err_chrdev;
	return 0;

err_chrdev:
	unregister_chrdev_region(fwctl_dev, FWCTL_MAX_DEVICES);
	return ret;
}

static void __exit fwctl_exit(void)
{
	class_unregister(&fwctl_class);
	unregister_chrdev_region(fwctl_dev, FWCTL_MAX_DEVICES);
}

module_init(fwctl_init);
module_exit(fwctl_exit);
MODULE_DESCRIPTION("fwctl device firmware access framework");
MODULE_LICENSE("GPL");

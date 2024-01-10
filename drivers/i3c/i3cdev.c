// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Synopsys, Inc. and/or its affiliates.
 *
 * Author: Vitor Soares <soares@synopsys.com>
 */

#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <linux/i3c/i3cdev.h>

#include "internals.h"

struct i3cdev_data {
	struct i3c_device *i3c;
	struct device *dev;
	struct mutex xfer_lock; /* prevent detach while transferring */
	struct cdev cdev;
	int id;
};

static DEFINE_IDA(i3cdev_ida);
static dev_t i3cdev_number;
#define I3C_MINORS (MINORMASK + 1)

static struct i3cdev_data *get_free_i3cdev(struct i3c_device *i3c)
{
	struct i3cdev_data *i3cdev;
	int id;

	id = ida_simple_get(&i3cdev_ida, 0, I3C_MINORS, GFP_KERNEL);
	if (id < 0) {
		pr_err("i3cdev: no minor number available!\n");
		return ERR_PTR(id);
	}

	i3cdev = kzalloc(sizeof(*i3cdev), GFP_KERNEL);
	if (!i3cdev) {
		ida_simple_remove(&i3cdev_ida, id);
		return ERR_PTR(-ENOMEM);
	}

	i3cdev->i3c = i3c;
	i3cdev->id = id;
	i3cdev_set_drvdata(i3c, i3cdev);

	return i3cdev;
}

static void put_i3cdev(struct i3cdev_data *i3cdev)
{
	i3cdev_set_drvdata(i3cdev->i3c, NULL);
	kfree(i3cdev);
}

static ssize_t
i3cdev_read(struct file *file, char __user *buf, size_t count, loff_t *f_pos)
{
	struct i3cdev_data *i3cdev = file->private_data;
	struct i3c_device *i3c = i3cdev->i3c;
	struct i3c_priv_xfer xfers = {
		.rnw = true,
		.len = count,
	};
	int ret = -EACCES;
	char *tmp;

	mutex_lock(&i3cdev->xfer_lock);
	if (i3c->dev.driver)
		goto err_out;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	xfers.data.in = tmp;

	dev_dbg(&i3c->dev, "Reading %zu bytes.\n", count);

	ret = i3c_device_do_priv_xfers(i3c, &xfers, 1);
	if (!ret)
		ret = copy_to_user(buf, tmp, xfers.len) ? -EFAULT : xfers.len;

	kfree(tmp);

err_out:
	mutex_unlock(&i3cdev->xfer_lock);
	return ret;
}

static ssize_t
i3cdev_write(struct file *file, const char __user *buf, size_t count,
	     loff_t *f_pos)
{
	struct i3cdev_data *i3cdev = file->private_data;
	struct i3c_device *i3c = i3cdev->i3c;
	struct i3c_priv_xfer xfers = {
		.rnw = false,
		.len = count,
	};
	int ret = -EACCES;
	char *tmp;

	mutex_lock(&i3cdev->xfer_lock);
	if (i3c->dev.driver)
		goto err_out;

	tmp = memdup_user(buf, count);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	xfers.data.out = tmp;

	dev_dbg(&i3c->dev, "Writing %zu bytes.\n", count);

	ret = i3c_device_do_priv_xfers(i3c, &xfers, 1);
	kfree(tmp);

err_out:
	mutex_unlock(&i3cdev->xfer_lock);
	return (!ret) ? count : ret;
}

static int
i3cdev_do_priv_xfer(struct i3c_device *dev, struct i3c_ioc_priv_xfer *xfers,
		    unsigned int nxfers)
{
	struct i3c_priv_xfer *k_xfers;
	u8 **data_ptrs;
	int i, j, ret = 0;

	/* Since we have nxfers we may allocate k_xfer + *data_ptrs together */
	k_xfers = kcalloc(nxfers, sizeof(*k_xfers) + sizeof(*data_ptrs),
			  GFP_KERNEL);
	if (!k_xfers)
		return -ENOMEM;

	/* set data_ptrs to be after nxfers * i3c_priv_xfer */
	data_ptrs = (void *)k_xfers + (nxfers * sizeof(*k_xfers));

	for (i = 0; i < nxfers; i++) {
		data_ptrs[i] = memdup_user((const u8 __user *)
					   (uintptr_t)xfers[i].data,
					   xfers[i].len);
		if (IS_ERR(data_ptrs[i])) {
			ret = PTR_ERR(data_ptrs[i]);
			break;
		}

		k_xfers[i].len = xfers[i].len;
		if (xfers[i].rnw) {
			k_xfers[i].rnw = true;
			k_xfers[i].data.in = data_ptrs[i];
		} else {
			k_xfers[i].rnw = false;
			k_xfers[i].data.out = data_ptrs[i];
		}
	}

	if (ret < 0)
		goto err_free_mem;

	ret = i3c_device_do_priv_xfers(dev, k_xfers, nxfers);
	if (ret)
		goto err_free_mem;

	for (i = 0; i < nxfers; i++) {
		if (xfers[i].rnw) {
			if (copy_to_user(u64_to_user_ptr(xfers[i].data),
					 data_ptrs[i], xfers[i].len))
				ret = -EFAULT;
		}
	}

err_free_mem:
	for (j = 0; j < i; j++)
		kfree(data_ptrs[j]);
	kfree(k_xfers);
	return ret;
}

static struct i3c_ioc_priv_xfer *
i3cdev_get_ioc_priv_xfer(unsigned int cmd, struct i3c_ioc_priv_xfer *u_xfers,
			 unsigned int *nxfers)
{
	u32 tmp = _IOC_SIZE(cmd);

	if ((tmp % sizeof(struct i3c_ioc_priv_xfer)) != 0)
		return ERR_PTR(-EINVAL);

	*nxfers = tmp / sizeof(struct i3c_ioc_priv_xfer);
	if (*nxfers == 0)
		return ERR_PTR(-EINVAL);

	return memdup_user(u_xfers, tmp);
}

static int
i3cdev_ioc_priv_xfer(struct i3c_device *i3c, unsigned int cmd,
		     struct i3c_ioc_priv_xfer *u_xfers)
{
	struct i3c_ioc_priv_xfer *k_xfers;
	unsigned int nxfers;
	int ret;

	k_xfers = i3cdev_get_ioc_priv_xfer(cmd, u_xfers, &nxfers);
	if (IS_ERR(k_xfers))
		return PTR_ERR(k_xfers);

	ret = i3cdev_do_priv_xfer(i3c, k_xfers, nxfers);

	kfree(k_xfers);

	return ret;
}

static long
i3cdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct i3cdev_data *i3cdev = file->private_data;
	struct i3c_device *i3c = i3cdev->i3c;
	int ret = -EACCES;

	dev_dbg(&i3c->dev, "ioctl, cmd=0x%02x, arg=0x%02lx\n", cmd, arg);

	if (_IOC_TYPE(cmd) != I3C_DEV_IOC_MAGIC)
		return -ENOTTY;

	/* Use the xfer_lock to prevent device detach during ioctl call */
	mutex_lock(&i3cdev->xfer_lock);
	if (i3c->dev.driver)
		goto err_no_dev;

	/* Check command number and direction */
	if (_IOC_NR(cmd) == _IOC_NR(I3C_IOC_PRIV_XFER(0)) &&
	    _IOC_DIR(cmd) == (_IOC_READ | _IOC_WRITE))
		ret = i3cdev_ioc_priv_xfer(i3c, cmd,
					(struct i3c_ioc_priv_xfer __user *)arg);

err_no_dev:
	mutex_unlock(&i3cdev->xfer_lock);
	return ret;
}

static int i3cdev_open(struct inode *inode, struct file *file)
{
	struct i3cdev_data *i3cdev = container_of(inode->i_cdev,
						  struct i3cdev_data,
						  cdev);
	file->private_data = i3cdev;

	return 0;
}

static int i3cdev_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;

	return 0;
}

static const struct file_operations i3cdev_fops = {
	.owner		= THIS_MODULE,
	.read		= i3cdev_read,
	.write		= i3cdev_write,
	.unlocked_ioctl	= i3cdev_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.open		= i3cdev_open,
	.release	= i3cdev_release,
};

/* ------------------------------------------------------------------------- */

static struct class *i3cdev_class;

static int i3cdev_attach(struct device *dev, void *dummy)
{
	struct i3cdev_data *i3cdev;
	struct i3c_device *i3c;
	int res;

	if (dev->type == &i3c_masterdev_type || dev->driver)
		return 0;

	i3c = dev_to_i3cdev(dev);

	/* Get a device */
	i3cdev = get_free_i3cdev(i3c);
	if (IS_ERR(i3cdev))
		return PTR_ERR(i3cdev);

	mutex_init(&i3cdev->xfer_lock);
	cdev_init(&i3cdev->cdev, &i3cdev_fops);
	i3cdev->cdev.owner = THIS_MODULE;
	res = cdev_add(&i3cdev->cdev,
		       MKDEV(MAJOR(i3cdev_number), i3cdev->id), 1);
	if (res)
		goto error_cdev;

	/* register this i3c device with the driver core */
	i3cdev->dev = device_create(i3cdev_class, &i3c->dev,
				    MKDEV(MAJOR(i3cdev_number), i3cdev->id),
				    NULL, "bus!i3c!%s", dev_name(&i3c->dev));
	if (IS_ERR(i3cdev->dev)) {
		res = PTR_ERR(i3cdev->dev);
		goto error;
	}
	pr_debug("i3cdev: I3C device [%s] registered as minor %d\n",
		 dev_name(&i3c->dev), i3cdev->id);
	return 0;

error:
	cdev_del(&i3cdev->cdev);
error_cdev:
	put_i3cdev(i3cdev);
	return res;
}

static int i3cdev_detach(struct device *dev, void *dummy)
{
	struct i3cdev_data *i3cdev;
	struct i3c_device *i3c;

	if (dev->type == &i3c_masterdev_type)
		return 0;

	i3c = dev_to_i3cdev(dev);

	i3cdev = i3cdev_get_drvdata(i3c);
	if (!i3cdev)
		return 0;

	/* Prevent transfers while cdev removal */
	mutex_lock(&i3cdev->xfer_lock);
	cdev_del(&i3cdev->cdev);
	device_destroy(i3cdev_class, MKDEV(MAJOR(i3cdev_number), i3cdev->id));
	mutex_unlock(&i3cdev->xfer_lock);

	ida_simple_remove(&i3cdev_ida, i3cdev->id);
	put_i3cdev(i3cdev);

	pr_debug("i3cdev: device [%s] unregistered\n", dev_name(&i3c->dev));

	return 0;
}

static int i3cdev_notifier_call(struct notifier_block *nb,
				unsigned long action,
				void *data)
{
	struct device *dev = data;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
	case BUS_NOTIFY_UNBOUND_DRIVER:
		return i3cdev_attach(dev, NULL);
	case BUS_NOTIFY_DEL_DEVICE:
	case BUS_NOTIFY_REMOVED_DEVICE:
	case BUS_NOTIFY_BIND_DRIVER:
		return i3cdev_detach(dev, NULL);
	}

	return 0;
}

static struct notifier_block i3cdev_notifier = {
	.notifier_call = i3cdev_notifier_call,
};

static int __init i3cdev_init(void)
{
	int res;

	/* Dynamically request unused major number */
	res = alloc_chrdev_region(&i3cdev_number, 0, I3C_MINORS, "i3c");
	if (res)
		goto out;

	/* Create a classe to populate sysfs entries*/
	i3cdev_class = class_create("i3cdev");
	if (IS_ERR(i3cdev_class)) {
		res = PTR_ERR(i3cdev_class);
		goto out_unreg_chrdev;
	}

	/* Keep track of busses which have devices to add or remove later */
	res = bus_register_notifier(&i3c_bus_type, &i3cdev_notifier);
	if (res)
		goto out_unreg_class;

	/* Bind to already existing device without driver right away */
	i3c_for_each_dev(NULL, i3cdev_attach);

	return 0;

out_unreg_class:
	class_destroy(i3cdev_class);
out_unreg_chrdev:
	unregister_chrdev_region(i3cdev_number, I3C_MINORS);
out:
	pr_err("%s: Driver Initialisation failed\n", __FILE__);
	return res;
}

static void __exit i3cdev_exit(void)
{
	bus_unregister_notifier(&i3c_bus_type, &i3cdev_notifier);
	i3c_for_each_dev(NULL, i3cdev_detach);
	class_destroy(i3cdev_class);
	unregister_chrdev_region(i3cdev_number, I3C_MINORS);
}

MODULE_AUTHOR("Vitor Soares <soares@synopsys.com>");
MODULE_DESCRIPTION("I3C /dev entries driver");
MODULE_LICENSE("GPL");

module_init(i3cdev_init);
module_exit(i3cdev_exit);

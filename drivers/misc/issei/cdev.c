// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023-2026 Intel Corporation */
#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/issei.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/xarray.h>

#include "issei_dev.h"
#include "host_client.h"
#include "cdev.h"

struct class *issei_class;
static dev_t issei_devt;

#define ISSEI_MAX_DEVS MINORMASK

static DEFINE_XARRAY_ALLOC(issei_minor_xa);

static int issei_open(struct inode *inode, struct file *fp)
{
	struct issei_host_client *cl;
	struct issei_device *idev;

	xa_lock(&issei_minor_xa);
	idev = xa_load(&issei_minor_xa, iminor(inode));
	if (idev)
		get_device(&idev->dev);
	xa_unlock(&issei_minor_xa);
	if (!idev)
		return -ENODEV;

	cl = issei_cl_create(idev, fp);
	if (IS_ERR(cl)) {
		put_device(&idev->dev);
		return PTR_ERR(cl);
	}
	fp->private_data = cl;

	return nonseekable_open(inode, fp);
}

static int issei_release(struct inode *inode, struct file *fp)
{
	struct issei_host_client *cl = fp->private_data;
	struct issei_device *idev = cl->idev;

	issei_cl_remove(cl);
	put_device(&idev->dev);

	return 0;
}

static long issei_ioctl(struct file *file, unsigned int cmd, unsigned long data)
{
	struct issei_host_client *cl = file->private_data;
	struct issei_connect_client_data conn;
	struct issei_device *idev = cl->idev;
	int ret;

	switch (cmd) {
	case IOCTL_ISSEI_CONNECT_CLIENT:
		dev_dbg(&idev->dev, "IOCTL_ISSEI_CONNECT_CLIENT\n");

		if (idev->rst_state != ISSEI_RST_STATE_DONE) {
			dev_dbg(&idev->dev, "Device is in transition\n");
			return -ENODEV;
		}

		if (copy_from_user(&conn, (char __user *)data, sizeof(conn))) {
			dev_dbg(&idev->dev, "failed to copy data from userland\n");
			return -EFAULT;
		}

		ret = issei_cl_connect(cl, (uuid_t *)&conn.in_client_uuid,
				       &conn.out_client_properties.max_msg_length,
				       &conn.out_client_properties.protocol_version,
				       &conn.out_client_properties.flags);
		if (ret)
			return ret;

		if (copy_to_user((char __user *)data, &conn, sizeof(conn))) {
			dev_dbg(&idev->dev, "failed to copy data to userland\n");
			issei_cl_disconnect(cl);
			return -EFAULT;
		}
		return 0;

	case IOCTL_ISSEI_DISCONNECT_CLIENT:
		dev_dbg(&idev->dev, "IOCTL_ISSEI_DISCONNECT_CLIENT\n");

		if (idev->rst_state != ISSEI_RST_STATE_DONE) {
			dev_dbg(&idev->dev, "Device is in transition\n");
			return -ENODEV;
		}

		return issei_cl_disconnect(cl);

	default:
		return -ENOIOCTLCMD;
	}
}

static ssize_t issei_write(struct file *file, const char __user *ubuf,
			   size_t length, loff_t *offset)
{
	struct issei_host_client *cl = file->private_data;
	struct issei_device *idev = cl->idev;
	ssize_t ret;

	if (!length)
		return 0;

	if (idev->rst_state != ISSEI_RST_STATE_DONE) {
		dev_dbg(&idev->dev, "Device is in transition\n");
		return -EBUSY;
	}

	/* sanity check */
	if (length > idev->dma.length.h2f) {
		dev_dbg(&idev->dev, "Write is too big %zu > %zu\n",
			length, idev->dma.length.h2f);
		return -EFBIG;
	}

	u8 *buf __free(kfree) = memdup_user(ubuf, length);
	if (IS_ERR(buf)) {
		dev_dbg(&idev->dev, "failed to copy data from userland\n");
		return PTR_ERR(buf);
	}

	do {
		ret = issei_cl_write(cl, buf, length);
		if (ret < 0 && ret != -EAGAIN)
			return ret;
		/* buf is consumed by issei_cl_write on success */
		if (ret >= 0)
			retain_and_null_ptr(buf);
		if (wait_event_interruptible(cl->write_wait, issei_cl_check_write(cl) != 1)) {
			issei_cl_clean_all_wbuf(cl);
			if (signal_pending(current))
				return -EINTR;
			return -ERESTARTSYS;
		}
	} while (ret == -EAGAIN);

	return ret;
}

static ssize_t issei_read(struct file *file, char __user *ubuf,
			  size_t length, loff_t *offset)
{
	struct issei_host_client *cl = file->private_data;
	struct issei_device *idev = cl->idev;
	u8 *data = NULL;
	ssize_t ret;

	if (!length)
		return 0;

	if (idev->rst_state != ISSEI_RST_STATE_DONE) {
		dev_dbg(&idev->dev, "Device is in transition\n");
		return -EBUSY;
	}

	/* sanity check */
	if (length > idev->dma.length.f2h) {
		dev_dbg(&idev->dev, "Read is too big %zu > %zu\n",
			length, idev->dma.length.f2h);
		return -EFBIG;
	}

	ret = issei_cl_read(cl, &data, length);
	if (ret < 0) {
		if (ret != -ENOENT)
			return ret;

		if (wait_event_interruptible(cl->read_wait, issei_cl_check_read(cl) != 0)) {
			if (signal_pending(current))
				return -EINTR;
			return -ERESTARTSYS;
		}

		ret = issei_cl_read(cl, &data, length);
		if (ret < 0)
			return ret;
	}

	if (copy_to_user(ubuf, data, ret)) {
		dev_dbg(&idev->dev, "failed to copy data to userland\n");
		ret = -EFAULT;
	} else {
		*offset = 0;
	}

	kfree(data);

	return ret;
}

static __poll_t issei_poll(struct file *file, poll_table *wait)
{
	__poll_t req_events = poll_requested_events(wait);
	struct issei_host_client *cl = file->private_data;
	struct issei_device *idev = cl->idev;
	__poll_t mask = 0;
	int ret;

	if (idev->rst_state != ISSEI_RST_STATE_DONE) {
		dev_dbg(&idev->dev, "Device is in transition\n");
		return EPOLLERR;
	}

	if (req_events & (EPOLLIN | EPOLLRDNORM)) {
		poll_wait(file, &cl->read_wait, wait);
		ret = issei_cl_check_read(cl);
		if (ret == 1)
			mask |= EPOLLIN | EPOLLRDNORM;
		else if (ret < 0)
			mask |= EPOLLERR;
	}

	if (req_events & (EPOLLOUT | EPOLLWRNORM)) {
		poll_wait(file, &cl->write_wait, wait);
		ret = issei_cl_check_write(cl);
		if (ret == 0)
			mask |= EPOLLOUT | EPOLLWRNORM;
		else if (ret < 0)
			mask |= EPOLLERR;
	}

	return mask;
}

static ssize_t fw_ver_show(struct device *device,
			   struct device_attribute *attr, char *buf)
{
	struct issei_device *idev = dev_get_drvdata(device);

	return sysfs_emit(buf, "%u.%u.%u.%u\n", idev->fw_version[0], idev->fw_version[1],
			  idev->fw_version[2], idev->fw_version[3]);
}
static DEVICE_ATTR_RO(fw_ver);

static struct attribute *issei_attrs[] = {
	&dev_attr_fw_ver.attr,
	NULL
};
ATTRIBUTE_GROUPS(issei);

static const struct file_operations issei_fops = {
	.owner = THIS_MODULE,
	.open = issei_open,
	.unlocked_ioctl = issei_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.write = issei_write,
	.read = issei_read,
	.release = issei_release,
	.poll = issei_poll,
};

static void issei_device_release(struct device *dev)
{
	kfree(dev_get_drvdata(dev));
}

static void issei_device_init(struct issei_device *idev, struct device *parent,
			      const struct issei_dma_length *dma_length,
			      const struct issei_hw_ops *ops)
{
	idev->parent = parent;
	idev->power_down = false;
	init_waitqueue_head(&idev->wait_has_data);
	idev->has_data = false;
	init_waitqueue_head(&idev->wait_rst_state);
	idev->rst_state = ISSEI_RST_STATE_INIT;

	mutex_init(&idev->client_lock);
	INIT_LIST_HEAD(&idev->host_client_list);
	idev->host_client_last_id = 0;
	idev->host_client_count = 0;
	INIT_LIST_HEAD(&idev->fw_client_list);
	INIT_LIST_HEAD(&idev->write_queue);
	idev->last_write_ts = 0;

	idev->dma.length = *dma_length;

	idev->ops = ops;
}

/**
 * issei_register: register issei character device
 * @hw_size: size of the hardware structure to allocate
 * @parent: parent device
 * @dma_length: structure with DMA sizes
 * @ops: hardware-related operations
 *
 * Return: pointer allocated to issei_device structure, error on failure
 */
struct issei_device *issei_register(size_t hw_size, struct device *parent,
				    const struct issei_dma_length *dma_length,
				    const struct issei_hw_ops *ops)
{
	struct issei_device *idev;
	u32 minor;
	int ret, devno;

	idev = kzalloc(sizeof(*idev) + hw_size, GFP_KERNEL);
	if (!idev)
		return ERR_PTR(-ENOMEM);

	issei_device_init(idev, parent, dma_length, ops);

	ret = xa_alloc(&issei_minor_xa, &minor, idev, XA_LIMIT(0, ISSEI_MAX_DEVS), GFP_KERNEL);
	if (ret < 0) {
		dev_err(&idev->dev, "Failed to allocate minor. ret = %d\n", ret);
		kfree(idev);
		return ERR_PTR(ret);
	}

	idev->minor = minor;
	devno = MKDEV(MAJOR(issei_devt), idev->minor);

	device_initialize(&idev->dev);
	idev->dev.devt = devno;
	idev->dev.class = issei_class;
	idev->dev.parent = parent;
	idev->dev.groups = issei_groups;
	idev->dev.release = issei_device_release;
	dev_set_drvdata(&idev->dev, idev);

	idev->cdev = cdev_alloc();
	if (!idev->cdev) {
		ret = -ENOMEM;
		goto err;
	}
	idev->cdev->ops = &issei_fops;
	if (parent->driver)
		idev->cdev->owner = parent->driver->owner;
	cdev_set_parent(idev->cdev, &idev->dev.kobj);

	ret = cdev_add(idev->cdev, devno, 1);
	if (ret) {
		dev_err(parent, "unable to add device %d:%u ret = %d\n",
			MAJOR(issei_devt), idev->minor, ret);
		goto err_del_cdev;
	}

	ret = dev_set_name(&idev->dev, "issei%u", idev->minor);
	if (ret) {
		dev_err(parent, "unable to set name to device %d:%u ret = %d\n",
			MAJOR(issei_devt), idev->minor, ret);
		goto err_del_cdev;
	}

	ret = device_add(&idev->dev);
	if (ret) {
		dev_err(parent, "unable to add device %d:%u ret = %d\n",
			MAJOR(issei_devt), idev->minor, ret);
		goto err_del_cdev;
	}

	idev->fw_clients = kset_create_and_add("fw_clients", NULL, &idev->dev.kobj);
	if (!idev->fw_clients) {
		ret = -ENOMEM;
		goto err_del_dev;
	}

	return idev;

err_del_dev:
	device_del(&idev->dev);
err_del_cdev:
	cdev_del(idev->cdev);
err:
	put_device(&idev->dev);
	xa_erase(&issei_minor_xa, minor);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(issei_register);

/**
 * issei_deregister: remove issei character device
 * @idev: the device structure
 */
void issei_deregister(struct issei_device *idev)
{
	u32 minor = idev->minor;

	cdev_del(idev->cdev);

	kset_unregister(idev->fw_clients);

	device_del(&idev->dev);

	put_device(&idev->dev);

	xa_erase(&issei_minor_xa, minor);
}
EXPORT_SYMBOL_GPL(issei_deregister);

static int __init issei_cdev_init(void)
{
	int ret;

	issei_class = class_create("issei");
	if (IS_ERR(issei_class)) {
		pr_err("couldn't create class\n");
		return PTR_ERR(issei_class);
	}

	ret = alloc_chrdev_region(&issei_devt, 0, ISSEI_MAX_DEVS, "issei");
	if (ret < 0) {
		pr_err("unable to allocate char dev region\n");
		class_destroy(issei_class);
		return ret;
	}

	return 0;
}

static void __exit issei_cdev_exit(void)
{
	unregister_chrdev_region(issei_devt, ISSEI_MAX_DEVS);
	class_destroy(issei_class);
}

module_init(issei_cdev_init);
module_exit(issei_cdev_exit);

MODULE_DESCRIPTION("Intel(R) Silicon Security Engine Interface");
MODULE_LICENSE("GPL");

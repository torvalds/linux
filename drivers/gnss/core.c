// SPDX-License-Identifier: GPL-2.0
/*
 * GNSS receiver core
 *
 * Copyright (C) 2018 Johan Hovold <johan@kernel.org>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gnss.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define GNSS_FLAG_HAS_WRITE_RAW		BIT(0)

#define GNSS_MINORS	16

static DEFINE_IDA(gnss_minors);
static dev_t gnss_first;

/* FIFO size must be a power of two */
#define GNSS_READ_FIFO_SIZE	4096
#define GNSS_WRITE_BUF_SIZE	1024

#define to_gnss_device(d) container_of((d), struct gnss_device, dev)

static int gnss_open(struct inode *inode, struct file *file)
{
	struct gnss_device *gdev;
	int ret = 0;

	gdev = container_of(inode->i_cdev, struct gnss_device, cdev);

	get_device(&gdev->dev);

	stream_open(inode, file);
	file->private_data = gdev;

	down_write(&gdev->rwsem);
	if (gdev->disconnected) {
		ret = -ENODEV;
		goto unlock;
	}

	if (gdev->count++ == 0) {
		ret = gdev->ops->open(gdev);
		if (ret)
			gdev->count--;
	}
unlock:
	up_write(&gdev->rwsem);

	if (ret)
		put_device(&gdev->dev);

	return ret;
}

static int gnss_release(struct inode *inode, struct file *file)
{
	struct gnss_device *gdev = file->private_data;

	down_write(&gdev->rwsem);
	if (gdev->disconnected)
		goto unlock;

	if (--gdev->count == 0) {
		gdev->ops->close(gdev);
		kfifo_reset(&gdev->read_fifo);
	}
unlock:
	up_write(&gdev->rwsem);

	put_device(&gdev->dev);

	return 0;
}

static ssize_t gnss_read(struct file *file, char __user *buf,
				size_t count, loff_t *pos)
{
	struct gnss_device *gdev = file->private_data;
	unsigned int copied;
	int ret;

	mutex_lock(&gdev->read_mutex);
	while (kfifo_is_empty(&gdev->read_fifo)) {
		mutex_unlock(&gdev->read_mutex);

		if (gdev->disconnected)
			return 0;

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		ret = wait_event_interruptible(gdev->read_queue,
				gdev->disconnected ||
				!kfifo_is_empty(&gdev->read_fifo));
		if (ret)
			return -ERESTARTSYS;

		mutex_lock(&gdev->read_mutex);
	}

	ret = kfifo_to_user(&gdev->read_fifo, buf, count, &copied);
	if (ret == 0)
		ret = copied;

	mutex_unlock(&gdev->read_mutex);

	return ret;
}

static ssize_t gnss_write(struct file *file, const char __user *buf,
				size_t count, loff_t *pos)
{
	struct gnss_device *gdev = file->private_data;
	size_t written = 0;
	int ret;

	if (gdev->disconnected)
		return -EIO;

	if (!count)
		return 0;

	if (!(gdev->flags & GNSS_FLAG_HAS_WRITE_RAW))
		return -EIO;

	/* Ignoring O_NONBLOCK, write_raw() is synchronous. */

	ret = mutex_lock_interruptible(&gdev->write_mutex);
	if (ret)
		return -ERESTARTSYS;

	for (;;) {
		size_t n = count - written;

		if (n > GNSS_WRITE_BUF_SIZE)
			n = GNSS_WRITE_BUF_SIZE;

		if (copy_from_user(gdev->write_buf, buf, n)) {
			ret = -EFAULT;
			goto out_unlock;
		}

		/*
		 * Assumes write_raw can always accept GNSS_WRITE_BUF_SIZE
		 * bytes.
		 *
		 * FIXME: revisit
		 */
		down_read(&gdev->rwsem);
		if (!gdev->disconnected)
			ret = gdev->ops->write_raw(gdev, gdev->write_buf, n);
		else
			ret = -EIO;
		up_read(&gdev->rwsem);

		if (ret < 0)
			break;

		written += ret;
		buf += ret;

		if (written == count)
			break;
	}

	if (written)
		ret = written;
out_unlock:
	mutex_unlock(&gdev->write_mutex);

	return ret;
}

static __poll_t gnss_poll(struct file *file, poll_table *wait)
{
	struct gnss_device *gdev = file->private_data;
	__poll_t mask = 0;

	poll_wait(file, &gdev->read_queue, wait);

	if (!kfifo_is_empty(&gdev->read_fifo))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (gdev->disconnected)
		mask |= EPOLLHUP;

	return mask;
}

static const struct file_operations gnss_fops = {
	.owner		= THIS_MODULE,
	.open		= gnss_open,
	.release	= gnss_release,
	.read		= gnss_read,
	.write		= gnss_write,
	.poll		= gnss_poll,
	.llseek		= no_llseek,
};

static struct class *gnss_class;

static void gnss_device_release(struct device *dev)
{
	struct gnss_device *gdev = to_gnss_device(dev);

	kfree(gdev->write_buf);
	kfifo_free(&gdev->read_fifo);
	ida_free(&gnss_minors, gdev->id);
	kfree(gdev);
}

struct gnss_device *gnss_allocate_device(struct device *parent)
{
	struct gnss_device *gdev;
	struct device *dev;
	int id;
	int ret;

	gdev = kzalloc(sizeof(*gdev), GFP_KERNEL);
	if (!gdev)
		return NULL;

	id = ida_alloc_max(&gnss_minors, GNSS_MINORS - 1, GFP_KERNEL);
	if (id < 0) {
		kfree(gdev);
		return NULL;
	}

	gdev->id = id;

	dev = &gdev->dev;
	device_initialize(dev);
	dev->devt = gnss_first + id;
	dev->class = gnss_class;
	dev->parent = parent;
	dev->release = gnss_device_release;
	dev_set_drvdata(dev, gdev);
	dev_set_name(dev, "gnss%d", id);

	init_rwsem(&gdev->rwsem);
	mutex_init(&gdev->read_mutex);
	mutex_init(&gdev->write_mutex);
	init_waitqueue_head(&gdev->read_queue);

	ret = kfifo_alloc(&gdev->read_fifo, GNSS_READ_FIFO_SIZE, GFP_KERNEL);
	if (ret)
		goto err_put_device;

	gdev->write_buf = kzalloc(GNSS_WRITE_BUF_SIZE, GFP_KERNEL);
	if (!gdev->write_buf)
		goto err_put_device;

	cdev_init(&gdev->cdev, &gnss_fops);
	gdev->cdev.owner = THIS_MODULE;

	return gdev;

err_put_device:
	put_device(dev);

	return NULL;
}
EXPORT_SYMBOL_GPL(gnss_allocate_device);

void gnss_put_device(struct gnss_device *gdev)
{
	put_device(&gdev->dev);
}
EXPORT_SYMBOL_GPL(gnss_put_device);

int gnss_register_device(struct gnss_device *gdev)
{
	int ret;

	/* Set a flag which can be accessed without holding the rwsem. */
	if (gdev->ops->write_raw != NULL)
		gdev->flags |= GNSS_FLAG_HAS_WRITE_RAW;

	ret = cdev_device_add(&gdev->cdev, &gdev->dev);
	if (ret) {
		dev_err(&gdev->dev, "failed to add device: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(gnss_register_device);

void gnss_deregister_device(struct gnss_device *gdev)
{
	down_write(&gdev->rwsem);
	gdev->disconnected = true;
	if (gdev->count) {
		wake_up_interruptible(&gdev->read_queue);
		gdev->ops->close(gdev);
	}
	up_write(&gdev->rwsem);

	cdev_device_del(&gdev->cdev, &gdev->dev);
}
EXPORT_SYMBOL_GPL(gnss_deregister_device);

/*
 * Caller guarantees serialisation.
 *
 * Must not be called for a closed device.
 */
int gnss_insert_raw(struct gnss_device *gdev, const unsigned char *buf,
				size_t count)
{
	int ret;

	ret = kfifo_in(&gdev->read_fifo, buf, count);

	wake_up_interruptible(&gdev->read_queue);

	return ret;
}
EXPORT_SYMBOL_GPL(gnss_insert_raw);

static const char * const gnss_type_names[GNSS_TYPE_COUNT] = {
	[GNSS_TYPE_NMEA]	= "NMEA",
	[GNSS_TYPE_SIRF]	= "SiRF",
	[GNSS_TYPE_UBX]		= "UBX",
	[GNSS_TYPE_MTK]		= "MTK",
};

static const char *gnss_type_name(const struct gnss_device *gdev)
{
	const char *name = NULL;

	if (gdev->type < GNSS_TYPE_COUNT)
		name = gnss_type_names[gdev->type];

	if (!name)
		dev_WARN(&gdev->dev, "type name not defined\n");

	return name;
}

static ssize_t type_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct gnss_device *gdev = to_gnss_device(dev);

	return sprintf(buf, "%s\n", gnss_type_name(gdev));
}
static DEVICE_ATTR_RO(type);

static struct attribute *gnss_attrs[] = {
	&dev_attr_type.attr,
	NULL,
};
ATTRIBUTE_GROUPS(gnss);

static int gnss_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct gnss_device *gdev = to_gnss_device(dev);
	int ret;

	ret = add_uevent_var(env, "GNSS_TYPE=%s", gnss_type_name(gdev));
	if (ret)
		return ret;

	return 0;
}

static int __init gnss_module_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&gnss_first, 0, GNSS_MINORS, "gnss");
	if (ret < 0) {
		pr_err("failed to allocate device numbers: %d\n", ret);
		return ret;
	}

	gnss_class = class_create("gnss");
	if (IS_ERR(gnss_class)) {
		ret = PTR_ERR(gnss_class);
		pr_err("failed to create class: %d\n", ret);
		goto err_unregister_chrdev;
	}

	gnss_class->dev_groups = gnss_groups;
	gnss_class->dev_uevent = gnss_uevent;

	pr_info("GNSS driver registered with major %d\n", MAJOR(gnss_first));

	return 0;

err_unregister_chrdev:
	unregister_chrdev_region(gnss_first, GNSS_MINORS);

	return ret;
}
module_init(gnss_module_init);

static void __exit gnss_module_exit(void)
{
	class_destroy(gnss_class);
	unregister_chrdev_region(gnss_first, GNSS_MINORS);
	ida_destroy(&gnss_minors);
}
module_exit(gnss_module_exit);

MODULE_AUTHOR("Johan Hovold <johan@kernel.org>");
MODULE_DESCRIPTION("GNSS receiver core");
MODULE_LICENSE("GPL v2");

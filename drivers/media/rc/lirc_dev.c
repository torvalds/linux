/*
 * LIRC base driver
 *
 * by Artur Lipowski <alipowski@interia.pl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/ioctl.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/idr.h>

#include <media/rc-core.h>
#include <media/lirc.h>
#include <media/lirc_dev.h>

#define LOGHEAD		"lirc_dev (%s[%d]): "

static dev_t lirc_base_dev;

/* Used to keep track of allocated lirc devices */
#define LIRC_MAX_DEVICES 256
static DEFINE_IDA(lirc_ida);

/* Only used for sysfs but defined to void otherwise */
static struct class *lirc_class;

static void lirc_release_device(struct device *ld)
{
	struct lirc_dev *d = container_of(ld, struct lirc_dev, dev);

	put_device(d->dev.parent);

	if (d->buf_internal) {
		lirc_buffer_free(d->buf);
		kfree(d->buf);
		d->buf = NULL;
	}
	kfree(d);
	module_put(THIS_MODULE);
}

static int lirc_allocate_buffer(struct lirc_dev *d)
{
	int err;

	if (d->buf) {
		d->buf_internal = false;
		return 0;
	}

	d->buf = kmalloc(sizeof(*d->buf), GFP_KERNEL);
	if (!d->buf)
		return -ENOMEM;

	err = lirc_buffer_init(d->buf, d->chunk_size, d->buffer_size);
	if (err) {
		kfree(d->buf);
		d->buf = NULL;
		return err;
	}

	d->buf_internal = true;
	return 0;
}

struct lirc_dev *
lirc_allocate_device(void)
{
	struct lirc_dev *d;

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (d) {
		mutex_init(&d->mutex);
		device_initialize(&d->dev);
		d->dev.class = lirc_class;
		d->dev.release = lirc_release_device;
		__module_get(THIS_MODULE);
	}

	return d;
}
EXPORT_SYMBOL(lirc_allocate_device);

void lirc_free_device(struct lirc_dev *d)
{
	if (!d)
		return;

	put_device(&d->dev);
}
EXPORT_SYMBOL(lirc_free_device);

int lirc_register_device(struct lirc_dev *d)
{
	int minor;
	int err;

	if (!d) {
		pr_err("driver pointer must be not NULL!\n");
		return -EBADRQC;
	}

	if (!d->dev.parent) {
		pr_err("dev parent pointer not filled in!\n");
		return -EINVAL;
	}

	if (!d->fops) {
		pr_err("fops pointer not filled in!\n");
		return -EINVAL;
	}

	if (!d->buf && d->chunk_size < 1) {
		pr_err("chunk_size must be set!\n");
		return -EINVAL;
	}

	if (!d->buf && d->buffer_size < 1) {
		pr_err("buffer_size must be set!\n");
		return -EINVAL;
	}

	if (d->code_length < 1 || d->code_length > (BUFLEN * 8)) {
		dev_err(&d->dev, "code length must be less than %d bits\n",
			BUFLEN * 8);
		return -EBADRQC;
	}

	if (!d->buf && !(d->fops && d->fops->read &&
			 d->fops->poll && d->fops->unlocked_ioctl)) {
		dev_err(&d->dev, "undefined read, poll, ioctl\n");
		return -EBADRQC;
	}

	/* some safety check 8-) */
	d->name[sizeof(d->name) - 1] = '\0';

	if (d->features == 0)
		d->features = LIRC_CAN_REC_LIRCCODE;

	if (LIRC_CAN_REC(d->features)) {
		err = lirc_allocate_buffer(d);
		if (err)
			return err;
	}

	minor = ida_simple_get(&lirc_ida, 0, LIRC_MAX_DEVICES, GFP_KERNEL);
	if (minor < 0)
		return minor;

	d->minor = minor;
	d->dev.devt = MKDEV(MAJOR(lirc_base_dev), d->minor);
	dev_set_name(&d->dev, "lirc%d", d->minor);

	cdev_init(&d->cdev, d->fops);
	d->cdev.owner = d->owner;
	d->attached = true;

	err = cdev_device_add(&d->cdev, &d->dev);
	if (err) {
		ida_simple_remove(&lirc_ida, minor);
		return err;
	}

	get_device(d->dev.parent);

	dev_info(&d->dev, "lirc_dev: driver %s registered at minor = %d\n",
		 d->name, d->minor);

	return 0;
}
EXPORT_SYMBOL(lirc_register_device);

void lirc_unregister_device(struct lirc_dev *d)
{
	if (!d)
		return;

	dev_dbg(&d->dev, "lirc_dev: driver %s unregistered from minor = %d\n",
		d->name, d->minor);

	mutex_lock(&d->mutex);

	d->attached = false;
	if (d->open) {
		dev_dbg(&d->dev, LOGHEAD "releasing opened driver\n",
			d->name, d->minor);
		wake_up_interruptible(&d->buf->wait_poll);
	}

	mutex_unlock(&d->mutex);

	cdev_device_del(&d->cdev, &d->dev);
	ida_simple_remove(&lirc_ida, d->minor);
	put_device(&d->dev);
}
EXPORT_SYMBOL(lirc_unregister_device);

int lirc_dev_fop_open(struct inode *inode, struct file *file)
{
	struct lirc_dev *d = container_of(inode->i_cdev, struct lirc_dev, cdev);
	int retval;

	dev_dbg(&d->dev, LOGHEAD "open called\n", d->name, d->minor);

	retval = mutex_lock_interruptible(&d->mutex);
	if (retval)
		return retval;

	if (!d->attached) {
		retval = -ENODEV;
		goto out;
	}

	if (d->open) {
		retval = -EBUSY;
		goto out;
	}

	if (d->rdev) {
		retval = rc_open(d->rdev);
		if (retval)
			goto out;
	}

	if (d->buf)
		lirc_buffer_clear(d->buf);

	d->open++;

	lirc_init_pdata(inode, file);
	nonseekable_open(inode, file);
	mutex_unlock(&d->mutex);

	return 0;

out:
	mutex_unlock(&d->mutex);
	return retval;
}
EXPORT_SYMBOL(lirc_dev_fop_open);

int lirc_dev_fop_close(struct inode *inode, struct file *file)
{
	struct lirc_dev *d = file->private_data;

	mutex_lock(&d->mutex);

	rc_close(d->rdev);
	d->open--;

	mutex_unlock(&d->mutex);

	return 0;
}
EXPORT_SYMBOL(lirc_dev_fop_close);

__poll_t lirc_dev_fop_poll(struct file *file, poll_table *wait)
{
	struct lirc_dev *d = file->private_data;
	__poll_t ret;

	if (!d->attached)
		return POLLHUP | POLLERR;

	if (d->buf) {
		poll_wait(file, &d->buf->wait_poll, wait);

		if (lirc_buffer_empty(d->buf))
			ret = 0;
		else
			ret = POLLIN | POLLRDNORM;
	} else {
		ret = POLLERR;
	}

	dev_dbg(&d->dev, LOGHEAD "poll result = %d\n", d->name, d->minor, ret);

	return ret;
}
EXPORT_SYMBOL(lirc_dev_fop_poll);

long lirc_dev_fop_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct lirc_dev *d = file->private_data;
	__u32 mode;
	int result;

	dev_dbg(&d->dev, LOGHEAD "ioctl called (0x%x)\n",
		d->name, d->minor, cmd);

	result = mutex_lock_interruptible(&d->mutex);
	if (result)
		return result;

	if (!d->attached) {
		result = -ENODEV;
		goto out;
	}

	switch (cmd) {
	case LIRC_GET_FEATURES:
		result = put_user(d->features, (__u32 __user *)arg);
		break;
	case LIRC_GET_REC_MODE:
		if (!LIRC_CAN_REC(d->features)) {
			result = -ENOTTY;
			break;
		}

		result = put_user(LIRC_REC2MODE
				  (d->features & LIRC_CAN_REC_MASK),
				  (__u32 __user *)arg);
		break;
	case LIRC_SET_REC_MODE:
		if (!LIRC_CAN_REC(d->features)) {
			result = -ENOTTY;
			break;
		}

		result = get_user(mode, (__u32 __user *)arg);
		if (!result && !(LIRC_MODE2REC(mode) & d->features))
			result = -EINVAL;
		/*
		 * FIXME: We should actually set the mode somehow but
		 * for now, lirc_serial doesn't support mode changing either
		 */
		break;
	case LIRC_GET_LENGTH:
		result = put_user(d->code_length, (__u32 __user *)arg);
		break;
	default:
		result = -ENOTTY;
	}

out:
	mutex_unlock(&d->mutex);
	return result;
}
EXPORT_SYMBOL(lirc_dev_fop_ioctl);

ssize_t lirc_dev_fop_read(struct file *file,
			  char __user *buffer,
			  size_t length,
			  loff_t *ppos)
{
	struct lirc_dev *d = file->private_data;
	unsigned char *buf;
	int ret, written = 0;
	DECLARE_WAITQUEUE(wait, current);

	buf = kzalloc(d->buf->chunk_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	dev_dbg(&d->dev, LOGHEAD "read called\n", d->name, d->minor);

	ret = mutex_lock_interruptible(&d->mutex);
	if (ret) {
		kfree(buf);
		return ret;
	}

	if (!d->attached) {
		ret = -ENODEV;
		goto out_locked;
	}

	if (!LIRC_CAN_REC(d->features)) {
		ret = -EINVAL;
		goto out_locked;
	}

	if (length % d->buf->chunk_size) {
		ret = -EINVAL;
		goto out_locked;
	}

	/*
	 * we add ourselves to the task queue before buffer check
	 * to avoid losing scan code (in case when queue is awaken somewhere
	 * between while condition checking and scheduling)
	 */
	add_wait_queue(&d->buf->wait_poll, &wait);

	/*
	 * while we didn't provide 'length' bytes, device is opened in blocking
	 * mode and 'copy_to_user' is happy, wait for data.
	 */
	while (written < length && ret == 0) {
		if (lirc_buffer_empty(d->buf)) {
			/* According to the read(2) man page, 'written' can be
			 * returned as less than 'length', instead of blocking
			 * again, returning -EWOULDBLOCK, or returning
			 * -ERESTARTSYS
			 */
			if (written)
				break;
			if (file->f_flags & O_NONBLOCK) {
				ret = -EWOULDBLOCK;
				break;
			}
			if (signal_pending(current)) {
				ret = -ERESTARTSYS;
				break;
			}

			mutex_unlock(&d->mutex);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			set_current_state(TASK_RUNNING);

			ret = mutex_lock_interruptible(&d->mutex);
			if (ret) {
				remove_wait_queue(&d->buf->wait_poll, &wait);
				goto out_unlocked;
			}

			if (!d->attached) {
				ret = -ENODEV;
				goto out_locked;
			}
		} else {
			lirc_buffer_read(d->buf, buf);
			ret = copy_to_user((void __user *)buffer+written, buf,
					   d->buf->chunk_size);
			if (!ret)
				written += d->buf->chunk_size;
			else
				ret = -EFAULT;
		}
	}

	remove_wait_queue(&d->buf->wait_poll, &wait);

out_locked:
	mutex_unlock(&d->mutex);

out_unlocked:
	kfree(buf);

	return ret ? ret : written;
}
EXPORT_SYMBOL(lirc_dev_fop_read);

void lirc_init_pdata(struct inode *inode, struct file *file)
{
	struct lirc_dev *d = container_of(inode->i_cdev, struct lirc_dev, cdev);

	file->private_data = d;
}
EXPORT_SYMBOL(lirc_init_pdata);

void *lirc_get_pdata(struct file *file)
{
	struct lirc_dev *d = file->private_data;

	return d->data;
}
EXPORT_SYMBOL(lirc_get_pdata);


static int __init lirc_dev_init(void)
{
	int retval;

	lirc_class = class_create(THIS_MODULE, "lirc");
	if (IS_ERR(lirc_class)) {
		pr_err("class_create failed\n");
		return PTR_ERR(lirc_class);
	}

	retval = alloc_chrdev_region(&lirc_base_dev, 0, LIRC_MAX_DEVICES,
				     "BaseRemoteCtl");
	if (retval) {
		class_destroy(lirc_class);
		pr_err("alloc_chrdev_region failed\n");
		return retval;
	}

	pr_info("IR Remote Control driver registered, major %d\n",
						MAJOR(lirc_base_dev));

	return 0;
}

static void __exit lirc_dev_exit(void)
{
	class_destroy(lirc_class);
	unregister_chrdev_region(lirc_base_dev, LIRC_MAX_DEVICES);
	pr_info("module unloaded\n");
}

module_init(lirc_dev_init);
module_exit(lirc_dev_exit);

MODULE_DESCRIPTION("LIRC base driver module");
MODULE_AUTHOR("Artur Lipowski");
MODULE_LICENSE("GPL");

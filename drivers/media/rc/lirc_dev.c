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

#include <media/rc-core.h>
#include <media/lirc.h>
#include <media/lirc_dev.h>

#define LOGHEAD		"lirc_dev (%s[%d]): "

static dev_t lirc_base_dev;

struct irctl {
	struct lirc_driver d;
	bool attached;
	int open;

	struct mutex mutex;	/* protect from simultaneous accesses */
	struct lirc_buffer *buf;
	bool buf_internal;

	struct device dev;
	struct cdev cdev;
};

/* This mutex protects the irctls array */
static DEFINE_MUTEX(lirc_dev_lock);

static struct irctl *irctls[MAX_IRCTL_DEVICES];

/* Only used for sysfs but defined to void otherwise */
static struct class *lirc_class;

static void lirc_free_buffer(struct irctl *ir)
{
	put_device(ir->dev.parent);

	if (ir->buf_internal) {
		lirc_buffer_free(ir->buf);
		kfree(ir->buf);
		ir->buf = NULL;
	}
}

static void lirc_release(struct device *ld)
{
	struct irctl *ir = container_of(ld, struct irctl, dev);

	mutex_lock(&lirc_dev_lock);
	irctls[ir->d.minor] = NULL;
	mutex_unlock(&lirc_dev_lock);
	lirc_free_buffer(ir);
	kfree(ir);
}

static int lirc_allocate_buffer(struct irctl *ir)
{
	int err = 0;
	struct lirc_driver *d = &ir->d;

	if (d->rbuf) {
		ir->buf = d->rbuf;
		ir->buf_internal = false;
	} else {
		ir->buf = kmalloc(sizeof(struct lirc_buffer), GFP_KERNEL);
		if (!ir->buf) {
			err = -ENOMEM;
			goto out;
		}

		err = lirc_buffer_init(ir->buf, d->chunk_size, d->buffer_size);
		if (err) {
			kfree(ir->buf);
			ir->buf = NULL;
			goto out;
		}

		ir->buf_internal = true;
		d->rbuf = ir->buf;
	}

out:
	return err;
}

int lirc_register_driver(struct lirc_driver *d)
{
	struct irctl *ir;
	unsigned int minor;
	int err;

	if (!d) {
		pr_err("driver pointer must be not NULL!\n");
		return -EBADRQC;
	}

	if (!d->dev) {
		pr_err("dev pointer not filled in!\n");
		return -EINVAL;
	}

	if (!d->fops) {
		pr_err("fops pointer not filled in!\n");
		return -EINVAL;
	}

	if (!d->rbuf && d->chunk_size < 1) {
		pr_err("chunk_size must be set!\n");
		return -EINVAL;
	}

	if (!d->rbuf && d->buffer_size < 1) {
		pr_err("buffer_size must be set!\n");
		return -EINVAL;
	}

	if (d->code_length < 1 || d->code_length > (BUFLEN * 8)) {
		dev_err(d->dev, "code length must be less than %d bits\n",
								BUFLEN * 8);
		return -EBADRQC;
	}

	if (!d->rbuf && !(d->fops && d->fops->read &&
			  d->fops->poll && d->fops->unlocked_ioctl)) {
		dev_err(d->dev, "undefined read, poll, ioctl\n");
		return -EBADRQC;
	}

	/* some safety check 8-) */
	d->name[sizeof(d->name) - 1] = '\0';

	if (d->features == 0)
		d->features = LIRC_CAN_REC_LIRCCODE;

	ir = kzalloc(sizeof(*ir), GFP_KERNEL);
	if (!ir)
		return -ENOMEM;

	mutex_init(&ir->mutex);
	ir->d = *d;

	if (LIRC_CAN_REC(d->features)) {
		err = lirc_allocate_buffer(ir);
		if (err) {
			kfree(ir);
			return err;
		}
		d->rbuf = ir->buf;
	}

	mutex_lock(&lirc_dev_lock);

	/* find first free slot for driver */
	for (minor = 0; minor < MAX_IRCTL_DEVICES; minor++)
		if (!irctls[minor])
			break;

	if (minor == MAX_IRCTL_DEVICES) {
		dev_err(d->dev, "no free slots for drivers!\n");
		mutex_unlock(&lirc_dev_lock);
		lirc_free_buffer(ir);
		kfree(ir);
		return -ENOMEM;
	}

	irctls[minor] = ir;
	d->irctl = ir;
	d->minor = minor;
	ir->d.minor = minor;

	mutex_unlock(&lirc_dev_lock);

	device_initialize(&ir->dev);
	ir->dev.devt = MKDEV(MAJOR(lirc_base_dev), ir->d.minor);
	ir->dev.class = lirc_class;
	ir->dev.parent = d->dev;
	ir->dev.release = lirc_release;
	dev_set_name(&ir->dev, "lirc%d", ir->d.minor);

	cdev_init(&ir->cdev, d->fops);
	ir->cdev.owner = ir->d.owner;
	ir->attached = true;

	err = cdev_device_add(&ir->cdev, &ir->dev);
	if (err) {
		put_device(&ir->dev);
		return err;
	}

	get_device(ir->dev.parent);

	dev_info(ir->d.dev, "lirc_dev: driver %s registered at minor = %d\n",
		 ir->d.name, ir->d.minor);

	return 0;
}
EXPORT_SYMBOL(lirc_register_driver);

void lirc_unregister_driver(struct lirc_driver *d)
{
	struct irctl *ir;

	if (!d || !d->irctl)
		return;

	ir = d->irctl;

	dev_dbg(ir->d.dev, "lirc_dev: driver %s unregistered from minor = %d\n",
		d->name, d->minor);

	cdev_device_del(&ir->cdev, &ir->dev);

	mutex_lock(&ir->mutex);

	ir->attached = false;
	if (ir->open) {
		dev_dbg(ir->d.dev, LOGHEAD "releasing opened driver\n",
			d->name, d->minor);
		wake_up_interruptible(&ir->buf->wait_poll);
	}

	mutex_unlock(&ir->mutex);

	put_device(&ir->dev);
}
EXPORT_SYMBOL(lirc_unregister_driver);

int lirc_dev_fop_open(struct inode *inode, struct file *file)
{
	struct irctl *ir = container_of(inode->i_cdev, struct irctl, cdev);
	int retval;

	dev_dbg(ir->d.dev, LOGHEAD "open called\n", ir->d.name, ir->d.minor);

	retval = mutex_lock_interruptible(&ir->mutex);
	if (retval)
		return retval;

	if (!ir->attached) {
		retval = -ENODEV;
		goto out;
	}

	if (ir->open) {
		retval = -EBUSY;
		goto out;
	}

	if (ir->d.rdev) {
		retval = rc_open(ir->d.rdev);
		if (retval)
			goto out;
	}

	if (ir->buf)
		lirc_buffer_clear(ir->buf);

	ir->open++;

	lirc_init_pdata(inode, file);
	nonseekable_open(inode, file);
	mutex_unlock(&ir->mutex);

	return 0;

out:
	mutex_unlock(&ir->mutex);
	return retval;
}
EXPORT_SYMBOL(lirc_dev_fop_open);

int lirc_dev_fop_close(struct inode *inode, struct file *file)
{
	struct irctl *ir = file->private_data;

	mutex_lock(&ir->mutex);

	rc_close(ir->d.rdev);
	ir->open--;

	mutex_unlock(&ir->mutex);

	return 0;
}
EXPORT_SYMBOL(lirc_dev_fop_close);

unsigned int lirc_dev_fop_poll(struct file *file, poll_table *wait)
{
	struct irctl *ir = file->private_data;
	unsigned int ret;

	if (!ir->attached)
		return POLLHUP | POLLERR;

	if (ir->buf) {
		poll_wait(file, &ir->buf->wait_poll, wait);

		if (lirc_buffer_empty(ir->buf))
			ret = 0;
		else
			ret = POLLIN | POLLRDNORM;
	} else
		ret = POLLERR;

	dev_dbg(ir->d.dev, LOGHEAD "poll result = %d\n",
		ir->d.name, ir->d.minor, ret);

	return ret;
}
EXPORT_SYMBOL(lirc_dev_fop_poll);

long lirc_dev_fop_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct irctl *ir = file->private_data;
	__u32 mode;
	int result;

	dev_dbg(ir->d.dev, LOGHEAD "ioctl called (0x%x)\n",
		ir->d.name, ir->d.minor, cmd);

	result = mutex_lock_interruptible(&ir->mutex);
	if (result)
		return result;

	if (!ir->attached) {
		result = -ENODEV;
		goto out;
	}

	switch (cmd) {
	case LIRC_GET_FEATURES:
		result = put_user(ir->d.features, (__u32 __user *)arg);
		break;
	case LIRC_GET_REC_MODE:
		if (!LIRC_CAN_REC(ir->d.features)) {
			result = -ENOTTY;
			break;
		}

		result = put_user(LIRC_REC2MODE
				  (ir->d.features & LIRC_CAN_REC_MASK),
				  (__u32 __user *)arg);
		break;
	case LIRC_SET_REC_MODE:
		if (!LIRC_CAN_REC(ir->d.features)) {
			result = -ENOTTY;
			break;
		}

		result = get_user(mode, (__u32 __user *)arg);
		if (!result && !(LIRC_MODE2REC(mode) & ir->d.features))
			result = -EINVAL;
		/*
		 * FIXME: We should actually set the mode somehow but
		 * for now, lirc_serial doesn't support mode changing either
		 */
		break;
	case LIRC_GET_LENGTH:
		result = put_user(ir->d.code_length, (__u32 __user *)arg);
		break;
	case LIRC_GET_MIN_TIMEOUT:
		if (!(ir->d.features & LIRC_CAN_SET_REC_TIMEOUT) ||
		    ir->d.min_timeout == 0) {
			result = -ENOTTY;
			break;
		}

		result = put_user(ir->d.min_timeout, (__u32 __user *)arg);
		break;
	case LIRC_GET_MAX_TIMEOUT:
		if (!(ir->d.features & LIRC_CAN_SET_REC_TIMEOUT) ||
		    ir->d.max_timeout == 0) {
			result = -ENOTTY;
			break;
		}

		result = put_user(ir->d.max_timeout, (__u32 __user *)arg);
		break;
	default:
		result = -ENOTTY;
	}

out:
	mutex_unlock(&ir->mutex);
	return result;
}
EXPORT_SYMBOL(lirc_dev_fop_ioctl);

ssize_t lirc_dev_fop_read(struct file *file,
			  char __user *buffer,
			  size_t length,
			  loff_t *ppos)
{
	struct irctl *ir = file->private_data;
	unsigned char *buf;
	int ret, written = 0;
	DECLARE_WAITQUEUE(wait, current);

	dev_dbg(ir->d.dev, LOGHEAD "read called\n", ir->d.name, ir->d.minor);

	buf = kzalloc(ir->buf->chunk_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = mutex_lock_interruptible(&ir->mutex);
	if (ret) {
		kfree(buf);
		return ret;
	}

	if (!ir->attached) {
		ret = -ENODEV;
		goto out_locked;
	}

	if (!LIRC_CAN_REC(ir->d.features)) {
		ret = -EINVAL;
		goto out_locked;
	}

	if (length % ir->buf->chunk_size) {
		ret = -EINVAL;
		goto out_locked;
	}

	/*
	 * we add ourselves to the task queue before buffer check
	 * to avoid losing scan code (in case when queue is awaken somewhere
	 * between while condition checking and scheduling)
	 */
	add_wait_queue(&ir->buf->wait_poll, &wait);

	/*
	 * while we didn't provide 'length' bytes, device is opened in blocking
	 * mode and 'copy_to_user' is happy, wait for data.
	 */
	while (written < length && ret == 0) {
		if (lirc_buffer_empty(ir->buf)) {
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

			mutex_unlock(&ir->mutex);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			set_current_state(TASK_RUNNING);

			ret = mutex_lock_interruptible(&ir->mutex);
			if (ret) {
				remove_wait_queue(&ir->buf->wait_poll, &wait);
				goto out_unlocked;
			}

			if (!ir->attached) {
				ret = -ENODEV;
				goto out_locked;
			}
		} else {
			lirc_buffer_read(ir->buf, buf);
			ret = copy_to_user((void __user *)buffer+written, buf,
					   ir->buf->chunk_size);
			if (!ret)
				written += ir->buf->chunk_size;
			else
				ret = -EFAULT;
		}
	}

	remove_wait_queue(&ir->buf->wait_poll, &wait);

out_locked:
	mutex_unlock(&ir->mutex);

out_unlocked:
	kfree(buf);

	return ret ? ret : written;
}
EXPORT_SYMBOL(lirc_dev_fop_read);

void lirc_init_pdata(struct inode *inode, struct file *file)
{
	struct irctl *ir = container_of(inode->i_cdev, struct irctl, cdev);

	file->private_data = ir;
}
EXPORT_SYMBOL(lirc_init_pdata);

void *lirc_get_pdata(struct file *file)
{
	struct irctl *ir = file->private_data;

	return ir->d.data;
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

	retval = alloc_chrdev_region(&lirc_base_dev, 0, MAX_IRCTL_DEVICES,
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
	unregister_chrdev_region(lirc_base_dev, MAX_IRCTL_DEVICES);
	pr_info("module unloaded\n");
}

module_init(lirc_dev_init);
module_exit(lirc_dev_exit);

MODULE_DESCRIPTION("LIRC base driver module");
MODULE_AUTHOR("Artur Lipowski");
MODULE_LICENSE("GPL");

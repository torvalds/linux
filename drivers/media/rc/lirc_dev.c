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
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/unistd.h>
#include <linux/kthread.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/cdev.h>

#include <media/rc-core.h>
#include <media/lirc.h>
#include <media/lirc_dev.h>

static bool debug;

#define IRCTL_DEV_NAME	"BaseRemoteCtl"
#define NOPLUG		-1
#define LOGHEAD		"lirc_dev (%s[%d]): "

static dev_t lirc_base_dev;

struct irctl {
	struct lirc_driver d;
	int attached;
	int open;

	struct mutex irctl_lock;
	struct lirc_buffer *buf;
	unsigned int chunk_size;

	struct cdev *cdev;

	struct task_struct *task;
	long jiffies_to_wait;
};

static DEFINE_MUTEX(lirc_dev_lock);

static struct irctl *irctls[MAX_IRCTL_DEVICES];

/* Only used for sysfs but defined to void otherwise */
static struct class *lirc_class;

/*  helper function
 *  initializes the irctl structure
 */
static void lirc_irctl_init(struct irctl *ir)
{
	mutex_init(&ir->irctl_lock);
	ir->d.minor = NOPLUG;
}

static void lirc_irctl_cleanup(struct irctl *ir)
{
	device_destroy(lirc_class, MKDEV(MAJOR(lirc_base_dev), ir->d.minor));

	if (ir->buf != ir->d.rbuf) {
		lirc_buffer_free(ir->buf);
		kfree(ir->buf);
	}
	ir->buf = NULL;
}

/*  helper function
 *  reads key codes from driver and puts them into buffer
 *  returns 0 on success
 */
static int lirc_add_to_buf(struct irctl *ir)
{
	int res;
	int got_data = -1;

	if (!ir->d.add_to_buf)
		return 0;

	/*
	 * service the device as long as it is returning
	 * data and we have space
	 */
	do {
		got_data++;
		res = ir->d.add_to_buf(ir->d.data, ir->buf);
	} while (!res);

	if (res == -ENODEV)
		kthread_stop(ir->task);

	return got_data ? 0 : res;
}

/* main function of the polling thread
 */
static int lirc_thread(void *irctl)
{
	struct irctl *ir = irctl;

	do {
		if (ir->open) {
			if (ir->jiffies_to_wait) {
				set_current_state(TASK_INTERRUPTIBLE);
				schedule_timeout(ir->jiffies_to_wait);
			}
			if (kthread_should_stop())
				break;
			if (!lirc_add_to_buf(ir))
				wake_up_interruptible(&ir->buf->wait_poll);
		} else {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
		}
	} while (!kthread_should_stop());

	return 0;
}


static const struct file_operations lirc_dev_fops = {
	.owner		= THIS_MODULE,
	.read		= lirc_dev_fop_read,
	.write		= lirc_dev_fop_write,
	.poll		= lirc_dev_fop_poll,
	.unlocked_ioctl	= lirc_dev_fop_ioctl,
	.open		= lirc_dev_fop_open,
	.release	= lirc_dev_fop_close,
	.llseek		= noop_llseek,
};

static int lirc_cdev_add(struct irctl *ir)
{
	struct lirc_driver *d = &ir->d;
	struct cdev *cdev;
	int retval;

	cdev = cdev_alloc();
	if (!cdev)
		return -ENOMEM;

	if (d->fops) {
		cdev->ops = d->fops;
		cdev->owner = d->owner;
	} else {
		cdev->ops = &lirc_dev_fops;
		cdev->owner = THIS_MODULE;
	}
	retval = kobject_set_name(&cdev->kobj, "lirc%d", d->minor);
	if (retval)
		goto err_out;

	retval = cdev_add(cdev, MKDEV(MAJOR(lirc_base_dev), d->minor), 1);
	if (retval)
		goto err_out;

	ir->cdev = cdev;

	return 0;

err_out:
	cdev_del(cdev);
	return retval;
}

static int lirc_allocate_buffer(struct irctl *ir)
{
	int err = 0;
	int bytes_in_key;
	unsigned int chunk_size;
	unsigned int buffer_size;
	struct lirc_driver *d = &ir->d;

	mutex_lock(&lirc_dev_lock);

	bytes_in_key = BITS_TO_LONGS(d->code_length) +
						(d->code_length % 8 ? 1 : 0);
	buffer_size = d->buffer_size ? d->buffer_size : BUFLEN / bytes_in_key;
	chunk_size  = d->chunk_size  ? d->chunk_size  : bytes_in_key;

	if (d->rbuf) {
		ir->buf = d->rbuf;
	} else {
		ir->buf = kmalloc(sizeof(struct lirc_buffer), GFP_KERNEL);
		if (!ir->buf) {
			err = -ENOMEM;
			goto out;
		}

		err = lirc_buffer_init(ir->buf, chunk_size, buffer_size);
		if (err) {
			kfree(ir->buf);
			goto out;
		}
	}
	ir->chunk_size = ir->buf->chunk_size;

out:
	mutex_unlock(&lirc_dev_lock);

	return err;
}

static int lirc_allocate_driver(struct lirc_driver *d)
{
	struct irctl *ir;
	int minor;
	int err;

	if (!d) {
		pr_err("driver pointer must be not NULL!\n");
		return -EBADRQC;
	}

	if (!d->dev) {
		pr_err("dev pointer not filled in!\n");
		return -EINVAL;
	}

	if (d->minor >= MAX_IRCTL_DEVICES) {
		dev_err(d->dev, "minor must be between 0 and %d!\n",
						MAX_IRCTL_DEVICES - 1);
		return -EBADRQC;
	}

	if (d->code_length < 1 || d->code_length > (BUFLEN * 8)) {
		dev_err(d->dev, "code length must be less than %d bits\n",
								BUFLEN * 8);
		return -EBADRQC;
	}

	if (d->sample_rate) {
		if (2 > d->sample_rate || HZ < d->sample_rate) {
			dev_err(d->dev, "invalid %d sample rate\n",
							d->sample_rate);
			return -EBADRQC;
		}
		if (!d->add_to_buf) {
			dev_err(d->dev, "add_to_buf not set\n");
			return -EBADRQC;
		}
	} else if (!d->rbuf && !(d->fops && d->fops->read &&
				d->fops->poll && d->fops->unlocked_ioctl)) {
		dev_err(d->dev, "undefined read, poll, ioctl\n");
		return -EBADRQC;
	}

	mutex_lock(&lirc_dev_lock);

	minor = d->minor;

	if (minor < 0) {
		/* find first free slot for driver */
		for (minor = 0; minor < MAX_IRCTL_DEVICES; minor++)
			if (!irctls[minor])
				break;
		if (minor == MAX_IRCTL_DEVICES) {
			dev_err(d->dev, "no free slots for drivers!\n");
			err = -ENOMEM;
			goto out_lock;
		}
	} else if (irctls[minor]) {
		dev_err(d->dev, "minor (%d) just registered!\n", minor);
		err = -EBUSY;
		goto out_lock;
	}

	ir = kzalloc(sizeof(struct irctl), GFP_KERNEL);
	if (!ir) {
		err = -ENOMEM;
		goto out_lock;
	}
	lirc_irctl_init(ir);
	irctls[minor] = ir;
	d->minor = minor;

	/* some safety check 8-) */
	d->name[sizeof(d->name)-1] = '\0';

	if (d->features == 0)
		d->features = LIRC_CAN_REC_LIRCCODE;

	ir->d = *d;

	device_create(lirc_class, ir->d.dev,
		      MKDEV(MAJOR(lirc_base_dev), ir->d.minor), NULL,
		      "lirc%u", ir->d.minor);

	if (d->sample_rate) {
		ir->jiffies_to_wait = HZ / d->sample_rate;

		/* try to fire up polling thread */
		ir->task = kthread_run(lirc_thread, (void *)ir, "lirc_dev");
		if (IS_ERR(ir->task)) {
			dev_err(d->dev, "cannot run thread for minor = %d\n",
								d->minor);
			err = -ECHILD;
			goto out_sysfs;
		}
	} else {
		/* it means - wait for external event in task queue */
		ir->jiffies_to_wait = 0;
	}

	err = lirc_cdev_add(ir);
	if (err)
		goto out_sysfs;

	ir->attached = 1;
	mutex_unlock(&lirc_dev_lock);

	dev_info(ir->d.dev, "lirc_dev: driver %s registered at minor = %d\n",
		 ir->d.name, ir->d.minor);
	return minor;

out_sysfs:
	device_destroy(lirc_class, MKDEV(MAJOR(lirc_base_dev), ir->d.minor));
out_lock:
	mutex_unlock(&lirc_dev_lock);

	return err;
}

int lirc_register_driver(struct lirc_driver *d)
{
	int minor, err = 0;

	minor = lirc_allocate_driver(d);
	if (minor < 0)
		return minor;

	if (LIRC_CAN_REC(d->features)) {
		err = lirc_allocate_buffer(irctls[minor]);
		if (err)
			lirc_unregister_driver(minor);
	}

	return err ? err : minor;
}
EXPORT_SYMBOL(lirc_register_driver);

int lirc_unregister_driver(int minor)
{
	struct irctl *ir;
	struct cdev *cdev;

	if (minor < 0 || minor >= MAX_IRCTL_DEVICES) {
		pr_err("minor (%d) must be between 0 and %d!\n",
					minor, MAX_IRCTL_DEVICES - 1);
		return -EBADRQC;
	}

	ir = irctls[minor];
	if (!ir) {
		pr_err("failed to get irctl\n");
		return -ENOENT;
	}

	cdev = ir->cdev;

	mutex_lock(&lirc_dev_lock);

	if (ir->d.minor != minor) {
		dev_err(ir->d.dev, "lirc_dev: minor %d device not registered\n",
									minor);
		mutex_unlock(&lirc_dev_lock);
		return -ENOENT;
	}

	/* end up polling thread */
	if (ir->task)
		kthread_stop(ir->task);

	dev_dbg(ir->d.dev, "lirc_dev: driver %s unregistered from minor = %d\n",
		ir->d.name, ir->d.minor);

	ir->attached = 0;
	if (ir->open) {
		dev_dbg(ir->d.dev, LOGHEAD "releasing opened driver\n",
			ir->d.name, ir->d.minor);
		wake_up_interruptible(&ir->buf->wait_poll);
		mutex_lock(&ir->irctl_lock);

		if (ir->d.set_use_dec)
			ir->d.set_use_dec(ir->d.data);

		module_put(cdev->owner);
		mutex_unlock(&ir->irctl_lock);
	} else {
		lirc_irctl_cleanup(ir);
		cdev_del(cdev);
		kfree(ir);
		irctls[minor] = NULL;
	}

	mutex_unlock(&lirc_dev_lock);

	return 0;
}
EXPORT_SYMBOL(lirc_unregister_driver);

int lirc_dev_fop_open(struct inode *inode, struct file *file)
{
	struct irctl *ir;
	struct cdev *cdev;
	int retval = 0;

	if (iminor(inode) >= MAX_IRCTL_DEVICES) {
		pr_err("open result for %d is -ENODEV\n", iminor(inode));
		return -ENODEV;
	}

	if (mutex_lock_interruptible(&lirc_dev_lock))
		return -ERESTARTSYS;

	ir = irctls[iminor(inode)];
	mutex_unlock(&lirc_dev_lock);

	if (!ir) {
		retval = -ENODEV;
		goto error;
	}

	dev_dbg(ir->d.dev, LOGHEAD "open called\n", ir->d.name, ir->d.minor);

	if (ir->d.minor == NOPLUG) {
		retval = -ENODEV;
		goto error;
	}

	if (ir->open) {
		retval = -EBUSY;
		goto error;
	}

	if (ir->d.rdev) {
		retval = rc_open(ir->d.rdev);
		if (retval)
			goto error;
	}

	cdev = ir->cdev;
	if (try_module_get(cdev->owner)) {
		ir->open++;
		if (ir->d.set_use_inc)
			retval = ir->d.set_use_inc(ir->d.data);

		if (retval) {
			module_put(cdev->owner);
			ir->open--;
		} else if (ir->buf) {
			lirc_buffer_clear(ir->buf);
		}
		if (ir->task)
			wake_up_process(ir->task);
	}

error:
	nonseekable_open(inode, file);

	return retval;
}
EXPORT_SYMBOL(lirc_dev_fop_open);

int lirc_dev_fop_close(struct inode *inode, struct file *file)
{
	struct irctl *ir = irctls[iminor(inode)];
	struct cdev *cdev;
	int ret;

	if (!ir) {
		pr_err("called with invalid irctl\n");
		return -EINVAL;
	}

	cdev = ir->cdev;

	ret = mutex_lock_killable(&lirc_dev_lock);
	WARN_ON(ret);

	rc_close(ir->d.rdev);

	ir->open--;
	if (ir->attached) {
		if (ir->d.set_use_dec)
			ir->d.set_use_dec(ir->d.data);
		module_put(cdev->owner);
	} else {
		lirc_irctl_cleanup(ir);
		cdev_del(cdev);
		irctls[ir->d.minor] = NULL;
		kfree(ir);
	}

	if (!ret)
		mutex_unlock(&lirc_dev_lock);

	return 0;
}
EXPORT_SYMBOL(lirc_dev_fop_close);

unsigned int lirc_dev_fop_poll(struct file *file, poll_table *wait)
{
	struct irctl *ir = irctls[iminor(file_inode(file))];
	unsigned int ret;

	if (!ir) {
		pr_err("called with invalid irctl\n");
		return POLLERR;
	}

	if (!ir->attached)
		return POLLERR;

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
	__u32 mode;
	int result = 0;
	struct irctl *ir = irctls[iminor(file_inode(file))];

	if (!ir) {
		pr_err("no irctl found!\n");
		return -ENODEV;
	}

	dev_dbg(ir->d.dev, LOGHEAD "ioctl called (0x%x)\n",
		ir->d.name, ir->d.minor, cmd);

	if (ir->d.minor == NOPLUG || !ir->attached) {
		dev_err(ir->d.dev, LOGHEAD "ioctl result = -ENODEV\n",
			ir->d.name, ir->d.minor);
		return -ENODEV;
	}

	mutex_lock(&ir->irctl_lock);

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
		result = -EINVAL;
	}

	mutex_unlock(&ir->irctl_lock);

	return result;
}
EXPORT_SYMBOL(lirc_dev_fop_ioctl);

ssize_t lirc_dev_fop_read(struct file *file,
			  char __user *buffer,
			  size_t length,
			  loff_t *ppos)
{
	struct irctl *ir = irctls[iminor(file_inode(file))];
	unsigned char *buf;
	int ret = 0, written = 0;
	DECLARE_WAITQUEUE(wait, current);

	if (!ir) {
		pr_err("called with invalid irctl\n");
		return -ENODEV;
	}

	if (!LIRC_CAN_REC(ir->d.features))
		return -EINVAL;

	dev_dbg(ir->d.dev, LOGHEAD "read called\n", ir->d.name, ir->d.minor);

	buf = kzalloc(ir->chunk_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (mutex_lock_interruptible(&ir->irctl_lock)) {
		ret = -ERESTARTSYS;
		goto out_unlocked;
	}
	if (!ir->attached) {
		ret = -ENODEV;
		goto out_locked;
	}

	if (length % ir->chunk_size) {
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

			mutex_unlock(&ir->irctl_lock);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			set_current_state(TASK_RUNNING);

			if (mutex_lock_interruptible(&ir->irctl_lock)) {
				ret = -ERESTARTSYS;
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
	mutex_unlock(&ir->irctl_lock);

out_unlocked:
	kfree(buf);

	return ret ? ret : written;
}
EXPORT_SYMBOL(lirc_dev_fop_read);

void *lirc_get_pdata(struct file *file)
{
	return irctls[iminor(file_inode(file))]->d.data;
}
EXPORT_SYMBOL(lirc_get_pdata);


ssize_t lirc_dev_fop_write(struct file *file, const char __user *buffer,
			   size_t length, loff_t *ppos)
{
	struct irctl *ir = irctls[iminor(file_inode(file))];

	if (!ir) {
		pr_err("called with invalid irctl\n");
		return -ENODEV;
	}

	if (!ir->attached)
		return -ENODEV;

	return -EINVAL;
}
EXPORT_SYMBOL(lirc_dev_fop_write);


static int __init lirc_dev_init(void)
{
	int retval;

	lirc_class = class_create(THIS_MODULE, "lirc");
	if (IS_ERR(lirc_class)) {
		pr_err("class_create failed\n");
		return PTR_ERR(lirc_class);
	}

	retval = alloc_chrdev_region(&lirc_base_dev, 0, MAX_IRCTL_DEVICES,
				     IRCTL_DEV_NAME);
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

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging messages");

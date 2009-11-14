/*
 * HID raw devices, giving access to raw HID events.
 *
 * In comparison to hiddev, this device does not process the
 * hid events at all (no parsing, no lookups). This lets applications
 * to work on raw hid events as they want to, and avoids a need to
 * use a transport-specific userspace libhid/libusb libraries.
 *
 *  Copyright (c) 2007 Jiri Kosina
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/major.h>
#include <linux/hid.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>

#include <linux/hidraw.h>

static int hidraw_major;
static struct cdev hidraw_cdev;
static struct class *hidraw_class;
static struct hidraw *hidraw_table[HIDRAW_MAX_DEVICES];
static DEFINE_MUTEX(minors_lock);

static ssize_t hidraw_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct hidraw_list *list = file->private_data;
	int ret = 0, len;
	char *report;
	DECLARE_WAITQUEUE(wait, current);

	mutex_lock(&list->read_mutex);

	while (ret == 0) {
		if (list->head == list->tail) {
			add_wait_queue(&list->hidraw->wait, &wait);
			set_current_state(TASK_INTERRUPTIBLE);

			while (list->head == list->tail) {
				if (file->f_flags & O_NONBLOCK) {
					ret = -EAGAIN;
					break;
				}
				if (signal_pending(current)) {
					ret = -ERESTARTSYS;
					break;
				}
				if (!list->hidraw->exist) {
					ret = -EIO;
					break;
				}

				/* allow O_NONBLOCK to work well from other threads */
				mutex_unlock(&list->read_mutex);
				schedule();
				mutex_lock(&list->read_mutex);
				set_current_state(TASK_INTERRUPTIBLE);
			}

			set_current_state(TASK_RUNNING);
			remove_wait_queue(&list->hidraw->wait, &wait);
		}

		if (ret)
			goto out;

		report = list->buffer[list->tail].value;
		len = list->buffer[list->tail].len > count ?
			count : list->buffer[list->tail].len;

		if (copy_to_user(buffer, list->buffer[list->tail].value, len)) {
			ret = -EFAULT;
			goto out;
		}
		ret += len;

		kfree(list->buffer[list->tail].value);
		list->tail = (list->tail + 1) & (HIDRAW_BUFFER_SIZE - 1);
	}
out:
	mutex_unlock(&list->read_mutex);
	return ret;
}

/* the first byte is expected to be a report number */
static ssize_t hidraw_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	unsigned int minor = iminor(file->f_path.dentry->d_inode);
	/* FIXME: What stops hidraw_table going NULL */
	struct hid_device *dev = hidraw_table[minor]->hid;
	__u8 *buf;
	int ret = 0;

	if (!dev->hid_output_raw_report)
		return -ENODEV;

	if (count > HID_MAX_BUFFER_SIZE) {
		printk(KERN_WARNING "hidraw: pid %d passed too large report\n",
				task_pid_nr(current));
		return -EINVAL;
	}

	if (count < 2) {
		printk(KERN_WARNING "hidraw: pid %d passed too short report\n",
				task_pid_nr(current));
		return -EINVAL;
	}

	buf = kmalloc(count * sizeof(__u8), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, buffer, count)) {
		ret = -EFAULT;
		goto out;
	}

	ret = dev->hid_output_raw_report(dev, buf, count);
out:
	kfree(buf);
	return ret;
}

static unsigned int hidraw_poll(struct file *file, poll_table *wait)
{
	struct hidraw_list *list = file->private_data;

	poll_wait(file, &list->hidraw->wait, wait);
	if (list->head != list->tail)
		return POLLIN | POLLRDNORM;
	if (!list->hidraw->exist)
		return POLLERR | POLLHUP;
	return 0;
}

static int hidraw_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	struct hidraw *dev;
	struct hidraw_list *list;
	int err = 0;

	if (!(list = kzalloc(sizeof(struct hidraw_list), GFP_KERNEL))) {
		err = -ENOMEM;
		goto out;
	}

	lock_kernel();
	mutex_lock(&minors_lock);
	if (!hidraw_table[minor]) {
		printk(KERN_EMERG "hidraw device with minor %d doesn't exist\n",
				minor);
		kfree(list);
		err = -ENODEV;
		goto out_unlock;
	}

	list->hidraw = hidraw_table[minor];
	mutex_init(&list->read_mutex);
	list_add_tail(&list->node, &hidraw_table[minor]->list);
	file->private_data = list;

	dev = hidraw_table[minor];
	if (!dev->open++) {
		if (dev->hid->ll_driver->power) {
			err = dev->hid->ll_driver->power(dev->hid, PM_HINT_FULLON);
			if (err < 0)
				goto out_unlock;
		}
		err = dev->hid->ll_driver->open(dev->hid);
		if (err < 0) {
			if (dev->hid->ll_driver->power)
				dev->hid->ll_driver->power(dev->hid, PM_HINT_NORMAL);
			dev->open--;
		}
	}

out_unlock:
	mutex_unlock(&minors_lock);
	unlock_kernel();
out:
	return err;

}

static int hidraw_release(struct inode * inode, struct file * file)
{
	unsigned int minor = iminor(inode);
	struct hidraw *dev;
	struct hidraw_list *list = file->private_data;

	if (!hidraw_table[minor]) {
		printk(KERN_EMERG "hidraw device with minor %d doesn't exist\n",
				minor);
		return -ENODEV;
	}

	list_del(&list->node);
	dev = hidraw_table[minor];
	if (!--dev->open) {
		if (list->hidraw->exist) {
			if (dev->hid->ll_driver->power)
				dev->hid->ll_driver->power(dev->hid, PM_HINT_NORMAL);
			dev->hid->ll_driver->close(dev->hid);
		} else {
			kfree(list->hidraw);
		}
	}

	kfree(list);

	return 0;
}

static long hidraw_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	unsigned int minor = iminor(inode);
	long ret = 0;
	/* FIXME: What stops hidraw_table going NULL */
	struct hidraw *dev = hidraw_table[minor];
	void __user *user_arg = (void __user*) arg;

	lock_kernel();
	switch (cmd) {
		case HIDIOCGRDESCSIZE:
			if (put_user(dev->hid->rsize, (int __user *)arg))
				ret = -EFAULT;
			break;

		case HIDIOCGRDESC:
			{
				__u32 len;

				if (get_user(len, (int __user *)arg))
					ret = -EFAULT;
				else if (len > HID_MAX_DESCRIPTOR_SIZE - 1)
					ret = -EINVAL;
				else if (copy_to_user(user_arg + offsetof(
					struct hidraw_report_descriptor,
					value[0]),
					dev->hid->rdesc,
					min(dev->hid->rsize, len)))
					ret = -EFAULT;
				break;
			}
		case HIDIOCGRAWINFO:
			{
				struct hidraw_devinfo dinfo;

				dinfo.bustype = dev->hid->bus;
				dinfo.vendor = dev->hid->vendor;
				dinfo.product = dev->hid->product;
				if (copy_to_user(user_arg, &dinfo, sizeof(dinfo)))
					ret = -EFAULT;
				break;
			}
		default:
			{
				struct hid_device *hid = dev->hid;
				if (_IOC_TYPE(cmd) != 'H' || _IOC_DIR(cmd) != _IOC_READ) {
					ret = -EINVAL;
					break;
				}

				if (_IOC_NR(cmd) == _IOC_NR(HIDIOCGRAWNAME(0))) {
					int len;
					if (!hid->name) {
						ret = 0;
						break;
					}
					len = strlen(hid->name) + 1;
					if (len > _IOC_SIZE(cmd))
						len = _IOC_SIZE(cmd);
					ret = copy_to_user(user_arg, hid->name, len) ?
						-EFAULT : len;
					break;
				}

				if (_IOC_NR(cmd) == _IOC_NR(HIDIOCGRAWPHYS(0))) {
					int len;
					if (!hid->phys) {
						ret = 0;
						break;
					}
					len = strlen(hid->phys) + 1;
					if (len > _IOC_SIZE(cmd))
						len = _IOC_SIZE(cmd);
					ret = copy_to_user(user_arg, hid->phys, len) ?
						-EFAULT : len;
					break;
				}
                }

		ret = -ENOTTY;
	}
	unlock_kernel();
	return ret;
}

static const struct file_operations hidraw_ops = {
	.owner =        THIS_MODULE,
	.read =         hidraw_read,
	.write =        hidraw_write,
	.poll =         hidraw_poll,
	.open =         hidraw_open,
	.release =      hidraw_release,
	.unlocked_ioctl = hidraw_ioctl,
};

void hidraw_report_event(struct hid_device *hid, u8 *data, int len)
{
	struct hidraw *dev = hid->hidraw;
	struct hidraw_list *list;

	list_for_each_entry(list, &dev->list, node) {
		list->buffer[list->head].value = kmemdup(data, len, GFP_ATOMIC);
		list->buffer[list->head].len = len;
		list->head = (list->head + 1) & (HIDRAW_BUFFER_SIZE - 1);
		kill_fasync(&list->fasync, SIGIO, POLL_IN);
	}

	wake_up_interruptible(&dev->wait);
}
EXPORT_SYMBOL_GPL(hidraw_report_event);

int hidraw_connect(struct hid_device *hid)
{
	int minor, result;
	struct hidraw *dev;

	/* we accept any HID device, no matter the applications */

	dev = kzalloc(sizeof(struct hidraw), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	result = -EINVAL;

	mutex_lock(&minors_lock);

	for (minor = 0; minor < HIDRAW_MAX_DEVICES; minor++) {
		if (hidraw_table[minor])
			continue;
		hidraw_table[minor] = dev;
		result = 0;
		break;
	}

	if (result) {
		mutex_unlock(&minors_lock);
		kfree(dev);
		goto out;
	}

	dev->dev = device_create(hidraw_class, &hid->dev, MKDEV(hidraw_major, minor),
				 NULL, "%s%d", "hidraw", minor);

	if (IS_ERR(dev->dev)) {
		hidraw_table[minor] = NULL;
		mutex_unlock(&minors_lock);
		result = PTR_ERR(dev->dev);
		kfree(dev);
		goto out;
	}

	mutex_unlock(&minors_lock);
	init_waitqueue_head(&dev->wait);
	INIT_LIST_HEAD(&dev->list);

	dev->hid = hid;
	dev->minor = minor;

	dev->exist = 1;
	hid->hidraw = dev;

out:
	return result;

}
EXPORT_SYMBOL_GPL(hidraw_connect);

void hidraw_disconnect(struct hid_device *hid)
{
	struct hidraw *hidraw = hid->hidraw;

	hidraw->exist = 0;

	mutex_lock(&minors_lock);
	hidraw_table[hidraw->minor] = NULL;
	mutex_unlock(&minors_lock);

	device_destroy(hidraw_class, MKDEV(hidraw_major, hidraw->minor));

	if (hidraw->open) {
		hid->ll_driver->close(hid);
		wake_up_interruptible(&hidraw->wait);
	} else {
		kfree(hidraw);
	}
}
EXPORT_SYMBOL_GPL(hidraw_disconnect);

int __init hidraw_init(void)
{
	int result;
	dev_t dev_id;

	result = alloc_chrdev_region(&dev_id, HIDRAW_FIRST_MINOR,
			HIDRAW_MAX_DEVICES, "hidraw");

	hidraw_major = MAJOR(dev_id);

	if (result < 0) {
		printk(KERN_WARNING "hidraw: can't get major number\n");
		result = 0;
		goto out;
	}

	hidraw_class = class_create(THIS_MODULE, "hidraw");
	if (IS_ERR(hidraw_class)) {
		result = PTR_ERR(hidraw_class);
		unregister_chrdev(hidraw_major, "hidraw");
		goto out;
	}

        cdev_init(&hidraw_cdev, &hidraw_ops);
        cdev_add(&hidraw_cdev, dev_id, HIDRAW_MAX_DEVICES);
out:
	return result;
}

void hidraw_exit(void)
{
	dev_t dev_id = MKDEV(hidraw_major, 0);

	cdev_del(&hidraw_cdev);
	class_destroy(hidraw_class);
	unregister_chrdev_region(dev_id, HIDRAW_MAX_DEVICES);

}

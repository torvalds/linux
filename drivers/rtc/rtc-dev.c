/*
 * RTC subsystem, dev interface
 *
 * Copyright (C) 2005 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * based on arch/arm/common/rtctime.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/rtc.h>

static struct class *rtc_dev_class;
static dev_t rtc_devt;

#define RTC_DEV_MAX 16 /* 16 RTCs should be enough for everyone... */

static int rtc_dev_open(struct inode *inode, struct file *file)
{
	int err;
	struct rtc_device *rtc = container_of(inode->i_cdev,
					struct rtc_device, char_dev);
	struct rtc_class_ops *ops = rtc->ops;

	/* We keep the lock as long as the device is in use
	 * and return immediately if busy
	 */
	if (!(mutex_trylock(&rtc->char_lock)))
		return -EBUSY;

	file->private_data = &rtc->class_dev;

	err = ops->open ? ops->open(rtc->class_dev.dev) : 0;
	if (err == 0) {
		spin_lock_irq(&rtc->irq_lock);
		rtc->irq_data = 0;
		spin_unlock_irq(&rtc->irq_lock);

		return 0;
	}

	/* something has gone wrong, release the lock */
	mutex_unlock(&rtc->char_lock);
	return err;
}


static ssize_t
rtc_dev_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct rtc_device *rtc = to_rtc_device(file->private_data);

	DECLARE_WAITQUEUE(wait, current);
	unsigned long data;
	ssize_t ret;

	if (count != sizeof(unsigned int) && count < sizeof(unsigned long))
		return -EINVAL;

	add_wait_queue(&rtc->irq_queue, &wait);
	do {
		__set_current_state(TASK_INTERRUPTIBLE);

		spin_lock_irq(&rtc->irq_lock);
		data = rtc->irq_data;
		rtc->irq_data = 0;
		spin_unlock_irq(&rtc->irq_lock);

		if (data != 0) {
			ret = 0;
			break;
		}
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		schedule();
	} while (1);
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&rtc->irq_queue, &wait);

	if (ret == 0) {
		/* Check for any data updates */
		if (rtc->ops->read_callback)
			data = rtc->ops->read_callback(rtc->class_dev.dev,
						       data);

		if (sizeof(int) != sizeof(long) &&
		    count == sizeof(unsigned int))
			ret = put_user(data, (unsigned int __user *)buf) ?:
				sizeof(unsigned int);
		else
			ret = put_user(data, (unsigned long __user *)buf) ?:
				sizeof(unsigned long);
	}
	return ret;
}

static unsigned int rtc_dev_poll(struct file *file, poll_table *wait)
{
	struct rtc_device *rtc = to_rtc_device(file->private_data);
	unsigned long data;

	poll_wait(file, &rtc->irq_queue, wait);

	data = rtc->irq_data;

	return (data != 0) ? (POLLIN | POLLRDNORM) : 0;
}

static int rtc_dev_ioctl(struct inode *inode, struct file *file,
		unsigned int cmd, unsigned long arg)
{
	int err = 0;
	struct class_device *class_dev = file->private_data;
	struct rtc_device *rtc = to_rtc_device(class_dev);
	struct rtc_class_ops *ops = rtc->ops;
	struct rtc_time tm;
	struct rtc_wkalrm alarm;
	void __user *uarg = (void __user *) arg;

	/* avoid conflicting IRQ users */
	if (cmd == RTC_PIE_ON || cmd == RTC_PIE_OFF || cmd == RTC_IRQP_SET) {
		spin_lock(&rtc->irq_task_lock);
		if (rtc->irq_task)
			err = -EBUSY;
		spin_unlock(&rtc->irq_task_lock);

		if (err < 0)
			return err;
	}

	/* try the driver's ioctl interface */
	if (ops->ioctl) {
		err = ops->ioctl(class_dev->dev, cmd, arg);
		if (err != -ENOIOCTLCMD)
			return err;
	}

	/* if the driver does not provide the ioctl interface
	 * or if that particular ioctl was not implemented
	 * (-ENOIOCTLCMD), we will try to emulate here.
	 */

	switch (cmd) {
	case RTC_ALM_READ:
		err = rtc_read_alarm(class_dev, &alarm);
		if (err < 0)
			return err;

		if (copy_to_user(uarg, &alarm.time, sizeof(tm)))
			return -EFAULT;
		break;

	case RTC_ALM_SET:
		if (copy_from_user(&alarm.time, uarg, sizeof(tm)))
			return -EFAULT;

		alarm.enabled = 0;
		alarm.pending = 0;
		alarm.time.tm_mday = -1;
		alarm.time.tm_mon = -1;
		alarm.time.tm_year = -1;
		alarm.time.tm_wday = -1;
		alarm.time.tm_yday = -1;
		alarm.time.tm_isdst = -1;
		err = rtc_set_alarm(class_dev, &alarm);
		break;

	case RTC_RD_TIME:
		err = rtc_read_time(class_dev, &tm);
		if (err < 0)
			return err;

		if (copy_to_user(uarg, &tm, sizeof(tm)))
			return -EFAULT;
		break;

	case RTC_SET_TIME:
		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		if (copy_from_user(&tm, uarg, sizeof(tm)))
			return -EFAULT;

		err = rtc_set_time(class_dev, &tm);
		break;
#if 0
	case RTC_EPOCH_SET:
#ifndef rtc_epoch
		/*
		 * There were no RTC clocks before 1900.
		 */
		if (arg < 1900) {
			err = -EINVAL;
			break;
		}
		if (!capable(CAP_SYS_TIME)) {
			err = -EACCES;
			break;
		}
		rtc_epoch = arg;
		err = 0;
#endif
		break;

	case RTC_EPOCH_READ:
		err = put_user(rtc_epoch, (unsigned long __user *)uarg);
		break;
#endif
	case RTC_WKALM_SET:
		if (copy_from_user(&alarm, uarg, sizeof(alarm)))
			return -EFAULT;

		err = rtc_set_alarm(class_dev, &alarm);
		break;

	case RTC_WKALM_RD:
		err = rtc_read_alarm(class_dev, &alarm);
		if (err < 0)
			return err;

		if (copy_to_user(uarg, &alarm, sizeof(alarm)))
			return -EFAULT;
		break;

	default:
		err = -ENOTTY;
		break;
	}

	return err;
}

static int rtc_dev_release(struct inode *inode, struct file *file)
{
	struct rtc_device *rtc = to_rtc_device(file->private_data);

	if (rtc->ops->release)
		rtc->ops->release(rtc->class_dev.dev);

	mutex_unlock(&rtc->char_lock);
	return 0;
}

static int rtc_dev_fasync(int fd, struct file *file, int on)
{
	struct rtc_device *rtc = to_rtc_device(file->private_data);
	return fasync_helper(fd, file, on, &rtc->async_queue);
}

static struct file_operations rtc_dev_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= rtc_dev_read,
	.poll		= rtc_dev_poll,
	.ioctl		= rtc_dev_ioctl,
	.open		= rtc_dev_open,
	.release	= rtc_dev_release,
	.fasync		= rtc_dev_fasync,
};

/* insertion/removal hooks */

static int rtc_dev_add_device(struct class_device *class_dev,
				struct class_interface *class_intf)
{
	int err = 0;
	struct rtc_device *rtc = to_rtc_device(class_dev);

	if (rtc->id >= RTC_DEV_MAX) {
		dev_err(class_dev->dev, "too many RTCs\n");
		return -EINVAL;
	}

	mutex_init(&rtc->char_lock);
	spin_lock_init(&rtc->irq_lock);
	init_waitqueue_head(&rtc->irq_queue);

	cdev_init(&rtc->char_dev, &rtc_dev_fops);
	rtc->char_dev.owner = rtc->owner;

	if (cdev_add(&rtc->char_dev, MKDEV(MAJOR(rtc_devt), rtc->id), 1)) {
		cdev_del(&rtc->char_dev);
		dev_err(class_dev->dev,
			"failed to add char device %d:%d\n",
			MAJOR(rtc_devt), rtc->id);
		return -ENODEV;
	}

	rtc->rtc_dev = class_device_create(rtc_dev_class, NULL,
						MKDEV(MAJOR(rtc_devt), rtc->id),
						class_dev->dev, "rtc%d", rtc->id);
	if (IS_ERR(rtc->rtc_dev)) {
		dev_err(class_dev->dev, "cannot create rtc_dev device\n");
		err = PTR_ERR(rtc->rtc_dev);
		goto err_cdev_del;
	}

	dev_info(class_dev->dev, "rtc intf: dev (%d:%d)\n",
		MAJOR(rtc->rtc_dev->devt),
		MINOR(rtc->rtc_dev->devt));

	return 0;

err_cdev_del:

	cdev_del(&rtc->char_dev);
	return err;
}

static void rtc_dev_remove_device(struct class_device *class_dev,
					struct class_interface *class_intf)
{
	struct rtc_device *rtc = to_rtc_device(class_dev);

	if (rtc->rtc_dev) {
		dev_dbg(class_dev->dev, "removing char %d:%d\n",
			MAJOR(rtc->rtc_dev->devt),
			MINOR(rtc->rtc_dev->devt));

		class_device_unregister(rtc->rtc_dev);
		cdev_del(&rtc->char_dev);
	}
}

/* interface registration */

static struct class_interface rtc_dev_interface = {
	.add = &rtc_dev_add_device,
	.remove = &rtc_dev_remove_device,
};

static int __init rtc_dev_init(void)
{
	int err;

	rtc_dev_class = class_create(THIS_MODULE, "rtc-dev");
	if (IS_ERR(rtc_dev_class))
		return PTR_ERR(rtc_dev_class);

	err = alloc_chrdev_region(&rtc_devt, 0, RTC_DEV_MAX, "rtc");
	if (err < 0) {
		printk(KERN_ERR "%s: failed to allocate char dev region\n",
			__FILE__);
		goto err_destroy_class;
	}

	err = rtc_interface_register(&rtc_dev_interface);
	if (err < 0) {
		printk(KERN_ERR "%s: failed to register the interface\n",
			__FILE__);
		goto err_unregister_chrdev;
	}

	return 0;

err_unregister_chrdev:
	unregister_chrdev_region(rtc_devt, RTC_DEV_MAX);

err_destroy_class:
	class_destroy(rtc_dev_class);

	return err;
}

static void __exit rtc_dev_exit(void)
{
	class_interface_unregister(&rtc_dev_interface);
	class_destroy(rtc_dev_class);
	unregister_chrdev_region(rtc_devt, RTC_DEV_MAX);
}

module_init(rtc_dev_init);
module_exit(rtc_dev_exit);

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("RTC class dev interface");
MODULE_LICENSE("GPL");

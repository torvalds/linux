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
#include "rtc-core.h"

static dev_t rtc_devt;

#define RTC_DEV_MAX 16 /* 16 RTCs should be enough for everyone... */

static int rtc_dev_open(struct inode *inode, struct file *file)
{
	int err;
	struct rtc_device *rtc = container_of(inode->i_cdev,
					struct rtc_device, char_dev);
	const struct rtc_class_ops *ops = rtc->ops;

	/* We keep the lock as long as the device is in use
	 * and return immediately if busy
	 */
	if (!(mutex_trylock(&rtc->char_lock)))
		return -EBUSY;

	file->private_data = rtc;

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

#ifdef CONFIG_RTC_INTF_DEV_UIE_EMUL
/*
 * Routine to poll RTC seconds field for change as often as possible,
 * after first RTC_UIE use timer to reduce polling
 */
static void rtc_uie_task(struct work_struct *work)
{
	struct rtc_device *rtc =
		container_of(work, struct rtc_device, uie_task);
	struct rtc_time tm;
	int num = 0;
	int err;

	err = rtc_read_time(rtc, &tm);

	local_irq_disable();
	spin_lock(&rtc->irq_lock);
	if (rtc->stop_uie_polling || err) {
		rtc->uie_task_active = 0;
	} else if (rtc->oldsecs != tm.tm_sec) {
		num = (tm.tm_sec + 60 - rtc->oldsecs) % 60;
		rtc->oldsecs = tm.tm_sec;
		rtc->uie_timer.expires = jiffies + HZ - (HZ/10);
		rtc->uie_timer_active = 1;
		rtc->uie_task_active = 0;
		add_timer(&rtc->uie_timer);
	} else if (schedule_work(&rtc->uie_task) == 0) {
		rtc->uie_task_active = 0;
	}
	spin_unlock(&rtc->irq_lock);
	if (num)
		rtc_update_irq(rtc, num, RTC_UF | RTC_IRQF);
	local_irq_enable();
}
static void rtc_uie_timer(unsigned long data)
{
	struct rtc_device *rtc = (struct rtc_device *)data;
	unsigned long flags;

	spin_lock_irqsave(&rtc->irq_lock, flags);
	rtc->uie_timer_active = 0;
	rtc->uie_task_active = 1;
	if ((schedule_work(&rtc->uie_task) == 0))
		rtc->uie_task_active = 0;
	spin_unlock_irqrestore(&rtc->irq_lock, flags);
}

static void clear_uie(struct rtc_device *rtc)
{
	spin_lock_irq(&rtc->irq_lock);
	if (rtc->irq_active) {
		rtc->stop_uie_polling = 1;
		if (rtc->uie_timer_active) {
			spin_unlock_irq(&rtc->irq_lock);
			del_timer_sync(&rtc->uie_timer);
			spin_lock_irq(&rtc->irq_lock);
			rtc->uie_timer_active = 0;
		}
		if (rtc->uie_task_active) {
			spin_unlock_irq(&rtc->irq_lock);
			flush_scheduled_work();
			spin_lock_irq(&rtc->irq_lock);
		}
		rtc->irq_active = 0;
	}
	spin_unlock_irq(&rtc->irq_lock);
}

static int set_uie(struct rtc_device *rtc)
{
	struct rtc_time tm;
	int err;

	err = rtc_read_time(rtc, &tm);
	if (err)
		return err;
	spin_lock_irq(&rtc->irq_lock);
	if (!rtc->irq_active) {
		rtc->irq_active = 1;
		rtc->stop_uie_polling = 0;
		rtc->oldsecs = tm.tm_sec;
		rtc->uie_task_active = 1;
		if (schedule_work(&rtc->uie_task) == 0)
			rtc->uie_task_active = 0;
	}
	rtc->irq_data = 0;
	spin_unlock_irq(&rtc->irq_lock);
	return 0;
}
#endif /* CONFIG_RTC_INTF_DEV_UIE_EMUL */

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
	struct rtc_device *rtc = file->private_data;
	const struct rtc_class_ops *ops = rtc->ops;
	struct rtc_time tm;
	struct rtc_wkalrm alarm;
	void __user *uarg = (void __user *) arg;

	/* check that the calling task has appropriate permissions
	 * for certain ioctls. doing this check here is useful
	 * to avoid duplicate code in each driver.
	 */
	switch (cmd) {
	case RTC_EPOCH_SET:
	case RTC_SET_TIME:
		if (!capable(CAP_SYS_TIME))
			return -EACCES;
		break;

	case RTC_IRQP_SET:
		if (arg > rtc->max_user_freq && !capable(CAP_SYS_RESOURCE))
			return -EACCES;
		break;

	case RTC_PIE_ON:
		if (!capable(CAP_SYS_RESOURCE))
			return -EACCES;
		break;
	}

	/* avoid conflicting IRQ users */
	if (cmd == RTC_PIE_ON || cmd == RTC_PIE_OFF || cmd == RTC_IRQP_SET) {
		spin_lock_irq(&rtc->irq_task_lock);
		if (rtc->irq_task)
			err = -EBUSY;
		spin_unlock_irq(&rtc->irq_task_lock);

		if (err < 0)
			return err;
	}

	/* try the driver's ioctl interface */
	if (ops->ioctl) {
		err = ops->ioctl(rtc->class_dev.dev, cmd, arg);
		if (err != -ENOIOCTLCMD)
			return err;
	}

	/* if the driver does not provide the ioctl interface
	 * or if that particular ioctl was not implemented
	 * (-ENOIOCTLCMD), we will try to emulate here.
	 */

	switch (cmd) {
	case RTC_ALM_READ:
		err = rtc_read_alarm(rtc, &alarm);
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
		err = rtc_set_alarm(rtc, &alarm);
		break;

	case RTC_RD_TIME:
		err = rtc_read_time(rtc, &tm);
		if (err < 0)
			return err;

		if (copy_to_user(uarg, &tm, sizeof(tm)))
			return -EFAULT;
		break;

	case RTC_SET_TIME:
		if (copy_from_user(&tm, uarg, sizeof(tm)))
			return -EFAULT;

		err = rtc_set_time(rtc, &tm);
		break;

	case RTC_IRQP_READ:
		if (ops->irq_set_freq)
			err = put_user(rtc->irq_freq, (unsigned long __user *)uarg);
		break;

	case RTC_IRQP_SET:
		if (ops->irq_set_freq)
			err = rtc_irq_set_freq(rtc, rtc->irq_task, arg);
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

		err = rtc_set_alarm(rtc, &alarm);
		break;

	case RTC_WKALM_RD:
		err = rtc_read_alarm(rtc, &alarm);
		if (err < 0)
			return err;

		if (copy_to_user(uarg, &alarm, sizeof(alarm)))
			return -EFAULT;
		break;

#ifdef CONFIG_RTC_INTF_DEV_UIE_EMUL
	case RTC_UIE_OFF:
		clear_uie(rtc);
		return 0;

	case RTC_UIE_ON:
		return set_uie(rtc);
#endif
	default:
		err = -ENOTTY;
		break;
	}

	return err;
}

static int rtc_dev_release(struct inode *inode, struct file *file)
{
	struct rtc_device *rtc = to_rtc_device(file->private_data);

#ifdef CONFIG_RTC_INTF_DEV_UIE_EMUL
	clear_uie(rtc);
#endif
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

static const struct file_operations rtc_dev_fops = {
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

void rtc_dev_add_device(struct rtc_device *rtc)
{
	if (!rtc_devt)
		return;

	if (rtc->id >= RTC_DEV_MAX) {
		pr_debug("%s: too many RTC devices\n", rtc->name);
		return;
	}

	rtc->class_dev.devt = MKDEV(MAJOR(rtc_devt), rtc->id);

	mutex_init(&rtc->char_lock);
	spin_lock_init(&rtc->irq_lock);
	init_waitqueue_head(&rtc->irq_queue);
#ifdef CONFIG_RTC_INTF_DEV_UIE_EMUL
	INIT_WORK(&rtc->uie_task, rtc_uie_task);
	setup_timer(&rtc->uie_timer, rtc_uie_timer, (unsigned long)rtc);
#endif

	cdev_init(&rtc->char_dev, &rtc_dev_fops);
	rtc->char_dev.owner = rtc->owner;

	if (cdev_add(&rtc->char_dev, rtc->class_dev.devt, 1))
		printk(KERN_WARNING "%s: failed to add char device %d:%d\n",
			rtc->name, MAJOR(rtc_devt), rtc->id);
	else
		pr_debug("%s: dev (%d:%d)\n", rtc->name,
			MAJOR(rtc_devt), rtc->id);
}

void rtc_dev_del_device(struct rtc_device *rtc)
{
	if (rtc->class_dev.devt)
		cdev_del(&rtc->char_dev);
}

void __init rtc_dev_init(void)
{
	int err;

	err = alloc_chrdev_region(&rtc_devt, 0, RTC_DEV_MAX, "rtc");
	if (err < 0)
		printk(KERN_ERR "%s: failed to allocate char dev region\n",
			__FILE__);
}

void __exit rtc_dev_exit(void)
{
	if (rtc_devt)
		unregister_chrdev_region(rtc_devt, RTC_DEV_MAX);
}

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
#include <linux/sched.h>
#include "rtc-core.h"

static dev_t rtc_devt;

#define RTC_DEV_MAX 16 /* 16 RTCs should be enough for everyone... */

static int rtc_dev_open(struct inode *inode, struct file *file)
{
	int err;
	struct rtc_device *rtc = container_of(inode->i_cdev,
					struct rtc_device, char_dev);
	const struct rtc_class_ops *ops = rtc->ops;

	if (test_and_set_bit_lock(RTC_DEV_BUSY, &rtc->flags))
		return -EBUSY;

	file->private_data = rtc;

	err = ops->open ? ops->open(rtc->dev.parent) : 0;
	if (err == 0) {
		spin_lock_irq(&rtc->irq_lock);
		rtc->irq_data = 0;
		spin_unlock_irq(&rtc->irq_lock);

		return 0;
	}

	/* something has gone wrong */
	clear_bit_unlock(RTC_DEV_BUSY, &rtc->flags);
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

	spin_lock_irq(&rtc->irq_lock);
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
	spin_unlock_irq(&rtc->irq_lock);
	if (num)
		rtc_handle_legacy_irq(rtc, num, RTC_UF);
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

static int clear_uie(struct rtc_device *rtc)
{
	spin_lock_irq(&rtc->irq_lock);
	if (rtc->uie_irq_active) {
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
		rtc->uie_irq_active = 0;
	}
	spin_unlock_irq(&rtc->irq_lock);
	return 0;
}

static int set_uie(struct rtc_device *rtc)
{
	struct rtc_time tm;
	int err;

	err = rtc_read_time(rtc, &tm);
	if (err)
		return err;
	spin_lock_irq(&rtc->irq_lock);
	if (!rtc->uie_irq_active) {
		rtc->uie_irq_active = 1;
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

int rtc_dev_update_irq_enable_emul(struct rtc_device *rtc, unsigned int enabled)
{
	if (enabled)
		return set_uie(rtc);
	else
		return clear_uie(rtc);
}
EXPORT_SYMBOL(rtc_dev_update_irq_enable_emul);

#endif /* CONFIG_RTC_INTF_DEV_UIE_EMUL */

static ssize_t
rtc_dev_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct rtc_device *rtc = file->private_data;

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
			data = rtc->ops->read_callback(rtc->dev.parent,
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
	struct rtc_device *rtc = file->private_data;
	unsigned long data;

	poll_wait(file, &rtc->irq_queue, wait);

	data = rtc->irq_data;

	return (data != 0) ? (POLLIN | POLLRDNORM) : 0;
}

static long rtc_dev_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	int err = 0;
	struct rtc_device *rtc = file->private_data;
	const struct rtc_class_ops *ops = rtc->ops;
	struct rtc_time tm;
	struct rtc_wkalrm alarm;
	void __user *uarg = (void __user *) arg;

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return err;

	/* check that the calling task has appropriate permissions
	 * for certain ioctls. doing this check here is useful
	 * to avoid duplicate code in each driver.
	 */
	switch (cmd) {
	case RTC_EPOCH_SET:
	case RTC_SET_TIME:
		if (!capable(CAP_SYS_TIME))
			err = -EACCES;
		break;

	case RTC_IRQP_SET:
		if (arg > rtc->max_user_freq && !capable(CAP_SYS_RESOURCE))
			err = -EACCES;
		break;

	case RTC_PIE_ON:
		if (rtc->irq_freq > rtc->max_user_freq &&
				!capable(CAP_SYS_RESOURCE))
			err = -EACCES;
		break;
	}

	if (err)
		goto done;

	/*
	 * Drivers *SHOULD NOT* provide ioctl implementations
	 * for these requests.  Instead, provide methods to
	 * support the following code, so that the RTC's main
	 * features are accessible without using ioctls.
	 *
	 * RTC and alarm times will be in UTC, by preference,
	 * but dual-booting with MS-Windows implies RTCs must
	 * use the local wall clock time.
	 */

	switch (cmd) {
	case RTC_ALM_READ:
		mutex_unlock(&rtc->ops_lock);

		err = rtc_read_alarm(rtc, &alarm);
		if (err < 0)
			return err;

		if (copy_to_user(uarg, &alarm.time, sizeof(tm)))
			err = -EFAULT;
		return err;

	case RTC_ALM_SET:
		mutex_unlock(&rtc->ops_lock);

		if (copy_from_user(&alarm.time, uarg, sizeof(tm)))
			return -EFAULT;

		alarm.enabled = 0;
		alarm.pending = 0;
		alarm.time.tm_wday = -1;
		alarm.time.tm_yday = -1;
		alarm.time.tm_isdst = -1;

		/* RTC_ALM_SET alarms may be up to 24 hours in the future.
		 * Rather than expecting every RTC to implement "don't care"
		 * for day/month/year fields, just force the alarm to have
		 * the right values for those fields.
		 *
		 * RTC_WKALM_SET should be used instead.  Not only does it
		 * eliminate the need for a separate RTC_AIE_ON call, it
		 * doesn't have the "alarm 23:59:59 in the future" race.
		 *
		 * NOTE:  some legacy code may have used invalid fields as
		 * wildcards, exposing hardware "periodic alarm" capabilities.
		 * Not supported here.
		 */
		{
			unsigned long now, then;

			err = rtc_read_time(rtc, &tm);
			if (err < 0)
				return err;
			rtc_tm_to_time(&tm, &now);

			alarm.time.tm_mday = tm.tm_mday;
			alarm.time.tm_mon = tm.tm_mon;
			alarm.time.tm_year = tm.tm_year;
			err  = rtc_valid_tm(&alarm.time);
			if (err < 0)
				return err;
			rtc_tm_to_time(&alarm.time, &then);

			/* alarm may need to wrap into tomorrow */
			if (then < now) {
				rtc_time_to_tm(now + 24 * 60 * 60, &tm);
				alarm.time.tm_mday = tm.tm_mday;
				alarm.time.tm_mon = tm.tm_mon;
				alarm.time.tm_year = tm.tm_year;
			}
		}

		return rtc_set_alarm(rtc, &alarm);

	case RTC_RD_TIME:
		mutex_unlock(&rtc->ops_lock);

		err = rtc_read_time(rtc, &tm);
		if (err < 0)
			return err;

		if (copy_to_user(uarg, &tm, sizeof(tm)))
			err = -EFAULT;
		return err;

	case RTC_SET_TIME:
		mutex_unlock(&rtc->ops_lock);

		if (copy_from_user(&tm, uarg, sizeof(tm)))
			return -EFAULT;

		return rtc_set_time(rtc, &tm);

	case RTC_PIE_ON:
		err = rtc_irq_set_state(rtc, NULL, 1);
		break;

	case RTC_PIE_OFF:
		err = rtc_irq_set_state(rtc, NULL, 0);
		break;

	case RTC_AIE_ON:
		mutex_unlock(&rtc->ops_lock);
		return rtc_alarm_irq_enable(rtc, 1);

	case RTC_AIE_OFF:
		mutex_unlock(&rtc->ops_lock);
		return rtc_alarm_irq_enable(rtc, 0);

	case RTC_UIE_ON:
		mutex_unlock(&rtc->ops_lock);
		return rtc_update_irq_enable(rtc, 1);

	case RTC_UIE_OFF:
		mutex_unlock(&rtc->ops_lock);
		return rtc_update_irq_enable(rtc, 0);

	case RTC_IRQP_SET:
		err = rtc_irq_set_freq(rtc, NULL, arg);
		break;

	case RTC_IRQP_READ:
		err = put_user(rtc->irq_freq, (unsigned long __user *)uarg);
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
		mutex_unlock(&rtc->ops_lock);
		if (copy_from_user(&alarm, uarg, sizeof(alarm)))
			return -EFAULT;

		return rtc_set_alarm(rtc, &alarm);

	case RTC_WKALM_RD:
		mutex_unlock(&rtc->ops_lock);
		err = rtc_read_alarm(rtc, &alarm);
		if (err < 0)
			return err;

		if (copy_to_user(uarg, &alarm, sizeof(alarm)))
			err = -EFAULT;
		return err;

	default:
		/* Finally try the driver's ioctl interface */
		if (ops->ioctl) {
			err = ops->ioctl(rtc->dev.parent, cmd, arg);
			if (err == -ENOIOCTLCMD)
				err = -ENOTTY;
		}
		break;
	}

done:
	mutex_unlock(&rtc->ops_lock);
	return err;
}

static int rtc_dev_fasync(int fd, struct file *file, int on)
{
	struct rtc_device *rtc = file->private_data;
	return fasync_helper(fd, file, on, &rtc->async_queue);
}

static int rtc_dev_release(struct inode *inode, struct file *file)
{
	struct rtc_device *rtc = file->private_data;

	/* We shut down the repeating IRQs that userspace enabled,
	 * since nothing is listening to them.
	 *  - Update (UIE) ... currently only managed through ioctls
	 *  - Periodic (PIE) ... also used through rtc_*() interface calls
	 *
	 * Leave the alarm alone; it may be set to trigger a system wakeup
	 * later, or be used by kernel code, and is a one-shot event anyway.
	 */

	/* Keep ioctl until all drivers are converted */
	rtc_dev_ioctl(file, RTC_UIE_OFF, 0);
	rtc_update_irq_enable(rtc, 0);
	rtc_irq_set_state(rtc, NULL, 0);

	if (rtc->ops->release)
		rtc->ops->release(rtc->dev.parent);

	clear_bit_unlock(RTC_DEV_BUSY, &rtc->flags);
	return 0;
}

static const struct file_operations rtc_dev_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= rtc_dev_read,
	.poll		= rtc_dev_poll,
	.unlocked_ioctl	= rtc_dev_ioctl,
	.open		= rtc_dev_open,
	.release	= rtc_dev_release,
	.fasync		= rtc_dev_fasync,
};

/* insertion/removal hooks */

void rtc_dev_prepare(struct rtc_device *rtc)
{
	if (!rtc_devt)
		return;

	if (rtc->id >= RTC_DEV_MAX) {
		pr_debug("%s: too many RTC devices\n", rtc->name);
		return;
	}

	rtc->dev.devt = MKDEV(MAJOR(rtc_devt), rtc->id);

#ifdef CONFIG_RTC_INTF_DEV_UIE_EMUL
	INIT_WORK(&rtc->uie_task, rtc_uie_task);
	setup_timer(&rtc->uie_timer, rtc_uie_timer, (unsigned long)rtc);
#endif

	cdev_init(&rtc->char_dev, &rtc_dev_fops);
	rtc->char_dev.owner = rtc->owner;
}

void rtc_dev_add_device(struct rtc_device *rtc)
{
	if (cdev_add(&rtc->char_dev, rtc->dev.devt, 1))
		printk(KERN_WARNING "%s: failed to add char device %d:%d\n",
			rtc->name, MAJOR(rtc_devt), rtc->id);
	else
		pr_debug("%s: dev (%d:%d)\n", rtc->name,
			MAJOR(rtc_devt), rtc->id);
}

void rtc_dev_del_device(struct rtc_device *rtc)
{
	if (rtc->dev.devt)
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

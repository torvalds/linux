/*
 *  linux/arch/arm/common/rtctime.c
 *
 *  Copyright (C) 2003 Deep Blue Solutions Ltd.
 *  Based on sa1100-rtc.c, Nils Faerber, CIH, Nicolas Pitre.
 *  Based on rtc.c by Paul Gortmaker
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/device.h>

#include <asm/rtc.h>
#include <asm/semaphore.h>

static DECLARE_WAIT_QUEUE_HEAD(rtc_wait);
static struct fasync_struct *rtc_async_queue;

/*
 * rtc_lock protects rtc_irq_data
 */
static DEFINE_SPINLOCK(rtc_lock);
static unsigned long rtc_irq_data;

/*
 * rtc_sem protects rtc_inuse and rtc_ops
 */
static DECLARE_MUTEX(rtc_sem);
static unsigned long rtc_inuse;
static struct rtc_ops *rtc_ops;

#define rtc_epoch 1900UL

static const unsigned char days_in_month[] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

#define LEAPS_THRU_END_OF(y) ((y)/4 - (y)/100 + (y)/400)
#define LEAP_YEAR(year) ((!(year % 4) && (year % 100)) || !(year % 400))

static int month_days(unsigned int month, unsigned int year)
{
	return days_in_month[month] + (LEAP_YEAR(year) && month == 1);
}

/*
 * Convert seconds since 01-01-1970 00:00:00 to Gregorian date.
 */
void rtc_time_to_tm(unsigned long time, struct rtc_time *tm)
{
	int days, month, year;

	days = time / 86400;
	time -= days * 86400;

	tm->tm_wday = (days + 4) % 7;

	year = 1970 + days / 365;
	days -= (year - 1970) * 365
	        + LEAPS_THRU_END_OF(year - 1)
	        - LEAPS_THRU_END_OF(1970 - 1);
	if (days < 0) {
		year -= 1;
		days += 365 + LEAP_YEAR(year);
	}
	tm->tm_year = year - 1900;
	tm->tm_yday = days + 1;

	for (month = 0; month < 11; month++) {
		int newdays;

		newdays = days - month_days(month, year);
		if (newdays < 0)
			break;
		days = newdays;
	}
	tm->tm_mon = month;
	tm->tm_mday = days + 1;

	tm->tm_hour = time / 3600;
	time -= tm->tm_hour * 3600;
	tm->tm_min = time / 60;
	tm->tm_sec = time - tm->tm_min * 60;
}
EXPORT_SYMBOL(rtc_time_to_tm);

/*
 * Does the rtc_time represent a valid date/time?
 */
int rtc_valid_tm(struct rtc_time *tm)
{
	if (tm->tm_year < 70 ||
	    tm->tm_mon >= 12 ||
	    tm->tm_mday < 1 ||
	    tm->tm_mday > month_days(tm->tm_mon, tm->tm_year + 1900) ||
	    tm->tm_hour >= 24 ||
	    tm->tm_min >= 60 ||
	    tm->tm_sec >= 60)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(rtc_valid_tm);

/*
 * Convert Gregorian date to seconds since 01-01-1970 00:00:00.
 */
int rtc_tm_to_time(struct rtc_time *tm, unsigned long *time)
{
	*time = mktime(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		       tm->tm_hour, tm->tm_min, tm->tm_sec);

	return 0;
}
EXPORT_SYMBOL(rtc_tm_to_time);

/*
 * Calculate the next alarm time given the requested alarm time mask
 * and the current time.
 *
 * FIXME: for now, we just copy the alarm time because we're lazy (and
 * is therefore buggy - setting a 10am alarm at 8pm will not result in
 * the alarm triggering.)
 */
void rtc_next_alarm_time(struct rtc_time *next, struct rtc_time *now, struct rtc_time *alrm)
{
	next->tm_year = now->tm_year;
	next->tm_mon = now->tm_mon;
	next->tm_mday = now->tm_mday;
	next->tm_hour = alrm->tm_hour;
	next->tm_min = alrm->tm_min;
	next->tm_sec = alrm->tm_sec;
}

static inline int rtc_read_time(struct rtc_ops *ops, struct rtc_time *tm)
{
	memset(tm, 0, sizeof(struct rtc_time));
	return ops->read_time(tm);
}

static inline int rtc_set_time(struct rtc_ops *ops, struct rtc_time *tm)
{
	int ret;

	ret = rtc_valid_tm(tm);
	if (ret == 0)
		ret = ops->set_time(tm);

	return ret;
}

static inline int rtc_read_alarm(struct rtc_ops *ops, struct rtc_wkalrm *alrm)
{
	int ret = -EINVAL;
	if (ops->read_alarm) {
		memset(alrm, 0, sizeof(struct rtc_wkalrm));
		ret = ops->read_alarm(alrm);
	}
	return ret;
}

static inline int rtc_set_alarm(struct rtc_ops *ops, struct rtc_wkalrm *alrm)
{
	int ret = -EINVAL;
	if (ops->set_alarm)
		ret = ops->set_alarm(alrm);
	return ret;
}

void rtc_update(unsigned long num, unsigned long events)
{
	spin_lock(&rtc_lock);
	rtc_irq_data = (rtc_irq_data + (num << 8)) | events;
	spin_unlock(&rtc_lock);

	wake_up_interruptible(&rtc_wait);
	kill_fasync(&rtc_async_queue, SIGIO, POLL_IN);
}
EXPORT_SYMBOL(rtc_update);


static ssize_t
rtc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long data;
	ssize_t ret;

	if (count < sizeof(unsigned long))
		return -EINVAL;

	add_wait_queue(&rtc_wait, &wait);
	do {
		__set_current_state(TASK_INTERRUPTIBLE);

		spin_lock_irq(&rtc_lock);
		data = rtc_irq_data;
		rtc_irq_data = 0;
		spin_unlock_irq(&rtc_lock);

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
	remove_wait_queue(&rtc_wait, &wait);

	if (ret == 0) {
		ret = put_user(data, (unsigned long __user *)buf);
		if (ret == 0)
			ret = sizeof(unsigned long);
	}
	return ret;
}

static unsigned int rtc_poll(struct file *file, poll_table *wait)
{
	unsigned long data;

	poll_wait(file, &rtc_wait, wait);

	spin_lock_irq(&rtc_lock);
	data = rtc_irq_data;
	spin_unlock_irq(&rtc_lock);

	return data != 0 ? POLLIN | POLLRDNORM : 0;
}

static int rtc_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		     unsigned long arg)
{
	struct rtc_ops *ops = file->private_data;
	struct rtc_time tm;
	struct rtc_wkalrm alrm;
	void __user *uarg = (void __user *)arg;
	int ret = -EINVAL;

	switch (cmd) {
	case RTC_ALM_READ:
		ret = rtc_read_alarm(ops, &alrm);
		if (ret)
			break;
		ret = copy_to_user(uarg, &alrm.time, sizeof(tm));
		if (ret)
			ret = -EFAULT;
		break;

	case RTC_ALM_SET:
		ret = copy_from_user(&alrm.time, uarg, sizeof(tm));
		if (ret) {
			ret = -EFAULT;
			break;
		}
		alrm.enabled = 0;
		alrm.pending = 0;
		alrm.time.tm_mday = -1;
		alrm.time.tm_mon = -1;
		alrm.time.tm_year = -1;
		alrm.time.tm_wday = -1;
		alrm.time.tm_yday = -1;
		alrm.time.tm_isdst = -1;
		ret = rtc_set_alarm(ops, &alrm);
		break;

	case RTC_RD_TIME:
		ret = rtc_read_time(ops, &tm);
		if (ret)
			break;
		ret = copy_to_user(uarg, &tm, sizeof(tm));
		if (ret)
			ret = -EFAULT;
		break;

	case RTC_SET_TIME:
		if (!capable(CAP_SYS_TIME)) {
			ret = -EACCES;
			break;
		}
		ret = copy_from_user(&tm, uarg, sizeof(tm));
		if (ret) {
			ret = -EFAULT;
			break;
		}
		ret = rtc_set_time(ops, &tm);
		break;

	case RTC_EPOCH_SET:
#ifndef rtc_epoch
		/*
		 * There were no RTC clocks before 1900.
		 */
		if (arg < 1900) {
			ret = -EINVAL;
			break;
		}
		if (!capable(CAP_SYS_TIME)) {
			ret = -EACCES;
			break;
		}
		rtc_epoch = arg;
		ret = 0;
#endif
		break;

	case RTC_EPOCH_READ:
		ret = put_user(rtc_epoch, (unsigned long __user *)uarg);
		break;

	case RTC_WKALM_SET:
		ret = copy_from_user(&alrm, uarg, sizeof(alrm));
		if (ret) {
			ret = -EFAULT;
			break;
		}
		ret = rtc_set_alarm(ops, &alrm);
		break;

	case RTC_WKALM_RD:
		ret = rtc_read_alarm(ops, &alrm);
		if (ret)
			break;
		ret = copy_to_user(uarg, &alrm, sizeof(alrm));
		if (ret)
			ret = -EFAULT;
		break;

	default:
		if (ops->ioctl)
			ret = ops->ioctl(cmd, arg);
		break;
	}
	return ret;
}

static int rtc_open(struct inode *inode, struct file *file)
{
	int ret;

	down(&rtc_sem);

	if (rtc_inuse) {
		ret = -EBUSY;
	} else if (!rtc_ops || !try_module_get(rtc_ops->owner)) {
		ret = -ENODEV;
	} else {
		file->private_data = rtc_ops;

		ret = rtc_ops->open ? rtc_ops->open() : 0;
		if (ret == 0) {
			spin_lock_irq(&rtc_lock);
			rtc_irq_data = 0;
			spin_unlock_irq(&rtc_lock);

			rtc_inuse = 1;
		}
	}
	up(&rtc_sem);

	return ret;
}

static int rtc_release(struct inode *inode, struct file *file)
{
	struct rtc_ops *ops = file->private_data;

	if (ops->release)
		ops->release();

	spin_lock_irq(&rtc_lock);
	rtc_irq_data = 0;
	spin_unlock_irq(&rtc_lock);

	module_put(rtc_ops->owner);
	rtc_inuse = 0;

	return 0;
}

static int rtc_fasync(int fd, struct file *file, int on)
{
	return fasync_helper(fd, file, on, &rtc_async_queue);
}

static struct file_operations rtc_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= rtc_read,
	.poll		= rtc_poll,
	.ioctl		= rtc_ioctl,
	.open		= rtc_open,
	.release	= rtc_release,
	.fasync		= rtc_fasync,
};

static struct miscdevice rtc_miscdev = {
	.minor		= RTC_MINOR,
	.name		= "rtc",
	.fops		= &rtc_fops,
};


static int rtc_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	struct rtc_ops *ops = data;
	struct rtc_wkalrm alrm;
	struct rtc_time tm;
	char *p = page;

	if (rtc_read_time(ops, &tm) == 0) {
		p += sprintf(p,
			"rtc_time\t: %02d:%02d:%02d\n"
			"rtc_date\t: %04d-%02d-%02d\n"
			"rtc_epoch\t: %04lu\n",
			tm.tm_hour, tm.tm_min, tm.tm_sec,
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			rtc_epoch);
	}

	if (rtc_read_alarm(ops, &alrm) == 0) {
		p += sprintf(p, "alrm_time\t: ");
		if ((unsigned int)alrm.time.tm_hour <= 24)
			p += sprintf(p, "%02d:", alrm.time.tm_hour);
		else
			p += sprintf(p, "**:");
		if ((unsigned int)alrm.time.tm_min <= 59)
			p += sprintf(p, "%02d:", alrm.time.tm_min);
		else
			p += sprintf(p, "**:");
		if ((unsigned int)alrm.time.tm_sec <= 59)
			p += sprintf(p, "%02d\n", alrm.time.tm_sec);
		else
			p += sprintf(p, "**\n");

		p += sprintf(p, "alrm_date\t: ");
		if ((unsigned int)alrm.time.tm_year <= 200)
			p += sprintf(p, "%04d-", alrm.time.tm_year + 1900);
		else
			p += sprintf(p, "****-");
		if ((unsigned int)alrm.time.tm_mon <= 11)
			p += sprintf(p, "%02d-", alrm.time.tm_mon + 1);
		else
			p += sprintf(p, "**-");
		if ((unsigned int)alrm.time.tm_mday <= 31)
			p += sprintf(p, "%02d\n", alrm.time.tm_mday);
		else
			p += sprintf(p, "**\n");
		p += sprintf(p, "alrm_wakeup\t: %s\n",
			     alrm.enabled ? "yes" : "no");
		p += sprintf(p, "alrm_pending\t: %s\n",
			     alrm.pending ? "yes" : "no");
	}

	if (ops->proc)
		p += ops->proc(p);

	return p - page;
}

int register_rtc(struct rtc_ops *ops)
{
	int ret = -EBUSY;

	down(&rtc_sem);
	if (rtc_ops == NULL) {
		rtc_ops = ops;

		ret = misc_register(&rtc_miscdev);
		if (ret == 0)
			create_proc_read_entry("driver/rtc", 0, NULL,
					       rtc_read_proc, ops);
	}
	up(&rtc_sem);

	return ret;
}
EXPORT_SYMBOL(register_rtc);

void unregister_rtc(struct rtc_ops *rtc)
{
	down(&rtc_sem);
	if (rtc == rtc_ops) {
		remove_proc_entry("driver/rtc", NULL);
		misc_deregister(&rtc_miscdev);
		rtc_ops = NULL;
	}
	up(&rtc_sem);
}
EXPORT_SYMBOL(unregister_rtc);

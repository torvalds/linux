/*
 *	Driver for the SGS-Thomson M48T35 Timekeeper RAM chip
 *
 *	Real Time Clock interface for Linux
 *
 *	TODO: Implement periodic interrupts.
 *
 *	Copyright (C) 2000 Silicon Graphics, Inc.
 *	Written by Ulf Carlsson (ulfc@engr.sgi.com)
 *
 *	Based on code written by Paul Gortmaker.
 *
 *	This driver allows use of the real time clock (built into
 *	nearly all computers) from user space. It exports the /dev/rtc
 *	interface supporting various ioctl() and also the /proc/rtc
 *	pseudo-file for status information.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */

#define RTC_VERSION		"1.09b"

#include <linux/bcd.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/smp_lock.h>

#include <asm/m48t35.h>
#include <asm/sn/ioc3.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/sn0/ip27.h>
#include <asm/sn/sn0/hub.h>
#include <asm/sn/sn_private.h>

static int rtc_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg);

static int rtc_read_proc(char *page, char **start, off_t off,
                         int count, int *eof, void *data);

static void get_rtc_time(struct rtc_time *rtc_tm);

/*
 *	Bits in rtc_status. (6 bits of room for future expansion)
 */

#define RTC_IS_OPEN		0x01	/* means /dev/rtc is in use	*/
#define RTC_TIMER_ON		0x02	/* missed irq timer active	*/

static unsigned char rtc_status;	/* bitmapped status byte.	*/
static unsigned long rtc_freq;	/* Current periodic IRQ rate	*/
static struct m48t35_rtc *rtc;

/*
 *	If this driver ever becomes modularised, it will be really nice
 *	to make the epoch retain its value across module reload...
 */

static unsigned long epoch = 1970;	/* year corresponding to 0x00	*/

static const unsigned char days_in_mo[] =
{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static int rtc_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		     unsigned long arg)
{

	struct rtc_time wtime;

	switch (cmd) {
	case RTC_RD_TIME:	/* Read the time/date from RTC	*/
	{
		get_rtc_time(&wtime);
		break;
	}
	case RTC_SET_TIME:	/* Set the RTC */
	{
		struct rtc_time rtc_tm;
		unsigned char mon, day, hrs, min, sec, leap_yr;
		unsigned int yrs;

		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		if (copy_from_user(&rtc_tm, (struct rtc_time*)arg,
				   sizeof(struct rtc_time)))
			return -EFAULT;

		yrs = rtc_tm.tm_year + 1900;
		mon = rtc_tm.tm_mon + 1;   /* tm_mon starts at zero */
		day = rtc_tm.tm_mday;
		hrs = rtc_tm.tm_hour;
		min = rtc_tm.tm_min;
		sec = rtc_tm.tm_sec;

		if (yrs < 1970)
			return -EINVAL;

		leap_yr = ((!(yrs % 4) && (yrs % 100)) || !(yrs % 400));

		if ((mon > 12) || (day == 0))
			return -EINVAL;

		if (day > (days_in_mo[mon] + ((mon == 2) && leap_yr)))
			return -EINVAL;

		if ((hrs >= 24) || (min >= 60) || (sec >= 60))
			return -EINVAL;

		if ((yrs -= epoch) > 255)    /* They are unsigned */
			return -EINVAL;

		if (yrs > 169)
			return -EINVAL;

		if (yrs >= 100)
			yrs -= 100;

		sec = BIN2BCD(sec);
		min = BIN2BCD(min);
		hrs = BIN2BCD(hrs);
		day = BIN2BCD(day);
		mon = BIN2BCD(mon);
		yrs = BIN2BCD(yrs);

		spin_lock_irq(&rtc_lock);
		rtc->control |= M48T35_RTC_SET;
		rtc->year = yrs;
		rtc->month = mon;
		rtc->date = day;
		rtc->hour = hrs;
		rtc->min = min;
		rtc->sec = sec;
		rtc->control &= ~M48T35_RTC_SET;
		spin_unlock_irq(&rtc_lock);

		return 0;
	}
	default:
		return -EINVAL;
	}
	return copy_to_user((void *)arg, &wtime, sizeof wtime) ? -EFAULT : 0;
}

/*
 *	We enforce only one user at a time here with the open/close.
 *	Also clear the previous interrupt data on an open, and clean
 *	up things on a close.
 */

static int rtc_open(struct inode *inode, struct file *file)
{
	spin_lock_irq(&rtc_lock);

	if (rtc_status & RTC_IS_OPEN) {
		spin_unlock_irq(&rtc_lock);
		return -EBUSY;
	}

	rtc_status |= RTC_IS_OPEN;
	spin_unlock_irq(&rtc_lock);

	return 0;
}

static int rtc_release(struct inode *inode, struct file *file)
{
	/*
	 * Turn off all interrupts once the device is no longer
	 * in use, and clear the data.
	 */

	spin_lock_irq(&rtc_lock);
	rtc_status &= ~RTC_IS_OPEN;
	spin_unlock_irq(&rtc_lock);

	return 0;
}

/*
 *	The various file operations we support.
 */

static const struct file_operations rtc_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= rtc_ioctl,
	.open		= rtc_open,
	.release	= rtc_release,
};

static struct miscdevice rtc_dev=
{
	RTC_MINOR,
	"rtc",
	&rtc_fops
};

static int __init rtc_init(void)
{
	rtc = (struct m48t35_rtc *)
	(KL_CONFIG_CH_CONS_INFO(master_nasid)->memory_base + IOC3_BYTEBUS_DEV0);

	printk(KERN_INFO "Real Time Clock Driver v%s\n", RTC_VERSION);
	if (misc_register(&rtc_dev)) {
		printk(KERN_ERR "rtc: cannot register misc device.\n");
		return -ENODEV;
	}
	if (!create_proc_read_entry("driver/rtc", 0, NULL, rtc_read_proc, NULL)) {
		printk(KERN_ERR "rtc: cannot create /proc/rtc.\n");
		misc_deregister(&rtc_dev);
		return -ENOENT;
	}

	rtc_freq = 1024;

	return 0;
}

static void __exit rtc_exit (void)
{
	/* interrupts and timer disabled at this point by rtc_release */

	remove_proc_entry ("rtc", NULL);
	misc_deregister(&rtc_dev);
}

module_init(rtc_init);
module_exit(rtc_exit);

/*
 *	Info exported via "/proc/rtc".
 */

static int rtc_get_status(char *buf)
{
	char *p;
	struct rtc_time tm;

	/*
	 * Just emulate the standard /proc/rtc
	 */

	p = buf;

	get_rtc_time(&tm);

	/*
	 * There is no way to tell if the luser has the RTC set for local
	 * time or for Universal Standard Time (GMT). Probably local though.
	 */
	p += sprintf(p,
		     "rtc_time\t: %02d:%02d:%02d\n"
		     "rtc_date\t: %04d-%02d-%02d\n"
	 	     "rtc_epoch\t: %04lu\n"
		     "24hr\t\t: yes\n",
		     tm.tm_hour, tm.tm_min, tm.tm_sec,
		     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, epoch);

	return  p - buf;
}

static int rtc_read_proc(char *page, char **start, off_t off,
                                 int count, int *eof, void *data)
{
        int len = rtc_get_status(page);
        if (len <= off+count) *eof = 1;
        *start = page + off;
        len -= off;
        if (len>count) len = count;
        if (len<0) len = 0;
        return len;
}

static void get_rtc_time(struct rtc_time *rtc_tm)
{
	/*
	 * Do we need to wait for the last update to finish?
	 */

	/*
	 * Only the values that we read from the RTC are set. We leave
	 * tm_wday, tm_yday and tm_isdst untouched. Even though the
	 * RTC has RTC_DAY_OF_WEEK, we ignore it, as it is only updated
	 * by the RTC when initially set to a non-zero value.
	 */
	spin_lock_irq(&rtc_lock);
	rtc->control |= M48T35_RTC_READ;
	rtc_tm->tm_sec = rtc->sec;
	rtc_tm->tm_min = rtc->min;
	rtc_tm->tm_hour = rtc->hour;
	rtc_tm->tm_mday = rtc->date;
	rtc_tm->tm_mon = rtc->month;
	rtc_tm->tm_year = rtc->year;
	rtc->control &= ~M48T35_RTC_READ;
	spin_unlock_irq(&rtc_lock);

	rtc_tm->tm_sec = BCD2BIN(rtc_tm->tm_sec);
	rtc_tm->tm_min = BCD2BIN(rtc_tm->tm_min);
	rtc_tm->tm_hour = BCD2BIN(rtc_tm->tm_hour);
	rtc_tm->tm_mday = BCD2BIN(rtc_tm->tm_mday);
	rtc_tm->tm_mon = BCD2BIN(rtc_tm->tm_mon);
	rtc_tm->tm_year = BCD2BIN(rtc_tm->tm_year);

	/*
	 * Account for differences between how the RTC uses the values
	 * and how they are defined in a struct rtc_time;
	 */
	if ((rtc_tm->tm_year += (epoch - 1900)) <= 69)
		rtc_tm->tm_year += 100;

	rtc_tm->tm_mon--;
}

/*
 * PCF8563 RTC
 *
 * From Phillips' datasheet:
 *
 * The PCF8563 is a CMOS real-time clock/calendar optimized for low power
 * consumption. A programmable clock output, interrupt output and voltage
 * low detector are also provided. All address and data are transferred
 * serially via two-line bidirectional I2C-bus. Maximum bus speed is
 * 400 kbits/s. The built-in word address register is incremented
 * automatically after each written or read byte.
 *
 * Copyright (c) 2002-2007, Axis Communications AB
 * All rights reserved.
 *
 * Author: Tobias Anderberg <tobiasa@axis.com>.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/smp_lock.h>
#include <linux/delay.h>
#include <linux/bcd.h>
#include <linux/mutex.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/rtc.h>

#include "i2c.h"

#define PCF8563_MAJOR	121	/* Local major number. */
#define DEVICE_NAME	"rtc"	/* Name which is registered in /proc/devices. */
#define PCF8563_NAME	"PCF8563"
#define DRIVER_VERSION	"$Revision: 1.17 $"

/* Two simple wrapper macros, saves a few keystrokes. */
#define rtc_read(x) i2c_readreg(RTC_I2C_READ, x)
#define rtc_write(x,y) i2c_writereg(RTC_I2C_WRITE, x, y)

static DEFINE_MUTEX(rtc_lock); /* Protect state etc */

static const unsigned char days_in_month[] =
	{ 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static long pcf8563_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

/* Cache VL bit value read at driver init since writing the RTC_SECOND
 * register clears the VL status.
 */
static int voltage_low;

static const struct file_operations pcf8563_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl = pcf8563_unlocked_ioctl,
};

unsigned char
pcf8563_readreg(int reg)
{
	unsigned char res = rtc_read(reg);

	/* The PCF8563 does not return 0 for unimplemented bits. */
	switch (reg) {
		case RTC_SECONDS:
		case RTC_MINUTES:
			res &= 0x7F;
			break;
		case RTC_HOURS:
		case RTC_DAY_OF_MONTH:
			res &= 0x3F;
			break;
		case RTC_WEEKDAY:
			res &= 0x07;
			break;
		case RTC_MONTH:
			res &= 0x1F;
			break;
		case RTC_CONTROL1:
			res &= 0xA8;
			break;
		case RTC_CONTROL2:
			res &= 0x1F;
			break;
		case RTC_CLOCKOUT_FREQ:
		case RTC_TIMER_CONTROL:
			res &= 0x83;
			break;
	}
	return res;
}

void
pcf8563_writereg(int reg, unsigned char val)
{
	rtc_write(reg, val);
}

void
get_rtc_time(struct rtc_time *tm)
{
	tm->tm_sec  = rtc_read(RTC_SECONDS);
	tm->tm_min  = rtc_read(RTC_MINUTES);
	tm->tm_hour = rtc_read(RTC_HOURS);
	tm->tm_mday = rtc_read(RTC_DAY_OF_MONTH);
	tm->tm_wday = rtc_read(RTC_WEEKDAY);
	tm->tm_mon  = rtc_read(RTC_MONTH);
	tm->tm_year = rtc_read(RTC_YEAR);

	if (tm->tm_sec & 0x80) {
		printk(KERN_ERR "%s: RTC Voltage Low - reliable date/time "
		       "information is no longer guaranteed!\n", PCF8563_NAME);
	}

	tm->tm_year  = bcd2bin(tm->tm_year) +
		       ((tm->tm_mon & 0x80) ? 100 : 0);
	tm->tm_sec  &= 0x7F;
	tm->tm_min  &= 0x7F;
	tm->tm_hour &= 0x3F;
	tm->tm_mday &= 0x3F;
	tm->tm_wday &= 0x07; /* Not coded in BCD. */
	tm->tm_mon  &= 0x1F;

	tm->tm_sec = bcd2bin(tm->tm_sec);
	tm->tm_min = bcd2bin(tm->tm_min);
	tm->tm_hour = bcd2bin(tm->tm_hour);
	tm->tm_mday = bcd2bin(tm->tm_mday);
	tm->tm_mon = bcd2bin(tm->tm_mon);
	tm->tm_mon--; /* Month is 1..12 in RTC but 0..11 in linux */
}

int __init
pcf8563_init(void)
{
	static int res;
	static int first = 1;

	if (!first)
		return res;
	first = 0;

	/* Initiate the i2c protocol. */
	res = i2c_init();
	if (res < 0) {
		printk(KERN_CRIT "pcf8563_init: Failed to init i2c.\n");
		return res;
	}

	/*
	 * First of all we need to reset the chip. This is done by
	 * clearing control1, control2 and clk freq and resetting
	 * all alarms.
	 */
	if (rtc_write(RTC_CONTROL1, 0x00) < 0)
		goto err;

	if (rtc_write(RTC_CONTROL2, 0x00) < 0)
		goto err;

	if (rtc_write(RTC_CLOCKOUT_FREQ, 0x00) < 0)
		goto err;

	if (rtc_write(RTC_TIMER_CONTROL, 0x03) < 0)
		goto err;

	/* Reset the alarms. */
	if (rtc_write(RTC_MINUTE_ALARM, 0x80) < 0)
		goto err;

	if (rtc_write(RTC_HOUR_ALARM, 0x80) < 0)
		goto err;

	if (rtc_write(RTC_DAY_ALARM, 0x80) < 0)
		goto err;

	if (rtc_write(RTC_WEEKDAY_ALARM, 0x80) < 0)
		goto err;

	/* Check for low voltage, and warn about it. */
	if (rtc_read(RTC_SECONDS) & 0x80) {
		voltage_low = 1;
		printk(KERN_WARNING "%s: RTC Voltage Low - reliable "
		       "date/time information is no longer guaranteed!\n",
		       PCF8563_NAME);
	}

	return res;

err:
	printk(KERN_INFO "%s: Error initializing chip.\n", PCF8563_NAME);
	res = -1;
	return res;
}

void __exit
pcf8563_exit(void)
{
	unregister_chrdev(PCF8563_MAJOR, DEVICE_NAME);
}

/*
 * ioctl calls for this driver. Why return -ENOTTY upon error? Because
 * POSIX says so!
 */
static int pcf8563_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	/* Some sanity checks. */
	if (_IOC_TYPE(cmd) != RTC_MAGIC)
		return -ENOTTY;

	if (_IOC_NR(cmd) > RTC_MAX_IOCTL)
		return -ENOTTY;

	switch (cmd) {
	case RTC_RD_TIME:
	{
		struct rtc_time tm;

		mutex_lock(&rtc_lock);
		memset(&tm, 0, sizeof tm);
		get_rtc_time(&tm);

		if (copy_to_user((struct rtc_time *) arg, &tm,
				 sizeof tm)) {
			mutex_unlock(&rtc_lock);
			return -EFAULT;
		}

		mutex_unlock(&rtc_lock);

		return 0;
	}
	case RTC_SET_TIME:
	{
		int leap;
		int year;
		int century;
		struct rtc_time tm;

		memset(&tm, 0, sizeof tm);
		if (!capable(CAP_SYS_TIME))
			return -EPERM;

		if (copy_from_user(&tm, (struct rtc_time *) arg,
				   sizeof tm))
			return -EFAULT;

		/* Convert from struct tm to struct rtc_time. */
		tm.tm_year += 1900;
		tm.tm_mon += 1;

		/*
		 * Check if tm.tm_year is a leap year. A year is a leap
		 * year if it is divisible by 4 but not 100, except
		 * that years divisible by 400 _are_ leap years.
		 */
		year = tm.tm_year;
		leap = (tm.tm_mon == 2) &&
			((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);

		/* Perform some sanity checks. */
		if ((tm.tm_year < 1970) ||
		    (tm.tm_mon > 12) ||
		    (tm.tm_mday == 0) ||
		    (tm.tm_mday > days_in_month[tm.tm_mon] + leap) ||
		    (tm.tm_wday >= 7) ||
		    (tm.tm_hour >= 24) ||
		    (tm.tm_min >= 60) ||
		    (tm.tm_sec >= 60))
			return -EINVAL;

		century = (tm.tm_year >= 2000) ? 0x80 : 0;
		tm.tm_year = tm.tm_year % 100;

		tm.tm_year = bin2bcd(tm.tm_year);
		tm.tm_mon = bin2bcd(tm.tm_mon);
		tm.tm_mday = bin2bcd(tm.tm_mday);
		tm.tm_hour = bin2bcd(tm.tm_hour);
		tm.tm_min = bin2bcd(tm.tm_min);
		tm.tm_sec = bin2bcd(tm.tm_sec);
		tm.tm_mon |= century;

		mutex_lock(&rtc_lock);

		rtc_write(RTC_YEAR, tm.tm_year);
		rtc_write(RTC_MONTH, tm.tm_mon);
		rtc_write(RTC_WEEKDAY, tm.tm_wday); /* Not coded in BCD. */
		rtc_write(RTC_DAY_OF_MONTH, tm.tm_mday);
		rtc_write(RTC_HOURS, tm.tm_hour);
		rtc_write(RTC_MINUTES, tm.tm_min);
		rtc_write(RTC_SECONDS, tm.tm_sec);

		mutex_unlock(&rtc_lock);

		return 0;
	}
	case RTC_VL_READ:
		if (voltage_low)
			printk(KERN_ERR "%s: RTC Voltage Low - "
			       "reliable date/time information is no "
			       "longer guaranteed!\n", PCF8563_NAME);

		if (copy_to_user((int *) arg, &voltage_low, sizeof(int)))
			return -EFAULT;
		return 0;

	case RTC_VL_CLR:
	{
		/* Clear the VL bit in the seconds register in case
		 * the time has not been set already (which would
		 * have cleared it). This does not really matter
		 * because of the cached voltage_low value but do it
		 * anyway for consistency. */

		int ret = rtc_read(RTC_SECONDS);

		rtc_write(RTC_SECONDS, (ret & 0x7F));

		/* Clear the cached value. */
		voltage_low = 0;

		return 0;
	}
	default:
		return -ENOTTY;
	}

	return 0;
}

static long pcf8563_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret;

	lock_kernel();
	return pcf8563_ioctl(filp, cmd, arg);
	unlock_kernel();

	return ret;
}

static int __init pcf8563_register(void)
{
	if (pcf8563_init() < 0) {
		printk(KERN_INFO "%s: Unable to initialize Real-Time Clock "
		       "Driver, %s\n", PCF8563_NAME, DRIVER_VERSION);
		return -1;
	}

	if (register_chrdev(PCF8563_MAJOR, DEVICE_NAME, &pcf8563_fops) < 0) {
		printk(KERN_INFO "%s: Unable to get major numer %d for RTC "
		       "device.\n", PCF8563_NAME, PCF8563_MAJOR);
		return -1;
	}

	printk(KERN_INFO "%s Real-Time Clock Driver, %s\n", PCF8563_NAME,
	       DRIVER_VERSION);

	/* Check for low voltage, and warn about it. */
	if (voltage_low) {
		printk(KERN_WARNING "%s: RTC Voltage Low - reliable date/time "
		       "information is no longer guaranteed!\n", PCF8563_NAME);
	}

	return 0;
}

module_init(pcf8563_register);
module_exit(pcf8563_exit);

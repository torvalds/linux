/*
 * PCF8563 RTC
 *
 * From Phillips' datasheet:
 *
 * The PCF8563 is a CMOS real-time clock/calendar optimized for low power
 * consumption. A programmable clock output, interupt output and voltage
 * low detector are also provided. All address and data are transferred
 * serially via two-line bidirectional I2C-bus. Maximum bus speed is
 * 400 kbits/s. The built-in word address register is incremented
 * automatically after each written or read byte.
 *
 * Copyright (c) 2002-2003, Axis Communications AB
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
#include <linux/delay.h>
#include <linux/bcd.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/rtc.h>

#include "i2c.h"

#define PCF8563_MAJOR	121	/* Local major number. */
#define DEVICE_NAME	"rtc"	/* Name which is registered in /proc/devices. */
#define PCF8563_NAME	"PCF8563"
#define DRIVER_VERSION	"$Revision: 1.1 $"

/* Two simple wrapper macros, saves a few keystrokes. */
#define rtc_read(x) i2c_readreg(RTC_I2C_READ, x)
#define rtc_write(x,y) i2c_writereg(RTC_I2C_WRITE, x, y)

static const unsigned char days_in_month[] =
	{ 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

int pcf8563_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
int pcf8563_open(struct inode *, struct file *);
int pcf8563_release(struct inode *, struct file *);

static const struct file_operations pcf8563_fops = {
	.owner =	THIS_MODULE,
	.ioctl =	pcf8563_ioctl,
	.open =		pcf8563_open,
	.release =	pcf8563_release,
};

unsigned char
pcf8563_readreg(int reg)
{
	unsigned char res = rtc_read(reg);

	/* The PCF8563 does not return 0 for unimplemented bits */
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
#ifdef CONFIG_ETRAX_RTC_READONLY
	if (reg == RTC_CONTROL1 || (reg >= RTC_SECONDS && reg <= RTC_YEAR))
		return;
#endif

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

	if (tm->tm_sec & 0x80)
		printk(KERN_WARNING "%s: RTC Voltage Low - reliable date/time "
		       "information is no longer guaranteed!\n", PCF8563_NAME);

	tm->tm_year  = BCD_TO_BIN(tm->tm_year) + ((tm->tm_mon & 0x80) ? 100 : 0);
	tm->tm_sec  &= 0x7F;
	tm->tm_min  &= 0x7F;
	tm->tm_hour &= 0x3F;
	tm->tm_mday &= 0x3F;
	tm->tm_wday &= 0x07; /* Not coded in BCD. */
	tm->tm_mon  &= 0x1F;

	BCD_TO_BIN(tm->tm_sec);
	BCD_TO_BIN(tm->tm_min);
	BCD_TO_BIN(tm->tm_hour);
	BCD_TO_BIN(tm->tm_mday);
	BCD_TO_BIN(tm->tm_mon);
	tm->tm_mon--; /* Month is 1..12 in RTC but 0..11 in linux */
}

int __init
pcf8563_init(void)
{
	/* Initiate the i2c protocol. */
	i2c_init();

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

	if (register_chrdev(PCF8563_MAJOR, DEVICE_NAME, &pcf8563_fops) < 0) {
		printk(KERN_INFO "%s: Unable to get major number %d for RTC device.\n",
		       PCF8563_NAME, PCF8563_MAJOR);
		return -1;
	}

	printk(KERN_INFO "%s Real-Time Clock Driver, %s\n", PCF8563_NAME, DRIVER_VERSION);

	/* Check for low voltage, and warn about it.. */
	if (rtc_read(RTC_SECONDS) & 0x80)
		printk(KERN_WARNING "%s: RTC Voltage Low - reliable date/time "
		       "information is no longer guaranteed!\n", PCF8563_NAME);

	return 0;

err:
	printk(KERN_INFO "%s: Error initializing chip.\n", PCF8563_NAME);
	return -1;
}

void __exit
pcf8563_exit(void)
{
	if (unregister_chrdev(PCF8563_MAJOR, DEVICE_NAME) < 0) {
		printk(KERN_INFO "%s: Unable to unregister device.\n", PCF8563_NAME);
	}
}

/*
 * ioctl calls for this driver. Why return -ENOTTY upon error? Because
 * POSIX says so!
 */
int
pcf8563_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
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

			memset(&tm, 0, sizeof (struct rtc_time));
			get_rtc_time(&tm);

			if (copy_to_user((struct rtc_time *) arg, &tm, sizeof tm)) {
				return -EFAULT;
			}

			return 0;
		}

		case RTC_SET_TIME:
		{
#ifdef CONFIG_ETRAX_RTC_READONLY
			return -EPERM;
#else
			int leap;
			int year;
			int century;
			struct rtc_time tm;

			if (!capable(CAP_SYS_TIME))
				return -EPERM;

			if (copy_from_user(&tm, (struct rtc_time *) arg, sizeof tm))
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
			leap = (tm.tm_mon == 2) && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);

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

			BIN_TO_BCD(tm.tm_year);
			BIN_TO_BCD(tm.tm_mday);
			BIN_TO_BCD(tm.tm_hour);
			BIN_TO_BCD(tm.tm_min);
			BIN_TO_BCD(tm.tm_sec);
			tm.tm_mon |= century;

			rtc_write(RTC_YEAR, tm.tm_year);
			rtc_write(RTC_MONTH, tm.tm_mon);
			rtc_write(RTC_WEEKDAY, tm.tm_wday); /* Not coded in BCD. */
			rtc_write(RTC_DAY_OF_MONTH, tm.tm_mday);
			rtc_write(RTC_HOURS, tm.tm_hour);
			rtc_write(RTC_MINUTES, tm.tm_min);
			rtc_write(RTC_SECONDS, tm.tm_sec);

			return 0;
#endif /* !CONFIG_ETRAX_RTC_READONLY */
		}

		case RTC_VLOW_RD:
		{
			int vl_bit = 0;

			if (rtc_read(RTC_SECONDS) & 0x80) {
				vl_bit = 1;
				printk(KERN_WARNING "%s: RTC Voltage Low - reliable "
				       "date/time information is no longer guaranteed!\n",
				       PCF8563_NAME);
			}
			if (copy_to_user((int *) arg, &vl_bit, sizeof(int)))
				return -EFAULT;

			return 0;
		}

		case RTC_VLOW_SET:
		{
			/* Clear the VL bit in the seconds register */
			int ret = rtc_read(RTC_SECONDS);

			rtc_write(RTC_SECONDS, (ret & 0x7F));

			return 0;
		}

		default:
			return -ENOTTY;
	}

	return 0;
}

int
pcf8563_open(struct inode *inode, struct file *filp)
{
	return 0;
}

int
pcf8563_release(struct inode *inode, struct file *filp)
{
	return 0;
}

module_init(pcf8563_init);
module_exit(pcf8563_exit);

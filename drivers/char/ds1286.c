/*
 * DS1286 Real Time Clock interface for Linux
 *
 * Copyright (C) 1998, 1999, 2000 Ralf Baechle
 *
 * Based on code written by Paul Gortmaker.
 *
 * This driver allows use of the real time clock (built into nearly all
 * computers) from user space. It exports the /dev/rtc interface supporting
 * various ioctl() and also the /proc/rtc pseudo-file for status
 * information.
 *
 * The ioctls can be used to set the interrupt behaviour and generation rate
 * from the RTC via IRQ 8. Then the /dev/rtc interface can be used to make
 * use of these timer interrupts, be they interval or alarm based.
 *
 * The /dev/rtc interface will block on reads until an interrupt has been
 * received. If a RTC interrupt has already happened, it will output an
 * unsigned long and then block. The output value contains the interrupt
 * status in the low byte and the number of interrupts since the last read
 * in the remaining high bytes. The /dev/rtc interface can also be used with
 * the select(2) call.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#include <linux/ds1286.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/rtc.h>
#include <linux/spinlock.h>
#include <linux/bcd.h>
#include <linux/proc_fs.h>
#include <linux/jiffies.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#define DS1286_VERSION		"1.0"

/*
 *	We sponge a minor off of the misc major. No need slurping
 *	up another valuable major dev number for this. If you add
 *	an ioctl, make sure you don't conflict with SPARC's RTC
 *	ioctls.
 */

static DECLARE_WAIT_QUEUE_HEAD(ds1286_wait);

static ssize_t ds1286_read(struct file *file, char *buf,
			size_t count, loff_t *ppos);

static int ds1286_ioctl(struct inode *inode, struct file *file,
                        unsigned int cmd, unsigned long arg);

static unsigned int ds1286_poll(struct file *file, poll_table *wait);

static void ds1286_get_alm_time (struct rtc_time *alm_tm);
static void ds1286_get_time(struct rtc_time *rtc_tm);
static int ds1286_set_time(struct rtc_time *rtc_tm);

static inline unsigned char ds1286_is_updating(void);

static DEFINE_SPINLOCK(ds1286_lock);

static int ds1286_read_proc(char *page, char **start, off_t off,
                            int count, int *eof, void *data);

/*
 *	Bits in rtc_status. (7 bits of room for future expansion)
 */

#define RTC_IS_OPEN		0x01	/* means /dev/rtc is in use	*/
#define RTC_TIMER_ON		0x02	/* missed irq timer active	*/

static unsigned char ds1286_status;	/* bitmapped status byte.	*/

static unsigned char days_in_mo[] = {
	0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 *	Now all the various file operations that we export.
 */

static ssize_t ds1286_read(struct file *file, char *buf,
                           size_t count, loff_t *ppos)
{
	return -EIO;
}

static int ds1286_ioctl(struct inode *inode, struct file *file,
                        unsigned int cmd, unsigned long arg)
{
	struct rtc_time wtime;

	switch (cmd) {
	case RTC_AIE_OFF:	/* Mask alarm int. enab. bit	*/
	{
		unsigned long flags;
		unsigned char val;

		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		spin_lock_irqsave(&ds1286_lock, flags);
		val = rtc_read(RTC_CMD);
		val |=  RTC_TDM;
		rtc_write(val, RTC_CMD);
		spin_unlock_irqrestore(&ds1286_lock, flags);

		return 0;
	}
	case RTC_AIE_ON:	/* Allow alarm interrupts.	*/
	{
		unsigned long flags;
		unsigned char val;

		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		spin_lock_irqsave(&ds1286_lock, flags);
		val = rtc_read(RTC_CMD);
		val &=  ~RTC_TDM;
		rtc_write(val, RTC_CMD);
		spin_unlock_irqrestore(&ds1286_lock, flags);

		return 0;
	}
	case RTC_WIE_OFF:	/* Mask watchdog int. enab. bit	*/
	{
		unsigned long flags;
		unsigned char val;

		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		spin_lock_irqsave(&ds1286_lock, flags);
		val = rtc_read(RTC_CMD);
		val |= RTC_WAM;
		rtc_write(val, RTC_CMD);
		spin_unlock_irqrestore(&ds1286_lock, flags);

		return 0;
	}
	case RTC_WIE_ON:	/* Allow watchdog interrupts.	*/
	{
		unsigned long flags;
		unsigned char val;

		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		spin_lock_irqsave(&ds1286_lock, flags);
		val = rtc_read(RTC_CMD);
		val &= ~RTC_WAM;
		rtc_write(val, RTC_CMD);
		spin_unlock_irqrestore(&ds1286_lock, flags);

		return 0;
	}
	case RTC_ALM_READ:	/* Read the present alarm time */
	{
		/*
		 * This returns a struct rtc_time. Reading >= 0xc0
		 * means "don't care" or "match all". Only the tm_hour,
		 * tm_min, and tm_sec values are filled in.
		 */

		memset(&wtime, 0, sizeof(wtime));
		ds1286_get_alm_time(&wtime);
		break;
	}
	case RTC_ALM_SET:	/* Store a time into the alarm */
	{
		/*
		 * This expects a struct rtc_time. Writing 0xff means
		 * "don't care" or "match all". Only the tm_hour,
		 * tm_min and tm_sec are used.
		 */
		unsigned char hrs, min, sec;
		struct rtc_time alm_tm;

		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		if (copy_from_user(&alm_tm, (struct rtc_time*)arg,
				   sizeof(struct rtc_time)))
			return -EFAULT;

		hrs = alm_tm.tm_hour;
		min = alm_tm.tm_min;
		sec = alm_tm.tm_sec;

		if (hrs >= 24)
			hrs = 0xff;

		if (min >= 60)
			min = 0xff;

		if (sec != 0)
			return -EINVAL;

		min = BIN2BCD(min);
		min = BIN2BCD(hrs);

		spin_lock(&ds1286_lock);
		rtc_write(hrs, RTC_HOURS_ALARM);
		rtc_write(min, RTC_MINUTES_ALARM);
		spin_unlock(&ds1286_lock);

		return 0;
	}
	case RTC_RD_TIME:	/* Read the time/date from RTC	*/
	{
		memset(&wtime, 0, sizeof(wtime));
		ds1286_get_time(&wtime);
		break;
	}
	case RTC_SET_TIME:	/* Set the RTC */
	{
		struct rtc_time rtc_tm;

		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		if (copy_from_user(&rtc_tm, (struct rtc_time*)arg,
				   sizeof(struct rtc_time)))
			return -EFAULT;

		return ds1286_set_time(&rtc_tm);
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

static int ds1286_open(struct inode *inode, struct file *file)
{
	spin_lock_irq(&ds1286_lock);

	if (ds1286_status & RTC_IS_OPEN)
		goto out_busy;

	ds1286_status |= RTC_IS_OPEN;

	spin_unlock_irq(&ds1286_lock);
	return 0;

out_busy:
	spin_lock_irq(&ds1286_lock);
	return -EBUSY;
}

static int ds1286_release(struct inode *inode, struct file *file)
{
	ds1286_status &= ~RTC_IS_OPEN;

	return 0;
}

static unsigned int ds1286_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &ds1286_wait, wait);

	return 0;
}

/*
 *	The various file operations we support.
 */

static const struct file_operations ds1286_fops = {
	.llseek		= no_llseek,
	.read		= ds1286_read,
	.poll		= ds1286_poll,
	.ioctl		= ds1286_ioctl,
	.open		= ds1286_open,
	.release	= ds1286_release,
};

static struct miscdevice ds1286_dev=
{
	.minor	= RTC_MINOR,
	.name	= "rtc",
	.fops	= &ds1286_fops,
};

static int __init ds1286_init(void)
{
	int err;

	printk(KERN_INFO "DS1286 Real Time Clock Driver v%s\n", DS1286_VERSION);

	err = misc_register(&ds1286_dev);
	if (err)
		goto out;

	if (!create_proc_read_entry("driver/rtc", 0, 0, ds1286_read_proc, NULL)) {
		err = -ENOMEM;

		goto out_deregister;
	}

	return 0;

out_deregister:
	misc_deregister(&ds1286_dev);

out:
	return err;
}

static void __exit ds1286_exit(void)
{
	remove_proc_entry("driver/rtc", NULL);
	misc_deregister(&ds1286_dev);
}

static char *days[] = {
	"***", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

/*
 *	Info exported via "/proc/rtc".
 */
static int ds1286_proc_output(char *buf)
{
	char *p, *s;
	struct rtc_time tm;
	unsigned char hundredth, month, cmd, amode;

	p = buf;

	ds1286_get_time(&tm);
	hundredth = rtc_read(RTC_HUNDREDTH_SECOND);
	BCD_TO_BIN(hundredth);

	p += sprintf(p,
	             "rtc_time\t: %02d:%02d:%02d.%02d\n"
	             "rtc_date\t: %04d-%02d-%02d\n",
		     tm.tm_hour, tm.tm_min, tm.tm_sec, hundredth,
		     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);

	/*
	 * We implicitly assume 24hr mode here. Alarm values >= 0xc0 will
	 * match any value for that particular field. Values that are
	 * greater than a valid time, but less than 0xc0 shouldn't appear.
	 */
	ds1286_get_alm_time(&tm);
	p += sprintf(p, "alarm\t\t: %s ", days[tm.tm_wday]);
	if (tm.tm_hour <= 24)
		p += sprintf(p, "%02d:", tm.tm_hour);
	else
		p += sprintf(p, "**:");

	if (tm.tm_min <= 59)
		p += sprintf(p, "%02d\n", tm.tm_min);
	else
		p += sprintf(p, "**\n");

	month = rtc_read(RTC_MONTH);
	p += sprintf(p,
	             "oscillator\t: %s\n"
	             "square_wave\t: %s\n",
	             (month & RTC_EOSC) ? "disabled" : "enabled",
	             (month & RTC_ESQW) ? "disabled" : "enabled");

	amode = ((rtc_read(RTC_MINUTES_ALARM) & 0x80) >> 5) |
	        ((rtc_read(RTC_HOURS_ALARM) & 0x80) >> 6) |
	        ((rtc_read(RTC_DAY_ALARM) & 0x80) >> 7);
	if (amode == 7)      s = "each minute";
	else if (amode == 3) s = "minutes match";
	else if (amode == 1) s = "hours and minutes match";
	else if (amode == 0) s = "days, hours and minutes match";
	else                 s = "invalid";
	p += sprintf(p, "alarm_mode\t: %s\n", s);

	cmd = rtc_read(RTC_CMD);
	p += sprintf(p,
	             "alarm_enable\t: %s\n"
	             "wdog_alarm\t: %s\n"
	             "alarm_mask\t: %s\n"
	             "wdog_alarm_mask\t: %s\n"
	             "interrupt_mode\t: %s\n"
	             "INTB_mode\t: %s_active\n"
	             "interrupt_pins\t: %s\n",
		     (cmd & RTC_TDF) ? "yes" : "no",
		     (cmd & RTC_WAF) ? "yes" : "no",
		     (cmd & RTC_TDM) ? "disabled" : "enabled",
		     (cmd & RTC_WAM) ? "disabled" : "enabled",
		     (cmd & RTC_PU_LVL) ? "pulse" : "level",
		     (cmd & RTC_IBH_LO) ? "low" : "high",
	             (cmd & RTC_IPSW) ? "unswapped" : "swapped");

	return  p - buf;
}

static int ds1286_read_proc(char *page, char **start, off_t off,
                         int count, int *eof, void *data)
{
	int len = ds1286_proc_output (page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count)
		len = count;
	if (len<0)
		len = 0;

	return len;
}

/*
 * Returns true if a clock update is in progress
 */
static inline unsigned char ds1286_is_updating(void)
{
	return rtc_read(RTC_CMD) & RTC_TE;
}


static void ds1286_get_time(struct rtc_time *rtc_tm)
{
	unsigned char save_control;
	unsigned long flags;
	unsigned long uip_watchdog = jiffies;

	/*
	 * read RTC once any update in progress is done. The update
	 * can take just over 2ms. We wait 10 to 20ms. There is no need to
	 * to poll-wait (up to 1s - eeccch) for the falling edge of RTC_UIP.
	 * If you need to know *exactly* when a second has started, enable
	 * periodic update complete interrupts, (via ioctl) and then
	 * immediately read /dev/rtc which will block until you get the IRQ.
	 * Once the read clears, read the RTC time (again via ioctl). Easy.
	 */

	if (ds1286_is_updating() != 0)
		while (time_before(jiffies, uip_watchdog + 2*HZ/100))
			barrier();

	/*
	 * Only the values that we read from the RTC are set. We leave
	 * tm_wday, tm_yday and tm_isdst untouched. Even though the
	 * RTC has RTC_DAY_OF_WEEK, we ignore it, as it is only updated
	 * by the RTC when initially set to a non-zero value.
	 */
	spin_lock_irqsave(&ds1286_lock, flags);
	save_control = rtc_read(RTC_CMD);
	rtc_write((save_control|RTC_TE), RTC_CMD);

	rtc_tm->tm_sec = rtc_read(RTC_SECONDS);
	rtc_tm->tm_min = rtc_read(RTC_MINUTES);
	rtc_tm->tm_hour = rtc_read(RTC_HOURS) & 0x3f;
	rtc_tm->tm_mday = rtc_read(RTC_DATE);
	rtc_tm->tm_mon = rtc_read(RTC_MONTH) & 0x1f;
	rtc_tm->tm_year = rtc_read(RTC_YEAR);

	rtc_write(save_control, RTC_CMD);
	spin_unlock_irqrestore(&ds1286_lock, flags);

	BCD_TO_BIN(rtc_tm->tm_sec);
	BCD_TO_BIN(rtc_tm->tm_min);
	BCD_TO_BIN(rtc_tm->tm_hour);
	BCD_TO_BIN(rtc_tm->tm_mday);
	BCD_TO_BIN(rtc_tm->tm_mon);
	BCD_TO_BIN(rtc_tm->tm_year);

	/*
	 * Account for differences between how the RTC uses the values
	 * and how they are defined in a struct rtc_time;
	 */
	if (rtc_tm->tm_year < 45)
		rtc_tm->tm_year += 30;
	if ((rtc_tm->tm_year += 40) < 70)
		rtc_tm->tm_year += 100;

	rtc_tm->tm_mon--;
}

static int ds1286_set_time(struct rtc_time *rtc_tm)
{
	unsigned char mon, day, hrs, min, sec, leap_yr;
	unsigned char save_control;
	unsigned int yrs;
	unsigned long flags;


	yrs = rtc_tm->tm_year + 1900;
	mon = rtc_tm->tm_mon + 1;   /* tm_mon starts at zero */
	day = rtc_tm->tm_mday;
	hrs = rtc_tm->tm_hour;
	min = rtc_tm->tm_min;
	sec = rtc_tm->tm_sec;

	if (yrs < 1970)
		return -EINVAL;

	leap_yr = ((!(yrs % 4) && (yrs % 100)) || !(yrs % 400));

	if ((mon > 12) || (day == 0))
		return -EINVAL;

	if (day > (days_in_mo[mon] + ((mon == 2) && leap_yr)))
		return -EINVAL;

	if ((hrs >= 24) || (min >= 60) || (sec >= 60))
		return -EINVAL;

	if ((yrs -= 1940) > 255)    /* They are unsigned */
		return -EINVAL;

	if (yrs >= 100)
		yrs -= 100;

	BIN_TO_BCD(sec);
	BIN_TO_BCD(min);
	BIN_TO_BCD(hrs);
	BIN_TO_BCD(day);
	BIN_TO_BCD(mon);
	BIN_TO_BCD(yrs);

	spin_lock_irqsave(&ds1286_lock, flags);
	save_control = rtc_read(RTC_CMD);
	rtc_write((save_control|RTC_TE), RTC_CMD);

	rtc_write(yrs, RTC_YEAR);
	rtc_write(mon, RTC_MONTH);
	rtc_write(day, RTC_DATE);
	rtc_write(hrs, RTC_HOURS);
	rtc_write(min, RTC_MINUTES);
	rtc_write(sec, RTC_SECONDS);
	rtc_write(0, RTC_HUNDREDTH_SECOND);

	rtc_write(save_control, RTC_CMD);
	spin_unlock_irqrestore(&ds1286_lock, flags);

	return 0;
}

static void ds1286_get_alm_time(struct rtc_time *alm_tm)
{
	unsigned char cmd;
	unsigned long flags;

	/*
	 * Only the values that we read from the RTC are set. That
	 * means only tm_wday, tm_hour, tm_min.
	 */
	spin_lock_irqsave(&ds1286_lock, flags);
	alm_tm->tm_min = rtc_read(RTC_MINUTES_ALARM) & 0x7f;
	alm_tm->tm_hour = rtc_read(RTC_HOURS_ALARM)  & 0x1f;
	alm_tm->tm_wday = rtc_read(RTC_DAY_ALARM)    & 0x07;
	cmd = rtc_read(RTC_CMD);
	spin_unlock_irqrestore(&ds1286_lock, flags);

	BCD_TO_BIN(alm_tm->tm_min);
	BCD_TO_BIN(alm_tm->tm_hour);
	alm_tm->tm_sec = 0;
}

module_init(ds1286_init);
module_exit(ds1286_exit);

MODULE_AUTHOR("Ralf Baechle");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(RTC_MINOR);

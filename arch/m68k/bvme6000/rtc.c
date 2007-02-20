/*
 *	Real Time Clock interface for Linux on the BVME6000
 *
 * Based on the PC driver by Paul Gortmaker.
 */

#define RTC_VERSION		"1.00"

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/capability.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/mc146818rtc.h>	/* For struct rtc_time and ioctls, etc */
#include <linux/smp_lock.h>
#include <linux/bcd.h>
#include <asm/bvme6000hw.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/setup.h>

/*
 *	We sponge a minor off of the misc major. No need slurping
 *	up another valuable major dev number for this. If you add
 *	an ioctl, make sure you don't conflict with SPARC's RTC
 *	ioctls.
 */

static unsigned char days_in_mo[] =
{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static char rtc_status;

static int rtc_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		     unsigned long arg)
{
	volatile RtcPtr_t rtc = (RtcPtr_t)BVME_RTC_BASE;
	unsigned char msr;
	unsigned long flags;
	struct rtc_time wtime;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case RTC_RD_TIME:	/* Read the time/date from RTC	*/
	{
		local_irq_save(flags);
		/* Ensure clock and real-time-mode-register are accessible */
		msr = rtc->msr & 0xc0;
		rtc->msr = 0x40;
		memset(&wtime, 0, sizeof(struct rtc_time));
		do {
			wtime.tm_sec =  BCD2BIN(rtc->bcd_sec);
			wtime.tm_min =  BCD2BIN(rtc->bcd_min);
			wtime.tm_hour = BCD2BIN(rtc->bcd_hr);
			wtime.tm_mday =  BCD2BIN(rtc->bcd_dom);
			wtime.tm_mon =  BCD2BIN(rtc->bcd_mth)-1;
			wtime.tm_year = BCD2BIN(rtc->bcd_year);
			if (wtime.tm_year < 70)
				wtime.tm_year += 100;
			wtime.tm_wday = BCD2BIN(rtc->bcd_dow)-1;
		} while (wtime.tm_sec != BCD2BIN(rtc->bcd_sec));
		rtc->msr = msr;
		local_irq_restore(flags);
		return copy_to_user(argp, &wtime, sizeof wtime) ?
								-EFAULT : 0;
	}
	case RTC_SET_TIME:	/* Set the RTC */
	{
		struct rtc_time rtc_tm;
		unsigned char mon, day, hrs, min, sec, leap_yr;
		unsigned int yrs;

		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;

		if (copy_from_user(&rtc_tm, argp, sizeof(struct rtc_time)))
			return -EFAULT;

		yrs = rtc_tm.tm_year;
		if (yrs < 1900)
			yrs += 1900;
		mon = rtc_tm.tm_mon + 1;   /* tm_mon starts at zero */
		day = rtc_tm.tm_mday;
		hrs = rtc_tm.tm_hour;
		min = rtc_tm.tm_min;
		sec = rtc_tm.tm_sec;

		leap_yr = ((!(yrs % 4) && (yrs % 100)) || !(yrs % 400));

		if ((mon > 12) || (mon < 1) || (day == 0))
			return -EINVAL;

		if (day > (days_in_mo[mon] + ((mon == 2) && leap_yr)))
			return -EINVAL;

		if ((hrs >= 24) || (min >= 60) || (sec >= 60))
			return -EINVAL;

		if (yrs >= 2070)
			return -EINVAL;

		local_irq_save(flags);
		/* Ensure clock and real-time-mode-register are accessible */
		msr = rtc->msr & 0xc0;
		rtc->msr = 0x40;

		rtc->t0cr_rtmr = yrs%4;
		rtc->bcd_tenms = 0;
		rtc->bcd_sec   = BIN2BCD(sec);
		rtc->bcd_min   = BIN2BCD(min);
		rtc->bcd_hr    = BIN2BCD(hrs);
		rtc->bcd_dom   = BIN2BCD(day);
		rtc->bcd_mth   = BIN2BCD(mon);
		rtc->bcd_year  = BIN2BCD(yrs%100);
		if (rtc_tm.tm_wday >= 0)
			rtc->bcd_dow = BIN2BCD(rtc_tm.tm_wday+1);
		rtc->t0cr_rtmr = yrs%4 | 0x08;

		rtc->msr = msr;
		local_irq_restore(flags);
		return 0;
	}
	default:
		return -EINVAL;
	}
}

/*
 *	We enforce only one user at a time here with the open/close.
 *	Also clear the previous interrupt data on an open, and clean
 *	up things on a close.
 */

static int rtc_open(struct inode *inode, struct file *file)
{
	if(rtc_status)
		return -EBUSY;

	rtc_status = 1;
	return 0;
}

static int rtc_release(struct inode *inode, struct file *file)
{
	lock_kernel();
	rtc_status = 0;
	unlock_kernel();
	return 0;
}

/*
 *	The various file operations we support.
 */

static const struct file_operations rtc_fops = {
	.ioctl =	rtc_ioctl,
	.open =		rtc_open,
	.release =	rtc_release,
};

static struct miscdevice rtc_dev = {
	.minor =	RTC_MINOR,
	.name =		"rtc",
	.fops =		&rtc_fops
};

static int __init rtc_DP8570A_init(void)
{
	if (!MACH_IS_BVME6000)
		return -ENODEV;

	printk(KERN_INFO "DP8570A Real Time Clock Driver v%s\n", RTC_VERSION);
	return misc_register(&rtc_dev);
}
module_init(rtc_DP8570A_init);

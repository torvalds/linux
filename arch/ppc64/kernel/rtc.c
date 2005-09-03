/*
 *	Real Time Clock interface for PPC64.
 *
 *	Based on rtc.c by Paul Gortmaker
 *
 *	This driver allows use of the real time clock
 *	from user space. It exports the /dev/rtc
 *	interface supporting various ioctl() and also the
 *	/proc/driver/rtc pseudo-file for status information.
 *
 * 	Interface does not support RTC interrupts nor an alarm.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *      1.0	Mike Corrigan:    IBM iSeries rtc support
 *      1.1	Dave Engebretsen: IBM pSeries rtc support
 */

#define RTC_VERSION		"1.1"

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/mc146818rtc.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/bcd.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/time.h>
#include <asm/rtas.h>

#include <asm/iSeries/mf.h>
#include <asm/machdep.h>

extern int piranha_simulator;

/*
 *	We sponge a minor off of the misc major. No need slurping
 *	up another valuable major dev number for this. If you add
 *	an ioctl, make sure you don't conflict with SPARC's RTC
 *	ioctls.
 */

static ssize_t rtc_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos);

static int rtc_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg);

static int rtc_read_proc(char *page, char **start, off_t off,
                         int count, int *eof, void *data);

/*
 *	If this driver ever becomes modularised, it will be really nice
 *	to make the epoch retain its value across module reload...
 */

static unsigned long epoch = 1900;	/* year corresponding to 0x00	*/

static const unsigned char days_in_mo[] = 
{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/*
 *	Now all the various file operations that we export.
 */

static ssize_t rtc_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	return -EIO;
}

static int rtc_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		     unsigned long arg)
{
	struct rtc_time wtime; 

	switch (cmd) {
	case RTC_RD_TIME:	/* Read the time/date from RTC	*/
	{
		memset(&wtime, 0, sizeof(struct rtc_time));
		ppc_md.get_rtc_time(&wtime);
		break;
	}
	case RTC_SET_TIME:	/* Set the RTC */
	{
		struct rtc_time rtc_tm;
		unsigned char mon, day, hrs, min, sec, leap_yr;
		unsigned int yrs;

		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		if (copy_from_user(&rtc_tm, (struct rtc_time __user *)arg,
				   sizeof(struct rtc_time)))
			return -EFAULT;

		yrs = rtc_tm.tm_year;
		mon = rtc_tm.tm_mon + 1;   /* tm_mon starts at zero */
		day = rtc_tm.tm_mday;
		hrs = rtc_tm.tm_hour;
		min = rtc_tm.tm_min;
		sec = rtc_tm.tm_sec;

		if (yrs < 70)
			return -EINVAL;

		leap_yr = ((!(yrs % 4) && (yrs % 100)) || !(yrs % 400));

		if ((mon > 12) || (day == 0))
			return -EINVAL;

		if (day > (days_in_mo[mon] + ((mon == 2) && leap_yr)))
			return -EINVAL;
			
		if ((hrs >= 24) || (min >= 60) || (sec >= 60))
			return -EINVAL;

		if ( yrs > 169 )
			return -EINVAL;

		ppc_md.set_rtc_time(&rtc_tm);
		
		return 0;
	}
	case RTC_EPOCH_READ:	/* Read the epoch.	*/
	{
		return put_user (epoch, (unsigned long __user *)arg);
	}
	case RTC_EPOCH_SET:	/* Set the epoch.	*/
	{
		/* 
		 * There were no RTC clocks before 1900.
		 */
		if (arg < 1900)
			return -EINVAL;

		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		epoch = arg;
		return 0;
	}
	default:
		return -EINVAL;
	}
	return copy_to_user((void __user *)arg, &wtime, sizeof wtime) ? -EFAULT : 0;
}

static int rtc_open(struct inode *inode, struct file *file)
{
	nonseekable_open(inode, file);
	return 0;
}

static int rtc_release(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 *	The various file operations we support.
 */
static struct file_operations rtc_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.read =		rtc_read,
	.ioctl =	rtc_ioctl,
	.open =		rtc_open,
	.release =	rtc_release,
};

static struct miscdevice rtc_dev = {
	.minor =	RTC_MINOR,
	.name =		"rtc",
	.fops =		&rtc_fops
};

static int __init rtc_init(void)
{
	int retval;

	retval = misc_register(&rtc_dev);
	if(retval < 0)
		return retval;

#ifdef CONFIG_PROC_FS
	if (create_proc_read_entry("driver/rtc", 0, NULL, rtc_read_proc, NULL)
			== NULL) {
		misc_deregister(&rtc_dev);
		return -ENOMEM;
	}
#endif

	printk(KERN_INFO "i/pSeries Real Time Clock Driver v" RTC_VERSION "\n");

	return 0;
}

static void __exit rtc_exit (void)
{
	remove_proc_entry ("driver/rtc", NULL);
	misc_deregister(&rtc_dev);
}

module_init(rtc_init);
module_exit(rtc_exit);

/*
 *	Info exported via "/proc/driver/rtc".
 */

static int rtc_proc_output (char *buf)
{
	
	char *p;
	struct rtc_time tm;
	
	p = buf;

	ppc_md.get_rtc_time(&tm);

	/*
	 * There is no way to tell if the luser has the RTC set for local
	 * time or for Universal Standard Time (GMT). Probably local though.
	 */
	p += sprintf(p,
		     "rtc_time\t: %02d:%02d:%02d\n"
		     "rtc_date\t: %04d-%02d-%02d\n"
	 	     "rtc_epoch\t: %04lu\n",
		     tm.tm_hour, tm.tm_min, tm.tm_sec,
		     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, epoch);

	p += sprintf(p,
		     "DST_enable\t: no\n"
		     "BCD\t\t: yes\n"
		     "24hr\t\t: yes\n" );

	return  p - buf;
}

static int rtc_read_proc(char *page, char **start, off_t off,
                         int count, int *eof, void *data)
{
        int len = rtc_proc_output (page);
        if (len <= off+count) *eof = 1;
        *start = page + off;
        len -= off;
        if (len>count) len = count;
        if (len<0) len = 0;
        return len;
}

#ifdef CONFIG_PPC_ISERIES
/*
 * Get the RTC from the virtual service processor
 * This requires flowing LpEvents to the primary partition
 */
void iSeries_get_rtc_time(struct rtc_time *rtc_tm)
{
	if (piranha_simulator)
		return;

	mf_get_rtc(rtc_tm);
	rtc_tm->tm_mon--;
}

/*
 * Set the RTC in the virtual service processor
 * This requires flowing LpEvents to the primary partition
 */
int iSeries_set_rtc_time(struct rtc_time *tm)
{
	mf_set_rtc(tm);
	return 0;
}

void iSeries_get_boot_time(struct rtc_time *tm)
{
	if ( piranha_simulator )
		return;

	mf_get_boot_rtc(tm);
	tm->tm_mon  -= 1;
}
#endif

#ifdef CONFIG_PPC_RTAS
#define MAX_RTC_WAIT 5000	/* 5 sec */
#define RTAS_CLOCK_BUSY (-2)
void rtas_get_boot_time(struct rtc_time *rtc_tm)
{
	int ret[8];
	int error, wait_time;
	unsigned long max_wait_tb;

	max_wait_tb = __get_tb() + tb_ticks_per_usec * 1000 * MAX_RTC_WAIT;
	do {
		error = rtas_call(rtas_token("get-time-of-day"), 0, 8, ret);
		if (error == RTAS_CLOCK_BUSY || rtas_is_extended_busy(error)) {
			wait_time = rtas_extended_busy_delay_time(error);
			/* This is boot time so we spin. */
			udelay(wait_time*1000);
			error = RTAS_CLOCK_BUSY;
		}
	} while (error == RTAS_CLOCK_BUSY && (__get_tb() < max_wait_tb));

	if (error != 0 && printk_ratelimit()) {
		printk(KERN_WARNING "error: reading the clock failed (%d)\n",
			error);
		return;
	}

	rtc_tm->tm_sec = ret[5];
	rtc_tm->tm_min = ret[4];
	rtc_tm->tm_hour = ret[3];
	rtc_tm->tm_mday = ret[2];
	rtc_tm->tm_mon = ret[1] - 1;
	rtc_tm->tm_year = ret[0] - 1900;
}

/* NOTE: get_rtc_time will get an error if executed in interrupt context
 * and if a delay is needed to read the clock.  In this case we just
 * silently return without updating rtc_tm.
 */
void rtas_get_rtc_time(struct rtc_time *rtc_tm)
{
        int ret[8];
	int error, wait_time;
	unsigned long max_wait_tb;

	max_wait_tb = __get_tb() + tb_ticks_per_usec * 1000 * MAX_RTC_WAIT;
	do {
		error = rtas_call(rtas_token("get-time-of-day"), 0, 8, ret);
		if (error == RTAS_CLOCK_BUSY || rtas_is_extended_busy(error)) {
			if (in_interrupt() && printk_ratelimit()) {
				printk(KERN_WARNING "error: reading clock would delay interrupt\n");
				return;	/* delay not allowed */
			}
			wait_time = rtas_extended_busy_delay_time(error);
			msleep_interruptible(wait_time);
			error = RTAS_CLOCK_BUSY;
		}
	} while (error == RTAS_CLOCK_BUSY && (__get_tb() < max_wait_tb));

        if (error != 0 && printk_ratelimit()) {
                printk(KERN_WARNING "error: reading the clock failed (%d)\n",
		       error);
		return;
        }

	rtc_tm->tm_sec = ret[5];
	rtc_tm->tm_min = ret[4];
	rtc_tm->tm_hour = ret[3];
	rtc_tm->tm_mday = ret[2];
	rtc_tm->tm_mon = ret[1] - 1;
	rtc_tm->tm_year = ret[0] - 1900;
}

int rtas_set_rtc_time(struct rtc_time *tm)
{
	int error, wait_time;
	unsigned long max_wait_tb;

	max_wait_tb = __get_tb() + tb_ticks_per_usec * 1000 * MAX_RTC_WAIT;
	do {
	        error = rtas_call(rtas_token("set-time-of-day"), 7, 1, NULL,
				  tm->tm_year + 1900, tm->tm_mon + 1, 
				  tm->tm_mday, tm->tm_hour, tm->tm_min, 
				  tm->tm_sec, 0);
		if (error == RTAS_CLOCK_BUSY || rtas_is_extended_busy(error)) {
			if (in_interrupt())
				return 1;	/* probably decrementer */
			wait_time = rtas_extended_busy_delay_time(error);
			msleep_interruptible(wait_time);
			error = RTAS_CLOCK_BUSY;
		}
	} while (error == RTAS_CLOCK_BUSY && (__get_tb() < max_wait_tb));

        if (error != 0 && printk_ratelimit())
                printk(KERN_WARNING "error: setting the clock failed (%d)\n",
		       error); 

        return 0;
}
#endif

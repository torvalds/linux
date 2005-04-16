/* $Id: rtc.c,v 1.28 2001/10/08 22:19:51 davem Exp $
 *
 * Linux/SPARC Real Time Clock Driver
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 *
 * This is a little driver that lets a user-level program access
 * the SPARC Mostek real time clock chip. It is no use unless you
 * use the modified clock utility.
 *
 * Get the modified clock utility from:
 *   ftp://vger.kernel.org/pub/linux/Sparc/userland/clock.c
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <asm/io.h>
#include <asm/mostek.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/rtc.h>

static int rtc_busy = 0;

/* Retrieve the current date and time from the real time clock. */
static void get_rtc_time(struct rtc_time *t)
{
	void * __iomem regs = mstk48t02_regs;
	u8 tmp;

	spin_lock_irq(&mostek_lock);

	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp |= MSTK_CREG_READ;
	mostek_write(regs + MOSTEK_CREG, tmp);

	t->sec = MSTK_REG_SEC(regs);
	t->min = MSTK_REG_MIN(regs);
	t->hour = MSTK_REG_HOUR(regs);
	t->dow = MSTK_REG_DOW(regs);
	t->dom = MSTK_REG_DOM(regs);
	t->month = MSTK_REG_MONTH(regs);
	t->year = MSTK_CVT_YEAR( MSTK_REG_YEAR(regs) );

	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp &= ~MSTK_CREG_READ;
	mostek_write(regs + MOSTEK_CREG, tmp);

	spin_unlock_irq(&mostek_lock);
}

/* Set the current date and time inthe real time clock. */
void set_rtc_time(struct rtc_time *t)
{
	void * __iomem regs = mstk48t02_regs;
	u8 tmp;

	spin_lock_irq(&mostek_lock);

	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp |= MSTK_CREG_WRITE;
	mostek_write(regs + MOSTEK_CREG, tmp);

	MSTK_SET_REG_SEC(regs,t->sec);
	MSTK_SET_REG_MIN(regs,t->min);
	MSTK_SET_REG_HOUR(regs,t->hour);
	MSTK_SET_REG_DOW(regs,t->dow);
	MSTK_SET_REG_DOM(regs,t->dom);
	MSTK_SET_REG_MONTH(regs,t->month);
	MSTK_SET_REG_YEAR(regs,t->year - MSTK_YEAR_ZERO);

	tmp = mostek_read(regs + MOSTEK_CREG);
	tmp &= ~MSTK_CREG_WRITE;
	mostek_write(regs + MOSTEK_CREG, tmp);

	spin_unlock_irq(&mostek_lock);
}

static int rtc_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	unsigned long arg)
{
	struct rtc_time rtc_tm;
	void __user *argp = (void __user *)arg;

	switch (cmd)
	{
	case RTCGET:
		memset(&rtc_tm, 0, sizeof(struct rtc_time));
		get_rtc_time(&rtc_tm);

		if (copy_to_user(argp, &rtc_tm, sizeof(struct rtc_time)))
			return -EFAULT;

		return 0;


	case RTCSET:
		if (!capable(CAP_SYS_TIME))
			return -EPERM;

		if (copy_from_user(&rtc_tm, argp, sizeof(struct rtc_time)))
			return -EFAULT;

		set_rtc_time(&rtc_tm);

		return 0;

	default:
		return -EINVAL;
	}
}

static int rtc_open(struct inode *inode, struct file *file)
{
	int ret;

	spin_lock_irq(&mostek_lock);
	if (rtc_busy) {
		ret = -EBUSY;
	} else {
		rtc_busy = 1;
		ret = 0;
	}
	spin_unlock_irq(&mostek_lock);

	return ret;
}

static int rtc_release(struct inode *inode, struct file *file)
{
	rtc_busy = 0;

	return 0;
}

static struct file_operations rtc_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.ioctl =	rtc_ioctl,
	.open =		rtc_open,
	.release =	rtc_release,
};

static struct miscdevice rtc_dev = { RTC_MINOR, "rtc", &rtc_fops };

static int __init rtc_sun_init(void)
{
	int error;

	/* It is possible we are being driven by some other RTC chip
	 * and thus another RTC driver is handling things.
	 */
	if (mstk48t02_regs == 0)
		return -ENODEV;

	error = misc_register(&rtc_dev);
	if (error) {
		printk(KERN_ERR "rtc: unable to get misc minor for Mostek\n");
		return error;
	}

	return 0;
}

static void __exit rtc_sun_cleanup(void)
{
	misc_deregister(&rtc_dev);
}

module_init(rtc_sun_init);
module_exit(rtc_sun_cleanup);
MODULE_LICENSE("GPL");

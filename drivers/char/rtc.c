/*
 *	Real Time Clock interface for Linux
 *
 *	Copyright (C) 1996 Paul Gortmaker
 *
 *	This driver allows use of the real time clock (built into
 *	nearly all computers) from user space. It exports the /dev/rtc
 *	interface supporting various ioctl() and also the
 *	/proc/driver/rtc pseudo-file for status information.
 *
 *	The ioctls can be used to set the interrupt behaviour and
 *	generation rate from the RTC via IRQ 8. Then the /dev/rtc
 *	interface can be used to make use of these timer interrupts,
 *	be they interval or alarm based.
 *
 *	The /dev/rtc interface will block on reads until an interrupt
 *	has been received. If a RTC interrupt has already happened,
 *	it will output an unsigned long and then block. The output value
 *	contains the interrupt status in the low byte and the number of
 *	interrupts since the last read in the remaining high bytes. The
 *	/dev/rtc interface can also be used with the select(2) call.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Based on other minimal char device drivers, like Alan's
 *	watchdog, Ted's random, etc. etc.
 *
 *	1.07	Paul Gortmaker.
 *	1.08	Miquel van Smoorenburg: disallow certain things on the
 *		DEC Alpha as the CMOS clock is also used for other things.
 *	1.09	Nikita Schmidt: epoch support and some Alpha cleanup.
 *	1.09a	Pete Zaitcev: Sun SPARC
 *	1.09b	Jeff Garzik: Modularize, init cleanup
 *	1.09c	Jeff Garzik: SMP cleanup
 *	1.10	Paul Barton-Davis: add support for async I/O
 *	1.10a	Andrea Arcangeli: Alpha updates
 *	1.10b	Andrew Morton: SMP lock fix
 *	1.10c	Cesar Barros: SMP locking fixes and cleanup
 *	1.10d	Paul Gortmaker: delete paranoia check in rtc_exit
 *	1.10e	Maciej W. Rozycki: Handle DECstation's year weirdness.
 *	1.11	Takashi Iwai: Kernel access functions
 *			      rtc_register/rtc_unregister/rtc_control
 *      1.11a   Daniele Bellucci: Audit create_proc_read_entry in rtc_init
 *	1.12	Venkatesh Pallipadi: Hooks for emulating rtc on HPET base-timer
 *		CONFIG_HPET_EMULATE_RTC
 *	1.12a	Maciej W. Rozycki: Handle memory-mapped chips properly.
 *	1.12ac	Alan Cox: Allow read access to the day of week register
 *	1.12b	David John: Remove calls to the BKL.
 */

#define RTC_VERSION		"1.12b"

/*
 *	Note that *all* calls to CMOS_READ and CMOS_WRITE are done with
 *	interrupts disabled. Due to the index-port/data-port (0x70/0x71)
 *	design of the RTC, we don't want two different things trying to
 *	get to it at once. (e.g. the periodic 11 min sync from
 *      kernel/time/ntp.c vs. this driver.)
 */

#include <linux/interrupt.h>
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
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/sched/signal.h>
#include <linux/sysctl.h>
#include <linux/wait.h>
#include <linux/bcd.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/ratelimit.h>

#include <asm/current.h>

#ifdef CONFIG_X86
#include <asm/hpet.h>
#endif

#ifdef CONFIG_SPARC32
#include <linux/of.h>
#include <linux/of_device.h>
#include <asm/io.h>

static unsigned long rtc_port;
static int rtc_irq;
#endif

#ifdef	CONFIG_HPET_EMULATE_RTC
#undef	RTC_IRQ
#endif

#ifdef RTC_IRQ
static int rtc_has_irq = 1;
#endif

#ifndef CONFIG_HPET_EMULATE_RTC
#define is_hpet_enabled()			0
#define hpet_set_alarm_time(hrs, min, sec)	0
#define hpet_set_periodic_freq(arg)		0
#define hpet_mask_rtc_irq_bit(arg)		0
#define hpet_set_rtc_irq_bit(arg)		0
#define hpet_rtc_timer_init()			do { } while (0)
#define hpet_rtc_dropped_irq()			0
#define hpet_register_irq_handler(h)		({ 0; })
#define hpet_unregister_irq_handler(h)		({ 0; })
#ifdef RTC_IRQ
static irqreturn_t hpet_rtc_interrupt(int irq, void *dev_id)
{
	return 0;
}
#endif
#endif

/*
 *	We sponge a minor off of the misc major. No need slurping
 *	up another valuable major dev number for this. If you add
 *	an ioctl, make sure you don't conflict with SPARC's RTC
 *	ioctls.
 */

static struct fasync_struct *rtc_async_queue;

static DECLARE_WAIT_QUEUE_HEAD(rtc_wait);

#ifdef RTC_IRQ
static void rtc_dropped_irq(unsigned long data);

static DEFINE_TIMER(rtc_irq_timer, rtc_dropped_irq, 0, 0);
#endif

static ssize_t rtc_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos);

static long rtc_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static void rtc_get_rtc_time(struct rtc_time *rtc_tm);

#ifdef RTC_IRQ
static unsigned int rtc_poll(struct file *file, poll_table *wait);
#endif

static void get_rtc_alm_time(struct rtc_time *alm_tm);
#ifdef RTC_IRQ
static void set_rtc_irq_bit_locked(unsigned char bit);
static void mask_rtc_irq_bit_locked(unsigned char bit);

static inline void set_rtc_irq_bit(unsigned char bit)
{
	spin_lock_irq(&rtc_lock);
	set_rtc_irq_bit_locked(bit);
	spin_unlock_irq(&rtc_lock);
}

static void mask_rtc_irq_bit(unsigned char bit)
{
	spin_lock_irq(&rtc_lock);
	mask_rtc_irq_bit_locked(bit);
	spin_unlock_irq(&rtc_lock);
}
#endif

#ifdef CONFIG_PROC_FS
static int rtc_proc_open(struct inode *inode, struct file *file);
#endif

/*
 *	Bits in rtc_status. (6 bits of room for future expansion)
 */

#define RTC_IS_OPEN		0x01	/* means /dev/rtc is in use	*/
#define RTC_TIMER_ON		0x02	/* missed irq timer active	*/

/*
 * rtc_status is never changed by rtc_interrupt, and ioctl/open/close is
 * protected by the spin lock rtc_lock. However, ioctl can still disable the
 * timer in rtc_status and then with del_timer after the interrupt has read
 * rtc_status but before mod_timer is called, which would then reenable the
 * timer (but you would need to have an awful timing before you'd trip on it)
 */
static unsigned long rtc_status;	/* bitmapped status byte.	*/
static unsigned long rtc_freq;		/* Current periodic IRQ rate	*/
static unsigned long rtc_irq_data;	/* our output to the world	*/
static unsigned long rtc_max_user_freq = 64; /* > this, need CAP_SYS_RESOURCE */

#ifdef RTC_IRQ
/*
 * rtc_task_lock nests inside rtc_lock.
 */
static DEFINE_SPINLOCK(rtc_task_lock);
static rtc_task_t *rtc_callback;
#endif

/*
 *	If this driver ever becomes modularised, it will be really nice
 *	to make the epoch retain its value across module reload...
 */

static unsigned long epoch = 1900;	/* year corresponding to 0x00	*/

static const unsigned char days_in_mo[] =
{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/*
 * Returns true if a clock update is in progress
 */
static inline unsigned char rtc_is_updating(void)
{
	unsigned long flags;
	unsigned char uip;

	spin_lock_irqsave(&rtc_lock, flags);
	uip = (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP);
	spin_unlock_irqrestore(&rtc_lock, flags);
	return uip;
}

#ifdef RTC_IRQ
/*
 *	A very tiny interrupt handler. It runs with interrupts disabled,
 *	but there is possibility of conflicting with the set_rtc_mmss()
 *	call (the rtc irq and the timer irq can easily run at the same
 *	time in two different CPUs). So we need to serialize
 *	accesses to the chip with the rtc_lock spinlock that each
 *	architecture should implement in the timer code.
 *	(See ./arch/XXXX/kernel/time.c for the set_rtc_mmss() function.)
 */

static irqreturn_t rtc_interrupt(int irq, void *dev_id)
{
	/*
	 *	Can be an alarm interrupt, update complete interrupt,
	 *	or a periodic interrupt. We store the status in the
	 *	low byte and the number of interrupts received since
	 *	the last read in the remainder of rtc_irq_data.
	 */

	spin_lock(&rtc_lock);
	rtc_irq_data += 0x100;
	rtc_irq_data &= ~0xff;
	if (is_hpet_enabled()) {
		/*
		 * In this case it is HPET RTC interrupt handler
		 * calling us, with the interrupt information
		 * passed as arg1, instead of irq.
		 */
		rtc_irq_data |= (unsigned long)irq & 0xF0;
	} else {
		rtc_irq_data |= (CMOS_READ(RTC_INTR_FLAGS) & 0xF0);
	}

	if (rtc_status & RTC_TIMER_ON)
		mod_timer(&rtc_irq_timer, jiffies + HZ/rtc_freq + 2*HZ/100);

	spin_unlock(&rtc_lock);

	/* Now do the rest of the actions */
	spin_lock(&rtc_task_lock);
	if (rtc_callback)
		rtc_callback->func(rtc_callback->private_data);
	spin_unlock(&rtc_task_lock);
	wake_up_interruptible(&rtc_wait);

	kill_fasync(&rtc_async_queue, SIGIO, POLL_IN);

	return IRQ_HANDLED;
}
#endif

/*
 * sysctl-tuning infrastructure.
 */
static struct ctl_table rtc_table[] = {
	{
		.procname	= "max-user-freq",
		.data		= &rtc_max_user_freq,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{ }
};

static struct ctl_table rtc_root[] = {
	{
		.procname	= "rtc",
		.mode		= 0555,
		.child		= rtc_table,
	},
	{ }
};

static struct ctl_table dev_root[] = {
	{
		.procname	= "dev",
		.mode		= 0555,
		.child		= rtc_root,
	},
	{ }
};

static struct ctl_table_header *sysctl_header;

static int __init init_sysctl(void)
{
    sysctl_header = register_sysctl_table(dev_root);
    return 0;
}

static void __exit cleanup_sysctl(void)
{
    unregister_sysctl_table(sysctl_header);
}

/*
 *	Now all the various file operations that we export.
 */

static ssize_t rtc_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
#ifndef RTC_IRQ
	return -EIO;
#else
	DECLARE_WAITQUEUE(wait, current);
	unsigned long data;
	ssize_t retval;

	if (rtc_has_irq == 0)
		return -EIO;

	/*
	 * Historically this function used to assume that sizeof(unsigned long)
	 * is the same in userspace and kernelspace.  This lead to problems
	 * for configurations with multiple ABIs such a the MIPS o32 and 64
	 * ABIs supported on the same kernel.  So now we support read of both
	 * 4 and 8 bytes and assume that's the sizeof(unsigned long) in the
	 * userspace ABI.
	 */
	if (count != sizeof(unsigned int) && count !=  sizeof(unsigned long))
		return -EINVAL;

	add_wait_queue(&rtc_wait, &wait);

	do {
		/* First make it right. Then make it fast. Putting this whole
		 * block within the parentheses of a while would be too
		 * confusing. And no, xchg() is not the answer. */

		__set_current_state(TASK_INTERRUPTIBLE);

		spin_lock_irq(&rtc_lock);
		data = rtc_irq_data;
		rtc_irq_data = 0;
		spin_unlock_irq(&rtc_lock);

		if (data != 0)
			break;

		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			goto out;
		}
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			goto out;
		}
		schedule();
	} while (1);

	if (count == sizeof(unsigned int)) {
		retval = put_user(data,
				  (unsigned int __user *)buf) ?: sizeof(int);
	} else {
		retval = put_user(data,
				  (unsigned long __user *)buf) ?: sizeof(long);
	}
	if (!retval)
		retval = count;
 out:
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&rtc_wait, &wait);

	return retval;
#endif
}

static int rtc_do_ioctl(unsigned int cmd, unsigned long arg, int kernel)
{
	struct rtc_time wtime;

#ifdef RTC_IRQ
	if (rtc_has_irq == 0) {
		switch (cmd) {
		case RTC_AIE_OFF:
		case RTC_AIE_ON:
		case RTC_PIE_OFF:
		case RTC_PIE_ON:
		case RTC_UIE_OFF:
		case RTC_UIE_ON:
		case RTC_IRQP_READ:
		case RTC_IRQP_SET:
			return -EINVAL;
		}
	}
#endif

	switch (cmd) {
#ifdef RTC_IRQ
	case RTC_AIE_OFF:	/* Mask alarm int. enab. bit	*/
	{
		mask_rtc_irq_bit(RTC_AIE);
		return 0;
	}
	case RTC_AIE_ON:	/* Allow alarm interrupts.	*/
	{
		set_rtc_irq_bit(RTC_AIE);
		return 0;
	}
	case RTC_PIE_OFF:	/* Mask periodic int. enab. bit	*/
	{
		/* can be called from isr via rtc_control() */
		unsigned long flags;

		spin_lock_irqsave(&rtc_lock, flags);
		mask_rtc_irq_bit_locked(RTC_PIE);
		if (rtc_status & RTC_TIMER_ON) {
			rtc_status &= ~RTC_TIMER_ON;
			del_timer(&rtc_irq_timer);
		}
		spin_unlock_irqrestore(&rtc_lock, flags);

		return 0;
	}
	case RTC_PIE_ON:	/* Allow periodic ints		*/
	{
		/* can be called from isr via rtc_control() */
		unsigned long flags;

		/*
		 * We don't really want Joe User enabling more
		 * than 64Hz of interrupts on a multi-user machine.
		 */
		if (!kernel && (rtc_freq > rtc_max_user_freq) &&
						(!capable(CAP_SYS_RESOURCE)))
			return -EACCES;

		spin_lock_irqsave(&rtc_lock, flags);
		if (!(rtc_status & RTC_TIMER_ON)) {
			mod_timer(&rtc_irq_timer, jiffies + HZ/rtc_freq +
					2*HZ/100);
			rtc_status |= RTC_TIMER_ON;
		}
		set_rtc_irq_bit_locked(RTC_PIE);
		spin_unlock_irqrestore(&rtc_lock, flags);

		return 0;
	}
	case RTC_UIE_OFF:	/* Mask ints from RTC updates.	*/
	{
		mask_rtc_irq_bit(RTC_UIE);
		return 0;
	}
	case RTC_UIE_ON:	/* Allow ints for RTC updates.	*/
	{
		set_rtc_irq_bit(RTC_UIE);
		return 0;
	}
#endif
	case RTC_ALM_READ:	/* Read the present alarm time */
	{
		/*
		 * This returns a struct rtc_time. Reading >= 0xc0
		 * means "don't care" or "match all". Only the tm_hour,
		 * tm_min, and tm_sec values are filled in.
		 */
		memset(&wtime, 0, sizeof(struct rtc_time));
		get_rtc_alm_time(&wtime);
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

		if (copy_from_user(&alm_tm, (struct rtc_time __user *)arg,
				   sizeof(struct rtc_time)))
			return -EFAULT;

		hrs = alm_tm.tm_hour;
		min = alm_tm.tm_min;
		sec = alm_tm.tm_sec;

		spin_lock_irq(&rtc_lock);
		if (hpet_set_alarm_time(hrs, min, sec)) {
			/*
			 * Fallthru and set alarm time in CMOS too,
			 * so that we will get proper value in RTC_ALM_READ
			 */
		}
		if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY) ||
							RTC_ALWAYS_BCD) {
			if (sec < 60)
				sec = bin2bcd(sec);
			else
				sec = 0xff;

			if (min < 60)
				min = bin2bcd(min);
			else
				min = 0xff;

			if (hrs < 24)
				hrs = bin2bcd(hrs);
			else
				hrs = 0xff;
		}
		CMOS_WRITE(hrs, RTC_HOURS_ALARM);
		CMOS_WRITE(min, RTC_MINUTES_ALARM);
		CMOS_WRITE(sec, RTC_SECONDS_ALARM);
		spin_unlock_irq(&rtc_lock);

		return 0;
	}
	case RTC_RD_TIME:	/* Read the time/date from RTC	*/
	{
		memset(&wtime, 0, sizeof(struct rtc_time));
		rtc_get_rtc_time(&wtime);
		break;
	}
	case RTC_SET_TIME:	/* Set the RTC */
	{
		struct rtc_time rtc_tm;
		unsigned char mon, day, hrs, min, sec, leap_yr;
		unsigned char save_control, save_freq_select;
		unsigned int yrs;
#ifdef CONFIG_MACH_DECSTATION
		unsigned int real_yrs;
#endif

		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		if (copy_from_user(&rtc_tm, (struct rtc_time __user *)arg,
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

		yrs -= epoch;
		if (yrs > 255)		/* They are unsigned */
			return -EINVAL;

		spin_lock_irq(&rtc_lock);
#ifdef CONFIG_MACH_DECSTATION
		real_yrs = yrs;
		yrs = 72;

		/*
		 * We want to keep the year set to 73 until March
		 * for non-leap years, so that Feb, 29th is handled
		 * correctly.
		 */
		if (!leap_yr && mon < 3) {
			real_yrs--;
			yrs = 73;
		}
#endif
		/* These limits and adjustments are independent of
		 * whether the chip is in binary mode or not.
		 */
		if (yrs > 169) {
			spin_unlock_irq(&rtc_lock);
			return -EINVAL;
		}
		if (yrs >= 100)
			yrs -= 100;

		if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY)
		    || RTC_ALWAYS_BCD) {
			sec = bin2bcd(sec);
			min = bin2bcd(min);
			hrs = bin2bcd(hrs);
			day = bin2bcd(day);
			mon = bin2bcd(mon);
			yrs = bin2bcd(yrs);
		}

		save_control = CMOS_READ(RTC_CONTROL);
		CMOS_WRITE((save_control|RTC_SET), RTC_CONTROL);
		save_freq_select = CMOS_READ(RTC_FREQ_SELECT);
		CMOS_WRITE((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

#ifdef CONFIG_MACH_DECSTATION
		CMOS_WRITE(real_yrs, RTC_DEC_YEAR);
#endif
		CMOS_WRITE(yrs, RTC_YEAR);
		CMOS_WRITE(mon, RTC_MONTH);
		CMOS_WRITE(day, RTC_DAY_OF_MONTH);
		CMOS_WRITE(hrs, RTC_HOURS);
		CMOS_WRITE(min, RTC_MINUTES);
		CMOS_WRITE(sec, RTC_SECONDS);

		CMOS_WRITE(save_control, RTC_CONTROL);
		CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);

		spin_unlock_irq(&rtc_lock);
		return 0;
	}
#ifdef RTC_IRQ
	case RTC_IRQP_READ:	/* Read the periodic IRQ rate.	*/
	{
		return put_user(rtc_freq, (unsigned long __user *)arg);
	}
	case RTC_IRQP_SET:	/* Set periodic IRQ rate.	*/
	{
		int tmp = 0;
		unsigned char val;
		/* can be called from isr via rtc_control() */
		unsigned long flags;

		/*
		 * The max we can do is 8192Hz.
		 */
		if ((arg < 2) || (arg > 8192))
			return -EINVAL;
		/*
		 * We don't really want Joe User generating more
		 * than 64Hz of interrupts on a multi-user machine.
		 */
		if (!kernel && (arg > rtc_max_user_freq) &&
					!capable(CAP_SYS_RESOURCE))
			return -EACCES;

		while (arg > (1<<tmp))
			tmp++;

		/*
		 * Check that the input was really a power of 2.
		 */
		if (arg != (1<<tmp))
			return -EINVAL;

		rtc_freq = arg;

		spin_lock_irqsave(&rtc_lock, flags);
		if (hpet_set_periodic_freq(arg)) {
			spin_unlock_irqrestore(&rtc_lock, flags);
			return 0;
		}

		val = CMOS_READ(RTC_FREQ_SELECT) & 0xf0;
		val |= (16 - tmp);
		CMOS_WRITE(val, RTC_FREQ_SELECT);
		spin_unlock_irqrestore(&rtc_lock, flags);
		return 0;
	}
#endif
	case RTC_EPOCH_READ:	/* Read the epoch.	*/
	{
		return put_user(epoch, (unsigned long __user *)arg);
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
		return -ENOTTY;
	}
	return copy_to_user((void __user *)arg,
			    &wtime, sizeof wtime) ? -EFAULT : 0;
}

static long rtc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret;
	ret = rtc_do_ioctl(cmd, arg, 0);
	return ret;
}

/*
 *	We enforce only one user at a time here with the open/close.
 *	Also clear the previous interrupt data on an open, and clean
 *	up things on a close.
 */
static int rtc_open(struct inode *inode, struct file *file)
{
	spin_lock_irq(&rtc_lock);

	if (rtc_status & RTC_IS_OPEN)
		goto out_busy;

	rtc_status |= RTC_IS_OPEN;

	rtc_irq_data = 0;
	spin_unlock_irq(&rtc_lock);
	return 0;

out_busy:
	spin_unlock_irq(&rtc_lock);
	return -EBUSY;
}

static int rtc_fasync(int fd, struct file *filp, int on)
{
	return fasync_helper(fd, filp, on, &rtc_async_queue);
}

static int rtc_release(struct inode *inode, struct file *file)
{
#ifdef RTC_IRQ
	unsigned char tmp;

	if (rtc_has_irq == 0)
		goto no_irq;

	/*
	 * Turn off all interrupts once the device is no longer
	 * in use, and clear the data.
	 */

	spin_lock_irq(&rtc_lock);
	if (!hpet_mask_rtc_irq_bit(RTC_PIE | RTC_AIE | RTC_UIE)) {
		tmp = CMOS_READ(RTC_CONTROL);
		tmp &=  ~RTC_PIE;
		tmp &=  ~RTC_AIE;
		tmp &=  ~RTC_UIE;
		CMOS_WRITE(tmp, RTC_CONTROL);
		CMOS_READ(RTC_INTR_FLAGS);
	}
	if (rtc_status & RTC_TIMER_ON) {
		rtc_status &= ~RTC_TIMER_ON;
		del_timer(&rtc_irq_timer);
	}
	spin_unlock_irq(&rtc_lock);

no_irq:
#endif

	spin_lock_irq(&rtc_lock);
	rtc_irq_data = 0;
	rtc_status &= ~RTC_IS_OPEN;
	spin_unlock_irq(&rtc_lock);

	return 0;
}

#ifdef RTC_IRQ
static unsigned int rtc_poll(struct file *file, poll_table *wait)
{
	unsigned long l;

	if (rtc_has_irq == 0)
		return 0;

	poll_wait(file, &rtc_wait, wait);

	spin_lock_irq(&rtc_lock);
	l = rtc_irq_data;
	spin_unlock_irq(&rtc_lock);

	if (l != 0)
		return POLLIN | POLLRDNORM;
	return 0;
}
#endif

int rtc_register(rtc_task_t *task)
{
#ifndef RTC_IRQ
	return -EIO;
#else
	if (task == NULL || task->func == NULL)
		return -EINVAL;
	spin_lock_irq(&rtc_lock);
	if (rtc_status & RTC_IS_OPEN) {
		spin_unlock_irq(&rtc_lock);
		return -EBUSY;
	}
	spin_lock(&rtc_task_lock);
	if (rtc_callback) {
		spin_unlock(&rtc_task_lock);
		spin_unlock_irq(&rtc_lock);
		return -EBUSY;
	}
	rtc_status |= RTC_IS_OPEN;
	rtc_callback = task;
	spin_unlock(&rtc_task_lock);
	spin_unlock_irq(&rtc_lock);
	return 0;
#endif
}
EXPORT_SYMBOL(rtc_register);

int rtc_unregister(rtc_task_t *task)
{
#ifndef RTC_IRQ
	return -EIO;
#else
	unsigned char tmp;

	spin_lock_irq(&rtc_lock);
	spin_lock(&rtc_task_lock);
	if (rtc_callback != task) {
		spin_unlock(&rtc_task_lock);
		spin_unlock_irq(&rtc_lock);
		return -ENXIO;
	}
	rtc_callback = NULL;

	/* disable controls */
	if (!hpet_mask_rtc_irq_bit(RTC_PIE | RTC_AIE | RTC_UIE)) {
		tmp = CMOS_READ(RTC_CONTROL);
		tmp &= ~RTC_PIE;
		tmp &= ~RTC_AIE;
		tmp &= ~RTC_UIE;
		CMOS_WRITE(tmp, RTC_CONTROL);
		CMOS_READ(RTC_INTR_FLAGS);
	}
	if (rtc_status & RTC_TIMER_ON) {
		rtc_status &= ~RTC_TIMER_ON;
		del_timer(&rtc_irq_timer);
	}
	rtc_status &= ~RTC_IS_OPEN;
	spin_unlock(&rtc_task_lock);
	spin_unlock_irq(&rtc_lock);
	return 0;
#endif
}
EXPORT_SYMBOL(rtc_unregister);

int rtc_control(rtc_task_t *task, unsigned int cmd, unsigned long arg)
{
#ifndef RTC_IRQ
	return -EIO;
#else
	unsigned long flags;
	if (cmd != RTC_PIE_ON && cmd != RTC_PIE_OFF && cmd != RTC_IRQP_SET)
		return -EINVAL;
	spin_lock_irqsave(&rtc_task_lock, flags);
	if (rtc_callback != task) {
		spin_unlock_irqrestore(&rtc_task_lock, flags);
		return -ENXIO;
	}
	spin_unlock_irqrestore(&rtc_task_lock, flags);
	return rtc_do_ioctl(cmd, arg, 1);
#endif
}
EXPORT_SYMBOL(rtc_control);

/*
 *	The various file operations we support.
 */

static const struct file_operations rtc_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= rtc_read,
#ifdef RTC_IRQ
	.poll		= rtc_poll,
#endif
	.unlocked_ioctl	= rtc_ioctl,
	.open		= rtc_open,
	.release	= rtc_release,
	.fasync		= rtc_fasync,
};

static struct miscdevice rtc_dev = {
	.minor		= RTC_MINOR,
	.name		= "rtc",
	.fops		= &rtc_fops,
};

#ifdef CONFIG_PROC_FS
static const struct file_operations rtc_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= rtc_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

static resource_size_t rtc_size;

static struct resource * __init rtc_request_region(resource_size_t size)
{
	struct resource *r;

	if (RTC_IOMAPPED)
		r = request_region(RTC_PORT(0), size, "rtc");
	else
		r = request_mem_region(RTC_PORT(0), size, "rtc");

	if (r)
		rtc_size = size;

	return r;
}

static void rtc_release_region(void)
{
	if (RTC_IOMAPPED)
		release_region(RTC_PORT(0), rtc_size);
	else
		release_mem_region(RTC_PORT(0), rtc_size);
}

static int __init rtc_init(void)
{
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *ent;
#endif
#if defined(__alpha__) || defined(__mips__)
	unsigned int year, ctrl;
	char *guess = NULL;
#endif
#ifdef CONFIG_SPARC32
	struct device_node *ebus_dp;
	struct platform_device *op;
#else
	void *r;
#ifdef RTC_IRQ
	irq_handler_t rtc_int_handler_ptr;
#endif
#endif

#ifdef CONFIG_SPARC32
	for_each_node_by_name(ebus_dp, "ebus") {
		struct device_node *dp;
		for (dp = ebus_dp; dp; dp = dp->sibling) {
			if (!strcmp(dp->name, "rtc")) {
				op = of_find_device_by_node(dp);
				if (op) {
					rtc_port = op->resource[0].start;
					rtc_irq = op->irqs[0];
					goto found;
				}
			}
		}
	}
	rtc_has_irq = 0;
	printk(KERN_ERR "rtc_init: no PC rtc found\n");
	return -EIO;

found:
	if (!rtc_irq) {
		rtc_has_irq = 0;
		goto no_irq;
	}

	/*
	 * XXX Interrupt pin #7 in Espresso is shared between RTC and
	 * PCI Slot 2 INTA# (and some INTx# in Slot 1).
	 */
	if (request_irq(rtc_irq, rtc_interrupt, IRQF_SHARED, "rtc",
			(void *)&rtc_port)) {
		rtc_has_irq = 0;
		printk(KERN_ERR "rtc: cannot register IRQ %d\n", rtc_irq);
		return -EIO;
	}
no_irq:
#else
	r = rtc_request_region(RTC_IO_EXTENT);

	/*
	 * If we've already requested a smaller range (for example, because
	 * PNPBIOS or ACPI told us how the device is configured), the request
	 * above might fail because it's too big.
	 *
	 * If so, request just the range we actually use.
	 */
	if (!r)
		r = rtc_request_region(RTC_IO_EXTENT_USED);
	if (!r) {
#ifdef RTC_IRQ
		rtc_has_irq = 0;
#endif
		printk(KERN_ERR "rtc: I/O resource %lx is not free.\n",
		       (long)(RTC_PORT(0)));
		return -EIO;
	}

#ifdef RTC_IRQ
	if (is_hpet_enabled()) {
		int err;

		rtc_int_handler_ptr = hpet_rtc_interrupt;
		err = hpet_register_irq_handler(rtc_interrupt);
		if (err != 0) {
			printk(KERN_WARNING "hpet_register_irq_handler failed "
					"in rtc_init().");
			return err;
		}
	} else {
		rtc_int_handler_ptr = rtc_interrupt;
	}

	if (request_irq(RTC_IRQ, rtc_int_handler_ptr, 0, "rtc", NULL)) {
		/* Yeah right, seeing as irq 8 doesn't even hit the bus. */
		rtc_has_irq = 0;
		printk(KERN_ERR "rtc: IRQ %d is not free.\n", RTC_IRQ);
		rtc_release_region();

		return -EIO;
	}
	hpet_rtc_timer_init();

#endif

#endif /* CONFIG_SPARC32 vs. others */

	if (misc_register(&rtc_dev)) {
#ifdef RTC_IRQ
		free_irq(RTC_IRQ, NULL);
		hpet_unregister_irq_handler(rtc_interrupt);
		rtc_has_irq = 0;
#endif
		rtc_release_region();
		return -ENODEV;
	}

#ifdef CONFIG_PROC_FS
	ent = proc_create("driver/rtc", 0, NULL, &rtc_proc_fops);
	if (!ent)
		printk(KERN_WARNING "rtc: Failed to register with procfs.\n");
#endif

#if defined(__alpha__) || defined(__mips__)
	rtc_freq = HZ;

	/* Each operating system on an Alpha uses its own epoch.
	   Let's try to guess which one we are using now. */

	if (rtc_is_updating() != 0)
		msleep(20);

	spin_lock_irq(&rtc_lock);
	year = CMOS_READ(RTC_YEAR);
	ctrl = CMOS_READ(RTC_CONTROL);
	spin_unlock_irq(&rtc_lock);

	if (!(ctrl & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
		year = bcd2bin(year);       /* This should never happen... */

	if (year < 20) {
		epoch = 2000;
		guess = "SRM (post-2000)";
	} else if (year >= 20 && year < 48) {
		epoch = 1980;
		guess = "ARC console";
	} else if (year >= 48 && year < 72) {
		epoch = 1952;
		guess = "Digital UNIX";
#if defined(__mips__)
	} else if (year >= 72 && year < 74) {
		epoch = 2000;
		guess = "Digital DECstation";
#else
	} else if (year >= 70) {
		epoch = 1900;
		guess = "Standard PC (1900)";
#endif
	}
	if (guess)
		printk(KERN_INFO "rtc: %s epoch (%lu) detected\n",
			guess, epoch);
#endif
#ifdef RTC_IRQ
	if (rtc_has_irq == 0)
		goto no_irq2;

	spin_lock_irq(&rtc_lock);
	rtc_freq = 1024;
	if (!hpet_set_periodic_freq(rtc_freq)) {
		/*
		 * Initialize periodic frequency to CMOS reset default,
		 * which is 1024Hz
		 */
		CMOS_WRITE(((CMOS_READ(RTC_FREQ_SELECT) & 0xF0) | 0x06),
			   RTC_FREQ_SELECT);
	}
	spin_unlock_irq(&rtc_lock);
no_irq2:
#endif

	(void) init_sysctl();

	printk(KERN_INFO "Real Time Clock Driver v" RTC_VERSION "\n");

	return 0;
}

static void __exit rtc_exit(void)
{
	cleanup_sysctl();
	remove_proc_entry("driver/rtc", NULL);
	misc_deregister(&rtc_dev);

#ifdef CONFIG_SPARC32
	if (rtc_has_irq)
		free_irq(rtc_irq, &rtc_port);
#else
	rtc_release_region();
#ifdef RTC_IRQ
	if (rtc_has_irq) {
		free_irq(RTC_IRQ, NULL);
		hpet_unregister_irq_handler(hpet_rtc_interrupt);
	}
#endif
#endif /* CONFIG_SPARC32 */
}

module_init(rtc_init);
module_exit(rtc_exit);

#ifdef RTC_IRQ
/*
 *	At IRQ rates >= 4096Hz, an interrupt may get lost altogether.
 *	(usually during an IDE disk interrupt, with IRQ unmasking off)
 *	Since the interrupt handler doesn't get called, the IRQ status
 *	byte doesn't get read, and the RTC stops generating interrupts.
 *	A timer is set, and will call this function if/when that happens.
 *	To get it out of this stalled state, we just read the status.
 *	At least a jiffy of interrupts (rtc_freq/HZ) will have been lost.
 *	(You *really* shouldn't be trying to use a non-realtime system
 *	for something that requires a steady > 1KHz signal anyways.)
 */

static void rtc_dropped_irq(unsigned long data)
{
	unsigned long freq;

	spin_lock_irq(&rtc_lock);

	if (hpet_rtc_dropped_irq()) {
		spin_unlock_irq(&rtc_lock);
		return;
	}

	/* Just in case someone disabled the timer from behind our back... */
	if (rtc_status & RTC_TIMER_ON)
		mod_timer(&rtc_irq_timer, jiffies + HZ/rtc_freq + 2*HZ/100);

	rtc_irq_data += ((rtc_freq/HZ)<<8);
	rtc_irq_data &= ~0xff;
	rtc_irq_data |= (CMOS_READ(RTC_INTR_FLAGS) & 0xF0);	/* restart */

	freq = rtc_freq;

	spin_unlock_irq(&rtc_lock);

	printk_ratelimited(KERN_WARNING "rtc: lost some interrupts at %ldHz.\n",
			   freq);

	/* Now we have new data */
	wake_up_interruptible(&rtc_wait);

	kill_fasync(&rtc_async_queue, SIGIO, POLL_IN);
}
#endif

#ifdef CONFIG_PROC_FS
/*
 *	Info exported via "/proc/driver/rtc".
 */

static int rtc_proc_show(struct seq_file *seq, void *v)
{
#define YN(bit) ((ctrl & bit) ? "yes" : "no")
#define NY(bit) ((ctrl & bit) ? "no" : "yes")
	struct rtc_time tm;
	unsigned char batt, ctrl;
	unsigned long freq;

	spin_lock_irq(&rtc_lock);
	batt = CMOS_READ(RTC_VALID) & RTC_VRT;
	ctrl = CMOS_READ(RTC_CONTROL);
	freq = rtc_freq;
	spin_unlock_irq(&rtc_lock);


	rtc_get_rtc_time(&tm);

	/*
	 * There is no way to tell if the luser has the RTC set for local
	 * time or for Universal Standard Time (GMT). Probably local though.
	 */
	seq_printf(seq,
		   "rtc_time\t: %02d:%02d:%02d\n"
		   "rtc_date\t: %04d-%02d-%02d\n"
		   "rtc_epoch\t: %04lu\n",
		   tm.tm_hour, tm.tm_min, tm.tm_sec,
		   tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, epoch);

	get_rtc_alm_time(&tm);

	/*
	 * We implicitly assume 24hr mode here. Alarm values >= 0xc0 will
	 * match any value for that particular field. Values that are
	 * greater than a valid time, but less than 0xc0 shouldn't appear.
	 */
	seq_puts(seq, "alarm\t\t: ");
	if (tm.tm_hour <= 24)
		seq_printf(seq, "%02d:", tm.tm_hour);
	else
		seq_puts(seq, "**:");

	if (tm.tm_min <= 59)
		seq_printf(seq, "%02d:", tm.tm_min);
	else
		seq_puts(seq, "**:");

	if (tm.tm_sec <= 59)
		seq_printf(seq, "%02d\n", tm.tm_sec);
	else
		seq_puts(seq, "**\n");

	seq_printf(seq,
		   "DST_enable\t: %s\n"
		   "BCD\t\t: %s\n"
		   "24hr\t\t: %s\n"
		   "square_wave\t: %s\n"
		   "alarm_IRQ\t: %s\n"
		   "update_IRQ\t: %s\n"
		   "periodic_IRQ\t: %s\n"
		   "periodic_freq\t: %ld\n"
		   "batt_status\t: %s\n",
		   YN(RTC_DST_EN),
		   NY(RTC_DM_BINARY),
		   YN(RTC_24H),
		   YN(RTC_SQWE),
		   YN(RTC_AIE),
		   YN(RTC_UIE),
		   YN(RTC_PIE),
		   freq,
		   batt ? "okay" : "dead");

	return  0;
#undef YN
#undef NY
}

static int rtc_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtc_proc_show, NULL);
}
#endif

static void rtc_get_rtc_time(struct rtc_time *rtc_tm)
{
	unsigned long uip_watchdog = jiffies, flags;
	unsigned char ctrl;
#ifdef CONFIG_MACH_DECSTATION
	unsigned int real_year;
#endif

	/*
	 * read RTC once any update in progress is done. The update
	 * can take just over 2ms. We wait 20ms. There is no need to
	 * to poll-wait (up to 1s - eeccch) for the falling edge of RTC_UIP.
	 * If you need to know *exactly* when a second has started, enable
	 * periodic update complete interrupts, (via ioctl) and then
	 * immediately read /dev/rtc which will block until you get the IRQ.
	 * Once the read clears, read the RTC time (again via ioctl). Easy.
	 */

	while (rtc_is_updating() != 0 &&
	       time_before(jiffies, uip_watchdog + 2*HZ/100))
		cpu_relax();

	/*
	 * Only the values that we read from the RTC are set. We leave
	 * tm_wday, tm_yday and tm_isdst untouched. Note that while the
	 * RTC has RTC_DAY_OF_WEEK, we should usually ignore it, as it is
	 * only updated by the RTC when initially set to a non-zero value.
	 */
	spin_lock_irqsave(&rtc_lock, flags);
	rtc_tm->tm_sec = CMOS_READ(RTC_SECONDS);
	rtc_tm->tm_min = CMOS_READ(RTC_MINUTES);
	rtc_tm->tm_hour = CMOS_READ(RTC_HOURS);
	rtc_tm->tm_mday = CMOS_READ(RTC_DAY_OF_MONTH);
	rtc_tm->tm_mon = CMOS_READ(RTC_MONTH);
	rtc_tm->tm_year = CMOS_READ(RTC_YEAR);
	/* Only set from 2.6.16 onwards */
	rtc_tm->tm_wday = CMOS_READ(RTC_DAY_OF_WEEK);

#ifdef CONFIG_MACH_DECSTATION
	real_year = CMOS_READ(RTC_DEC_YEAR);
#endif
	ctrl = CMOS_READ(RTC_CONTROL);
	spin_unlock_irqrestore(&rtc_lock, flags);

	if (!(ctrl & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		rtc_tm->tm_sec = bcd2bin(rtc_tm->tm_sec);
		rtc_tm->tm_min = bcd2bin(rtc_tm->tm_min);
		rtc_tm->tm_hour = bcd2bin(rtc_tm->tm_hour);
		rtc_tm->tm_mday = bcd2bin(rtc_tm->tm_mday);
		rtc_tm->tm_mon = bcd2bin(rtc_tm->tm_mon);
		rtc_tm->tm_year = bcd2bin(rtc_tm->tm_year);
		rtc_tm->tm_wday = bcd2bin(rtc_tm->tm_wday);
	}

#ifdef CONFIG_MACH_DECSTATION
	rtc_tm->tm_year += real_year - 72;
#endif

	/*
	 * Account for differences between how the RTC uses the values
	 * and how they are defined in a struct rtc_time;
	 */
	rtc_tm->tm_year += epoch - 1900;
	if (rtc_tm->tm_year <= 69)
		rtc_tm->tm_year += 100;

	rtc_tm->tm_mon--;
}

static void get_rtc_alm_time(struct rtc_time *alm_tm)
{
	unsigned char ctrl;

	/*
	 * Only the values that we read from the RTC are set. That
	 * means only tm_hour, tm_min, and tm_sec.
	 */
	spin_lock_irq(&rtc_lock);
	alm_tm->tm_sec = CMOS_READ(RTC_SECONDS_ALARM);
	alm_tm->tm_min = CMOS_READ(RTC_MINUTES_ALARM);
	alm_tm->tm_hour = CMOS_READ(RTC_HOURS_ALARM);
	ctrl = CMOS_READ(RTC_CONTROL);
	spin_unlock_irq(&rtc_lock);

	if (!(ctrl & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		alm_tm->tm_sec = bcd2bin(alm_tm->tm_sec);
		alm_tm->tm_min = bcd2bin(alm_tm->tm_min);
		alm_tm->tm_hour = bcd2bin(alm_tm->tm_hour);
	}
}

#ifdef RTC_IRQ
/*
 * Used to disable/enable interrupts for any one of UIE, AIE, PIE.
 * Rumour has it that if you frob the interrupt enable/disable
 * bits in RTC_CONTROL, you should read RTC_INTR_FLAGS, to
 * ensure you actually start getting interrupts. Probably for
 * compatibility with older/broken chipset RTC implementations.
 * We also clear out any old irq data after an ioctl() that
 * meddles with the interrupt enable/disable bits.
 */

static void mask_rtc_irq_bit_locked(unsigned char bit)
{
	unsigned char val;

	if (hpet_mask_rtc_irq_bit(bit))
		return;
	val = CMOS_READ(RTC_CONTROL);
	val &=  ~bit;
	CMOS_WRITE(val, RTC_CONTROL);
	CMOS_READ(RTC_INTR_FLAGS);

	rtc_irq_data = 0;
}

static void set_rtc_irq_bit_locked(unsigned char bit)
{
	unsigned char val;

	if (hpet_set_rtc_irq_bit(bit))
		return;
	val = CMOS_READ(RTC_CONTROL);
	val |= bit;
	CMOS_WRITE(val, RTC_CONTROL);
	CMOS_READ(RTC_INTR_FLAGS);

	rtc_irq_data = 0;
}
#endif

MODULE_AUTHOR("Paul Gortmaker");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(RTC_MINOR);

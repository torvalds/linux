/*
 *  linux/arch/alpha/kernel/rtc.c
 *
 *  Copyright (C) 1991, 1992, 1995, 1999, 2000  Linus Torvalds
 *
 * This file contains date handling.
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mc146818rtc.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>

#include "proto.h"


/*
 * Support for the RTC device.
 *
 * We don't want to use the rtc-cmos driver, because we don't want to support
 * alarms, as that would be indistinguishable from timer interrupts.
 *
 * Further, generic code is really, really tied to a 1900 epoch.  This is
 * true in __get_rtc_time as well as the users of struct rtc_time e.g.
 * rtc_tm_to_time.  Thankfully all of the other epochs in use are later
 * than 1900, and so it's easy to adjust.
 */

static unsigned long rtc_epoch;

static int __init
specifiy_epoch(char *str)
{
	unsigned long epoch = simple_strtoul(str, NULL, 0);
	if (epoch < 1900)
		printk("Ignoring invalid user specified epoch %lu\n", epoch);
	else
		rtc_epoch = epoch;
	return 1;
}
__setup("epoch=", specifiy_epoch);

static void __init
init_rtc_epoch(void)
{
	int epoch, year, ctrl;

	if (rtc_epoch != 0) {
		/* The epoch was specified on the command-line.  */
		return;
	}

	/* Detect the epoch in use on this computer.  */
	ctrl = CMOS_READ(RTC_CONTROL);
	year = CMOS_READ(RTC_YEAR);
	if (!(ctrl & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
		year = bcd2bin(year);

	/* PC-like is standard; used for year >= 70 */
	epoch = 1900;
	if (year < 20) {
		epoch = 2000;
	} else if (year >= 20 && year < 48) {
		/* NT epoch */
		epoch = 1980;
	} else if (year >= 48 && year < 70) {
		/* Digital UNIX epoch */
		epoch = 1952;
	}
	rtc_epoch = epoch;

	printk(KERN_INFO "Using epoch %d for rtc year %d\n", epoch, year);
}

static int
alpha_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	mc146818_get_time(tm);

	/* Adjust for non-default epochs.  It's easier to depend on the
	   generic __get_rtc_time and adjust the epoch here than create
	   a copy of __get_rtc_time with the edits we need.  */
	if (rtc_epoch != 1900) {
		int year = tm->tm_year;
		/* Undo the century adjustment made in __get_rtc_time.  */
		if (year >= 100)
			year -= 100;
		year += rtc_epoch - 1900;
		/* Redo the century adjustment with the epoch in place.  */
		if (year <= 69)
			year += 100;
		tm->tm_year = year;
	}

	return rtc_valid_tm(tm);
}

static int
alpha_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rtc_time xtm;

	if (rtc_epoch != 1900) {
		xtm = *tm;
		xtm.tm_year -= rtc_epoch - 1900;
		tm = &xtm;
	}

	return mc146818_set_time(tm);
}

static int
alpha_rtc_set_mmss(struct device *dev, time64_t nowtime)
{
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;
	unsigned char save_control, save_freq_select;

	/* Note: This code only updates minutes and seconds.  Comments
	   indicate this was to avoid messing with unknown time zones,
	   and with the epoch nonsense described above.  In order for
	   this to work, the existing clock cannot be off by more than
	   15 minutes.

	   ??? This choice is may be out of date.  The x86 port does
	   not have problems with timezones, and the epoch processing has
	   now been fixed in alpha_set_rtc_time.

	   In either case, one can always force a full rtc update with
	   the userland hwclock program, so surely 15 minute accuracy
	   is no real burden.  */

	/* In order to set the CMOS clock precisely, we have to be called
	   500 ms after the second nowtime has started, because when
	   nowtime is written into the registers of the CMOS clock, it will
	   jump to the next second precisely 500 ms later. Check the Motorola
	   MC146818A or Dallas DS12887 data sheet for details.  */

	/* irq are locally disabled here */
	spin_lock(&rtc_lock);
	/* Tell the clock it's being set */
	save_control = CMOS_READ(RTC_CONTROL);
	CMOS_WRITE((save_control|RTC_SET), RTC_CONTROL);

	/* Stop and reset prescaler */
	save_freq_select = CMOS_READ(RTC_FREQ_SELECT);
	CMOS_WRITE((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

	cmos_minutes = CMOS_READ(RTC_MINUTES);
	if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
		cmos_minutes = bcd2bin(cmos_minutes);

	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - cmos_minutes) + 15) / 30) & 1) {
		/* correct for half hour time zone */
		real_minutes += 30;
	}
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) < 30) {
		if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
			real_seconds = bin2bcd(real_seconds);
			real_minutes = bin2bcd(real_minutes);
		}
		CMOS_WRITE(real_seconds,RTC_SECONDS);
		CMOS_WRITE(real_minutes,RTC_MINUTES);
	} else {
		printk_once(KERN_NOTICE
			    "set_rtc_mmss: can't update from %d to %d\n",
			    cmos_minutes, real_minutes);
		retval = -1;
	}

	/* The following flags have to be released exactly in this order,
	 * otherwise the DS12887 (popular MC146818A clone with integrated
	 * battery and quartz) will not reset the oscillator and will not
	 * update precisely 500 ms later. You won't find this mentioned in
	 * the Dallas Semiconductor data sheets, but who believes data
	 * sheets anyway ...                           -- Markus Kuhn
	 */
	CMOS_WRITE(save_control, RTC_CONTROL);
	CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);
	spin_unlock(&rtc_lock);

	return retval;
}

static int
alpha_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case RTC_EPOCH_READ:
		return put_user(rtc_epoch, (unsigned long __user *)arg);
	case RTC_EPOCH_SET:
		if (arg < 1900)
			return -EINVAL;
		rtc_epoch = arg;
		return 0;
	default:
		return -ENOIOCTLCMD;
	}
}

static const struct rtc_class_ops alpha_rtc_ops = {
	.read_time = alpha_rtc_read_time,
	.set_time = alpha_rtc_set_time,
	.set_mmss64 = alpha_rtc_set_mmss,
	.ioctl = alpha_rtc_ioctl,
};

/*
 * Similarly, except do the actual CMOS access on the boot cpu only.
 * This requires marshalling the data across an interprocessor call.
 */

#if defined(CONFIG_SMP) && \
    (defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_MARVEL))
# define HAVE_REMOTE_RTC 1

union remote_data {
	struct rtc_time *tm;
	unsigned long now;
	long retval;
};

static void
do_remote_read(void *data)
{
	union remote_data *x = data;
	x->retval = alpha_rtc_read_time(NULL, x->tm);
}

static int
remote_read_time(struct device *dev, struct rtc_time *tm)
{
	union remote_data x;
	if (smp_processor_id() != boot_cpuid) {
		x.tm = tm;
		smp_call_function_single(boot_cpuid, do_remote_read, &x, 1);
		return x.retval;
	}
	return alpha_rtc_read_time(NULL, tm);
}

static void
do_remote_set(void *data)
{
	union remote_data *x = data;
	x->retval = alpha_rtc_set_time(NULL, x->tm);
}

static int
remote_set_time(struct device *dev, struct rtc_time *tm)
{
	union remote_data x;
	if (smp_processor_id() != boot_cpuid) {
		x.tm = tm;
		smp_call_function_single(boot_cpuid, do_remote_set, &x, 1);
		return x.retval;
	}
	return alpha_rtc_set_time(NULL, tm);
}

static void
do_remote_mmss(void *data)
{
	union remote_data *x = data;
	x->retval = alpha_rtc_set_mmss(NULL, x->now);
}

static int
remote_set_mmss(struct device *dev, time64_t now)
{
	union remote_data x;
	if (smp_processor_id() != boot_cpuid) {
		x.now = now;
		smp_call_function_single(boot_cpuid, do_remote_mmss, &x, 1);
		return x.retval;
	}
	return alpha_rtc_set_mmss(NULL, now);
}

static const struct rtc_class_ops remote_rtc_ops = {
	.read_time = remote_read_time,
	.set_time = remote_set_time,
	.set_mmss64 = remote_set_mmss,
	.ioctl = alpha_rtc_ioctl,
};
#endif

static int __init
alpha_rtc_init(void)
{
	const struct rtc_class_ops *ops;
	struct platform_device *pdev;
	struct rtc_device *rtc;
	const char *name;

	init_rtc_epoch();
	name = "rtc-alpha";
	ops = &alpha_rtc_ops;

#ifdef HAVE_REMOTE_RTC
	if (alpha_mv.rtc_boot_cpu_only)
		ops = &remote_rtc_ops;
#endif

	pdev = platform_device_register_simple(name, -1, NULL, 0);
	rtc = devm_rtc_device_register(&pdev->dev, name, ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	platform_set_drvdata(pdev, rtc);
	return 0;
}
device_initcall(alpha_rtc_init);

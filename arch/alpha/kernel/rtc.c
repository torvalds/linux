// SPDX-License-Identifier: GPL-2.0
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

	return 0;
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

static const struct rtc_class_ops remote_rtc_ops = {
	.read_time = remote_read_time,
	.set_time = remote_set_time,
	.ioctl = alpha_rtc_ioctl,
};
#endif

static int __init
alpha_rtc_init(void)
{
	struct platform_device *pdev;
	struct rtc_device *rtc;

	init_rtc_epoch();

	pdev = platform_device_register_simple("rtc-alpha", -1, NULL, 0);
	rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	platform_set_drvdata(pdev, rtc);
	rtc->ops = &alpha_rtc_ops;

#ifdef HAVE_REMOTE_RTC
	if (alpha_mv.rtc_boot_cpu_only)
		rtc->ops = &remote_rtc_ops;
#endif

	return rtc_register_device(rtc);
}
device_initcall(alpha_rtc_init);

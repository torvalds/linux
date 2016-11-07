/*
 *  arch/sh/kernel/time.c
 *
 *  Copyright (C) 1999  Tetsuya Okada & Niibe Yutaka
 *  Copyright (C) 2000  Philipp Rumpf <prumpf@tux.org>
 *  Copyright (C) 2002 - 2009  Paul Mundt
 *  Copyright (C) 2002  M. R. Brown  <mrbrown@linux-sh.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/profile.h>
#include <linux/timex.h>
#include <linux/sched.h>
#include <linux/clockchips.h>
#include <linux/platform_device.h>
#include <linux/smp.h>
#include <linux/rtc.h>
#include <asm/clock.h>
#include <asm/rtc.h>

/* Dummy RTC ops */
static void null_rtc_get_time(struct timespec *tv)
{
	tv->tv_sec = mktime(2000, 1, 1, 0, 0, 0);
	tv->tv_nsec = 0;
}

static int null_rtc_set_time(const time_t secs)
{
	return 0;
}

void (*rtc_sh_get_time)(struct timespec *) = null_rtc_get_time;
int (*rtc_sh_set_time)(const time_t) = null_rtc_set_time;

void read_persistent_clock(struct timespec *ts)
{
	rtc_sh_get_time(ts);
}

#ifdef CONFIG_GENERIC_CMOS_UPDATE
int update_persistent_clock(struct timespec now)
{
	return rtc_sh_set_time(now.tv_sec);
}
#endif

static int rtc_generic_get_time(struct device *dev, struct rtc_time *tm)
{
	struct timespec tv;

	rtc_sh_get_time(&tv);
	rtc_time_to_tm(tv.tv_sec, tm);
	return 0;
}

static int rtc_generic_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long secs;

	rtc_tm_to_time(tm, &secs);
	if ((rtc_sh_set_time == null_rtc_set_time) ||
	    (rtc_sh_set_time(secs) < 0))
		return -EOPNOTSUPP;

	return 0;
}

static const struct rtc_class_ops rtc_generic_ops = {
	.read_time = rtc_generic_get_time,
	.set_time = rtc_generic_set_time,
};

static int __init rtc_generic_init(void)
{
	struct platform_device *pdev;

	if (rtc_sh_get_time == null_rtc_get_time)
		return -ENODEV;

	pdev = platform_device_register_data(NULL, "rtc-generic", -1,
					     &rtc_generic_ops,
					     sizeof(rtc_generic_ops));


	return PTR_ERR_OR_ZERO(pdev);
}
device_initcall(rtc_generic_init);

void (*board_time_init)(void);

static void __init sh_late_time_init(void)
{
	/*
	 * Make sure all compiled-in early timers register themselves.
	 *
	 * Run probe() for two "earlytimer" devices, these will be the
	 * clockevents and clocksource devices respectively. In the event
	 * that only a clockevents device is available, we -ENODEV on the
	 * clocksource and the jiffies clocksource is used transparently
	 * instead. No error handling is necessary here.
	 */
	early_platform_driver_register_all("earlytimer");
	early_platform_driver_probe("earlytimer", 2, 0);
}

void __init time_init(void)
{
	if (board_time_init)
		board_time_init();

	clk_init();

	late_time_init = sh_late_time_init;
}

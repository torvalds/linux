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
#include <linux/module.h>
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

#ifdef CONFIG_GENERIC_CMOS_UPDATE
unsigned long read_persistent_clock(void)
{
	struct timespec tv;
	rtc_sh_get_time(&tv);
	return tv.tv_sec;
}

int update_persistent_clock(struct timespec now)
{
	return rtc_sh_set_time(now.tv_sec);
}
#endif

unsigned int get_rtc_time(struct rtc_time *tm)
{
	if (rtc_sh_get_time != null_rtc_get_time) {
		struct timespec tv;

		rtc_sh_get_time(&tv);
		rtc_time_to_tm(tv.tv_sec, tm);
	}

	return RTC_24H;
}
EXPORT_SYMBOL(get_rtc_time);

int set_rtc_time(struct rtc_time *tm)
{
	unsigned long secs;

	rtc_tm_to_time(tm, &secs);
	return rtc_sh_set_time(secs);
}
EXPORT_SYMBOL(set_rtc_time);

static int __init rtc_generic_init(void)
{
	struct platform_device *pdev;

	if (rtc_sh_get_time == null_rtc_get_time)
		return -ENODEV;

	pdev = platform_device_register_simple("rtc-generic", -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	return 0;
}
module_init(rtc_generic_init);

void (*board_time_init)(void);

unsigned long long sched_clock(void)
{
	return (jiffies_64 - INITIAL_JIFFIES) * (NSEC_PER_SEC / HZ);
}

static void __init sh_late_time_init(void)
{
	/*
	 * Make sure all compiled-in early timers register themselves.
	 * Run probe() for one "earlytimer" device.
	 */
	early_platform_driver_register_all("earlytimer");
	early_platform_driver_probe("earlytimer", 1, 0);
}

void __init time_init(void)
{
	if (board_time_init)
		board_time_init();

	clk_init();

	rtc_sh_get_time(&xtime);
	set_normalized_timespec(&wall_to_monotonic,
				-xtime.tv_sec, -xtime.tv_nsec);

#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
	local_timer_setup(smp_processor_id());
#endif

	late_time_init = sh_late_time_init;
}

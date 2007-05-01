/*
 * Common code to keep time when machine suspends.
 *
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * GPLv2
 */

#include <linux/time.h>
#include <linux/sysdev.h>
#include <asm/rtc.h>

static unsigned long suspend_rtc_time;

/*
 * Reset the time after a sleep.
 */
static int timer_resume(struct sys_device *dev)
{
	struct timeval tv;
	struct timespec ts;
	struct rtc_time cur_rtc_tm;
	unsigned long cur_rtc_time, diff;

	/* get current RTC time and convert to seconds */
	get_rtc_time(&cur_rtc_tm);
	rtc_tm_to_time(&cur_rtc_tm, &cur_rtc_time);

	diff = cur_rtc_time - suspend_rtc_time;

	/* adjust time of day by seconds that elapsed while
	 * we were suspended */
	do_gettimeofday(&tv);
	ts.tv_sec = tv.tv_sec + diff;
	ts.tv_nsec = tv.tv_usec * NSEC_PER_USEC;
	do_settimeofday(&ts);

	return 0;
}

static int timer_suspend(struct sys_device *dev, pm_message_t state)
{
	struct rtc_time suspend_rtc_tm;
	WARN_ON(!ppc_md.get_rtc_time);

	get_rtc_time(&suspend_rtc_tm);
	rtc_tm_to_time(&suspend_rtc_tm, &suspend_rtc_time);

	return 0;
}

static struct sysdev_class timer_sysclass = {
	.resume = timer_resume,
	.suspend = timer_suspend,
	set_kset_name("timer"),
};

static struct sys_device device_timer = {
	.id = 0,
	.cls = &timer_sysclass,
};

static int time_init_device(void)
{
	int error = sysdev_class_register(&timer_sysclass);
	if (!error)
		error = sysdev_register(&device_timer);
	return error;
}

device_initcall(time_init_device);

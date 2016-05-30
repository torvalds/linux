/*
 *  linux/arch/m68k/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *
 * This file contains the m68k-specific time handling details.
 * Most of the stuff is located in the machine specific files.
 *
 * 1997-09-10	Updated NTP code according to technical memorandum Jan '96
 *		"A Kernel Model for Precision Timekeeping" by Dave Mills
 */

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>

#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/irq_regs.h>

#include <linux/time.h>
#include <linux/timex.h>
#include <linux/profile.h>


unsigned long (*mach_random_get_entropy)(void);
EXPORT_SYMBOL_GPL(mach_random_get_entropy);


/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "xtime_update()" routine every clocktick
 */
static irqreturn_t timer_interrupt(int irq, void *dummy)
{
	xtime_update(1);
	update_process_times(user_mode(get_irq_regs()));
	profile_tick(CPU_PROFILING);

#ifdef CONFIG_HEARTBEAT
	/* use power LED as a heartbeat instead -- much more useful
	   for debugging -- based on the version for PReP by Cort */
	/* acts like an actual heart beat -- ie thump-thump-pause... */
	if (mach_heartbeat) {
	    static unsigned cnt = 0, period = 0, dist = 0;

	    if (cnt == 0 || cnt == dist)
		mach_heartbeat( 1 );
	    else if (cnt == 7 || cnt == dist+7)
		mach_heartbeat( 0 );

	    if (++cnt > period) {
		cnt = 0;
		/* The hyperbolic function below modifies the heartbeat period
		 * length in dependency of the current (5min) load. It goes
		 * through the points f(0)=126, f(1)=86, f(5)=51,
		 * f(inf)->30. */
		period = ((672<<FSHIFT)/(5*avenrun[0]+(7<<FSHIFT))) + 30;
		dist = period / 4;
	    }
	}
#endif /* CONFIG_HEARTBEAT */
	return IRQ_HANDLED;
}

void read_persistent_clock(struct timespec *ts)
{
	struct rtc_time time;
	ts->tv_sec = 0;
	ts->tv_nsec = 0;

	if (mach_hwclk) {
		mach_hwclk(0, &time);

		if ((time.tm_year += 1900) < 1970)
			time.tm_year += 100;
		ts->tv_sec = mktime(time.tm_year, time.tm_mon, time.tm_mday,
				      time.tm_hour, time.tm_min, time.tm_sec);
	}
}

#if defined(CONFIG_ARCH_USES_GETTIMEOFFSET) && IS_ENABLED(CONFIG_RTC_DRV_GENERIC)
static int rtc_generic_get_time(struct device *dev, struct rtc_time *tm)
{
	mach_hwclk(0, tm);
	return rtc_valid_tm(tm);
}

static int rtc_generic_set_time(struct device *dev, struct rtc_time *tm)
{
	if (mach_hwclk(1, tm) < 0)
		return -EOPNOTSUPP;
	return 0;
}

static int rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct rtc_pll_info pll;
	struct rtc_pll_info __user *argp = (void __user *)arg;

	switch (cmd) {
	case RTC_PLL_GET:
		if (!mach_get_rtc_pll || mach_get_rtc_pll(&pll))
			return -EINVAL;
		return copy_to_user(argp, &pll, sizeof pll) ? -EFAULT : 0;

	case RTC_PLL_SET:
		if (!mach_set_rtc_pll)
			return -EINVAL;
		if (!capable(CAP_SYS_TIME))
			return -EACCES;
		if (copy_from_user(&pll, argp, sizeof(pll)))
			return -EFAULT;
		return mach_set_rtc_pll(&pll);
	}

	return -ENOIOCTLCMD;
}

static const struct rtc_class_ops generic_rtc_ops = {
	.ioctl = rtc_ioctl,
	.read_time = rtc_generic_get_time,
	.set_time = rtc_generic_set_time,
};

static int __init rtc_init(void)
{
	struct platform_device *pdev;

	if (!mach_hwclk)
		return -ENODEV;

	pdev = platform_device_register_data(NULL, "rtc-generic", -1,
					     &generic_rtc_ops,
					     sizeof(generic_rtc_ops));
	return PTR_ERR_OR_ZERO(pdev);
}

module_init(rtc_init);

#endif /* CONFIG_ARCH_USES_GETTIMEOFFSET */

void __init time_init(void)
{
	mach_sched_init(timer_interrupt);
}

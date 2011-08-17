/*
 * Real Time Clock interface for StrongARM SA1x00 and XScale PXA2xx
 *
 * Copyright (c) 2000 Nils Faerber
 *
 * Based on rtc.c by Paul Gortmaker
 *
 * Original Driver by Nils Faerber <nils@kernelconcepts.de>
 *
 * Modifications from:
 *   CIH <cih@coventive.com>
 *   Nicolas Pitre <nico@fluxnic.net>
 *   Andrew Christian <andrew.christian@hp.com>
 *
 * Converted to the RTC subsystem and Driver Model
 *   by Richard Purdie <rpurdie@rpsys.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/pm.h>
#include <linux/bitops.h>

#include <mach/hardware.h>
#include <asm/irq.h>

#ifdef CONFIG_ARCH_PXA
#include <mach/regs-rtc.h>
#include <mach/regs-ost.h>
#endif

#define RTC_DEF_DIVIDER		(32768 - 1)
#define RTC_DEF_TRIM		0

static const unsigned long RTC_FREQ = 1024;
static struct rtc_time rtc_alarm;
static DEFINE_SPINLOCK(sa1100_rtc_lock);

static inline int rtc_periodic_alarm(struct rtc_time *tm)
{
	return  (tm->tm_year == -1) ||
		((unsigned)tm->tm_mon >= 12) ||
		((unsigned)(tm->tm_mday - 1) >= 31) ||
		((unsigned)tm->tm_hour > 23) ||
		((unsigned)tm->tm_min > 59) ||
		((unsigned)tm->tm_sec > 59);
}

/*
 * Calculate the next alarm time given the requested alarm time mask
 * and the current time.
 */
static void rtc_next_alarm_time(struct rtc_time *next, struct rtc_time *now,
	struct rtc_time *alrm)
{
	unsigned long next_time;
	unsigned long now_time;

	next->tm_year = now->tm_year;
	next->tm_mon = now->tm_mon;
	next->tm_mday = now->tm_mday;
	next->tm_hour = alrm->tm_hour;
	next->tm_min = alrm->tm_min;
	next->tm_sec = alrm->tm_sec;

	rtc_tm_to_time(now, &now_time);
	rtc_tm_to_time(next, &next_time);

	if (next_time < now_time) {
		/* Advance one day */
		next_time += 60 * 60 * 24;
		rtc_time_to_tm(next_time, next);
	}
}

static int rtc_update_alarm(struct rtc_time *alrm)
{
	struct rtc_time alarm_tm, now_tm;
	unsigned long now, time;
	int ret;

	do {
		now = RCNR;
		rtc_time_to_tm(now, &now_tm);
		rtc_next_alarm_time(&alarm_tm, &now_tm, alrm);
		ret = rtc_tm_to_time(&alarm_tm, &time);
		if (ret != 0)
			break;

		RTSR = RTSR & (RTSR_HZE|RTSR_ALE|RTSR_AL);
		RTAR = time;
	} while (now != RCNR);

	return ret;
}

static irqreturn_t sa1100_rtc_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = to_platform_device(dev_id);
	struct rtc_device *rtc = platform_get_drvdata(pdev);
	unsigned int rtsr;
	unsigned long events = 0;

	spin_lock(&sa1100_rtc_lock);

	rtsr = RTSR;
	/* clear interrupt sources */
	RTSR = 0;
	/* Fix for a nasty initialization problem the in SA11xx RTSR register.
	 * See also the comments in sa1100_rtc_probe(). */
	if (rtsr & (RTSR_ALE | RTSR_HZE)) {
		/* This is the original code, before there was the if test
		 * above. This code does not clear interrupts that were not
		 * enabled. */
		RTSR = (RTSR_AL | RTSR_HZ) & (rtsr >> 2);
	} else {
		/* For some reason, it is possible to enter this routine
		 * without interruptions enabled, it has been tested with
		 * several units (Bug in SA11xx chip?).
		 *
		 * This situation leads to an infinite "loop" of interrupt
		 * routine calling and as a result the processor seems to
		 * lock on its first call to open(). */
		RTSR = RTSR_AL | RTSR_HZ;
	}

	/* clear alarm interrupt if it has occurred */
	if (rtsr & RTSR_AL)
		rtsr &= ~RTSR_ALE;
	RTSR = rtsr & (RTSR_ALE | RTSR_HZE);

	/* update irq data & counter */
	if (rtsr & RTSR_AL)
		events |= RTC_AF | RTC_IRQF;
	if (rtsr & RTSR_HZ)
		events |= RTC_UF | RTC_IRQF;

	rtc_update_irq(rtc, 1, events);

	if (rtsr & RTSR_AL && rtc_periodic_alarm(&rtc_alarm))
		rtc_update_alarm(&rtc_alarm);

	spin_unlock(&sa1100_rtc_lock);

	return IRQ_HANDLED;
}

static int sa1100_rtc_open(struct device *dev)
{
	int ret;
	struct platform_device *plat_dev = to_platform_device(dev);
	struct rtc_device *rtc = platform_get_drvdata(plat_dev);

	ret = request_irq(IRQ_RTC1Hz, sa1100_rtc_interrupt, IRQF_DISABLED,
		"rtc 1Hz", dev);
	if (ret) {
		dev_err(dev, "IRQ %d already in use.\n", IRQ_RTC1Hz);
		goto fail_ui;
	}
	ret = request_irq(IRQ_RTCAlrm, sa1100_rtc_interrupt, IRQF_DISABLED,
		"rtc Alrm", dev);
	if (ret) {
		dev_err(dev, "IRQ %d already in use.\n", IRQ_RTCAlrm);
		goto fail_ai;
	}
	rtc->max_user_freq = RTC_FREQ;
	rtc_irq_set_freq(rtc, NULL, RTC_FREQ);

	return 0;

 fail_ai:
	free_irq(IRQ_RTC1Hz, dev);
 fail_ui:
	return ret;
}

static void sa1100_rtc_release(struct device *dev)
{
	spin_lock_irq(&sa1100_rtc_lock);
	RTSR = 0;
	OIER &= ~OIER_E1;
	OSSR = OSSR_M1;
	spin_unlock_irq(&sa1100_rtc_lock);

	free_irq(IRQ_RTCAlrm, dev);
	free_irq(IRQ_RTC1Hz, dev);
}

static int sa1100_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	spin_lock_irq(&sa1100_rtc_lock);
	if (enabled)
		RTSR |= RTSR_ALE;
	else
		RTSR &= ~RTSR_ALE;
	spin_unlock_irq(&sa1100_rtc_lock);
	return 0;
}

static int sa1100_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	rtc_time_to_tm(RCNR, tm);
	return 0;
}

static int sa1100_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long time;
	int ret;

	ret = rtc_tm_to_time(tm, &time);
	if (ret == 0)
		RCNR = time;
	return ret;
}

static int sa1100_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	u32	rtsr;

	memcpy(&alrm->time, &rtc_alarm, sizeof(struct rtc_time));
	rtsr = RTSR;
	alrm->enabled = (rtsr & RTSR_ALE) ? 1 : 0;
	alrm->pending = (rtsr & RTSR_AL) ? 1 : 0;
	return 0;
}

static int sa1100_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	int ret;

	spin_lock_irq(&sa1100_rtc_lock);
	ret = rtc_update_alarm(&alrm->time);
	if (ret == 0) {
		if (alrm->enabled)
			RTSR |= RTSR_ALE;
		else
			RTSR &= ~RTSR_ALE;
	}
	spin_unlock_irq(&sa1100_rtc_lock);

	return ret;
}

static int sa1100_rtc_proc(struct device *dev, struct seq_file *seq)
{
	seq_printf(seq, "trim/divider\t\t: 0x%08x\n", (u32) RTTR);
	seq_printf(seq, "RTSR\t\t\t: 0x%08x\n", (u32)RTSR);

	return 0;
}

static const struct rtc_class_ops sa1100_rtc_ops = {
	.open = sa1100_rtc_open,
	.release = sa1100_rtc_release,
	.read_time = sa1100_rtc_read_time,
	.set_time = sa1100_rtc_set_time,
	.read_alarm = sa1100_rtc_read_alarm,
	.set_alarm = sa1100_rtc_set_alarm,
	.proc = sa1100_rtc_proc,
	.alarm_irq_enable = sa1100_rtc_alarm_irq_enable,
};

static int sa1100_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;

	/*
	 * According to the manual we should be able to let RTTR be zero
	 * and then a default diviser for a 32.768KHz clock is used.
	 * Apparently this doesn't work, at least for my SA1110 rev 5.
	 * If the clock divider is uninitialized then reset it to the
	 * default value to get the 1Hz clock.
	 */
	if (RTTR == 0) {
		RTTR = RTC_DEF_DIVIDER + (RTC_DEF_TRIM << 16);
		dev_warn(&pdev->dev, "warning: "
			"initializing default clock divider/trim value\n");
		/* The current RTC value probably doesn't make sense either */
		RCNR = 0;
	}

	device_init_wakeup(&pdev->dev, 1);

	rtc = rtc_device_register(pdev->name, &pdev->dev, &sa1100_rtc_ops,
		THIS_MODULE);

	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	platform_set_drvdata(pdev, rtc);

	/* Fix for a nasty initialization problem the in SA11xx RTSR register.
	 * See also the comments in sa1100_rtc_interrupt().
	 *
	 * Sometimes bit 1 of the RTSR (RTSR_HZ) will wake up 1, which means an
	 * interrupt pending, even though interrupts were never enabled.
	 * In this case, this bit it must be reset before enabling
	 * interruptions to avoid a nonexistent interrupt to occur.
	 *
	 * In principle, the same problem would apply to bit 0, although it has
	 * never been observed to happen.
	 *
	 * This issue is addressed both here and in sa1100_rtc_interrupt().
	 * If the issue is not addressed here, in the times when the processor
	 * wakes up with the bit set there will be one spurious interrupt.
	 *
	 * The issue is also dealt with in sa1100_rtc_interrupt() to be on the
	 * safe side, once the condition that lead to this strange
	 * initialization is unknown and could in principle happen during
	 * normal processing.
	 *
	 * Notice that clearing bit 1 and 0 is accomplished by writting ONES to
	 * the corresponding bits in RTSR. */
	RTSR = RTSR_AL | RTSR_HZ;

	return 0;
}

static int sa1100_rtc_remove(struct platform_device *pdev)
{
	struct rtc_device *rtc = platform_get_drvdata(pdev);

	if (rtc)
		rtc_device_unregister(rtc);

	return 0;
}

#ifdef CONFIG_PM
static int sa1100_rtc_suspend(struct device *dev)
{
	if (device_may_wakeup(dev))
		enable_irq_wake(IRQ_RTCAlrm);
	return 0;
}

static int sa1100_rtc_resume(struct device *dev)
{
	if (device_may_wakeup(dev))
		disable_irq_wake(IRQ_RTCAlrm);
	return 0;
}

static const struct dev_pm_ops sa1100_rtc_pm_ops = {
	.suspend	= sa1100_rtc_suspend,
	.resume		= sa1100_rtc_resume,
};
#endif

static struct platform_driver sa1100_rtc_driver = {
	.probe		= sa1100_rtc_probe,
	.remove		= sa1100_rtc_remove,
	.driver		= {
		.name	= "sa1100-rtc",
#ifdef CONFIG_PM
		.pm	= &sa1100_rtc_pm_ops,
#endif
	},
};

static int __init sa1100_rtc_init(void)
{
	return platform_driver_register(&sa1100_rtc_driver);
}

static void __exit sa1100_rtc_exit(void)
{
	platform_driver_unregister(&sa1100_rtc_driver);
}

module_init(sa1100_rtc_init);
module_exit(sa1100_rtc_exit);

MODULE_AUTHOR("Richard Purdie <rpurdie@rpsys.net>");
MODULE_DESCRIPTION("SA11x0/PXA2xx Realtime Clock Driver (RTC)");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sa1100-rtc");

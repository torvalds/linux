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
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/irq.h>

#define RTC_DEF_DIVIDER		(32768 - 1)
#define RTC_DEF_TRIM		0
#define RTC_FREQ		1024

#define RCNR		0x00	/* RTC Count Register */
#define RTAR		0x04	/* RTC Alarm Register */
#define RTSR		0x08	/* RTC Status Register */
#define RTTR		0x0c	/* RTC Timer Trim Register */

#define RTSR_HZE	(1 << 3)	/* HZ interrupt enable */
#define RTSR_ALE	(1 << 2)	/* RTC alarm interrupt enable */
#define RTSR_HZ		(1 << 1)	/* HZ rising-edge detected */
#define RTSR_AL		(1 << 0)	/* RTC alarm detected */

#define rtc_readl(sa1100_rtc, reg)	\
	readl_relaxed((sa1100_rtc)->base + (reg))
#define rtc_writel(sa1100_rtc, reg, value)	\
	writel_relaxed((value), (sa1100_rtc)->base + (reg))

struct sa1100_rtc {
	struct resource		*ress;
	void __iomem		*base;
	struct clk		*clk;
	int			irq_1Hz;
	int			irq_Alrm;
	struct rtc_device	*rtc;
	spinlock_t		lock;		/* Protects this structure */
};
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

static irqreturn_t sa1100_rtc_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = to_platform_device(dev_id);
	struct sa1100_rtc *sa1100_rtc = platform_get_drvdata(pdev);
	unsigned int rtsr;
	unsigned long events = 0;

	spin_lock(&sa1100_rtc->lock);

	/* clear interrupt sources */
	rtsr = rtc_readl(sa1100_rtc, RTSR);
	rtc_writel(sa1100_rtc, RTSR, 0);

	/* Fix for a nasty initialization problem the in SA11xx RTSR register.
	 * See also the comments in sa1100_rtc_probe(). */
	if (rtsr & (RTSR_ALE | RTSR_HZE)) {
		/* This is the original code, before there was the if test
		 * above. This code does not clear interrupts that were not
		 * enabled. */
		rtc_writel(sa1100_rtc, RTSR, (RTSR_AL | RTSR_HZ) & (rtsr >> 2));
	} else {
		/* For some reason, it is possible to enter this routine
		 * without interruptions enabled, it has been tested with
		 * several units (Bug in SA11xx chip?).
		 *
		 * This situation leads to an infinite "loop" of interrupt
		 * routine calling and as a result the processor seems to
		 * lock on its first call to open(). */
		rtc_writel(sa1100_rtc, RTSR, (RTSR_AL | RTSR_HZ));
	}

	/* clear alarm interrupt if it has occurred */
	if (rtsr & RTSR_AL)
		rtsr &= ~RTSR_ALE;
	rtc_writel(sa1100_rtc, RTSR, rtsr & (RTSR_ALE | RTSR_HZE));

	/* update irq data & counter */
	if (rtsr & RTSR_AL)
		events |= RTC_AF | RTC_IRQF;
	if (rtsr & RTSR_HZ)
		events |= RTC_UF | RTC_IRQF;

	rtc_update_irq(sa1100_rtc->rtc, 1, events);

	spin_unlock(&sa1100_rtc->lock);

	return IRQ_HANDLED;
}

static int sa1100_rtc_open(struct device *dev)
{
	struct sa1100_rtc *sa1100_rtc = dev_get_drvdata(dev);
	int ret;

	ret = request_irq(sa1100_rtc->irq_1Hz, sa1100_rtc_interrupt,
				IRQF_DISABLED, "rtc 1Hz", dev);
	if (ret) {
		dev_err(dev, "IRQ %d already in use.\n", sa1100_rtc->irq_1Hz);
		goto fail_ui;
	}
	ret = request_irq(sa1100_rtc->irq_Alrm, sa1100_rtc_interrupt,
				IRQF_DISABLED, "rtc Alrm", dev);
	if (ret) {
		dev_err(dev, "IRQ %d already in use.\n", sa1100_rtc->irq_Alrm);
		goto fail_ai;
	}
	sa1100_rtc->rtc->max_user_freq = RTC_FREQ;
	rtc_irq_set_freq(sa1100_rtc->rtc, NULL, RTC_FREQ);

	return 0;

 fail_ai:
	free_irq(sa1100_rtc->irq_1Hz, dev);
 fail_ui:
	return ret;
}

static void sa1100_rtc_release(struct device *dev)
{
	struct sa1100_rtc *sa1100_rtc = dev_get_drvdata(dev);

	spin_lock_irq(&sa1100_rtc->lock);
	rtc_writel(sa1100_rtc, RTSR, 0);
	spin_unlock_irq(&sa1100_rtc->lock);

	free_irq(sa1100_rtc->irq_Alrm, dev);
	free_irq(sa1100_rtc->irq_1Hz, dev);
}

static int sa1100_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct sa1100_rtc *sa1100_rtc = dev_get_drvdata(dev);
	unsigned int rtsr;

	spin_lock_irq(&sa1100_rtc->lock);

	rtsr = rtc_readl(sa1100_rtc, RTSR);
	if (enabled)
		rtsr |= RTSR_ALE;
	else
		rtsr &= ~RTSR_ALE;
	rtc_writel(sa1100_rtc, RTSR, rtsr);

	spin_unlock_irq(&sa1100_rtc->lock);
	return 0;
}

static int sa1100_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct sa1100_rtc *sa1100_rtc = dev_get_drvdata(dev);

	rtc_time_to_tm(rtc_readl(sa1100_rtc, RCNR), tm);
	return 0;
}

static int sa1100_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct sa1100_rtc *sa1100_rtc = dev_get_drvdata(dev);
	unsigned long time;
	int ret;

	ret = rtc_tm_to_time(tm, &time);
	if (ret == 0)
		rtc_writel(sa1100_rtc, RCNR, time);
	return ret;
}

static int sa1100_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct sa1100_rtc *sa1100_rtc = dev_get_drvdata(dev);
	unsigned long time;
	unsigned int rtsr;

	time = rtc_readl(sa1100_rtc, RCNR);
	rtc_time_to_tm(time, &alrm->time);
	rtsr = rtc_readl(sa1100_rtc, RTSR);
	alrm->enabled = (rtsr & RTSR_ALE) ? 1 : 0;
	alrm->pending = (rtsr & RTSR_AL) ? 1 : 0;
	return 0;
}

static int sa1100_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct sa1100_rtc *sa1100_rtc = dev_get_drvdata(dev);
	struct rtc_time now_tm, alarm_tm;
	unsigned long time, alarm;
	unsigned int rtsr;

	spin_lock_irq(&sa1100_rtc->lock);

	time = rtc_readl(sa1100_rtc, RCNR);
	rtc_time_to_tm(time, &now_tm);
	rtc_next_alarm_time(&alarm_tm, &now_tm, &alrm->time);
	rtc_tm_to_time(&alarm_tm, &alarm);
	rtc_writel(sa1100_rtc, RTAR, alarm);

	rtsr = rtc_readl(sa1100_rtc, RTSR);
	if (alrm->enabled)
		rtsr |= RTSR_ALE;
	else
		rtsr &= ~RTSR_ALE;
	rtc_writel(sa1100_rtc, RTSR, rtsr);

	spin_unlock_irq(&sa1100_rtc->lock);

	return 0;
}

static int sa1100_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct sa1100_rtc *sa1100_rtc = dev_get_drvdata(dev);

	seq_printf(seq, "trim/divider\t\t: 0x%08x\n",
			rtc_readl(sa1100_rtc, RTTR));
	seq_printf(seq, "RTSR\t\t\t: 0x%08x\n",
			rtc_readl(sa1100_rtc, RTSR));
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
	struct sa1100_rtc *sa1100_rtc;
	unsigned int rttr;
	int ret;

	sa1100_rtc = kzalloc(sizeof(struct sa1100_rtc), GFP_KERNEL);
	if (!sa1100_rtc)
		return -ENOMEM;

	spin_lock_init(&sa1100_rtc->lock);
	platform_set_drvdata(pdev, sa1100_rtc);

	ret = -ENXIO;
	sa1100_rtc->ress = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!sa1100_rtc->ress) {
		dev_err(&pdev->dev, "No I/O memory resource defined\n");
		goto err_ress;
	}

	sa1100_rtc->irq_1Hz = platform_get_irq(pdev, 0);
	if (sa1100_rtc->irq_1Hz < 0) {
		dev_err(&pdev->dev, "No 1Hz IRQ resource defined\n");
		goto err_ress;
	}
	sa1100_rtc->irq_Alrm = platform_get_irq(pdev, 1);
	if (sa1100_rtc->irq_Alrm < 0) {
		dev_err(&pdev->dev, "No alarm IRQ resource defined\n");
		goto err_ress;
	}

	ret = -ENOMEM;
	sa1100_rtc->base = ioremap(sa1100_rtc->ress->start,
				resource_size(sa1100_rtc->ress));
	if (!sa1100_rtc->base) {
		dev_err(&pdev->dev, "Unable to map pxa RTC I/O memory\n");
		goto err_map;
	}

	sa1100_rtc->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(sa1100_rtc->clk)) {
		dev_err(&pdev->dev, "failed to find rtc clock source\n");
		ret = PTR_ERR(sa1100_rtc->clk);
		goto err_clk;
	}
	clk_prepare(sa1100_rtc->clk);
	clk_enable(sa1100_rtc->clk);

	/*
	 * According to the manual we should be able to let RTTR be zero
	 * and then a default diviser for a 32.768KHz clock is used.
	 * Apparently this doesn't work, at least for my SA1110 rev 5.
	 * If the clock divider is uninitialized then reset it to the
	 * default value to get the 1Hz clock.
	 */
	if (rtc_readl(sa1100_rtc, RTTR) == 0) {
		rttr = RTC_DEF_DIVIDER + (RTC_DEF_TRIM << 16);
		rtc_writel(sa1100_rtc, RTTR, rttr);
		dev_warn(&pdev->dev, "warning: initializing default clock"
			 " divider/trim value\n");
		/* The current RTC value probably doesn't make sense either */
		rtc_writel(sa1100_rtc, RCNR, 0);
	}

	device_init_wakeup(&pdev->dev, 1);

	sa1100_rtc->rtc = rtc_device_register(pdev->name, &pdev->dev,
						&sa1100_rtc_ops, THIS_MODULE);
	if (IS_ERR(sa1100_rtc->rtc)) {
		dev_err(&pdev->dev, "Failed to register RTC device -> %d\n",
			ret);
		goto err_rtc_reg;
	}
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
	rtc_writel(sa1100_rtc, RTSR, (RTSR_AL | RTSR_HZ));

	return 0;

err_rtc_reg:
err_clk:
	iounmap(sa1100_rtc->base);
err_ress:
err_map:
	kfree(sa1100_rtc);
	return ret;
}

static int sa1100_rtc_remove(struct platform_device *pdev)
{
	struct sa1100_rtc *sa1100_rtc = platform_get_drvdata(pdev);

	rtc_device_unregister(sa1100_rtc->rtc);
	clk_disable(sa1100_rtc->clk);
	clk_unprepare(sa1100_rtc->clk);
	iounmap(sa1100_rtc->base);
	return 0;
}

#ifdef CONFIG_PM
static int sa1100_rtc_suspend(struct device *dev)
{
	struct sa1100_rtc *sa1100_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(sa1100_rtc->irq_Alrm);
	return 0;
}

static int sa1100_rtc_resume(struct device *dev)
{
	struct sa1100_rtc *sa1100_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(sa1100_rtc->irq_Alrm);
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

module_platform_driver(sa1100_rtc_driver);

MODULE_AUTHOR("Richard Purdie <rpurdie@rpsys.net>");
MODULE_DESCRIPTION("SA11x0/PXA2xx Realtime Clock Driver (RTC)");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sa1100-rtc");

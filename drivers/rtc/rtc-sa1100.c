// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/bitops.h>
#include <linux/io.h>

#define RTSR_HZE		BIT(3)	/* HZ interrupt enable */
#define RTSR_ALE		BIT(2)	/* RTC alarm interrupt enable */
#define RTSR_HZ			BIT(1)	/* HZ rising-edge detected */
#define RTSR_AL			BIT(0)	/* RTC alarm detected */

#include "rtc-sa1100.h"

#define RTC_DEF_DIVIDER		(32768 - 1)
#define RTC_DEF_TRIM		0
#define RTC_FREQ		1024


static irqreturn_t sa1100_rtc_interrupt(int irq, void *dev_id)
{
	struct sa1100_rtc *info = dev_get_drvdata(dev_id);
	struct rtc_device *rtc = info->rtc;
	unsigned int rtsr;
	unsigned long events = 0;

	spin_lock(&info->lock);

	rtsr = readl_relaxed(info->rtsr);
	/* clear interrupt sources */
	writel_relaxed(0, info->rtsr);
	/* Fix for a nasty initialization problem the in SA11xx RTSR register.
	 * See also the comments in sa1100_rtc_probe(). */
	if (rtsr & (RTSR_ALE | RTSR_HZE)) {
		/* This is the original code, before there was the if test
		 * above. This code does not clear interrupts that were not
		 * enabled. */
		writel_relaxed((RTSR_AL | RTSR_HZ) & (rtsr >> 2), info->rtsr);
	} else {
		/* For some reason, it is possible to enter this routine
		 * without interruptions enabled, it has been tested with
		 * several units (Bug in SA11xx chip?).
		 *
		 * This situation leads to an infinite "loop" of interrupt
		 * routine calling and as a result the processor seems to
		 * lock on its first call to open(). */
		writel_relaxed(RTSR_AL | RTSR_HZ, info->rtsr);
	}

	/* clear alarm interrupt if it has occurred */
	if (rtsr & RTSR_AL)
		rtsr &= ~RTSR_ALE;
	writel_relaxed(rtsr & (RTSR_ALE | RTSR_HZE), info->rtsr);

	/* update irq data & counter */
	if (rtsr & RTSR_AL)
		events |= RTC_AF | RTC_IRQF;
	if (rtsr & RTSR_HZ)
		events |= RTC_UF | RTC_IRQF;

	rtc_update_irq(rtc, 1, events);

	spin_unlock(&info->lock);

	return IRQ_HANDLED;
}

static int sa1100_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	u32 rtsr;
	struct sa1100_rtc *info = dev_get_drvdata(dev);

	spin_lock_irq(&info->lock);
	rtsr = readl_relaxed(info->rtsr);
	if (enabled)
		rtsr |= RTSR_ALE;
	else
		rtsr &= ~RTSR_ALE;
	writel_relaxed(rtsr, info->rtsr);
	spin_unlock_irq(&info->lock);
	return 0;
}

static int sa1100_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct sa1100_rtc *info = dev_get_drvdata(dev);

	rtc_time64_to_tm(readl_relaxed(info->rcnr), tm);
	return 0;
}

static int sa1100_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct sa1100_rtc *info = dev_get_drvdata(dev);

	writel_relaxed(rtc_tm_to_time64(tm), info->rcnr);

	return 0;
}

static int sa1100_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	u32	rtsr;
	struct sa1100_rtc *info = dev_get_drvdata(dev);

	rtsr = readl_relaxed(info->rtsr);
	alrm->enabled = (rtsr & RTSR_ALE) ? 1 : 0;
	alrm->pending = (rtsr & RTSR_AL) ? 1 : 0;
	return 0;
}

static int sa1100_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct sa1100_rtc *info = dev_get_drvdata(dev);

	spin_lock_irq(&info->lock);
	writel_relaxed(readl_relaxed(info->rtsr) &
		(RTSR_HZE | RTSR_ALE | RTSR_AL), info->rtsr);
	writel_relaxed(rtc_tm_to_time64(&alrm->time), info->rtar);
	if (alrm->enabled)
		writel_relaxed(readl_relaxed(info->rtsr) | RTSR_ALE, info->rtsr);
	else
		writel_relaxed(readl_relaxed(info->rtsr) & ~RTSR_ALE, info->rtsr);
	spin_unlock_irq(&info->lock);

	return 0;
}

static int sa1100_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct sa1100_rtc *info = dev_get_drvdata(dev);

	seq_printf(seq, "trim/divider\t\t: 0x%08x\n", readl_relaxed(info->rttr));
	seq_printf(seq, "RTSR\t\t\t: 0x%08x\n", readl_relaxed(info->rtsr));

	return 0;
}

static const struct rtc_class_ops sa1100_rtc_ops = {
	.read_time = sa1100_rtc_read_time,
	.set_time = sa1100_rtc_set_time,
	.read_alarm = sa1100_rtc_read_alarm,
	.set_alarm = sa1100_rtc_set_alarm,
	.proc = sa1100_rtc_proc,
	.alarm_irq_enable = sa1100_rtc_alarm_irq_enable,
};

int sa1100_rtc_init(struct platform_device *pdev, struct sa1100_rtc *info)
{
	int ret;

	spin_lock_init(&info->lock);

	info->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(info->clk)) {
		dev_err(&pdev->dev, "failed to find rtc clock source\n");
		return PTR_ERR(info->clk);
	}

	ret = clk_prepare_enable(info->clk);
	if (ret)
		return ret;
	/*
	 * According to the manual we should be able to let RTTR be zero
	 * and then a default diviser for a 32.768KHz clock is used.
	 * Apparently this doesn't work, at least for my SA1110 rev 5.
	 * If the clock divider is uninitialized then reset it to the
	 * default value to get the 1Hz clock.
	 */
	if (readl_relaxed(info->rttr) == 0) {
		writel_relaxed(RTC_DEF_DIVIDER + (RTC_DEF_TRIM << 16), info->rttr);
		dev_warn(&pdev->dev, "warning: "
			"initializing default clock divider/trim value\n");
		/* The current RTC value probably doesn't make sense either */
		writel_relaxed(0, info->rcnr);
	}

	info->rtc->ops = &sa1100_rtc_ops;
	info->rtc->max_user_freq = RTC_FREQ;
	info->rtc->range_max = U32_MAX;

	ret = rtc_register_device(info->rtc);
	if (ret) {
		clk_disable_unprepare(info->clk);
		return ret;
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
	writel_relaxed(RTSR_AL | RTSR_HZ, info->rtsr);

	return 0;
}
EXPORT_SYMBOL_GPL(sa1100_rtc_init);

static int sa1100_rtc_probe(struct platform_device *pdev)
{
	struct sa1100_rtc *info;
	void __iomem *base;
	int irq_1hz, irq_alarm;
	int ret;

	irq_1hz = platform_get_irq_byname(pdev, "rtc 1Hz");
	irq_alarm = platform_get_irq_byname(pdev, "rtc alarm");
	if (irq_1hz < 0 || irq_alarm < 0)
		return -ENODEV;

	info = devm_kzalloc(&pdev->dev, sizeof(struct sa1100_rtc), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->irq_1hz = irq_1hz;
	info->irq_alarm = irq_alarm;

	info->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(info->rtc))
		return PTR_ERR(info->rtc);

	ret = devm_request_irq(&pdev->dev, irq_1hz, sa1100_rtc_interrupt, 0,
			       "rtc 1Hz", &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "IRQ %d already in use.\n", irq_1hz);
		return ret;
	}
	ret = devm_request_irq(&pdev->dev, irq_alarm, sa1100_rtc_interrupt, 0,
			       "rtc Alrm", &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "IRQ %d already in use.\n", irq_alarm);
		return ret;
	}

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	if (IS_ENABLED(CONFIG_ARCH_SA1100) ||
	    of_device_is_compatible(pdev->dev.of_node, "mrvl,sa1100-rtc")) {
		info->rcnr = base + 0x04;
		info->rtsr = base + 0x10;
		info->rtar = base + 0x00;
		info->rttr = base + 0x08;
	} else {
		info->rcnr = base + 0x0;
		info->rtsr = base + 0x8;
		info->rtar = base + 0x4;
		info->rttr = base + 0xc;
	}

	platform_set_drvdata(pdev, info);
	device_init_wakeup(&pdev->dev, 1);

	return sa1100_rtc_init(pdev, info);
}

static int sa1100_rtc_remove(struct platform_device *pdev)
{
	struct sa1100_rtc *info = platform_get_drvdata(pdev);

	if (info) {
		spin_lock_irq(&info->lock);
		writel_relaxed(0, info->rtsr);
		spin_unlock_irq(&info->lock);
		clk_disable_unprepare(info->clk);
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sa1100_rtc_suspend(struct device *dev)
{
	struct sa1100_rtc *info = dev_get_drvdata(dev);
	if (device_may_wakeup(dev))
		enable_irq_wake(info->irq_alarm);
	return 0;
}

static int sa1100_rtc_resume(struct device *dev)
{
	struct sa1100_rtc *info = dev_get_drvdata(dev);
	if (device_may_wakeup(dev))
		disable_irq_wake(info->irq_alarm);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(sa1100_rtc_pm_ops, sa1100_rtc_suspend,
			sa1100_rtc_resume);

#ifdef CONFIG_OF
static const struct of_device_id sa1100_rtc_dt_ids[] = {
	{ .compatible = "mrvl,sa1100-rtc", },
	{ .compatible = "mrvl,mmp-rtc", },
	{}
};
MODULE_DEVICE_TABLE(of, sa1100_rtc_dt_ids);
#endif

static struct platform_driver sa1100_rtc_driver = {
	.probe		= sa1100_rtc_probe,
	.remove		= sa1100_rtc_remove,
	.driver		= {
		.name	= "sa1100-rtc",
		.pm	= &sa1100_rtc_pm_ops,
		.of_match_table = of_match_ptr(sa1100_rtc_dt_ids),
	},
};

module_platform_driver(sa1100_rtc_driver);

MODULE_AUTHOR("Richard Purdie <rpurdie@rpsys.net>");
MODULE_DESCRIPTION("SA11x0/PXA2xx Realtime Clock Driver (RTC)");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sa1100-rtc");

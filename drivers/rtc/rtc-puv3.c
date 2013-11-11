/*
 * RTC driver code specific to PKUnity SoC and UniCore ISA
 *
 *	Maintained by GUAN Xue-tao <gxt@mprc.pku.edu.cn>
 *	Copyright (C) 2001-2010 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/clk.h>
#include <linux/log2.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <mach/hardware.h>

static struct resource *puv3_rtc_mem;

static int puv3_rtc_alarmno = IRQ_RTCAlarm;
static int puv3_rtc_tickno  = IRQ_RTC;

static DEFINE_SPINLOCK(puv3_rtc_pie_lock);

/* IRQ Handlers */
static irqreturn_t puv3_rtc_alarmirq(int irq, void *id)
{
	struct rtc_device *rdev = id;

	writel(readl(RTC_RTSR) | RTC_RTSR_AL, RTC_RTSR);
	rtc_update_irq(rdev, 1, RTC_AF | RTC_IRQF);
	return IRQ_HANDLED;
}

static irqreturn_t puv3_rtc_tickirq(int irq, void *id)
{
	struct rtc_device *rdev = id;

	writel(readl(RTC_RTSR) | RTC_RTSR_HZ, RTC_RTSR);
	rtc_update_irq(rdev, 1, RTC_PF | RTC_IRQF);
	return IRQ_HANDLED;
}

/* Update control registers */
static void puv3_rtc_setaie(int to)
{
	unsigned int tmp;

	pr_debug("%s: aie=%d\n", __func__, to);

	tmp = readl(RTC_RTSR) & ~RTC_RTSR_ALE;

	if (to)
		tmp |= RTC_RTSR_ALE;

	writel(tmp, RTC_RTSR);
}

static int puv3_rtc_setpie(struct device *dev, int enabled)
{
	unsigned int tmp;

	pr_debug("%s: pie=%d\n", __func__, enabled);

	spin_lock_irq(&puv3_rtc_pie_lock);
	tmp = readl(RTC_RTSR) & ~RTC_RTSR_HZE;

	if (enabled)
		tmp |= RTC_RTSR_HZE;

	writel(tmp, RTC_RTSR);
	spin_unlock_irq(&puv3_rtc_pie_lock);

	return 0;
}

/* Time read/write */
static int puv3_rtc_gettime(struct device *dev, struct rtc_time *rtc_tm)
{
	rtc_time_to_tm(readl(RTC_RCNR), rtc_tm);

	pr_debug("read time %02x.%02x.%02x %02x/%02x/%02x\n",
		 rtc_tm->tm_year, rtc_tm->tm_mon, rtc_tm->tm_mday,
		 rtc_tm->tm_hour, rtc_tm->tm_min, rtc_tm->tm_sec);

	return 0;
}

static int puv3_rtc_settime(struct device *dev, struct rtc_time *tm)
{
	unsigned long rtc_count = 0;

	pr_debug("set time %02d.%02d.%02d %02d/%02d/%02d\n",
		 tm->tm_year, tm->tm_mon, tm->tm_mday,
		 tm->tm_hour, tm->tm_min, tm->tm_sec);

	rtc_tm_to_time(tm, &rtc_count);
	writel(rtc_count, RTC_RCNR);

	return 0;
}

static int puv3_rtc_getalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_time *alm_tm = &alrm->time;

	rtc_time_to_tm(readl(RTC_RTAR), alm_tm);

	alrm->enabled = readl(RTC_RTSR) & RTC_RTSR_ALE;

	pr_debug("read alarm %02x %02x.%02x.%02x %02x/%02x/%02x\n",
		 alrm->enabled,
		 alm_tm->tm_year, alm_tm->tm_mon, alm_tm->tm_mday,
		 alm_tm->tm_hour, alm_tm->tm_min, alm_tm->tm_sec);

	return 0;
}

static int puv3_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_time *tm = &alrm->time;
	unsigned long rtcalarm_count = 0;

	pr_debug("puv3_rtc_setalarm: %d, %02x/%02x/%02x %02x.%02x.%02x\n",
		 alrm->enabled,
		 tm->tm_mday & 0xff, tm->tm_mon & 0xff, tm->tm_year & 0xff,
		 tm->tm_hour & 0xff, tm->tm_min & 0xff, tm->tm_sec);

	rtc_tm_to_time(tm, &rtcalarm_count);
	writel(rtcalarm_count, RTC_RTAR);

	puv3_rtc_setaie(alrm->enabled);

	if (alrm->enabled)
		enable_irq_wake(puv3_rtc_alarmno);
	else
		disable_irq_wake(puv3_rtc_alarmno);

	return 0;
}

static int puv3_rtc_proc(struct device *dev, struct seq_file *seq)
{
	seq_printf(seq, "periodic_IRQ\t: %s\n",
		     (readl(RTC_RTSR) & RTC_RTSR_HZE) ? "yes" : "no");
	return 0;
}

static int puv3_rtc_open(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_device *rtc_dev = platform_get_drvdata(pdev);
	int ret;

	ret = request_irq(puv3_rtc_alarmno, puv3_rtc_alarmirq,
			0, "pkunity-rtc alarm", rtc_dev);

	if (ret) {
		dev_err(dev, "IRQ%d error %d\n", puv3_rtc_alarmno, ret);
		return ret;
	}

	ret = request_irq(puv3_rtc_tickno, puv3_rtc_tickirq,
			0, "pkunity-rtc tick", rtc_dev);

	if (ret) {
		dev_err(dev, "IRQ%d error %d\n", puv3_rtc_tickno, ret);
		goto tick_err;
	}

	return ret;

 tick_err:
	free_irq(puv3_rtc_alarmno, rtc_dev);
	return ret;
}

static void puv3_rtc_release(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_device *rtc_dev = platform_get_drvdata(pdev);

	/* do not clear AIE here, it may be needed for wake */
	puv3_rtc_setpie(dev, 0);
	free_irq(puv3_rtc_alarmno, rtc_dev);
	free_irq(puv3_rtc_tickno, rtc_dev);
}

static const struct rtc_class_ops puv3_rtcops = {
	.open		= puv3_rtc_open,
	.release	= puv3_rtc_release,
	.read_time	= puv3_rtc_gettime,
	.set_time	= puv3_rtc_settime,
	.read_alarm	= puv3_rtc_getalarm,
	.set_alarm	= puv3_rtc_setalarm,
	.proc	        = puv3_rtc_proc,
};

static void puv3_rtc_enable(struct device *dev, int en)
{
	if (!en) {
		writel(readl(RTC_RTSR) & ~RTC_RTSR_HZE, RTC_RTSR);
	} else {
		/* re-enable the device, and check it is ok */
		if ((readl(RTC_RTSR) & RTC_RTSR_HZE) == 0) {
			dev_info(dev, "rtc disabled, re-enabling\n");
			writel(readl(RTC_RTSR) | RTC_RTSR_HZE, RTC_RTSR);
		}
	}
}

static int puv3_rtc_remove(struct platform_device *dev)
{
	struct rtc_device *rtc = platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);
	rtc_device_unregister(rtc);

	puv3_rtc_setpie(&dev->dev, 0);
	puv3_rtc_setaie(0);

	release_resource(puv3_rtc_mem);
	kfree(puv3_rtc_mem);

	return 0;
}

static int puv3_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;
	struct resource *res;
	int ret;

	pr_debug("%s: probe=%p\n", __func__, pdev);

	/* find the IRQs */
	puv3_rtc_tickno = platform_get_irq(pdev, 1);
	if (puv3_rtc_tickno < 0) {
		dev_err(&pdev->dev, "no irq for rtc tick\n");
		return -ENOENT;
	}

	puv3_rtc_alarmno = platform_get_irq(pdev, 0);
	if (puv3_rtc_alarmno < 0) {
		dev_err(&pdev->dev, "no irq for alarm\n");
		return -ENOENT;
	}

	pr_debug("PKUnity_rtc: tick irq %d, alarm irq %d\n",
		 puv3_rtc_tickno, puv3_rtc_alarmno);

	/* get the memory region */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to get memory region resource\n");
		return -ENOENT;
	}

	puv3_rtc_mem = request_mem_region(res->start, resource_size(res),
					  pdev->name);

	if (puv3_rtc_mem == NULL) {
		dev_err(&pdev->dev, "failed to reserve memory region\n");
		ret = -ENOENT;
		goto err_nores;
	}

	puv3_rtc_enable(&pdev->dev, 1);

	/* register RTC and exit */
	rtc = rtc_device_register("pkunity", &pdev->dev, &puv3_rtcops,
				  THIS_MODULE);

	if (IS_ERR(rtc)) {
		dev_err(&pdev->dev, "cannot attach rtc\n");
		ret = PTR_ERR(rtc);
		goto err_nortc;
	}

	/* platform setup code should have handled this; sigh */
	if (!device_can_wakeup(&pdev->dev))
		device_init_wakeup(&pdev->dev, 1);

	platform_set_drvdata(pdev, rtc);
	return 0;

 err_nortc:
	puv3_rtc_enable(&pdev->dev, 0);
	release_resource(puv3_rtc_mem);

 err_nores:
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int ticnt_save;

static int puv3_rtc_suspend(struct device *dev)
{
	/* save RTAR for anyone using periodic interrupts */
	ticnt_save = readl(RTC_RTAR);
	puv3_rtc_enable(dev, 0);
	return 0;
}

static int puv3_rtc_resume(struct device *dev)
{
	puv3_rtc_enable(dev, 1);
	writel(ticnt_save, RTC_RTAR);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(puv3_rtc_pm_ops, puv3_rtc_suspend, puv3_rtc_resume);

static struct platform_driver puv3_rtc_driver = {
	.probe		= puv3_rtc_probe,
	.remove		= puv3_rtc_remove,
	.driver		= {
		.name	= "PKUnity-v3-RTC",
		.owner	= THIS_MODULE,
		.pm	= &puv3_rtc_pm_ops,
	}
};

module_platform_driver(puv3_rtc_driver);

MODULE_DESCRIPTION("RTC Driver for the PKUnity v3 chip");
MODULE_AUTHOR("Hu Dongliang");
MODULE_LICENSE("GPL v2");

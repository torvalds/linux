/*
 * Copyright (C) 2007-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * Real Time Clock interface for ST-Ericsson AB COH 901 331 RTC.
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 * Based on rtc-pl031.c by Deepak Saxena <dsaxena@plexity.net>
 * Copyright 2006 (c) MontaVista Software, Inc.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/io.h>

/*
 * Registers in the COH 901 331
 */
/* Alarm value 32bit (R/W) */
#define COH901331_ALARM		0x00U
/* Used to set current time 32bit (R/W) */
#define COH901331_SET_TIME	0x04U
/* Indication if current time is valid 32bit (R/-) */
#define COH901331_VALID		0x08U
/* Read the current time 32bit (R/-) */
#define COH901331_CUR_TIME	0x0cU
/* Event register for the "alarm" interrupt */
#define COH901331_IRQ_EVENT	0x10U
/* Mask register for the "alarm" interrupt */
#define COH901331_IRQ_MASK	0x14U
/* Force register for the "alarm" interrupt */
#define COH901331_IRQ_FORCE	0x18U

/*
 * Reference to RTC block clock
 * Notice that the frequent clk_enable()/clk_disable() on this
 * clock is mainly to be able to turn on/off other clocks in the
 * hierarchy as needed, the RTC clock is always on anyway.
 */
struct coh901331_port {
	struct rtc_device *rtc;
	struct clk *clk;
	u32 phybase;
	u32 physize;
	void __iomem *virtbase;
	int irq;
#ifdef CONFIG_PM
	u32 irqmaskstore;
#endif
};

static irqreturn_t coh901331_interrupt(int irq, void *data)
{
	struct coh901331_port *rtap = data;

	clk_enable(rtap->clk);
	/* Ack IRQ */
	writel(1, rtap->virtbase + COH901331_IRQ_EVENT);
	/*
	 * Disable the interrupt. This is necessary because
	 * the RTC lives on a lower-clocked line and will
	 * not release the IRQ line until after a few (slower)
	 * clock cycles. The interrupt will be re-enabled when
	 * a new alarm is set anyway.
	 */
	writel(0, rtap->virtbase + COH901331_IRQ_MASK);
	clk_disable(rtap->clk);

	/* Set alarm flag */
	rtc_update_irq(rtap->rtc, 1, RTC_AF);

	return IRQ_HANDLED;
}

static int coh901331_read_time(struct device *dev, struct rtc_time *tm)
{
	struct coh901331_port *rtap = dev_get_drvdata(dev);

	clk_enable(rtap->clk);
	/* Check if the time is valid */
	if (readl(rtap->virtbase + COH901331_VALID)) {
		rtc_time_to_tm(readl(rtap->virtbase + COH901331_CUR_TIME), tm);
		clk_disable(rtap->clk);
		return rtc_valid_tm(tm);
	}
	clk_disable(rtap->clk);
	return -EINVAL;
}

static int coh901331_set_mmss(struct device *dev, unsigned long secs)
{
	struct coh901331_port *rtap = dev_get_drvdata(dev);

	clk_enable(rtap->clk);
	writel(secs, rtap->virtbase + COH901331_SET_TIME);
	clk_disable(rtap->clk);

	return 0;
}

static int coh901331_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct coh901331_port *rtap = dev_get_drvdata(dev);

	clk_enable(rtap->clk);
	rtc_time_to_tm(readl(rtap->virtbase + COH901331_ALARM), &alarm->time);
	alarm->pending = readl(rtap->virtbase + COH901331_IRQ_EVENT) & 1U;
	alarm->enabled = readl(rtap->virtbase + COH901331_IRQ_MASK) & 1U;
	clk_disable(rtap->clk);

	return 0;
}

static int coh901331_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct coh901331_port *rtap = dev_get_drvdata(dev);
	unsigned long time;

	rtc_tm_to_time(&alarm->time, &time);
	clk_enable(rtap->clk);
	writel(time, rtap->virtbase + COH901331_ALARM);
	writel(alarm->enabled, rtap->virtbase + COH901331_IRQ_MASK);
	clk_disable(rtap->clk);

	return 0;
}

static int coh901331_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct coh901331_port *rtap = dev_get_drvdata(dev);

	clk_enable(rtap->clk);
	if (enabled)
		writel(1, rtap->virtbase + COH901331_IRQ_MASK);
	else
		writel(0, rtap->virtbase + COH901331_IRQ_MASK);
	clk_disable(rtap->clk);

	return 0;
}

static struct rtc_class_ops coh901331_ops = {
	.read_time = coh901331_read_time,
	.set_mmss = coh901331_set_mmss,
	.read_alarm = coh901331_read_alarm,
	.set_alarm = coh901331_set_alarm,
	.alarm_irq_enable = coh901331_alarm_irq_enable,
};

static int __exit coh901331_remove(struct platform_device *pdev)
{
	struct coh901331_port *rtap = dev_get_drvdata(&pdev->dev);

	if (rtap) {
		free_irq(rtap->irq, rtap);
		rtc_device_unregister(rtap->rtc);
		clk_put(rtap->clk);
		iounmap(rtap->virtbase);
		release_mem_region(rtap->phybase, rtap->physize);
		platform_set_drvdata(pdev, NULL);
		kfree(rtap);
	}

	return 0;
}


static int __init coh901331_probe(struct platform_device *pdev)
{
	int ret;
	struct coh901331_port *rtap;
	struct resource *res;

	rtap = kzalloc(sizeof(struct coh901331_port), GFP_KERNEL);
	if (!rtap)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENOENT;
		goto out_no_resource;
	}
	rtap->phybase = res->start;
	rtap->physize = resource_size(res);

	if (request_mem_region(rtap->phybase, rtap->physize,
			       "rtc-coh901331") == NULL) {
		ret = -EBUSY;
		goto out_no_memregion;
	}

	rtap->virtbase = ioremap(rtap->phybase, rtap->physize);
	if (!rtap->virtbase) {
		ret = -ENOMEM;
		goto out_no_remap;
	}

	rtap->irq = platform_get_irq(pdev, 0);
	if (request_irq(rtap->irq, coh901331_interrupt, IRQF_DISABLED,
			"RTC COH 901 331 Alarm", rtap)) {
		ret = -EIO;
		goto out_no_irq;
	}

	rtap->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(rtap->clk)) {
		ret = PTR_ERR(rtap->clk);
		dev_err(&pdev->dev, "could not get clock\n");
		goto out_no_clk;
	}

	/* We enable/disable the clock only to assure it works */
	ret = clk_enable(rtap->clk);
	if (ret) {
		dev_err(&pdev->dev, "could not enable clock\n");
		goto out_no_clk_enable;
	}
	clk_disable(rtap->clk);

	rtap->rtc = rtc_device_register("coh901331", &pdev->dev, &coh901331_ops,
					 THIS_MODULE);
	if (IS_ERR(rtap->rtc)) {
		ret = PTR_ERR(rtap->rtc);
		goto out_no_rtc;
	}

	platform_set_drvdata(pdev, rtap);

	return 0;

 out_no_rtc:
 out_no_clk_enable:
	clk_put(rtap->clk);
 out_no_clk:
	free_irq(rtap->irq, rtap);
 out_no_irq:
	iounmap(rtap->virtbase);
 out_no_remap:
	platform_set_drvdata(pdev, NULL);
 out_no_memregion:
	release_mem_region(rtap->phybase, SZ_4K);
 out_no_resource:
	kfree(rtap);
	return ret;
}

#ifdef CONFIG_PM
static int coh901331_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct coh901331_port *rtap = dev_get_drvdata(&pdev->dev);

	/*
	 * If this RTC alarm will be used for waking the system up,
	 * don't disable it of course. Else we just disable the alarm
	 * and await suspension.
	 */
	if (device_may_wakeup(&pdev->dev)) {
		enable_irq_wake(rtap->irq);
	} else {
		clk_enable(rtap->clk);
		rtap->irqmaskstore = readl(rtap->virtbase + COH901331_IRQ_MASK);
		writel(0, rtap->virtbase + COH901331_IRQ_MASK);
		clk_disable(rtap->clk);
	}
	return 0;
}

static int coh901331_resume(struct platform_device *pdev)
{
	struct coh901331_port *rtap = dev_get_drvdata(&pdev->dev);

	if (device_may_wakeup(&pdev->dev)) {
		disable_irq_wake(rtap->irq);
	} else {
		clk_enable(rtap->clk);
		writel(rtap->irqmaskstore, rtap->virtbase + COH901331_IRQ_MASK);
		clk_disable(rtap->clk);
	}
	return 0;
}
#else
#define coh901331_suspend NULL
#define coh901331_resume NULL
#endif

static void coh901331_shutdown(struct platform_device *pdev)
{
	struct coh901331_port *rtap = dev_get_drvdata(&pdev->dev);

	clk_enable(rtap->clk);
	writel(0, rtap->virtbase + COH901331_IRQ_MASK);
	clk_disable(rtap->clk);
}

static struct platform_driver coh901331_driver = {
	.driver = {
		.name = "rtc-coh901331",
		.owner = THIS_MODULE,
	},
	.remove = __exit_p(coh901331_remove),
	.suspend = coh901331_suspend,
	.resume = coh901331_resume,
	.shutdown = coh901331_shutdown,
};

static int __init coh901331_init(void)
{
	return platform_driver_probe(&coh901331_driver, coh901331_probe);
}

static void __exit coh901331_exit(void)
{
	platform_driver_unregister(&coh901331_driver);
}

module_init(coh901331_init);
module_exit(coh901331_exit);

MODULE_AUTHOR("Linus Walleij <linus.walleij@stericsson.com>");
MODULE_DESCRIPTION("ST-Ericsson AB COH 901 331 RTC Driver");
MODULE_LICENSE("GPL");

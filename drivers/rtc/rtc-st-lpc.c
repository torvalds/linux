// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * rtc-st-lpc.c - ST's LPC RTC, powered by the Low Power Timer
 *
 * Copyright (C) 2014 STMicroelectronics Limited
 *
 * Author: David Paris <david.paris@st.com> for STMicroelectronics
 *         Lee Jones <lee.jones@linaro.org> for STMicroelectronics
 *
 * Based on the original driver written by Stuart Menefy.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#include <dt-bindings/mfd/st-lpc.h>

/* Low Power Timer */
#define LPC_LPT_LSB_OFF		0x400
#define LPC_LPT_MSB_OFF		0x404
#define LPC_LPT_START_OFF	0x408

/* Low Power Alarm */
#define LPC_LPA_LSB_OFF		0x410
#define LPC_LPA_MSB_OFF		0x414
#define LPC_LPA_START_OFF	0x418

/* LPC as WDT */
#define LPC_WDT_OFF		0x510
#define LPC_WDT_FLAG_OFF	0x514

struct st_rtc {
	struct rtc_device *rtc_dev;
	struct rtc_wkalrm alarm;
	struct resource *res;
	struct clk *clk;
	unsigned long clkrate;
	void __iomem *ioaddr;
	bool irq_enabled:1;
	spinlock_t lock;
	short irq;
};

static void st_rtc_set_hw_alarm(struct st_rtc *rtc,
				unsigned long msb, unsigned long  lsb)
{
	unsigned long flags;

	spin_lock_irqsave(&rtc->lock, flags);

	writel_relaxed(1, rtc->ioaddr + LPC_WDT_OFF);

	writel_relaxed(msb, rtc->ioaddr + LPC_LPA_MSB_OFF);
	writel_relaxed(lsb, rtc->ioaddr + LPC_LPA_LSB_OFF);
	writel_relaxed(1, rtc->ioaddr + LPC_LPA_START_OFF);

	writel_relaxed(0, rtc->ioaddr + LPC_WDT_OFF);

	spin_unlock_irqrestore(&rtc->lock, flags);
}

static irqreturn_t st_rtc_handler(int this_irq, void *data)
{
	struct st_rtc *rtc = (struct st_rtc *)data;

	rtc_update_irq(rtc->rtc_dev, 1, RTC_AF);

	return IRQ_HANDLED;
}

static int st_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct st_rtc *rtc = dev_get_drvdata(dev);
	unsigned long lpt_lsb, lpt_msb;
	unsigned long long lpt;
	unsigned long flags;

	spin_lock_irqsave(&rtc->lock, flags);

	do {
		lpt_msb = readl_relaxed(rtc->ioaddr + LPC_LPT_MSB_OFF);
		lpt_lsb = readl_relaxed(rtc->ioaddr + LPC_LPT_LSB_OFF);
	} while (readl_relaxed(rtc->ioaddr + LPC_LPT_MSB_OFF) != lpt_msb);

	spin_unlock_irqrestore(&rtc->lock, flags);

	lpt = ((unsigned long long)lpt_msb << 32) | lpt_lsb;
	do_div(lpt, rtc->clkrate);
	rtc_time64_to_tm(lpt, tm);

	return 0;
}

static int st_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct st_rtc *rtc = dev_get_drvdata(dev);
	unsigned long long lpt, secs;
	unsigned long flags;

	secs = rtc_tm_to_time64(tm);

	lpt = (unsigned long long)secs * rtc->clkrate;

	spin_lock_irqsave(&rtc->lock, flags);

	writel_relaxed(lpt >> 32, rtc->ioaddr + LPC_LPT_MSB_OFF);
	writel_relaxed(lpt, rtc->ioaddr + LPC_LPT_LSB_OFF);
	writel_relaxed(1, rtc->ioaddr + LPC_LPT_START_OFF);

	spin_unlock_irqrestore(&rtc->lock, flags);

	return 0;
}

static int st_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct st_rtc *rtc = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&rtc->lock, flags);

	memcpy(wkalrm, &rtc->alarm, sizeof(struct rtc_wkalrm));

	spin_unlock_irqrestore(&rtc->lock, flags);

	return 0;
}

static int st_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct st_rtc *rtc = dev_get_drvdata(dev);

	if (enabled && !rtc->irq_enabled) {
		enable_irq(rtc->irq);
		rtc->irq_enabled = true;
	} else if (!enabled && rtc->irq_enabled) {
		disable_irq(rtc->irq);
		rtc->irq_enabled = false;
	}

	return 0;
}

static int st_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct st_rtc *rtc = dev_get_drvdata(dev);
	struct rtc_time now;
	unsigned long long now_secs;
	unsigned long long alarm_secs;
	unsigned long long lpa;

	st_rtc_read_time(dev, &now);
	now_secs = rtc_tm_to_time64(&now);
	alarm_secs = rtc_tm_to_time64(&t->time);

	memcpy(&rtc->alarm, t, sizeof(struct rtc_wkalrm));

	/* Now many secs to fire */
	alarm_secs -= now_secs;
	lpa = (unsigned long long)alarm_secs * rtc->clkrate;

	st_rtc_set_hw_alarm(rtc, lpa >> 32, lpa);
	st_rtc_alarm_irq_enable(dev, t->enabled);

	return 0;
}

static struct rtc_class_ops st_rtc_ops = {
	.read_time		= st_rtc_read_time,
	.set_time		= st_rtc_set_time,
	.read_alarm		= st_rtc_read_alarm,
	.set_alarm		= st_rtc_set_alarm,
	.alarm_irq_enable	= st_rtc_alarm_irq_enable,
};

static int st_rtc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct st_rtc *rtc;
	uint32_t mode;
	int ret = 0;

	ret = of_property_read_u32(np, "st,lpc-mode", &mode);
	if (ret) {
		dev_err(&pdev->dev, "An LPC mode must be provided\n");
		return -EINVAL;
	}

	/* LPC can either run as a Clocksource or in RTC or WDT mode */
	if (mode != ST_LPC_MODE_RTC)
		return -ENODEV;

	rtc = devm_kzalloc(&pdev->dev, sizeof(struct st_rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->rtc_dev = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc->rtc_dev))
		return PTR_ERR(rtc->rtc_dev);

	spin_lock_init(&rtc->lock);

	rtc->ioaddr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rtc->ioaddr))
		return PTR_ERR(rtc->ioaddr);

	rtc->irq = irq_of_parse_and_map(np, 0);
	if (!rtc->irq) {
		dev_err(&pdev->dev, "IRQ missing or invalid\n");
		return -EINVAL;
	}

	ret = devm_request_irq(&pdev->dev, rtc->irq, st_rtc_handler, 0,
			       pdev->name, rtc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq %i\n", rtc->irq);
		return ret;
	}

	enable_irq_wake(rtc->irq);
	disable_irq(rtc->irq);

	rtc->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(rtc->clk)) {
		dev_err(&pdev->dev, "Unable to request clock\n");
		return PTR_ERR(rtc->clk);
	}

	clk_prepare_enable(rtc->clk);

	rtc->clkrate = clk_get_rate(rtc->clk);
	if (!rtc->clkrate) {
		dev_err(&pdev->dev, "Unable to fetch clock rate\n");
		return -EINVAL;
	}

	device_set_wakeup_capable(&pdev->dev, 1);

	platform_set_drvdata(pdev, rtc);

	rtc->rtc_dev->ops = &st_rtc_ops;
	rtc->rtc_dev->range_max = U64_MAX;
	do_div(rtc->rtc_dev->range_max, rtc->clkrate);

	ret = rtc_register_device(rtc->rtc_dev);
	if (ret) {
		clk_disable_unprepare(rtc->clk);
		return ret;
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int st_rtc_suspend(struct device *dev)
{
	struct st_rtc *rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		return 0;

	writel_relaxed(1, rtc->ioaddr + LPC_WDT_OFF);
	writel_relaxed(0, rtc->ioaddr + LPC_LPA_START_OFF);
	writel_relaxed(0, rtc->ioaddr + LPC_WDT_OFF);

	return 0;
}

static int st_rtc_resume(struct device *dev)
{
	struct st_rtc *rtc = dev_get_drvdata(dev);

	rtc_alarm_irq_enable(rtc->rtc_dev, 0);

	/*
	 * clean 'rtc->alarm' to allow a new
	 * .set_alarm to the upper RTC layer
	 */
	memset(&rtc->alarm, 0, sizeof(struct rtc_wkalrm));

	writel_relaxed(0, rtc->ioaddr + LPC_LPA_MSB_OFF);
	writel_relaxed(0, rtc->ioaddr + LPC_LPA_LSB_OFF);
	writel_relaxed(1, rtc->ioaddr + LPC_WDT_OFF);
	writel_relaxed(1, rtc->ioaddr + LPC_LPA_START_OFF);
	writel_relaxed(0, rtc->ioaddr + LPC_WDT_OFF);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(st_rtc_pm_ops, st_rtc_suspend, st_rtc_resume);

static const struct of_device_id st_rtc_match[] = {
	{ .compatible = "st,stih407-lpc" },
	{}
};
MODULE_DEVICE_TABLE(of, st_rtc_match);

static struct platform_driver st_rtc_platform_driver = {
	.driver = {
		.name = "st-lpc-rtc",
		.pm = &st_rtc_pm_ops,
		.of_match_table = st_rtc_match,
	},
	.probe = st_rtc_probe,
};

module_platform_driver(st_rtc_platform_driver);

MODULE_DESCRIPTION("STMicroelectronics LPC RTC driver");
MODULE_AUTHOR("David Paris <david.paris@st.com>");
MODULE_LICENSE("GPL");

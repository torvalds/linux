/*
 *  Faraday Technology FTRTC010 driver
 *
 *  Copyright (C) 2009 Janos Laube <janos.dev@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Original code for older kernel 2.6.15 are from Stormlinksemi
 * first update from Janos Laube for > 2.6.29 kernels
 *
 * checkpatch fixes and usage of rtc-lib code
 * Hans Ulli Kroll <ulli.kroll@googlemail.com>
 */

#include <linux/rtc.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>

#define DRV_NAME        "rtc-ftrtc010"

MODULE_AUTHOR("Hans Ulli Kroll <ulli.kroll@googlemail.com>");
MODULE_DESCRIPTION("RTC driver for Gemini SoC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);

struct ftrtc010_rtc {
	struct rtc_device	*rtc_dev;
	void __iomem		*rtc_base;
	int			rtc_irq;
	struct clk		*pclk;
	struct clk		*extclk;
};

enum ftrtc010_rtc_offsets {
	FTRTC010_RTC_SECOND		= 0x00,
	FTRTC010_RTC_MINUTE		= 0x04,
	FTRTC010_RTC_HOUR		= 0x08,
	FTRTC010_RTC_DAYS		= 0x0C,
	FTRTC010_RTC_ALARM_SECOND	= 0x10,
	FTRTC010_RTC_ALARM_MINUTE	= 0x14,
	FTRTC010_RTC_ALARM_HOUR		= 0x18,
	FTRTC010_RTC_RECORD		= 0x1C,
	FTRTC010_RTC_CR			= 0x20,
};

static irqreturn_t ftrtc010_rtc_interrupt(int irq, void *dev)
{
	return IRQ_HANDLED;
}

/*
 * Looks like the RTC in the Gemini SoC is (totaly) broken
 * We can't read/write directly the time from RTC registers.
 * We must do some "offset" calculation to get the real time
 *
 * This FIX works pretty fine and Stormlinksemi aka Cortina-Networks does
 * the same thing, without the rtc-lib.c calls.
 */

static int ftrtc010_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct ftrtc010_rtc *rtc = dev_get_drvdata(dev);

	unsigned int  days, hour, min, sec;
	unsigned long offset, time;

	sec  = readl(rtc->rtc_base + FTRTC010_RTC_SECOND);
	min  = readl(rtc->rtc_base + FTRTC010_RTC_MINUTE);
	hour = readl(rtc->rtc_base + FTRTC010_RTC_HOUR);
	days = readl(rtc->rtc_base + FTRTC010_RTC_DAYS);
	offset = readl(rtc->rtc_base + FTRTC010_RTC_RECORD);

	time = offset + days * 86400 + hour * 3600 + min * 60 + sec;

	rtc_time_to_tm(time, tm);

	return 0;
}

static int ftrtc010_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct ftrtc010_rtc *rtc = dev_get_drvdata(dev);
	unsigned int sec, min, hour, day;
	unsigned long offset, time;

	if (tm->tm_year >= 2148)	/* EPOCH Year + 179 */
		return -EINVAL;

	rtc_tm_to_time(tm, &time);

	sec = readl(rtc->rtc_base + FTRTC010_RTC_SECOND);
	min = readl(rtc->rtc_base + FTRTC010_RTC_MINUTE);
	hour = readl(rtc->rtc_base + FTRTC010_RTC_HOUR);
	day = readl(rtc->rtc_base + FTRTC010_RTC_DAYS);

	offset = time - (day * 86400 + hour * 3600 + min * 60 + sec);

	writel(offset, rtc->rtc_base + FTRTC010_RTC_RECORD);
	writel(0x01, rtc->rtc_base + FTRTC010_RTC_CR);

	return 0;
}

static const struct rtc_class_ops ftrtc010_rtc_ops = {
	.read_time     = ftrtc010_rtc_read_time,
	.set_time      = ftrtc010_rtc_set_time,
};

static int ftrtc010_rtc_probe(struct platform_device *pdev)
{
	struct ftrtc010_rtc *rtc;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (unlikely(!rtc))
		return -ENOMEM;
	platform_set_drvdata(pdev, rtc);

	rtc->pclk = devm_clk_get(dev, "PCLK");
	if (IS_ERR(rtc->pclk)) {
		dev_err(dev, "could not get PCLK\n");
	} else {
		ret = clk_prepare_enable(rtc->pclk);
		if (ret) {
			dev_err(dev, "failed to enable PCLK\n");
			return ret;
		}
	}
	rtc->extclk = devm_clk_get(dev, "EXTCLK");
	if (IS_ERR(rtc->extclk)) {
		dev_err(dev, "could not get EXTCLK\n");
	} else {
		ret = clk_prepare_enable(rtc->extclk);
		if (ret) {
			dev_err(dev, "failed to enable EXTCLK\n");
			return ret;
		}
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res)
		return -ENODEV;

	rtc->rtc_irq = res->start;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	rtc->rtc_base = devm_ioremap(dev, res->start,
				     resource_size(res));
	if (!rtc->rtc_base)
		return -ENOMEM;

	ret = devm_request_irq(dev, rtc->rtc_irq, ftrtc010_rtc_interrupt,
			       IRQF_SHARED, pdev->name, dev);
	if (unlikely(ret))
		return ret;

	rtc->rtc_dev = rtc_device_register(pdev->name, dev,
					   &ftrtc010_rtc_ops, THIS_MODULE);
	return PTR_ERR_OR_ZERO(rtc->rtc_dev);
}

static int ftrtc010_rtc_remove(struct platform_device *pdev)
{
	struct ftrtc010_rtc *rtc = platform_get_drvdata(pdev);

	if (!IS_ERR(rtc->extclk))
		clk_disable_unprepare(rtc->extclk);
	if (!IS_ERR(rtc->pclk))
		clk_disable_unprepare(rtc->pclk);
	rtc_device_unregister(rtc->rtc_dev);

	return 0;
}

static const struct of_device_id ftrtc010_rtc_dt_match[] = {
	{ .compatible = "cortina,gemini-rtc" },
	{ .compatible = "faraday,ftrtc010" },
	{ }
};
MODULE_DEVICE_TABLE(of, ftrtc010_rtc_dt_match);

static struct platform_driver ftrtc010_rtc_driver = {
	.driver		= {
		.name	= DRV_NAME,
		.of_match_table = ftrtc010_rtc_dt_match,
	},
	.probe		= ftrtc010_rtc_probe,
	.remove		= ftrtc010_rtc_remove,
};

module_platform_driver_probe(ftrtc010_rtc_driver, ftrtc010_rtc_probe);

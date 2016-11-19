/*
 *  Gemini OnChip RTC
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

#define DRV_NAME        "rtc-gemini"

MODULE_AUTHOR("Hans Ulli Kroll <ulli.kroll@googlemail.com>");
MODULE_DESCRIPTION("RTC driver for Gemini SoC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);

struct gemini_rtc {
	struct rtc_device	*rtc_dev;
	void __iomem		*rtc_base;
	int			rtc_irq;
};

enum gemini_rtc_offsets {
	GEMINI_RTC_SECOND	= 0x00,
	GEMINI_RTC_MINUTE	= 0x04,
	GEMINI_RTC_HOUR		= 0x08,
	GEMINI_RTC_DAYS		= 0x0C,
	GEMINI_RTC_ALARM_SECOND	= 0x10,
	GEMINI_RTC_ALARM_MINUTE	= 0x14,
	GEMINI_RTC_ALARM_HOUR	= 0x18,
	GEMINI_RTC_RECORD	= 0x1C,
	GEMINI_RTC_CR		= 0x20
};

static irqreturn_t gemini_rtc_interrupt(int irq, void *dev)
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

static int gemini_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct gemini_rtc *rtc = dev_get_drvdata(dev);

	unsigned int  days, hour, min, sec;
	unsigned long offset, time;

	sec  = readl(rtc->rtc_base + GEMINI_RTC_SECOND);
	min  = readl(rtc->rtc_base + GEMINI_RTC_MINUTE);
	hour = readl(rtc->rtc_base + GEMINI_RTC_HOUR);
	days = readl(rtc->rtc_base + GEMINI_RTC_DAYS);
	offset = readl(rtc->rtc_base + GEMINI_RTC_RECORD);

	time = offset + days * 86400 + hour * 3600 + min * 60 + sec;

	rtc_time_to_tm(time, tm);

	return 0;
}

static int gemini_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct gemini_rtc *rtc = dev_get_drvdata(dev);
	unsigned int sec, min, hour, day;
	unsigned long offset, time;

	if (tm->tm_year >= 2148)	/* EPOCH Year + 179 */
		return -EINVAL;

	rtc_tm_to_time(tm, &time);

	sec = readl(rtc->rtc_base + GEMINI_RTC_SECOND);
	min = readl(rtc->rtc_base + GEMINI_RTC_MINUTE);
	hour = readl(rtc->rtc_base + GEMINI_RTC_HOUR);
	day = readl(rtc->rtc_base + GEMINI_RTC_DAYS);

	offset = time - (day * 86400 + hour * 3600 + min * 60 + sec);

	writel(offset, rtc->rtc_base + GEMINI_RTC_RECORD);
	writel(0x01, rtc->rtc_base + GEMINI_RTC_CR);

	return 0;
}

static const struct rtc_class_ops gemini_rtc_ops = {
	.read_time     = gemini_rtc_read_time,
	.set_time      = gemini_rtc_set_time,
};

static int gemini_rtc_probe(struct platform_device *pdev)
{
	struct gemini_rtc *rtc;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (unlikely(!rtc))
		return -ENOMEM;
	platform_set_drvdata(pdev, rtc);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res)
		return -ENODEV;

	rtc->rtc_irq = res->start;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	rtc->rtc_base = devm_ioremap(dev, res->start,
				     resource_size(res));

	ret = devm_request_irq(dev, rtc->rtc_irq, gemini_rtc_interrupt,
			       IRQF_SHARED, pdev->name, dev);
	if (unlikely(ret))
		return ret;

	rtc->rtc_dev = rtc_device_register(pdev->name, dev,
					   &gemini_rtc_ops, THIS_MODULE);
	return PTR_ERR_OR_ZERO(rtc->rtc_dev);
}

static int gemini_rtc_remove(struct platform_device *pdev)
{
	struct gemini_rtc *rtc = platform_get_drvdata(pdev);

	rtc_device_unregister(rtc->rtc_dev);

	return 0;
}

static struct platform_driver gemini_rtc_driver = {
	.driver		= {
		.name	= DRV_NAME,
	},
	.probe		= gemini_rtc_probe,
	.remove		= gemini_rtc_remove,
};

module_platform_driver_probe(gemini_rtc_driver, gemini_rtc_probe);

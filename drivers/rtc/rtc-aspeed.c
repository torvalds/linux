// SPDX-License-Identifier: GPL-2.0+
// Copyright 2015 IBM Corp.

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/io.h>

struct aspeed_rtc {
	void __iomem *base;
};

#define RTC_TIME	0x00
#define RTC_YEAR	0x04
#define RTC_CTRL	0x10

#define RTC_UNLOCK	BIT(1)
#define RTC_ENABLE	BIT(0)

static int aspeed_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct aspeed_rtc *rtc = dev_get_drvdata(dev);
	unsigned int cent, year;
	u32 reg1, reg2;

	if (!(readl(rtc->base + RTC_CTRL) & RTC_ENABLE)) {
		dev_dbg(dev, "%s failing as rtc disabled\n", __func__);
		return -EINVAL;
	}

	do {
		reg2 = readl(rtc->base + RTC_YEAR);
		reg1 = readl(rtc->base + RTC_TIME);
	} while (reg2 != readl(rtc->base + RTC_YEAR));

	tm->tm_mday = (reg1 >> 24) & 0x1f;
	tm->tm_hour = (reg1 >> 16) & 0x1f;
	tm->tm_min = (reg1 >> 8) & 0x3f;
	tm->tm_sec = (reg1 >> 0) & 0x3f;

	cent = (reg2 >> 16) & 0x1f;
	year = (reg2 >> 8) & 0x7f;
	tm->tm_mon = ((reg2 >>  0) & 0x0f) - 1;
	tm->tm_year = year + (cent * 100) - 1900;

	dev_dbg(dev, "%s %ptR", __func__, tm);

	return 0;
}

static int aspeed_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct aspeed_rtc *rtc = dev_get_drvdata(dev);
	u32 reg1, reg2, ctrl;
	int year, cent;

	cent = (tm->tm_year + 1900) / 100;
	year = tm->tm_year % 100;

	reg1 = (tm->tm_mday << 24) | (tm->tm_hour << 16) | (tm->tm_min << 8) |
		tm->tm_sec;

	reg2 = ((cent & 0x1f) << 16) | ((year & 0x7f) << 8) |
		((tm->tm_mon + 1) & 0xf);

	ctrl = readl(rtc->base + RTC_CTRL);
	writel(ctrl | RTC_UNLOCK, rtc->base + RTC_CTRL);

	writel(reg1, rtc->base + RTC_TIME);
	writel(reg2, rtc->base + RTC_YEAR);

	/* Re-lock and ensure enable is set now that a time is programmed */
	writel(ctrl | RTC_ENABLE, rtc->base + RTC_CTRL);

	return 0;
}

static const struct rtc_class_ops aspeed_rtc_ops = {
	.read_time = aspeed_rtc_read_time,
	.set_time = aspeed_rtc_set_time,
};

static int aspeed_rtc_probe(struct platform_device *pdev)
{
	struct aspeed_rtc *rtc;
	struct rtc_device *rtc_dev;

	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rtc->base))
		return PTR_ERR(rtc->base);

	rtc_dev = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc_dev))
		return PTR_ERR(rtc_dev);

	platform_set_drvdata(pdev, rtc);

	rtc_dev->ops = &aspeed_rtc_ops;
	rtc_dev->range_min = RTC_TIMESTAMP_BEGIN_1900;
	rtc_dev->range_max = 38814989399LL; /* 3199-12-31 23:59:59 */

	return devm_rtc_register_device(rtc_dev);
}

static const struct of_device_id aspeed_rtc_match[] = {
	{ .compatible = "aspeed,ast2400-rtc", },
	{ .compatible = "aspeed,ast2500-rtc", },
	{ .compatible = "aspeed,ast2600-rtc", },
	{}
};
MODULE_DEVICE_TABLE(of, aspeed_rtc_match);

static struct platform_driver aspeed_rtc_driver = {
	.driver = {
		.name = "aspeed-rtc",
		.of_match_table = aspeed_rtc_match,
	},
};

module_platform_driver_probe(aspeed_rtc_driver, aspeed_rtc_probe);

MODULE_DESCRIPTION("ASPEED RTC driver");
MODULE_AUTHOR("Joel Stanley <joel@jms.id.au>");
MODULE_LICENSE("GPL");

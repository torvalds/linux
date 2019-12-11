// SPDX-License-Identifier: GPL-2.0+
/*
 * Real Time Clock driver for Conexant Digicolor
 *
 * Copyright (C) 2015 Paradox Innovation Ltd.
 *
 * Author: Baruch Siach <baruch@tkos.co.il>
 */

#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/of.h>

#define DC_RTC_CONTROL		0x0
#define DC_RTC_TIME		0x8
#define DC_RTC_REFERENCE	0xc
#define DC_RTC_ALARM		0x10
#define DC_RTC_INTFLAG_CLEAR	0x14
#define DC_RTC_INTENABLE	0x16

#define DC_RTC_CMD_MASK		0xf
#define DC_RTC_GO_BUSY		BIT(7)

#define CMD_NOP			0
#define CMD_RESET		1
#define CMD_WRITE		3
#define CMD_READ		4

#define CMD_DELAY_US		(10*1000)
#define CMD_TIMEOUT_US		(500*CMD_DELAY_US)

struct dc_rtc {
	struct rtc_device	*rtc_dev;
	void __iomem		*regs;
};

static int dc_rtc_cmds(struct dc_rtc *rtc, const u8 *cmds, int len)
{
	u8 val;
	int i, ret;

	for (i = 0; i < len; i++) {
		writeb_relaxed((cmds[i] & DC_RTC_CMD_MASK) | DC_RTC_GO_BUSY,
			       rtc->regs + DC_RTC_CONTROL);
		ret = readb_relaxed_poll_timeout(
			rtc->regs + DC_RTC_CONTROL, val,
			!(val & DC_RTC_GO_BUSY), CMD_DELAY_US, CMD_TIMEOUT_US);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int dc_rtc_read(struct dc_rtc *rtc, unsigned long *val)
{
	static const u8 read_cmds[] = {CMD_READ, CMD_NOP};
	u32 reference, time1, time2;
	int ret;

	ret = dc_rtc_cmds(rtc, read_cmds, ARRAY_SIZE(read_cmds));
	if (ret < 0)
		return ret;

	reference = readl_relaxed(rtc->regs + DC_RTC_REFERENCE);
	time1 = readl_relaxed(rtc->regs + DC_RTC_TIME);
	/* Read twice to ensure consistency */
	while (1) {
		time2 = readl_relaxed(rtc->regs + DC_RTC_TIME);
		if (time1 == time2)
			break;
		time1 = time2;
	}

	*val = reference + time1;
	return 0;
}

static int dc_rtc_write(struct dc_rtc *rtc, u32 val)
{
	static const u8 write_cmds[] = {CMD_WRITE, CMD_NOP, CMD_RESET, CMD_NOP};

	writel_relaxed(val, rtc->regs + DC_RTC_REFERENCE);
	return dc_rtc_cmds(rtc, write_cmds, ARRAY_SIZE(write_cmds));
}

static int dc_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct dc_rtc *rtc = dev_get_drvdata(dev);
	unsigned long now;
	int ret;

	ret = dc_rtc_read(rtc, &now);
	if (ret < 0)
		return ret;
	rtc_time64_to_tm(now, tm);

	return 0;
}

static int dc_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct dc_rtc *rtc = dev_get_drvdata(dev);

	return dc_rtc_write(rtc, rtc_tm_to_time64(tm));
}

static int dc_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct dc_rtc *rtc = dev_get_drvdata(dev);
	u32 alarm_reg, reference;
	unsigned long now;
	int ret;

	alarm_reg = readl_relaxed(rtc->regs + DC_RTC_ALARM);
	reference = readl_relaxed(rtc->regs + DC_RTC_REFERENCE);
	rtc_time64_to_tm(reference + alarm_reg, &alarm->time);

	ret = dc_rtc_read(rtc, &now);
	if (ret < 0)
		return ret;

	alarm->pending = alarm_reg + reference > now;
	alarm->enabled = readl_relaxed(rtc->regs + DC_RTC_INTENABLE);

	return 0;
}

static int dc_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct dc_rtc *rtc = dev_get_drvdata(dev);
	time64_t alarm_time;
	u32 reference;

	alarm_time = rtc_tm_to_time64(&alarm->time);

	reference = readl_relaxed(rtc->regs + DC_RTC_REFERENCE);
	writel_relaxed(alarm_time - reference, rtc->regs + DC_RTC_ALARM);

	writeb_relaxed(!!alarm->enabled, rtc->regs + DC_RTC_INTENABLE);

	return 0;
}

static int dc_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct dc_rtc *rtc = dev_get_drvdata(dev);

	writeb_relaxed(!!enabled, rtc->regs + DC_RTC_INTENABLE);

	return 0;
}

static const struct rtc_class_ops dc_rtc_ops = {
	.read_time		= dc_rtc_read_time,
	.set_time		= dc_rtc_set_time,
	.read_alarm		= dc_rtc_read_alarm,
	.set_alarm		= dc_rtc_set_alarm,
	.alarm_irq_enable	= dc_rtc_alarm_irq_enable,
};

static irqreturn_t dc_rtc_irq(int irq, void *dev_id)
{
	struct dc_rtc *rtc = dev_id;

	writeb_relaxed(1, rtc->regs + DC_RTC_INTFLAG_CLEAR);
	rtc_update_irq(rtc->rtc_dev, 1, RTC_AF | RTC_IRQF);

	return IRQ_HANDLED;
}

static int __init dc_rtc_probe(struct platform_device *pdev)
{
	struct dc_rtc *rtc;
	int irq, ret;

	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rtc->regs))
		return PTR_ERR(rtc->regs);

	rtc->rtc_dev = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc->rtc_dev))
		return PTR_ERR(rtc->rtc_dev);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	ret = devm_request_irq(&pdev->dev, irq, dc_rtc_irq, 0, pdev->name, rtc);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, rtc);

	rtc->rtc_dev->ops = &dc_rtc_ops;
	rtc->rtc_dev->range_max = U32_MAX;

	return rtc_register_device(rtc->rtc_dev);
}

static const struct of_device_id dc_dt_ids[] = {
	{ .compatible = "cnxt,cx92755-rtc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dc_dt_ids);

static struct platform_driver dc_rtc_driver = {
	.driver = {
		.name = "digicolor_rtc",
		.of_match_table = of_match_ptr(dc_dt_ids),
	},
};
module_platform_driver_probe(dc_rtc_driver, dc_rtc_probe);

MODULE_AUTHOR("Baruch Siach <baruch@tkos.co.il>");
MODULE_DESCRIPTION("Conexant Digicolor Realtime Clock Driver (RTC)");
MODULE_LICENSE("GPL");

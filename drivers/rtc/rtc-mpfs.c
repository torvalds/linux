// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip MPFS RTC driver
 *
 * Copyright (c) 2021-2022 Microchip Corporation. All rights reserved.
 *
 * Author: Daire McNamara <daire.mcnamara@microchip.com>
 *         & Conor Dooley <conor.dooley@microchip.com>
 */
#include "linux/bits.h"
#include "linux/iopoll.h"
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/slab.h>
#include <linux/rtc.h>

#define CONTROL_REG		0x00
#define MODE_REG		0x04
#define PRESCALER_REG		0x08
#define ALARM_LOWER_REG		0x0c
#define ALARM_UPPER_REG		0x10
#define COMPARE_LOWER_REG	0x14
#define COMPARE_UPPER_REG	0x18
#define DATETIME_LOWER_REG	0x20
#define DATETIME_UPPER_REG	0x24

#define CONTROL_RUNNING_BIT	BIT(0)
#define CONTROL_START_BIT	BIT(0)
#define CONTROL_STOP_BIT	BIT(1)
#define CONTROL_ALARM_ON_BIT	BIT(2)
#define CONTROL_ALARM_OFF_BIT	BIT(3)
#define CONTROL_RESET_BIT	BIT(4)
#define CONTROL_UPLOAD_BIT	BIT(5)
#define CONTROL_DOWNLOAD_BIT	BIT(6)
#define CONTROL_MATCH_BIT	BIT(7)
#define CONTROL_WAKEUP_CLR_BIT	BIT(8)
#define CONTROL_WAKEUP_SET_BIT	BIT(9)
#define CONTROL_UPDATED_BIT	BIT(10)

#define MODE_CLOCK_CALENDAR	BIT(0)
#define MODE_WAKE_EN		BIT(1)
#define MODE_WAKE_RESET		BIT(2)
#define MODE_WAKE_CONTINUE	BIT(3)

#define MAX_PRESCALER_COUNT	GENMASK(25, 0)
#define DATETIME_UPPER_MASK	GENMASK(29, 0)
#define ALARM_UPPER_MASK	GENMASK(10, 0)

#define UPLOAD_TIMEOUT_US	50

struct mpfs_rtc_dev {
	struct rtc_device *rtc;
	void __iomem *base;
};

static void mpfs_rtc_start(struct mpfs_rtc_dev *rtcdev)
{
	u32 ctrl;

	ctrl = readl(rtcdev->base + CONTROL_REG);
	ctrl &= ~CONTROL_STOP_BIT;
	ctrl |= CONTROL_START_BIT;
	writel(ctrl, rtcdev->base + CONTROL_REG);
}

static void mpfs_rtc_clear_irq(struct mpfs_rtc_dev *rtcdev)
{
	u32 val = readl(rtcdev->base + CONTROL_REG);

	val &= ~(CONTROL_ALARM_ON_BIT | CONTROL_STOP_BIT);
	val |= CONTROL_ALARM_OFF_BIT;
	writel(val, rtcdev->base + CONTROL_REG);
	/*
	 * Ensure that the posted write to the CONTROL_REG register completed before
	 * returning from this function. Not doing this may result in the interrupt
	 * only being cleared some time after this function returns.
	 */
	(void)readl(rtcdev->base + CONTROL_REG);
}

static int mpfs_rtc_readtime(struct device *dev, struct rtc_time *tm)
{
	struct mpfs_rtc_dev *rtcdev = dev_get_drvdata(dev);
	u64 time;

	time = readl(rtcdev->base + DATETIME_LOWER_REG);
	time |= ((u64)readl(rtcdev->base + DATETIME_UPPER_REG) & DATETIME_UPPER_MASK) << 32;
	rtc_time64_to_tm(time, tm);

	return 0;
}

static int mpfs_rtc_settime(struct device *dev, struct rtc_time *tm)
{
	struct mpfs_rtc_dev *rtcdev = dev_get_drvdata(dev);
	u32 ctrl, prog;
	u64 time;
	int ret;

	time = rtc_tm_to_time64(tm);

	writel((u32)time, rtcdev->base + DATETIME_LOWER_REG);
	writel((u32)(time >> 32) & DATETIME_UPPER_MASK, rtcdev->base + DATETIME_UPPER_REG);

	ctrl = readl(rtcdev->base + CONTROL_REG);
	ctrl &= ~CONTROL_STOP_BIT;
	ctrl |= CONTROL_UPLOAD_BIT;
	writel(ctrl, rtcdev->base + CONTROL_REG);

	ret = read_poll_timeout(readl, prog, prog & CONTROL_UPLOAD_BIT, 0, UPLOAD_TIMEOUT_US,
				false, rtcdev->base + CONTROL_REG);
	if (ret) {
		dev_err(dev, "timed out uploading time to rtc");
		return ret;
	}
	mpfs_rtc_start(rtcdev);

	return 0;
}

static int mpfs_rtc_readalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct mpfs_rtc_dev *rtcdev = dev_get_drvdata(dev);
	u32 mode = readl(rtcdev->base + MODE_REG);
	u64 time;

	alrm->enabled = mode & MODE_WAKE_EN;

	time = (u64)readl(rtcdev->base + ALARM_LOWER_REG) << 32;
	time |= (readl(rtcdev->base + ALARM_UPPER_REG) & ALARM_UPPER_MASK);
	rtc_time64_to_tm(time, &alrm->time);

	return 0;
}

static int mpfs_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct mpfs_rtc_dev *rtcdev = dev_get_drvdata(dev);
	u32 mode, ctrl;
	u64 time;

	/* Disable the alarm before updating */
	ctrl = readl(rtcdev->base + CONTROL_REG);
	ctrl |= CONTROL_ALARM_OFF_BIT;
	writel(ctrl, rtcdev->base + CONTROL_REG);

	time = rtc_tm_to_time64(&alrm->time);

	writel((u32)time, rtcdev->base + ALARM_LOWER_REG);
	writel((u32)(time >> 32) & ALARM_UPPER_MASK, rtcdev->base + ALARM_UPPER_REG);

	/* Bypass compare register in alarm mode */
	writel(GENMASK(31, 0), rtcdev->base + COMPARE_LOWER_REG);
	writel(GENMASK(29, 0), rtcdev->base + COMPARE_UPPER_REG);

	/* Configure the RTC to enable the alarm. */
	ctrl = readl(rtcdev->base + CONTROL_REG);
	mode = readl(rtcdev->base + MODE_REG);
	if (alrm->enabled) {
		mode = MODE_WAKE_EN | MODE_WAKE_CONTINUE;
		/* Enable the alarm */
		ctrl &= ~CONTROL_ALARM_OFF_BIT;
		ctrl |= CONTROL_ALARM_ON_BIT;
	}
	ctrl &= ~CONTROL_STOP_BIT;
	ctrl |= CONTROL_START_BIT;
	writel(ctrl, rtcdev->base + CONTROL_REG);
	writel(mode, rtcdev->base + MODE_REG);

	return 0;
}

static int mpfs_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct mpfs_rtc_dev *rtcdev = dev_get_drvdata(dev);
	u32 ctrl;

	ctrl = readl(rtcdev->base + CONTROL_REG);
	ctrl &= ~(CONTROL_ALARM_ON_BIT | CONTROL_ALARM_OFF_BIT | CONTROL_STOP_BIT);

	if (enabled)
		ctrl |= CONTROL_ALARM_ON_BIT;
	else
		ctrl |= CONTROL_ALARM_OFF_BIT;

	writel(ctrl, rtcdev->base + CONTROL_REG);

	return 0;
}

static irqreturn_t mpfs_rtc_wakeup_irq_handler(int irq, void *dev)
{
	struct mpfs_rtc_dev *rtcdev = dev;

	mpfs_rtc_clear_irq(rtcdev);

	rtc_update_irq(rtcdev->rtc, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops mpfs_rtc_ops = {
	.read_time		= mpfs_rtc_readtime,
	.set_time		= mpfs_rtc_settime,
	.read_alarm		= mpfs_rtc_readalarm,
	.set_alarm		= mpfs_rtc_setalarm,
	.alarm_irq_enable	= mpfs_rtc_alarm_irq_enable,
};

static int mpfs_rtc_probe(struct platform_device *pdev)
{
	struct mpfs_rtc_dev *rtcdev;
	struct clk *clk;
	unsigned long prescaler;
	int wakeup_irq, ret;

	rtcdev = devm_kzalloc(&pdev->dev, sizeof(struct mpfs_rtc_dev), GFP_KERNEL);
	if (!rtcdev)
		return -ENOMEM;

	platform_set_drvdata(pdev, rtcdev);

	rtcdev->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtcdev->rtc))
		return PTR_ERR(rtcdev->rtc);

	rtcdev->rtc->ops = &mpfs_rtc_ops;

	/* range is capped by alarm max, lower reg is 31:0 & upper is 10:0 */
	rtcdev->rtc->range_max = GENMASK_ULL(42, 0);

	clk = devm_clk_get_enabled(&pdev->dev, "rtc");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	rtcdev->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rtcdev->base)) {
		dev_dbg(&pdev->dev, "invalid ioremap resources\n");
		return PTR_ERR(rtcdev->base);
	}

	wakeup_irq = platform_get_irq(pdev, 0);
	if (wakeup_irq <= 0) {
		dev_dbg(&pdev->dev, "could not get wakeup irq\n");
		return wakeup_irq;
	}
	ret = devm_request_irq(&pdev->dev, wakeup_irq, mpfs_rtc_wakeup_irq_handler, 0,
			       dev_name(&pdev->dev), rtcdev);
	if (ret) {
		dev_dbg(&pdev->dev, "could not request wakeup irq\n");
		return ret;
	}

	/* prescaler hardware adds 1 to reg value */
	prescaler = clk_get_rate(devm_clk_get(&pdev->dev, "rtcref")) - 1;
	if (prescaler > MAX_PRESCALER_COUNT) {
		dev_dbg(&pdev->dev, "invalid prescaler %lu\n", prescaler);
		return -EINVAL;
	}

	writel(prescaler, rtcdev->base + PRESCALER_REG);
	dev_info(&pdev->dev, "prescaler set to: %lu\n", prescaler);

	device_init_wakeup(&pdev->dev, true);
	ret = dev_pm_set_wake_irq(&pdev->dev, wakeup_irq);
	if (ret)
		dev_err(&pdev->dev, "failed to enable irq wake\n");

	return devm_rtc_register_device(rtcdev->rtc);
}

static int mpfs_rtc_remove(struct platform_device *pdev)
{
	dev_pm_clear_wake_irq(&pdev->dev);

	return 0;
}

static const struct of_device_id mpfs_rtc_of_match[] = {
	{ .compatible = "microchip,mpfs-rtc" },
	{ }
};

MODULE_DEVICE_TABLE(of, mpfs_rtc_of_match);

static struct platform_driver mpfs_rtc_driver = {
	.probe = mpfs_rtc_probe,
	.remove = mpfs_rtc_remove,
	.driver	= {
		.name = "mpfs_rtc",
		.of_match_table = mpfs_rtc_of_match,
	},
};

module_platform_driver(mpfs_rtc_driver);

MODULE_DESCRIPTION("Real time clock for Microchip Polarfire SoC");
MODULE_AUTHOR("Daire McNamara <daire.mcnamara@microchip.com>");
MODULE_AUTHOR("Conor Dooley <conor.dooley@microchip.com>");
MODULE_LICENSE("GPL");

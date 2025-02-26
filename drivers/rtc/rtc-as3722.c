// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * rtc-as3722.c - Real Time Clock driver for ams AS3722 PMICs
 *
 * Copyright (C) 2013 ams AG
 * Copyright (c) 2013, NVIDIA Corporation. All rights reserved.
 *
 * Author: Florian Lobmaier <florian.lobmaier@ams.com>
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 */

#include <linux/bcd.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/as3722.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/time.h>

#define AS3722_RTC_START_YEAR	  2000
struct as3722_rtc {
	struct rtc_device	*rtc;
	struct device		*dev;
	struct as3722		*as3722;
	int			alarm_irq;
	bool			irq_enable;
};

static void as3722_time_to_reg(u8 *rbuff, struct rtc_time *tm)
{
	rbuff[0] = bin2bcd(tm->tm_sec);
	rbuff[1] = bin2bcd(tm->tm_min);
	rbuff[2] = bin2bcd(tm->tm_hour);
	rbuff[3] = bin2bcd(tm->tm_mday);
	rbuff[4] = bin2bcd(tm->tm_mon + 1);
	rbuff[5] = bin2bcd(tm->tm_year - (AS3722_RTC_START_YEAR - 1900));
}

static void as3722_reg_to_time(u8 *rbuff, struct rtc_time *tm)
{
	tm->tm_sec = bcd2bin(rbuff[0] & 0x7F);
	tm->tm_min = bcd2bin(rbuff[1] & 0x7F);
	tm->tm_hour = bcd2bin(rbuff[2] & 0x3F);
	tm->tm_mday = bcd2bin(rbuff[3] & 0x3F);
	tm->tm_mon = bcd2bin(rbuff[4] & 0x1F) - 1;
	tm->tm_year = (AS3722_RTC_START_YEAR - 1900) + bcd2bin(rbuff[5] & 0x7F);
	return;
}

static int as3722_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct as3722_rtc *as3722_rtc = dev_get_drvdata(dev);
	struct as3722 *as3722 = as3722_rtc->as3722;
	u8 as_time_array[6];
	int ret;

	ret = as3722_block_read(as3722, AS3722_RTC_SECOND_REG,
			6, as_time_array);
	if (ret < 0) {
		dev_err(dev, "RTC_SECOND reg block read failed %d\n", ret);
		return ret;
	}
	as3722_reg_to_time(as_time_array, tm);
	return 0;
}

static int as3722_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct as3722_rtc *as3722_rtc = dev_get_drvdata(dev);
	struct as3722 *as3722 = as3722_rtc->as3722;
	u8 as_time_array[6];
	int ret;

	if (tm->tm_year < (AS3722_RTC_START_YEAR - 1900))
		return -EINVAL;

	as3722_time_to_reg(as_time_array, tm);
	ret = as3722_block_write(as3722, AS3722_RTC_SECOND_REG, 6,
			as_time_array);
	if (ret < 0)
		dev_err(dev, "RTC_SECOND reg block write failed %d\n", ret);
	return ret;
}

static int as3722_rtc_alarm_irq_enable(struct device *dev,
		unsigned int enabled)
{
	struct as3722_rtc *as3722_rtc = dev_get_drvdata(dev);

	if (enabled && !as3722_rtc->irq_enable) {
		enable_irq(as3722_rtc->alarm_irq);
		as3722_rtc->irq_enable = true;
	} else if (!enabled && as3722_rtc->irq_enable)  {
		disable_irq(as3722_rtc->alarm_irq);
		as3722_rtc->irq_enable = false;
	}
	return 0;
}

static int as3722_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct as3722_rtc *as3722_rtc = dev_get_drvdata(dev);
	struct as3722 *as3722 = as3722_rtc->as3722;
	u8 as_time_array[6];
	int ret;

	ret = as3722_block_read(as3722, AS3722_RTC_ALARM_SECOND_REG, 6,
			as_time_array);
	if (ret < 0) {
		dev_err(dev, "RTC_ALARM_SECOND block read failed %d\n", ret);
		return ret;
	}

	as3722_reg_to_time(as_time_array, &alrm->time);
	return 0;
}

static int as3722_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct as3722_rtc *as3722_rtc = dev_get_drvdata(dev);
	struct as3722 *as3722 = as3722_rtc->as3722;
	u8 as_time_array[6];
	int ret;

	if (alrm->time.tm_year < (AS3722_RTC_START_YEAR - 1900))
		return -EINVAL;

	ret = as3722_rtc_alarm_irq_enable(dev, 0);
	if (ret < 0) {
		dev_err(dev, "Disable RTC alarm failed\n");
		return ret;
	}

	as3722_time_to_reg(as_time_array, &alrm->time);
	ret = as3722_block_write(as3722, AS3722_RTC_ALARM_SECOND_REG, 6,
			as_time_array);
	if (ret < 0) {
		dev_err(dev, "RTC_ALARM_SECOND block write failed %d\n", ret);
		return ret;
	}

	if (alrm->enabled)
		ret = as3722_rtc_alarm_irq_enable(dev, alrm->enabled);
	return ret;
}

static irqreturn_t as3722_alarm_irq(int irq, void *data)
{
	struct as3722_rtc *as3722_rtc = data;

	rtc_update_irq(as3722_rtc->rtc, 1, RTC_IRQF | RTC_AF);
	return IRQ_HANDLED;
}

static const struct rtc_class_ops as3722_rtc_ops = {
	.read_time = as3722_rtc_read_time,
	.set_time = as3722_rtc_set_time,
	.read_alarm = as3722_rtc_read_alarm,
	.set_alarm = as3722_rtc_set_alarm,
	.alarm_irq_enable = as3722_rtc_alarm_irq_enable,
};

static int as3722_rtc_probe(struct platform_device *pdev)
{
	struct as3722 *as3722 = dev_get_drvdata(pdev->dev.parent);
	struct as3722_rtc *as3722_rtc;
	int ret;

	as3722_rtc = devm_kzalloc(&pdev->dev, sizeof(*as3722_rtc), GFP_KERNEL);
	if (!as3722_rtc)
		return -ENOMEM;

	as3722_rtc->as3722 = as3722;
	as3722_rtc->dev = &pdev->dev;
	platform_set_drvdata(pdev, as3722_rtc);

	/* Enable the RTC to make sure it is running. */
	ret = as3722_update_bits(as3722, AS3722_RTC_CONTROL_REG,
			AS3722_RTC_ON | AS3722_RTC_ALARM_WAKEUP_EN,
			AS3722_RTC_ON | AS3722_RTC_ALARM_WAKEUP_EN);
	if (ret < 0) {
		dev_err(&pdev->dev, "RTC_CONTROL reg write failed: %d\n", ret);
		return ret;
	}

	device_init_wakeup(&pdev->dev, true);

	as3722_rtc->rtc = devm_rtc_device_register(&pdev->dev, "as3722-rtc",
				&as3722_rtc_ops, THIS_MODULE);
	if (IS_ERR(as3722_rtc->rtc)) {
		ret = PTR_ERR(as3722_rtc->rtc);
		dev_err(&pdev->dev, "RTC register failed: %d\n", ret);
		return ret;
	}

	as3722_rtc->alarm_irq = platform_get_irq(pdev, 0);
	dev_info(&pdev->dev, "RTC interrupt %d\n", as3722_rtc->alarm_irq);

	ret = devm_request_threaded_irq(&pdev->dev, as3722_rtc->alarm_irq, NULL,
			as3722_alarm_irq, IRQF_ONESHOT,
			"rtc-alarm", as3722_rtc);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request alarm IRQ %d: %d\n",
				as3722_rtc->alarm_irq, ret);
		return ret;
	}
	disable_irq(as3722_rtc->alarm_irq);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int as3722_rtc_suspend(struct device *dev)
{
	struct as3722_rtc *as3722_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(as3722_rtc->alarm_irq);

	return 0;
}

static int as3722_rtc_resume(struct device *dev)
{
	struct as3722_rtc *as3722_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(as3722_rtc->alarm_irq);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(as3722_rtc_pm_ops, as3722_rtc_suspend,
			 as3722_rtc_resume);

static struct platform_driver as3722_rtc_driver = {
	.probe = as3722_rtc_probe,
	.driver = {
		.name = "as3722-rtc",
		.pm = &as3722_rtc_pm_ops,
	},
};
module_platform_driver(as3722_rtc_driver);

MODULE_DESCRIPTION("RTC driver for AS3722 PMICs");
MODULE_ALIAS("platform:as3722-rtc");
MODULE_AUTHOR("Florian Lobmaier <florian.lobmaier@ams.com>");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_LICENSE("GPL");

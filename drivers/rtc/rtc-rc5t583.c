// SPDX-License-Identifier: GPL-2.0-only
/*
 * rtc-rc5t583.c -- RICOH RC5T583 Real Time Clock
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 * Author: Venu Byravarasu <vbyravarasu@nvidia.com>
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/mfd/rc5t583.h>

struct rc5t583_rtc {
	struct rtc_device	*rtc;
	/* To store the list of enabled interrupts, during system suspend */
	u32 irqen;
};

/* Total number of RTC registers needed to set time*/
#define NUM_TIME_REGS	(RC5T583_RTC_YEAR - RC5T583_RTC_SEC + 1)

/* Total number of RTC registers needed to set Y-Alarm*/
#define NUM_YAL_REGS	(RC5T583_RTC_AY_YEAR - RC5T583_RTC_AY_MIN + 1)

/* Set Y-Alarm interrupt */
#define SET_YAL BIT(5)

/* Get Y-Alarm interrupt status*/
#define GET_YAL_STATUS BIT(3)

static int rc5t583_rtc_alarm_irq_enable(struct device *dev, unsigned enabled)
{
	struct rc5t583 *rc5t583 = dev_get_drvdata(dev->parent);
	u8 val;

	/* Set Y-Alarm, based on 'enabled' */
	val = enabled ? SET_YAL : 0;

	return regmap_update_bits(rc5t583->regmap, RC5T583_RTC_CTL1, SET_YAL,
		val);
}

/*
 * Gets current rc5t583 RTC time and date parameters.
 *
 * The RTC's time/alarm representation is not what gmtime(3) requires
 * Linux to use:
 *
 *  - Months are 1..12 vs Linux 0-11
 *  - Years are 0..99 vs Linux 1900..N (we assume 21st century)
 */
static int rc5t583_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rc5t583 *rc5t583 = dev_get_drvdata(dev->parent);
	u8 rtc_data[NUM_TIME_REGS];
	int ret;

	ret = regmap_bulk_read(rc5t583->regmap, RC5T583_RTC_SEC, rtc_data,
		NUM_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "RTC read time failed with err:%d\n", ret);
		return ret;
	}

	tm->tm_sec = bcd2bin(rtc_data[0]);
	tm->tm_min = bcd2bin(rtc_data[1]);
	tm->tm_hour = bcd2bin(rtc_data[2]);
	tm->tm_wday = bcd2bin(rtc_data[3]);
	tm->tm_mday = bcd2bin(rtc_data[4]);
	tm->tm_mon = bcd2bin(rtc_data[5]) - 1;
	tm->tm_year = bcd2bin(rtc_data[6]) + 100;

	return ret;
}

static int rc5t583_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rc5t583 *rc5t583 = dev_get_drvdata(dev->parent);
	unsigned char rtc_data[NUM_TIME_REGS];
	int ret;

	rtc_data[0] = bin2bcd(tm->tm_sec);
	rtc_data[1] = bin2bcd(tm->tm_min);
	rtc_data[2] = bin2bcd(tm->tm_hour);
	rtc_data[3] = bin2bcd(tm->tm_wday);
	rtc_data[4] = bin2bcd(tm->tm_mday);
	rtc_data[5] = bin2bcd(tm->tm_mon + 1);
	rtc_data[6] = bin2bcd(tm->tm_year - 100);

	ret = regmap_bulk_write(rc5t583->regmap, RC5T583_RTC_SEC, rtc_data,
		NUM_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "RTC set time failed with error %d\n", ret);
		return ret;
	}

	return ret;
}

static int rc5t583_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct rc5t583 *rc5t583 = dev_get_drvdata(dev->parent);
	unsigned char alarm_data[NUM_YAL_REGS];
	u32 interrupt_enable;
	int ret;

	ret = regmap_bulk_read(rc5t583->regmap, RC5T583_RTC_AY_MIN, alarm_data,
		NUM_YAL_REGS);
	if (ret < 0) {
		dev_err(dev, "rtc_read_alarm error %d\n", ret);
		return ret;
	}

	alm->time.tm_sec = 0;
	alm->time.tm_min = bcd2bin(alarm_data[0]);
	alm->time.tm_hour = bcd2bin(alarm_data[1]);
	alm->time.tm_mday = bcd2bin(alarm_data[2]);
	alm->time.tm_mon = bcd2bin(alarm_data[3]) - 1;
	alm->time.tm_year = bcd2bin(alarm_data[4]) + 100;

	ret = regmap_read(rc5t583->regmap, RC5T583_RTC_CTL1, &interrupt_enable);
	if (ret < 0)
		return ret;

	/* check if YALE is set */
	if (interrupt_enable & SET_YAL)
		alm->enabled = 1;

	return ret;
}

static int rc5t583_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct rc5t583 *rc5t583 = dev_get_drvdata(dev->parent);
	unsigned char alarm_data[NUM_YAL_REGS];
	int ret;

	ret = rc5t583_rtc_alarm_irq_enable(dev, 0);
	if (ret)
		return ret;

	alarm_data[0] = bin2bcd(alm->time.tm_min);
	alarm_data[1] = bin2bcd(alm->time.tm_hour);
	alarm_data[2] = bin2bcd(alm->time.tm_mday);
	alarm_data[3] = bin2bcd(alm->time.tm_mon + 1);
	alarm_data[4] = bin2bcd(alm->time.tm_year - 100);

	ret = regmap_bulk_write(rc5t583->regmap, RC5T583_RTC_AY_MIN, alarm_data,
		NUM_YAL_REGS);
	if (ret) {
		dev_err(dev, "rtc_set_alarm error %d\n", ret);
		return ret;
	}

	if (alm->enabled)
		ret = rc5t583_rtc_alarm_irq_enable(dev, 1);

	return ret;
}

static irqreturn_t rc5t583_rtc_interrupt(int irq, void *rtc)
{
	struct device *dev = rtc;
	struct rc5t583 *rc5t583 = dev_get_drvdata(dev->parent);
	struct rc5t583_rtc *rc5t583_rtc = dev_get_drvdata(dev);
	unsigned long events = 0;
	int ret;
	u32 rtc_reg;

	ret = regmap_read(rc5t583->regmap, RC5T583_RTC_CTL2, &rtc_reg);
	if (ret < 0)
		return IRQ_NONE;

	if (rtc_reg & GET_YAL_STATUS) {
		events = RTC_IRQF | RTC_AF;
		/* clear pending Y-alarm interrupt bit */
		rtc_reg &= ~GET_YAL_STATUS;
	}

	ret = regmap_write(rc5t583->regmap, RC5T583_RTC_CTL2, rtc_reg);
	if (ret)
		return IRQ_NONE;

	/* Notify RTC core on event */
	rtc_update_irq(rc5t583_rtc->rtc, 1, events);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops rc5t583_rtc_ops = {
	.read_time	= rc5t583_rtc_read_time,
	.set_time	= rc5t583_rtc_set_time,
	.read_alarm	= rc5t583_rtc_read_alarm,
	.set_alarm	= rc5t583_rtc_set_alarm,
	.alarm_irq_enable = rc5t583_rtc_alarm_irq_enable,
};

static int rc5t583_rtc_probe(struct platform_device *pdev)
{
	struct rc5t583 *rc5t583 = dev_get_drvdata(pdev->dev.parent);
	struct rc5t583_rtc *ricoh_rtc;
	struct rc5t583_platform_data *pmic_plat_data;
	int ret;
	int irq;

	ricoh_rtc = devm_kzalloc(&pdev->dev, sizeof(struct rc5t583_rtc),
			GFP_KERNEL);
	if (!ricoh_rtc)
		return -ENOMEM;

	platform_set_drvdata(pdev, ricoh_rtc);

	/* Clear pending interrupts */
	ret = regmap_write(rc5t583->regmap, RC5T583_RTC_CTL2, 0);
	if (ret < 0)
		return ret;

	/* clear RTC Adjust register */
	ret = regmap_write(rc5t583->regmap, RC5T583_RTC_ADJ, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to program rtc_adjust reg\n");
		return -EBUSY;
	}

	pmic_plat_data = dev_get_platdata(rc5t583->dev);
	irq = pmic_plat_data->irq_base;
	if (irq <= 0) {
		dev_warn(&pdev->dev, "Wake up is not possible as irq = %d\n",
			irq);
		return ret;
	}

	irq += RC5T583_IRQ_YALE;
	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
		rc5t583_rtc_interrupt, IRQF_TRIGGER_LOW,
		"rtc-rc5t583", &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "IRQ is not free.\n");
		return ret;
	}
	device_init_wakeup(&pdev->dev, 1);

	ricoh_rtc->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
		&rc5t583_rtc_ops, THIS_MODULE);
	if (IS_ERR(ricoh_rtc->rtc)) {
		ret = PTR_ERR(ricoh_rtc->rtc);
		dev_err(&pdev->dev, "RTC device register: err %d\n", ret);
		return ret;
	}

	return 0;
}

/*
 * Disable rc5t583 RTC interrupts.
 * Sets status flag to free.
 */
static int rc5t583_rtc_remove(struct platform_device *pdev)
{
	struct rc5t583_rtc *rc5t583_rtc = platform_get_drvdata(pdev);

	rc5t583_rtc_alarm_irq_enable(&rc5t583_rtc->rtc->dev, 0);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rc5t583_rtc_suspend(struct device *dev)
{
	struct rc5t583 *rc5t583 = dev_get_drvdata(dev->parent);
	struct rc5t583_rtc *rc5t583_rtc = dev_get_drvdata(dev);
	int ret;

	/* Store current list of enabled interrupts*/
	ret = regmap_read(rc5t583->regmap, RC5T583_RTC_CTL1,
		&rc5t583_rtc->irqen);
	return ret;
}

static int rc5t583_rtc_resume(struct device *dev)
{
	struct rc5t583 *rc5t583 = dev_get_drvdata(dev->parent);
	struct rc5t583_rtc *rc5t583_rtc = dev_get_drvdata(dev);

	/* Restore list of enabled interrupts before suspend */
	return regmap_write(rc5t583->regmap, RC5T583_RTC_CTL1,
		rc5t583_rtc->irqen);
}
#endif

static SIMPLE_DEV_PM_OPS(rc5t583_rtc_pm_ops, rc5t583_rtc_suspend,
			rc5t583_rtc_resume);

static struct platform_driver rc5t583_rtc_driver = {
	.probe		= rc5t583_rtc_probe,
	.remove		= rc5t583_rtc_remove,
	.driver		= {
		.name	= "rtc-rc5t583",
		.pm	= &rc5t583_rtc_pm_ops,
	},
};

module_platform_driver(rc5t583_rtc_driver);
MODULE_ALIAS("platform:rtc-rc5t583");
MODULE_AUTHOR("Venu Byravarasu <vbyravarasu@nvidia.com>");
MODULE_LICENSE("GPL v2");

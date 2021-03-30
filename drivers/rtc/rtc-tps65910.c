// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * rtc-tps65910.c -- TPS65910 Real Time Clock interface
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 * Author: Venu Byravarasu <vbyravarasu@nvidia.com>
 *
 * Based on original TI driver rtc-twl.c
 *   Copyright (C) 2007 MontaVista Software, Inc
 *   Author: Alexandre Rusev <source@mvista.com>
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/math64.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/mfd/tps65910.h>

struct tps65910_rtc {
	struct rtc_device	*rtc;
	int irq;
};

/* Total number of RTC registers needed to set time*/
#define NUM_TIME_REGS	(TPS65910_YEARS - TPS65910_SECONDS + 1)

/* Total number of RTC registers needed to set compensation registers */
#define NUM_COMP_REGS	(TPS65910_RTC_COMP_MSB - TPS65910_RTC_COMP_LSB + 1)

/* Min and max values supported with 'offset' interface (swapped sign) */
#define MIN_OFFSET	(-277761)
#define MAX_OFFSET	(277778)

/* Number of ticks per hour */
#define TICKS_PER_HOUR	(32768 * 3600)

/* Multiplier for ppb conversions */
#define PPB_MULT	(1000000000LL)

static int tps65910_rtc_alarm_irq_enable(struct device *dev,
					 unsigned int enabled)
{
	struct tps65910 *tps = dev_get_drvdata(dev->parent);
	u8 val = 0;

	if (enabled)
		val = TPS65910_RTC_INTERRUPTS_IT_ALARM;

	return regmap_write(tps->regmap, TPS65910_RTC_INTERRUPTS, val);
}

/*
 * Gets current tps65910 RTC time and date parameters.
 *
 * The RTC's time/alarm representation is not what gmtime(3) requires
 * Linux to use:
 *
 *  - Months are 1..12 vs Linux 0-11
 *  - Years are 0..99 vs Linux 1900..N (we assume 21st century)
 */
static int tps65910_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char rtc_data[NUM_TIME_REGS];
	struct tps65910 *tps = dev_get_drvdata(dev->parent);
	int ret;

	/* Copy RTC counting registers to static registers or latches */
	ret = regmap_update_bits(tps->regmap, TPS65910_RTC_CTRL,
		TPS65910_RTC_CTRL_GET_TIME, TPS65910_RTC_CTRL_GET_TIME);
	if (ret < 0) {
		dev_err(dev, "RTC CTRL reg update failed with err:%d\n", ret);
		return ret;
	}

	ret = regmap_bulk_read(tps->regmap, TPS65910_SECONDS, rtc_data,
		NUM_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "reading from RTC failed with err:%d\n", ret);
		return ret;
	}

	tm->tm_sec = bcd2bin(rtc_data[0]);
	tm->tm_min = bcd2bin(rtc_data[1]);
	tm->tm_hour = bcd2bin(rtc_data[2]);
	tm->tm_mday = bcd2bin(rtc_data[3]);
	tm->tm_mon = bcd2bin(rtc_data[4]) - 1;
	tm->tm_year = bcd2bin(rtc_data[5]) + 100;

	return ret;
}

static int tps65910_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char rtc_data[NUM_TIME_REGS];
	struct tps65910 *tps = dev_get_drvdata(dev->parent);
	int ret;

	rtc_data[0] = bin2bcd(tm->tm_sec);
	rtc_data[1] = bin2bcd(tm->tm_min);
	rtc_data[2] = bin2bcd(tm->tm_hour);
	rtc_data[3] = bin2bcd(tm->tm_mday);
	rtc_data[4] = bin2bcd(tm->tm_mon + 1);
	rtc_data[5] = bin2bcd(tm->tm_year - 100);

	/* Stop RTC while updating the RTC time registers */
	ret = regmap_update_bits(tps->regmap, TPS65910_RTC_CTRL,
		TPS65910_RTC_CTRL_STOP_RTC, 0);
	if (ret < 0) {
		dev_err(dev, "RTC stop failed with err:%d\n", ret);
		return ret;
	}

	/* update all the time registers in one shot */
	ret = regmap_bulk_write(tps->regmap, TPS65910_SECONDS, rtc_data,
		NUM_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "rtc_set_time error %d\n", ret);
		return ret;
	}

	/* Start back RTC */
	ret = regmap_update_bits(tps->regmap, TPS65910_RTC_CTRL,
		TPS65910_RTC_CTRL_STOP_RTC, 1);
	if (ret < 0)
		dev_err(dev, "RTC start failed with err:%d\n", ret);

	return ret;
}

/*
 * Gets current tps65910 RTC alarm time.
 */
static int tps65910_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	unsigned char alarm_data[NUM_TIME_REGS];
	u32 int_val;
	struct tps65910 *tps = dev_get_drvdata(dev->parent);
	int ret;

	ret = regmap_bulk_read(tps->regmap, TPS65910_ALARM_SECONDS, alarm_data,
		NUM_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "rtc_read_alarm error %d\n", ret);
		return ret;
	}

	alm->time.tm_sec = bcd2bin(alarm_data[0]);
	alm->time.tm_min = bcd2bin(alarm_data[1]);
	alm->time.tm_hour = bcd2bin(alarm_data[2]);
	alm->time.tm_mday = bcd2bin(alarm_data[3]);
	alm->time.tm_mon = bcd2bin(alarm_data[4]) - 1;
	alm->time.tm_year = bcd2bin(alarm_data[5]) + 100;

	ret = regmap_read(tps->regmap, TPS65910_RTC_INTERRUPTS, &int_val);
	if (ret < 0)
		return ret;

	if (int_val & TPS65910_RTC_INTERRUPTS_IT_ALARM)
		alm->enabled = 1;

	return ret;
}

static int tps65910_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	unsigned char alarm_data[NUM_TIME_REGS];
	struct tps65910 *tps = dev_get_drvdata(dev->parent);
	int ret;

	ret = tps65910_rtc_alarm_irq_enable(dev, 0);
	if (ret)
		return ret;

	alarm_data[0] = bin2bcd(alm->time.tm_sec);
	alarm_data[1] = bin2bcd(alm->time.tm_min);
	alarm_data[2] = bin2bcd(alm->time.tm_hour);
	alarm_data[3] = bin2bcd(alm->time.tm_mday);
	alarm_data[4] = bin2bcd(alm->time.tm_mon + 1);
	alarm_data[5] = bin2bcd(alm->time.tm_year - 100);

	/* update all the alarm registers in one shot */
	ret = regmap_bulk_write(tps->regmap, TPS65910_ALARM_SECONDS,
		alarm_data, NUM_TIME_REGS);
	if (ret) {
		dev_err(dev, "rtc_set_alarm error %d\n", ret);
		return ret;
	}

	if (alm->enabled)
		ret = tps65910_rtc_alarm_irq_enable(dev, 1);

	return ret;
}

static int tps65910_rtc_set_calibration(struct device *dev, int calibration)
{
	unsigned char comp_data[NUM_COMP_REGS];
	struct tps65910 *tps = dev_get_drvdata(dev->parent);
	s16 value;
	int ret;

	/*
	 * TPS65910 uses two's complement 16 bit value for compensation for RTC
	 * crystal inaccuracies. One time every hour when seconds counter
	 * increments from 0 to 1 compensation value will be added to internal
	 * RTC counter value.
	 *
	 * Compensation value 0x7FFF is prohibited value.
	 *
	 * Valid range for compensation value: [-32768 .. 32766]
	 */
	if ((calibration < -32768) || (calibration > 32766)) {
		dev_err(dev, "RTC calibration value out of range: %d\n",
			calibration);
		return -EINVAL;
	}

	value = (s16)calibration;

	comp_data[0] = (u16)value & 0xFF;
	comp_data[1] = ((u16)value >> 8) & 0xFF;

	/* Update all the compensation registers in one shot */
	ret = regmap_bulk_write(tps->regmap, TPS65910_RTC_COMP_LSB,
		comp_data, NUM_COMP_REGS);
	if (ret < 0) {
		dev_err(dev, "rtc_set_calibration error: %d\n", ret);
		return ret;
	}

	/* Enable automatic compensation */
	ret = regmap_update_bits(tps->regmap, TPS65910_RTC_CTRL,
		TPS65910_RTC_CTRL_AUTO_COMP, TPS65910_RTC_CTRL_AUTO_COMP);
	if (ret < 0)
		dev_err(dev, "auto_comp enable failed with error: %d\n", ret);

	return ret;
}

static int tps65910_rtc_get_calibration(struct device *dev, int *calibration)
{
	unsigned char comp_data[NUM_COMP_REGS];
	struct tps65910 *tps = dev_get_drvdata(dev->parent);
	unsigned int ctrl;
	u16 value;
	int ret;

	ret = regmap_read(tps->regmap, TPS65910_RTC_CTRL, &ctrl);
	if (ret < 0)
		return ret;

	/* If automatic compensation is not enabled report back zero */
	if (!(ctrl & TPS65910_RTC_CTRL_AUTO_COMP)) {
		*calibration = 0;
		return 0;
	}

	ret = regmap_bulk_read(tps->regmap, TPS65910_RTC_COMP_LSB, comp_data,
		NUM_COMP_REGS);
	if (ret < 0) {
		dev_err(dev, "rtc_get_calibration error: %d\n", ret);
		return ret;
	}

	value = (u16)comp_data[0] | ((u16)comp_data[1] << 8);

	*calibration = (s16)value;

	return 0;
}

static int tps65910_read_offset(struct device *dev, long *offset)
{
	int calibration;
	s64 tmp;
	int ret;

	ret = tps65910_rtc_get_calibration(dev, &calibration);
	if (ret < 0)
		return ret;

	/* Convert from RTC calibration register format to ppb format */
	tmp = calibration * (s64)PPB_MULT;
	if (tmp < 0)
		tmp -= TICKS_PER_HOUR / 2LL;
	else
		tmp += TICKS_PER_HOUR / 2LL;
	tmp = div_s64(tmp, TICKS_PER_HOUR);

	/* Offset value operates in negative way, so swap sign */
	*offset = (long)-tmp;

	return 0;
}

static int tps65910_set_offset(struct device *dev, long offset)
{
	int calibration;
	s64 tmp;
	int ret;

	/* Make sure offset value is within supported range */
	if (offset < MIN_OFFSET || offset > MAX_OFFSET)
		return -ERANGE;

	/* Convert from ppb format to RTC calibration register format */
	tmp = offset * (s64)TICKS_PER_HOUR;
	if (tmp < 0)
		tmp -= PPB_MULT / 2LL;
	else
		tmp += PPB_MULT / 2LL;
	tmp = div_s64(tmp, PPB_MULT);

	/* Offset value operates in negative way, so swap sign */
	calibration = (int)-tmp;

	ret = tps65910_rtc_set_calibration(dev, calibration);

	return ret;
}

static irqreturn_t tps65910_rtc_interrupt(int irq, void *rtc)
{
	struct device *dev = rtc;
	unsigned long events = 0;
	struct tps65910 *tps = dev_get_drvdata(dev->parent);
	struct tps65910_rtc *tps_rtc = dev_get_drvdata(dev);
	int ret;
	u32 rtc_reg;

	ret = regmap_read(tps->regmap, TPS65910_RTC_STATUS, &rtc_reg);
	if (ret)
		return IRQ_NONE;

	if (rtc_reg & TPS65910_RTC_STATUS_ALARM)
		events = RTC_IRQF | RTC_AF;

	ret = regmap_write(tps->regmap, TPS65910_RTC_STATUS, rtc_reg);
	if (ret)
		return IRQ_NONE;

	/* Notify RTC core on event */
	rtc_update_irq(tps_rtc->rtc, 1, events);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops tps65910_rtc_ops = {
	.read_time	= tps65910_rtc_read_time,
	.set_time	= tps65910_rtc_set_time,
	.read_alarm	= tps65910_rtc_read_alarm,
	.set_alarm	= tps65910_rtc_set_alarm,
	.alarm_irq_enable = tps65910_rtc_alarm_irq_enable,
	.read_offset	= tps65910_read_offset,
	.set_offset	= tps65910_set_offset,
};

static int tps65910_rtc_probe(struct platform_device *pdev)
{
	struct tps65910 *tps65910 = NULL;
	struct tps65910_rtc *tps_rtc = NULL;
	int ret;
	int irq;
	u32 rtc_reg;

	tps65910 = dev_get_drvdata(pdev->dev.parent);

	tps_rtc = devm_kzalloc(&pdev->dev, sizeof(struct tps65910_rtc),
			GFP_KERNEL);
	if (!tps_rtc)
		return -ENOMEM;

	tps_rtc->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(tps_rtc->rtc))
		return PTR_ERR(tps_rtc->rtc);

	/* Clear pending interrupts */
	ret = regmap_read(tps65910->regmap, TPS65910_RTC_STATUS, &rtc_reg);
	if (ret < 0)
		return ret;

	ret = regmap_write(tps65910->regmap, TPS65910_RTC_STATUS, rtc_reg);
	if (ret < 0)
		return ret;

	dev_dbg(&pdev->dev, "Enabling rtc-tps65910.\n");

	/* Enable RTC digital power domain */
	ret = regmap_update_bits(tps65910->regmap, TPS65910_DEVCTRL,
		DEVCTRL_RTC_PWDN_MASK, 0 << DEVCTRL_RTC_PWDN_SHIFT);
	if (ret < 0)
		return ret;

	rtc_reg = TPS65910_RTC_CTRL_STOP_RTC;
	ret = regmap_write(tps65910->regmap, TPS65910_RTC_CTRL, rtc_reg);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, tps_rtc);

	irq  = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_warn(&pdev->dev, "Wake up is not possible as irq = %d\n",
			irq);
		return -ENXIO;
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
		tps65910_rtc_interrupt, IRQF_TRIGGER_LOW,
		dev_name(&pdev->dev), &pdev->dev);
	if (ret < 0)
		irq = -1;

	tps_rtc->irq = irq;
	if (irq != -1) {
		if (device_property_present(tps65910->dev, "wakeup-source"))
			device_init_wakeup(&pdev->dev, 1);
		else
			device_set_wakeup_capable(&pdev->dev, 1);
	} else {
		clear_bit(RTC_FEATURE_ALARM, tps_rtc->rtc->features);
	}

	tps_rtc->rtc->ops = &tps65910_rtc_ops;
	tps_rtc->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	tps_rtc->rtc->range_max = RTC_TIMESTAMP_END_2099;

	return devm_rtc_register_device(tps_rtc->rtc);
}

#ifdef CONFIG_PM_SLEEP
static int tps65910_rtc_suspend(struct device *dev)
{
	struct tps65910_rtc *tps_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(tps_rtc->irq);
	return 0;
}

static int tps65910_rtc_resume(struct device *dev)
{
	struct tps65910_rtc *tps_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(tps_rtc->irq);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(tps65910_rtc_pm_ops, tps65910_rtc_suspend,
			tps65910_rtc_resume);

static struct platform_driver tps65910_rtc_driver = {
	.probe		= tps65910_rtc_probe,
	.driver		= {
		.name	= "tps65910-rtc",
		.pm	= &tps65910_rtc_pm_ops,
	},
};

module_platform_driver(tps65910_rtc_driver);
MODULE_ALIAS("platform:rtc-tps65910");
MODULE_AUTHOR("Venu Byravarasu <vbyravarasu@nvidia.com>");
MODULE_LICENSE("GPL");

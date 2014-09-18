/* rtc-da9063.c - Real time clock device driver for DA9063
 * Copyright (C) 2013-14  Dialog Semiconductor Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/mfd/da9063/registers.h>
#include <linux/mfd/da9063/core.h>

#define YEARS_TO_DA9063(year)		((year) - 100)
#define MONTHS_TO_DA9063(month)		((month) + 1)
#define YEARS_FROM_DA9063(year)		((year) + 100)
#define MONTHS_FROM_DA9063(month)	((month) - 1)

#define RTC_ALARM_DATA_LEN (DA9063_AD_REG_ALARM_Y - DA9063_AD_REG_ALARM_MI + 1)

#define RTC_DATA_LEN	(DA9063_REG_COUNT_Y - DA9063_REG_COUNT_S + 1)
#define RTC_SEC		0
#define RTC_MIN		1
#define RTC_HOUR	2
#define RTC_DAY		3
#define RTC_MONTH	4
#define RTC_YEAR	5

struct da9063_rtc {
	struct rtc_device	*rtc_dev;
	struct da9063		*hw;
	struct rtc_time		alarm_time;
	bool			rtc_sync;
	int			alarm_year;
	int			alarm_start;
	int			alarm_len;
	int			data_start;
};

static void da9063_data_to_tm(u8 *data, struct rtc_time *tm)
{
	tm->tm_sec  = data[RTC_SEC]  & DA9063_COUNT_SEC_MASK;
	tm->tm_min  = data[RTC_MIN]  & DA9063_COUNT_MIN_MASK;
	tm->tm_hour = data[RTC_HOUR] & DA9063_COUNT_HOUR_MASK;
	tm->tm_mday = data[RTC_DAY]  & DA9063_COUNT_DAY_MASK;
	tm->tm_mon  = MONTHS_FROM_DA9063(data[RTC_MONTH] &
					 DA9063_COUNT_MONTH_MASK);
	tm->tm_year = YEARS_FROM_DA9063(data[RTC_YEAR] &
					DA9063_COUNT_YEAR_MASK);
}

static void da9063_tm_to_data(struct rtc_time *tm, u8 *data)
{
	data[RTC_SEC] &= ~DA9063_COUNT_SEC_MASK;
	data[RTC_SEC] |= tm->tm_sec & DA9063_COUNT_SEC_MASK;

	data[RTC_MIN] &= ~DA9063_COUNT_MIN_MASK;
	data[RTC_MIN] |= tm->tm_min & DA9063_COUNT_MIN_MASK;

	data[RTC_HOUR] &= ~DA9063_COUNT_HOUR_MASK;
	data[RTC_HOUR] |= tm->tm_hour & DA9063_COUNT_HOUR_MASK;

	data[RTC_DAY] &= ~DA9063_COUNT_DAY_MASK;
	data[RTC_DAY] |= tm->tm_mday & DA9063_COUNT_DAY_MASK;

	data[RTC_MONTH] &= ~DA9063_COUNT_MONTH_MASK;
	data[RTC_MONTH] |= MONTHS_TO_DA9063(tm->tm_mon) &
				DA9063_COUNT_MONTH_MASK;

	data[RTC_YEAR] &= ~DA9063_COUNT_YEAR_MASK;
	data[RTC_YEAR] |= YEARS_TO_DA9063(tm->tm_year) &
				DA9063_COUNT_YEAR_MASK;
}

static int da9063_rtc_stop_alarm(struct device *dev)
{
	struct da9063_rtc *rtc = dev_get_drvdata(dev);

	return regmap_update_bits(rtc->hw->regmap, rtc->alarm_year,
				  DA9063_ALARM_ON, 0);
}

static int da9063_rtc_start_alarm(struct device *dev)
{
	struct da9063_rtc *rtc = dev_get_drvdata(dev);

	return regmap_update_bits(rtc->hw->regmap, rtc->alarm_year,
				  DA9063_ALARM_ON, DA9063_ALARM_ON);
}

static int da9063_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct da9063_rtc *rtc = dev_get_drvdata(dev);
	unsigned long tm_secs;
	unsigned long al_secs;
	u8 data[RTC_DATA_LEN];
	int ret;

	ret = regmap_bulk_read(rtc->hw->regmap, DA9063_REG_COUNT_S,
			       data, RTC_DATA_LEN);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC time data: %d\n", ret);
		return ret;
	}

	if (!(data[RTC_SEC] & DA9063_RTC_READ)) {
		dev_dbg(dev, "RTC not yet ready to be read by the host\n");
		return -EINVAL;
	}

	da9063_data_to_tm(data, tm);

	rtc_tm_to_time(tm, &tm_secs);
	rtc_tm_to_time(&rtc->alarm_time, &al_secs);

	/* handle the rtc synchronisation delay */
	if (rtc->rtc_sync == true && al_secs - tm_secs == 1)
		memcpy(tm, &rtc->alarm_time, sizeof(struct rtc_time));
	else
		rtc->rtc_sync = false;

	return rtc_valid_tm(tm);
}

static int da9063_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct da9063_rtc *rtc = dev_get_drvdata(dev);
	u8 data[RTC_DATA_LEN];
	int ret;

	da9063_tm_to_data(tm, data);
	ret = regmap_bulk_write(rtc->hw->regmap, DA9063_REG_COUNT_S,
				data, RTC_DATA_LEN);
	if (ret < 0)
		dev_err(dev, "Failed to set RTC time data: %d\n", ret);

	return ret;
}

static int da9063_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct da9063_rtc *rtc = dev_get_drvdata(dev);
	u8 data[RTC_DATA_LEN];
	int ret;
	unsigned int val;

	data[RTC_SEC] = 0;
	ret = regmap_bulk_read(rtc->hw->regmap, rtc->alarm_start,
			       &data[rtc->data_start], rtc->alarm_len);
	if (ret < 0)
		return ret;

	da9063_data_to_tm(data, &alrm->time);

	alrm->enabled = !!(data[RTC_YEAR] & DA9063_ALARM_ON);

	ret = regmap_read(rtc->hw->regmap, DA9063_REG_EVENT_A, &val);
	if (ret < 0)
		return ret;

	if (val & (DA9063_E_ALARM))
		alrm->pending = 1;
	else
		alrm->pending = 0;

	return 0;
}

static int da9063_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct da9063_rtc *rtc = dev_get_drvdata(dev);
	u8 data[RTC_DATA_LEN];
	int ret;

	da9063_tm_to_data(&alrm->time, data);

	ret = da9063_rtc_stop_alarm(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to stop alarm: %d\n", ret);
		return ret;
	}

	ret = regmap_bulk_write(rtc->hw->regmap, rtc->alarm_start,
			       &data[rtc->data_start], rtc->alarm_len);
	if (ret < 0) {
		dev_err(dev, "Failed to write alarm: %d\n", ret);
		return ret;
	}

	da9063_data_to_tm(data, &rtc->alarm_time);

	if (alrm->enabled) {
		ret = da9063_rtc_start_alarm(dev);
		if (ret < 0) {
			dev_err(dev, "Failed to start alarm: %d\n", ret);
			return ret;
		}
	}

	return ret;
}

static int da9063_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	if (enabled)
		return da9063_rtc_start_alarm(dev);
	else
		return da9063_rtc_stop_alarm(dev);
}

static irqreturn_t da9063_alarm_event(int irq, void *data)
{
	struct da9063_rtc *rtc = data;

	regmap_update_bits(rtc->hw->regmap, rtc->alarm_year,
			   DA9063_ALARM_ON, 0);

	rtc->rtc_sync = true;
	rtc_update_irq(rtc->rtc_dev, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops da9063_rtc_ops = {
	.read_time = da9063_rtc_read_time,
	.set_time = da9063_rtc_set_time,
	.read_alarm = da9063_rtc_read_alarm,
	.set_alarm = da9063_rtc_set_alarm,
	.alarm_irq_enable = da9063_rtc_alarm_irq_enable,
};

static int da9063_rtc_probe(struct platform_device *pdev)
{
	struct da9063 *da9063 = dev_get_drvdata(pdev->dev.parent);
	struct da9063_rtc *rtc;
	int irq_alarm;
	u8 data[RTC_DATA_LEN];
	int ret;

	ret = regmap_update_bits(da9063->regmap, DA9063_REG_CONTROL_E,
				 DA9063_RTC_EN, DA9063_RTC_EN);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable RTC\n");
		goto err;
	}

	ret = regmap_update_bits(da9063->regmap, DA9063_REG_EN_32K,
				 DA9063_CRYSTAL, DA9063_CRYSTAL);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to run 32kHz oscillator\n");
		goto err;
	}

	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	if (da9063->variant_code == PMIC_DA9063_AD) {
		rtc->alarm_year = DA9063_AD_REG_ALARM_Y;
		rtc->alarm_start = DA9063_AD_REG_ALARM_MI;
		rtc->alarm_len = RTC_ALARM_DATA_LEN;
		rtc->data_start = RTC_MIN;
	} else {
		rtc->alarm_year = DA9063_BB_REG_ALARM_Y;
		rtc->alarm_start = DA9063_BB_REG_ALARM_S;
		rtc->alarm_len = RTC_DATA_LEN;
		rtc->data_start = RTC_SEC;
	}

	ret = regmap_update_bits(da9063->regmap, rtc->alarm_start,
			DA9063_ALARM_STATUS_TICK | DA9063_ALARM_STATUS_ALARM,
			0);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to access RTC alarm register\n");
		goto err;
	}

	ret = regmap_update_bits(da9063->regmap, rtc->alarm_start,
				 DA9063_ALARM_STATUS_ALARM,
				 DA9063_ALARM_STATUS_ALARM);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to access RTC alarm register\n");
		goto err;
	}

	ret = regmap_update_bits(da9063->regmap, rtc->alarm_year,
				 DA9063_TICK_ON, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to disable TICKs\n");
		goto err;
	}

	data[RTC_SEC] = 0;
	ret = regmap_bulk_read(da9063->regmap, rtc->alarm_start,
			       &data[rtc->data_start], rtc->alarm_len);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to read initial alarm data: %d\n",
			ret);
		goto err;
	}

	platform_set_drvdata(pdev, rtc);

	irq_alarm = platform_get_irq_byname(pdev, "ALARM");
	ret = devm_request_threaded_irq(&pdev->dev, irq_alarm, NULL,
					da9063_alarm_event,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					"ALARM", rtc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request ALARM IRQ %d: %d\n",
			irq_alarm, ret);
		goto err;
	}

	rtc->hw = da9063;
	rtc->rtc_dev = devm_rtc_device_register(&pdev->dev, DA9063_DRVNAME_RTC,
					   &da9063_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc->rtc_dev))
		return PTR_ERR(rtc->rtc_dev);

	da9063_data_to_tm(data, &rtc->alarm_time);
	rtc->rtc_sync = false;
err:
	return ret;
}

static struct platform_driver da9063_rtc_driver = {
	.probe		= da9063_rtc_probe,
	.driver		= {
		.name	= DA9063_DRVNAME_RTC,
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(da9063_rtc_driver);

MODULE_AUTHOR("S Twiss <stwiss.opensource@diasemi.com>");
MODULE_DESCRIPTION("Real time clock device driver for Dialog DA9063");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DA9063_DRVNAME_RTC);

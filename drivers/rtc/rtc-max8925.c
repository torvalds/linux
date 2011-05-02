/*
 * RTC driver for Maxim MAX8925
 *
 * Copyright (C) 2009-2010 Marvell International Ltd.
 *	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <linux/mfd/max8925.h>

enum {
	RTC_SEC = 0,
	RTC_MIN,
	RTC_HOUR,
	RTC_WEEKDAY,
	RTC_DATE,
	RTC_MONTH,
	RTC_YEAR1,
	RTC_YEAR2,
};

#define MAX8925_RTC_SEC			0x00
#define MAX8925_RTC_MIN			0x01
#define MAX8925_RTC_HOUR		0x02
#define MAX8925_RTC_WEEKDAY		0x03
#define MAX8925_RTC_DATE		0x04
#define MAX8925_RTC_MONTH		0x05
#define MAX8925_RTC_YEAR1		0x06
#define MAX8925_RTC_YEAR2		0x07
#define MAX8925_ALARM0_SEC		0x08
#define MAX8925_ALARM0_MIN		0x09
#define MAX8925_ALARM0_HOUR		0x0a
#define MAX8925_ALARM0_WEEKDAY		0x0b
#define MAX8925_ALARM0_DATE		0x0c
#define MAX8925_ALARM0_MON		0x0d
#define MAX8925_ALARM0_YEAR1		0x0e
#define MAX8925_ALARM0_YEAR2		0x0f
#define MAX8925_ALARM1_SEC		0x10
#define MAX8925_ALARM1_MIN		0x11
#define MAX8925_ALARM1_HOUR		0x12
#define MAX8925_ALARM1_WEEKDAY		0x13
#define MAX8925_ALARM1_DATE		0x14
#define MAX8925_ALARM1_MON		0x15
#define MAX8925_ALARM1_YEAR1		0x16
#define MAX8925_ALARM1_YEAR2		0x17
#define MAX8925_RTC_CNTL		0x1b
#define MAX8925_RTC_STATUS		0x20

#define TIME_NUM			8
#define ALARM_1SEC			(1 << 7)
#define HOUR_12				(1 << 7)
#define HOUR_AM_PM			(1 << 5)
#define ALARM0_IRQ			(1 << 3)
#define ALARM1_IRQ			(1 << 2)
#define ALARM0_STATUS			(1 << 2)
#define ALARM1_STATUS			(1 << 1)


struct max8925_rtc_info {
	struct rtc_device	*rtc_dev;
	struct max8925_chip	*chip;
	struct i2c_client	*rtc;
	struct device		*dev;
};

static irqreturn_t rtc_update_handler(int irq, void *data)
{
	struct max8925_rtc_info *info = (struct max8925_rtc_info *)data;

	/* disable ALARM0 except for 1SEC alarm */
	max8925_set_bits(info->rtc, MAX8925_ALARM0_CNTL, 0x7f, 0);
	rtc_update_irq(info->rtc_dev, 1, RTC_IRQF | RTC_AF);
	return IRQ_HANDLED;
}

static int tm_calc(struct rtc_time *tm, unsigned char *buf, int len)
{
	if (len < TIME_NUM)
		return -EINVAL;
	tm->tm_year = (buf[RTC_YEAR2] >> 4) * 1000
			+ (buf[RTC_YEAR2] & 0xf) * 100
			+ (buf[RTC_YEAR1] >> 4) * 10
			+ (buf[RTC_YEAR1] & 0xf);
	tm->tm_year -= 1900;
	tm->tm_mon = ((buf[RTC_MONTH] >> 4) & 0x01) * 10
			+ (buf[RTC_MONTH] & 0x0f);
	tm->tm_mday = ((buf[RTC_DATE] >> 4) & 0x03) * 10
			+ (buf[RTC_DATE] & 0x0f);
	tm->tm_wday = buf[RTC_WEEKDAY] & 0x07;
	if (buf[RTC_HOUR] & HOUR_12) {
		tm->tm_hour = ((buf[RTC_HOUR] >> 4) & 0x1) * 10
				+ (buf[RTC_HOUR] & 0x0f);
		if (buf[RTC_HOUR] & HOUR_AM_PM)
			tm->tm_hour += 12;
	} else
		tm->tm_hour = ((buf[RTC_HOUR] >> 4) & 0x03) * 10
				+ (buf[RTC_HOUR] & 0x0f);
	tm->tm_min = ((buf[RTC_MIN] >> 4) & 0x7) * 10
			+ (buf[RTC_MIN] & 0x0f);
	tm->tm_sec = ((buf[RTC_SEC] >> 4) & 0x7) * 10
			+ (buf[RTC_SEC] & 0x0f);
	return 0;
}

static int data_calc(unsigned char *buf, struct rtc_time *tm, int len)
{
	unsigned char high, low;

	if (len < TIME_NUM)
		return -EINVAL;

	high = (tm->tm_year + 1900) / 1000;
	low = (tm->tm_year + 1900) / 100;
	low = low - high * 10;
	buf[RTC_YEAR2] = (high << 4) + low;
	high = (tm->tm_year + 1900) / 10;
	low = tm->tm_year + 1900;
	low = low - high * 10;
	high = high - (high / 10) * 10;
	buf[RTC_YEAR1] = (high << 4) + low;
	high = tm->tm_mon / 10;
	low = tm->tm_mon;
	low = low - high * 10;
	buf[RTC_MONTH] = (high << 4) + low;
	high = tm->tm_mday / 10;
	low = tm->tm_mday;
	low = low - high * 10;
	buf[RTC_DATE] = (high << 4) + low;
	buf[RTC_WEEKDAY] = tm->tm_wday;
	high = tm->tm_hour / 10;
	low = tm->tm_hour;
	low = low - high * 10;
	buf[RTC_HOUR] = (high << 4) + low;
	high = tm->tm_min / 10;
	low = tm->tm_min;
	low = low - high * 10;
	buf[RTC_MIN] = (high << 4) + low;
	high = tm->tm_sec / 10;
	low = tm->tm_sec;
	low = low - high * 10;
	buf[RTC_SEC] = (high << 4) + low;
	return 0;
}

static int max8925_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct max8925_rtc_info *info = dev_get_drvdata(dev);
	unsigned char buf[TIME_NUM];
	int ret;

	ret = max8925_bulk_read(info->rtc, MAX8925_RTC_SEC, TIME_NUM, buf);
	if (ret < 0)
		goto out;
	ret = tm_calc(tm, buf, TIME_NUM);
out:
	return ret;
}

static int max8925_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct max8925_rtc_info *info = dev_get_drvdata(dev);
	unsigned char buf[TIME_NUM];
	int ret;

	ret = data_calc(buf, tm, TIME_NUM);
	if (ret < 0)
		goto out;
	ret = max8925_bulk_write(info->rtc, MAX8925_RTC_SEC, TIME_NUM, buf);
out:
	return ret;
}

static int max8925_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct max8925_rtc_info *info = dev_get_drvdata(dev);
	unsigned char buf[TIME_NUM];
	int ret;

	ret = max8925_bulk_read(info->rtc, MAX8925_ALARM0_SEC, TIME_NUM, buf);
	if (ret < 0)
		goto out;
	ret = tm_calc(&alrm->time, buf, TIME_NUM);
	if (ret < 0)
		goto out;
	ret = max8925_reg_read(info->rtc, MAX8925_RTC_IRQ_MASK);
	if (ret < 0)
		goto out;
	if ((ret & ALARM0_IRQ) == 0)
		alrm->enabled = 1;
	else
		alrm->enabled = 0;
	ret = max8925_reg_read(info->rtc, MAX8925_RTC_STATUS);
	if (ret < 0)
		goto out;
	if (ret & ALARM0_STATUS)
		alrm->pending = 1;
	else
		alrm->pending = 0;
out:
	return ret;
}

static int max8925_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct max8925_rtc_info *info = dev_get_drvdata(dev);
	unsigned char buf[TIME_NUM];
	int ret;

	ret = data_calc(buf, &alrm->time, TIME_NUM);
	if (ret < 0)
		goto out;
	ret = max8925_bulk_write(info->rtc, MAX8925_ALARM0_SEC, TIME_NUM, buf);
	if (ret < 0)
		goto out;
	/* only enable alarm on year/month/day/hour/min/sec */
	ret = max8925_reg_write(info->rtc, MAX8925_ALARM0_CNTL, 0x77);
	if (ret < 0)
		goto out;
out:
	return ret;
}

static const struct rtc_class_ops max8925_rtc_ops = {
	.read_time	= max8925_rtc_read_time,
	.set_time	= max8925_rtc_set_time,
	.read_alarm	= max8925_rtc_read_alarm,
	.set_alarm	= max8925_rtc_set_alarm,
};

static int __devinit max8925_rtc_probe(struct platform_device *pdev)
{
	struct max8925_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct max8925_rtc_info *info;
	int irq, ret;

	info = kzalloc(sizeof(struct max8925_rtc_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->chip = chip;
	info->rtc = chip->rtc;
	info->dev = &pdev->dev;
	irq = chip->irq_base + MAX8925_IRQ_RTC_ALARM0;

	ret = request_threaded_irq(irq, NULL, rtc_update_handler,
				   IRQF_ONESHOT, "rtc-alarm0", info);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to request IRQ: #%d: %d\n",
			irq, ret);
		goto out_irq;
	}

	dev_set_drvdata(&pdev->dev, info);

	info->rtc_dev = rtc_device_register("max8925-rtc", &pdev->dev,
					&max8925_rtc_ops, THIS_MODULE);
	ret = PTR_ERR(info->rtc_dev);
	if (IS_ERR(info->rtc_dev)) {
		dev_err(&pdev->dev, "Failed to register RTC device: %d\n", ret);
		goto out_rtc;
	}

	platform_set_drvdata(pdev, info);

	return 0;
out_rtc:
	free_irq(chip->irq_base + MAX8925_IRQ_RTC_ALARM0, info);
out_irq:
	kfree(info);
	return ret;
}

static int __devexit max8925_rtc_remove(struct platform_device *pdev)
{
	struct max8925_rtc_info *info = platform_get_drvdata(pdev);

	if (info) {
		free_irq(info->chip->irq_base + MAX8925_IRQ_RTC_ALARM0, info);
		rtc_device_unregister(info->rtc_dev);
		kfree(info);
	}
	return 0;
}

static struct platform_driver max8925_rtc_driver = {
	.driver		= {
		.name	= "max8925-rtc",
		.owner	= THIS_MODULE,
	},
	.probe		= max8925_rtc_probe,
	.remove		= __devexit_p(max8925_rtc_remove),
};

static int __init max8925_rtc_init(void)
{
	return platform_driver_register(&max8925_rtc_driver);
}
module_init(max8925_rtc_init);

static void __exit max8925_rtc_exit(void)
{
	platform_driver_unregister(&max8925_rtc_driver);
}
module_exit(max8925_rtc_exit);

MODULE_DESCRIPTION("Maxim MAX8925 RTC driver");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_LICENSE("GPL");


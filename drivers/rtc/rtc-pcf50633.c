/* NXP PCF50633 RTC Driver
 *
 * (C) 2006-2008 by Openmoko, Inc.
 * Author: Balaji Rao <balajirrao@openmoko.org>
 * All rights reserved.
 *
 * Broken down from monstrous PCF50633 driver mainly by
 * Harald Welte, Andy Green and Werner Almesberger
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/err.h>

#include <linux/mfd/pcf50633/core.h>

#define PCF50633_REG_RTCSC	0x59 /* Second */
#define PCF50633_REG_RTCMN	0x5a /* Minute */
#define PCF50633_REG_RTCHR	0x5b /* Hour */
#define PCF50633_REG_RTCWD	0x5c /* Weekday */
#define PCF50633_REG_RTCDT	0x5d /* Day */
#define PCF50633_REG_RTCMT	0x5e /* Month */
#define PCF50633_REG_RTCYR	0x5f /* Year */
#define PCF50633_REG_RTCSCA	0x60 /* Alarm Second */
#define PCF50633_REG_RTCMNA	0x61 /* Alarm Minute */
#define PCF50633_REG_RTCHRA	0x62 /* Alarm Hour */
#define PCF50633_REG_RTCWDA	0x63 /* Alarm Weekday */
#define PCF50633_REG_RTCDTA	0x64 /* Alarm Day */
#define PCF50633_REG_RTCMTA	0x65 /* Alarm Month */
#define PCF50633_REG_RTCYRA	0x66 /* Alarm Year */

enum pcf50633_time_indexes {
	PCF50633_TI_SEC,
	PCF50633_TI_MIN,
	PCF50633_TI_HOUR,
	PCF50633_TI_WKDAY,
	PCF50633_TI_DAY,
	PCF50633_TI_MONTH,
	PCF50633_TI_YEAR,
	PCF50633_TI_EXTENT /* always last */
};

struct pcf50633_time {
	u_int8_t time[PCF50633_TI_EXTENT];
};

struct pcf50633_rtc {
	int alarm_enabled;
	int alarm_pending;

	struct pcf50633 *pcf;
	struct rtc_device *rtc_dev;
};

static void pcf2rtc_time(struct rtc_time *rtc, struct pcf50633_time *pcf)
{
	rtc->tm_sec = bcd2bin(pcf->time[PCF50633_TI_SEC]);
	rtc->tm_min = bcd2bin(pcf->time[PCF50633_TI_MIN]);
	rtc->tm_hour = bcd2bin(pcf->time[PCF50633_TI_HOUR]);
	rtc->tm_wday = bcd2bin(pcf->time[PCF50633_TI_WKDAY]);
	rtc->tm_mday = bcd2bin(pcf->time[PCF50633_TI_DAY]);
	rtc->tm_mon = bcd2bin(pcf->time[PCF50633_TI_MONTH]) - 1;
	rtc->tm_year = bcd2bin(pcf->time[PCF50633_TI_YEAR]) + 100;
}

static void rtc2pcf_time(struct pcf50633_time *pcf, struct rtc_time *rtc)
{
	pcf->time[PCF50633_TI_SEC] = bin2bcd(rtc->tm_sec);
	pcf->time[PCF50633_TI_MIN] = bin2bcd(rtc->tm_min);
	pcf->time[PCF50633_TI_HOUR] = bin2bcd(rtc->tm_hour);
	pcf->time[PCF50633_TI_WKDAY] = bin2bcd(rtc->tm_wday);
	pcf->time[PCF50633_TI_DAY] = bin2bcd(rtc->tm_mday);
	pcf->time[PCF50633_TI_MONTH] = bin2bcd(rtc->tm_mon + 1);
	pcf->time[PCF50633_TI_YEAR] = bin2bcd(rtc->tm_year % 100);
}

static int
pcf50633_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct pcf50633_rtc *rtc = dev_get_drvdata(dev);
	int err;

	if (enabled)
		err = pcf50633_irq_unmask(rtc->pcf, PCF50633_IRQ_ALARM);
	else
		err = pcf50633_irq_mask(rtc->pcf, PCF50633_IRQ_ALARM);

	if (err < 0)
		return err;

	rtc->alarm_enabled = enabled;

	return 0;
}

static int pcf50633_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct pcf50633_rtc *rtc;
	struct pcf50633_time pcf_tm;
	int ret;

	rtc = dev_get_drvdata(dev);

	ret = pcf50633_read_block(rtc->pcf, PCF50633_REG_RTCSC,
					    PCF50633_TI_EXTENT,
					    &pcf_tm.time[0]);
	if (ret != PCF50633_TI_EXTENT) {
		dev_err(dev, "Failed to read time\n");
		return -EIO;
	}

	dev_dbg(dev, "PCF_TIME: %02x.%02x.%02x %02x:%02x:%02x\n",
		pcf_tm.time[PCF50633_TI_DAY],
		pcf_tm.time[PCF50633_TI_MONTH],
		pcf_tm.time[PCF50633_TI_YEAR],
		pcf_tm.time[PCF50633_TI_HOUR],
		pcf_tm.time[PCF50633_TI_MIN],
		pcf_tm.time[PCF50633_TI_SEC]);

	pcf2rtc_time(tm, &pcf_tm);

	dev_dbg(dev, "RTC_TIME: %u.%u.%u %u:%u:%u\n",
		tm->tm_mday, tm->tm_mon, tm->tm_year,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	return rtc_valid_tm(tm);
}

static int pcf50633_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct pcf50633_rtc *rtc;
	struct pcf50633_time pcf_tm;
	int alarm_masked, ret = 0;

	rtc = dev_get_drvdata(dev);

	dev_dbg(dev, "RTC_TIME: %u.%u.%u %u:%u:%u\n",
		tm->tm_mday, tm->tm_mon, tm->tm_year,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	rtc2pcf_time(&pcf_tm, tm);

	dev_dbg(dev, "PCF_TIME: %02x.%02x.%02x %02x:%02x:%02x\n",
		pcf_tm.time[PCF50633_TI_DAY],
		pcf_tm.time[PCF50633_TI_MONTH],
		pcf_tm.time[PCF50633_TI_YEAR],
		pcf_tm.time[PCF50633_TI_HOUR],
		pcf_tm.time[PCF50633_TI_MIN],
		pcf_tm.time[PCF50633_TI_SEC]);


	alarm_masked = pcf50633_irq_mask_get(rtc->pcf, PCF50633_IRQ_ALARM);

	if (!alarm_masked)
		pcf50633_irq_mask(rtc->pcf, PCF50633_IRQ_ALARM);

	/* Returns 0 on success */
	ret = pcf50633_write_block(rtc->pcf, PCF50633_REG_RTCSC,
					     PCF50633_TI_EXTENT,
					     &pcf_tm.time[0]);

	if (!alarm_masked)
		pcf50633_irq_unmask(rtc->pcf, PCF50633_IRQ_ALARM);

	return ret;
}

static int pcf50633_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct pcf50633_rtc *rtc;
	struct pcf50633_time pcf_tm;
	int ret = 0;

	rtc = dev_get_drvdata(dev);

	alrm->enabled = rtc->alarm_enabled;
	alrm->pending = rtc->alarm_pending;

	ret = pcf50633_read_block(rtc->pcf, PCF50633_REG_RTCSCA,
				PCF50633_TI_EXTENT, &pcf_tm.time[0]);
	if (ret != PCF50633_TI_EXTENT) {
		dev_err(dev, "Failed to read time\n");
		return -EIO;
	}

	pcf2rtc_time(&alrm->time, &pcf_tm);

	return rtc_valid_tm(&alrm->time);
}

static int pcf50633_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct pcf50633_rtc *rtc;
	struct pcf50633_time pcf_tm;
	int alarm_masked, ret = 0;

	rtc = dev_get_drvdata(dev);

	rtc2pcf_time(&pcf_tm, &alrm->time);

	/* do like mktime does and ignore tm_wday */
	pcf_tm.time[PCF50633_TI_WKDAY] = 7;

	alarm_masked = pcf50633_irq_mask_get(rtc->pcf, PCF50633_IRQ_ALARM);

	/* disable alarm interrupt */
	if (!alarm_masked)
		pcf50633_irq_mask(rtc->pcf, PCF50633_IRQ_ALARM);

	/* Returns 0 on success */
	ret = pcf50633_write_block(rtc->pcf, PCF50633_REG_RTCSCA,
				PCF50633_TI_EXTENT, &pcf_tm.time[0]);
	if (!alrm->enabled)
		rtc->alarm_pending = 0;

	if (!alarm_masked || alrm->enabled)
		pcf50633_irq_unmask(rtc->pcf, PCF50633_IRQ_ALARM);
	rtc->alarm_enabled = alrm->enabled;

	return ret;
}

static struct rtc_class_ops pcf50633_rtc_ops = {
	.read_time		= pcf50633_rtc_read_time,
	.set_time		= pcf50633_rtc_set_time,
	.read_alarm		= pcf50633_rtc_read_alarm,
	.set_alarm		= pcf50633_rtc_set_alarm,
	.alarm_irq_enable	= pcf50633_rtc_alarm_irq_enable,
};

static void pcf50633_rtc_irq(int irq, void *data)
{
	struct pcf50633_rtc *rtc = data;

	rtc_update_irq(rtc->rtc_dev, 1, RTC_AF | RTC_IRQF);
	rtc->alarm_pending = 1;
}

static int __devinit pcf50633_rtc_probe(struct platform_device *pdev)
{
	struct pcf50633_rtc *rtc;

	rtc = kzalloc(sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->pcf = dev_to_pcf50633(pdev->dev.parent);
	platform_set_drvdata(pdev, rtc);
	rtc->rtc_dev = rtc_device_register("pcf50633-rtc", &pdev->dev,
				&pcf50633_rtc_ops, THIS_MODULE);

	if (IS_ERR(rtc->rtc_dev)) {
		int ret =  PTR_ERR(rtc->rtc_dev);
		kfree(rtc);
		return ret;
	}

	pcf50633_register_irq(rtc->pcf, PCF50633_IRQ_ALARM,
					pcf50633_rtc_irq, rtc);
	return 0;
}

static int __devexit pcf50633_rtc_remove(struct platform_device *pdev)
{
	struct pcf50633_rtc *rtc;

	rtc = platform_get_drvdata(pdev);

	pcf50633_free_irq(rtc->pcf, PCF50633_IRQ_ALARM);

	rtc_device_unregister(rtc->rtc_dev);
	kfree(rtc);

	return 0;
}

static struct platform_driver pcf50633_rtc_driver = {
	.driver = {
		.name = "pcf50633-rtc",
	},
	.probe = pcf50633_rtc_probe,
	.remove = __devexit_p(pcf50633_rtc_remove),
};

module_platform_driver(pcf50633_rtc_driver);

MODULE_DESCRIPTION("PCF50633 RTC driver");
MODULE_AUTHOR("Balaji Rao <balajirrao@openmoko.org>");
MODULE_LICENSE("GPL");


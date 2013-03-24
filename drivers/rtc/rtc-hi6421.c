/*
 * Hisilicon Hi6421 RTC driver
 *
 * Copyright (C) 2013 Hisilicon Ltd.
 * Copyright (C) 2013 Linaro Ltd.
 *
 * Author: Haojian Zhuang <haojian.zhuang@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/rtc.h>
#include <linux/mfd/hi6421-pmic.h>

#define	REG_IRQ1			0x01
#define	REG_IRQM1			0x04
#define	REG_RTCDR0			0x58
#define	REG_RTCDR1			0x59
#define	REG_RTCDR2			0x5a
#define	REG_RTCDR3			0x5b
#define	REG_RTCMR0			0x5c
#define	REG_RTCMR1			0x5d
#define	REG_RTCMR2			0x5e
#define	REG_RTCMR3			0x5f
#define	REG_RTCLR0			0x60
#define	REG_RTCLR1			0x61
#define	REG_RTCLR2			0x62
#define	REG_RTCLR3			0x63
#define	REG_RTCCTRL			0x64
#define REG_SOFT_RST			0x86

#define ALARM_ON			(1 << HI6421_IRQ_ALARM)

struct hi6421_rtc_info {
	struct rtc_device	*rtc;
	struct hi6421_pmic	*pmic;
	int			irq;
};

static irqreturn_t hi6421_rtc_handler(int irq, void *data)
{
	struct hi6421_rtc_info *info = (struct hi6421_rtc_info *)data;

	/* clear alarm status */
	hi6421_pmic_rmw(info->pmic, REG_IRQ1, ALARM_ON, ALARM_ON);
	rtc_update_irq(info->rtc, 1, RTC_AF);
	return IRQ_HANDLED;
}

/* read 4 8-bit registers & covert it into a 32-bit data */
static unsigned int hi6421_read_bulk(struct hi6421_pmic *pmic,
				     unsigned int addr)
{
	unsigned int data, sum = 0;
	int i;

	for (i = 0; i < 4; i++) {
		data = hi6421_pmic_read(pmic, addr + i);
		sum |= (data & 0xff) << (i * 8);
	}
	return sum;
}

/* write a 32-bit data into 4 8-bit registers */
static void hi6421_write_bulk(struct hi6421_pmic *pmic, unsigned int addr,
			      unsigned int data)
{
	unsigned int value;
	int i;

	for (i = 0; i < 4; i++) {
		value = (data >> (i * 8)) & 0xff;
		hi6421_pmic_write(pmic, addr + i, value);
	}
}

static int hi6421_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct hi6421_rtc_info *info = dev_get_drvdata(dev);
	unsigned long ticks;

	ticks = hi6421_read_bulk(info->pmic, REG_RTCDR0);
	rtc_time_to_tm(ticks, tm);
	return 0;
}

static int hi6421_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct hi6421_rtc_info *info = dev_get_drvdata(dev);
	unsigned long ticks;

	if ((tm->tm_year < 70) || (tm->tm_year > 138)) {
		dev_dbg(dev, "Set time %d out of range. "
			"Please set time between 1970 to 2038.\n",
			1900 + tm->tm_year);
		return -EINVAL;
	}
	rtc_tm_to_time(tm, &ticks);
	hi6421_write_bulk(info->pmic, REG_RTCLR0, ticks);
	return 0;
}

static int hi6421_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct hi6421_rtc_info *info = dev_get_drvdata(dev);
	unsigned long ticks, data;

	ticks = hi6421_read_bulk(info->pmic, REG_RTCMR0);
	rtc_time_to_tm(ticks, &alrm->time);

	data = hi6421_pmic_read(info->pmic, REG_IRQ1);
	alrm->pending = (data & ALARM_ON) ? 1 : 0;

	data = hi6421_pmic_read(info->pmic, REG_IRQM1);
	alrm->enabled = (data & ALARM_ON) ? 0 : 1;
	return 0;
}

/*
 * Calculate the next alarm time given the requested alarm time mask
 * and the current time.
 */
static void rtc_next_alarm_time(struct rtc_time *next, struct rtc_time *now,
				struct rtc_time *alrm)
{
	unsigned long next_time;
	unsigned long now_time;

	next->tm_year = now->tm_year;
	next->tm_mon = now->tm_mon;
	next->tm_mday = now->tm_mday;
	next->tm_hour = alrm->tm_hour;
	next->tm_min = alrm->tm_min;
	next->tm_sec = alrm->tm_sec;

	rtc_tm_to_time(now, &now_time);
	rtc_tm_to_time(next, &next_time);

	if (next_time < now_time) {
		/* Advance one day */
		next_time += 60 * 60 * 24;
		rtc_time_to_tm(next_time, next);
	}
}

static int hi6421_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct hi6421_rtc_info *info = dev_get_drvdata(dev);
	struct rtc_time now_tm, alarm_tm;
	unsigned long ticks;

	/* load 32-bit read-only counter */
	ticks = hi6421_read_bulk(info->pmic, REG_RTCDR0);
	rtc_time_to_tm(ticks, &now_tm);
	dev_dbg(dev, "%s, now time : %lu\n", __func__, ticks);
	rtc_next_alarm_time(&alarm_tm, &now_tm, &alrm->time);

	/* get new ticks for alarm in 24 hours */
	rtc_tm_to_time(&alarm_tm, &ticks);
	dev_dbg(dev, "%s, alarm time: %lu\n", __func__, ticks);
	if (alrm->enabled)
		hi6421_write_bulk(info->pmic, REG_RTCMR0, ticks);
	return 0;
}

static int hi6421_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct hi6421_rtc_info *info = dev_get_drvdata(dev);
	unsigned int data = 0;

	if (!enabled)
		data = ALARM_ON;
	hi6421_pmic_rmw(info->pmic, REG_IRQM1, ALARM_ON, data);
	return 0;
}

static const struct rtc_class_ops hi6421_rtc_ops = {
	.read_time	= hi6421_rtc_read_time,
	.set_time	= hi6421_rtc_set_time,
	.read_alarm	= hi6421_rtc_read_alarm,
	.set_alarm	= hi6421_rtc_set_alarm,
	.alarm_irq_enable = hi6421_rtc_alarm_irq_enable,
};

static int hi6421_rtc_probe(struct platform_device *pdev)
{
	struct hi6421_rtc_info *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}
	info->irq = platform_get_irq(pdev, 0);
	if (info->irq < 0)
		return -ENOENT;

	info->pmic = dev_get_drvdata(pdev->dev.parent);
	platform_set_drvdata(pdev, info);

	/* enable RTC device */
	hi6421_pmic_write(info->pmic, REG_RTCCTRL, 1);

	info->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
					&hi6421_rtc_ops, THIS_MODULE);
	if (IS_ERR(info->rtc))
		return PTR_ERR(info->rtc);

	ret = devm_request_irq(&pdev->dev, info->irq, hi6421_rtc_handler,
			       IRQF_DISABLED, "alarm", info);
	if (ret < 0)
		return ret;

	return 0;
}

static int hi6421_rtc_remove(struct platform_device *pdev)
{
	return 0;
}

static struct of_device_id hi6421_rtc_of_match[] = {
	{ .compatible = "hisilicon,hi6421-rtc" },
	{ }
};
MODULE_DEVICE_TABLE(of, hi6421_rtc_of_match);

static struct platform_driver hi6421_rtc_driver = {
	.driver = {
		.name = "hi6421-rtc",
		.of_match_table = of_match_ptr(hi6421_rtc_of_match),
	},
	.probe = hi6421_rtc_probe,
	.remove = hi6421_rtc_remove,
};
module_platform_driver(hi6421_rtc_driver);

MODULE_LICENSE("GPLv2");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@linaro.org");
MODULE_ALIAS("platform:hi6421-rtc");

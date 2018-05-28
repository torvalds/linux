// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/time.h>
#include <linux/of_platform.h>

static struct timespec begtime;
static unsigned long time;

static unsigned long timespec_to_ulong(struct timespec *ts)
{
	return ts->tv_nsec < NSEC_PER_SEC / 2 ? ts->tv_sec : ts->tv_sec + 1;
}

static void get_uptime(struct timespec *ts)
{
	getrawmonotonic(ts);
}

static int fake_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct timespec now, diff;

	get_uptime(&now);
	diff = timespec_sub(now, begtime);

	rtc_time_to_tm(time + timespec_to_ulong(&diff), tm);

	return rtc_valid_tm(tm);
}

static int fake_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	get_uptime(&begtime);
	rtc_tm_to_time(tm, &time);

	return 0;
}

static int fake_rtc_alarm_irq_enable(struct device *dev,
					unsigned int enabled)
{
	return 0;
}

static int fake_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	return 0;
}

static int fake_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	return 0;
}

static const struct rtc_class_ops fake_rtc_ops = {
	.read_time	= fake_rtc_read_time,
	.set_time	= fake_rtc_set_time,
	.alarm_irq_enable	= fake_rtc_alarm_irq_enable,
	.read_alarm		= fake_rtc_read_alarm,
	.set_alarm		= fake_rtc_set_alarm,
};

static int fake_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *fake_rtc;
	struct rtc_time tm = {
		.tm_wday = 1,
		.tm_year = 118,
		.tm_mon = 0,
		.tm_mday = 1,
		.tm_hour = 12,
		.tm_min = 0,
		.tm_sec = 0,
	};

	get_uptime(&begtime);
	fake_rtc_set_time(&pdev->dev, &tm);

	device_init_wakeup(&pdev->dev, 1);

	fake_rtc = devm_rtc_device_register(&pdev->dev,
				pdev->name, &fake_rtc_ops, THIS_MODULE);
	if (IS_ERR(fake_rtc))
		return PTR_ERR(fake_rtc);

	dev_info(&pdev->dev, "loaded; begtime is %lu, time is %lu\n",
		 timespec_to_ulong(&begtime), time);

	return 0;
}

static const struct of_device_id rtc_dt_ids[] = {
	{ .compatible = "rtc-fake" },
	{},
};

struct platform_driver fake_rtc_driver = {
	.driver		= {
		.name	= "rtc-fake",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(rtc_dt_ids),
	},
	.probe		= fake_rtc_probe,
};

module_platform_driver(fake_rtc_driver);

MODULE_AUTHOR("jesse.huang@rock-chips.com");
MODULE_DESCRIPTION("FAKE RTC driver");
MODULE_LICENSE("GPL");


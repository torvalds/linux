// SPDX-License-Identifier: GPL-2.0
/*
 * RTC interface for Wilco Embedded Controller with R/W abilities
 *
 * Copyright 2018 Google LLC
 *
 * The corresponding platform device is typically registered in
 * drivers/platform/chrome/wilco_ec/core.c
 */

#include <linux/bcd.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_data/wilco-ec.h>
#include <linux/rtc.h>
#include <linux/timekeeping.h>

#define EC_COMMAND_CMOS			0x7c
#define EC_CMOS_TOD_WRITE		0x02
#define EC_CMOS_TOD_READ		0x08

/**
 * struct ec_rtc_read - Format of RTC returned by EC.
 * @second: Second value (0..59)
 * @minute: Minute value (0..59)
 * @hour: Hour value (0..23)
 * @day: Day value (1..31)
 * @month: Month value (1..12)
 * @year: Year value (full year % 100)
 * @century: Century value (full year / 100)
 *
 * All values are presented in binary (not BCD).
 */
struct ec_rtc_read {
	u8 second;
	u8 minute;
	u8 hour;
	u8 day;
	u8 month;
	u8 year;
	u8 century;
} __packed;

/**
 * struct ec_rtc_write - Format of RTC sent to the EC.
 * @param: EC_CMOS_TOD_WRITE
 * @century: Century value (full year / 100)
 * @year: Year value (full year % 100)
 * @month: Month value (1..12)
 * @day: Day value (1..31)
 * @hour: Hour value (0..23)
 * @minute: Minute value (0..59)
 * @second: Second value (0..59)
 * @weekday: Day of the week (0=Saturday)
 *
 * All values are presented in BCD.
 */
struct ec_rtc_write {
	u8 param;
	u8 century;
	u8 year;
	u8 month;
	u8 day;
	u8 hour;
	u8 minute;
	u8 second;
	u8 weekday;
} __packed;

static int wilco_ec_rtc_read(struct device *dev, struct rtc_time *tm)
{
	struct wilco_ec_device *ec = dev_get_drvdata(dev->parent);
	u8 param = EC_CMOS_TOD_READ;
	struct ec_rtc_read rtc;
	struct wilco_ec_message msg = {
		.type = WILCO_EC_MSG_LEGACY,
		.flags = WILCO_EC_FLAG_RAW_RESPONSE,
		.command = EC_COMMAND_CMOS,
		.request_data = &param,
		.request_size = sizeof(param),
		.response_data = &rtc,
		.response_size = sizeof(rtc),
	};
	int ret;

	ret = wilco_ec_mailbox(ec, &msg);
	if (ret < 0)
		return ret;

	tm->tm_sec	= rtc.second;
	tm->tm_min	= rtc.minute;
	tm->tm_hour	= rtc.hour;
	tm->tm_mday	= rtc.day;
	tm->tm_mon	= rtc.month - 1;
	tm->tm_year	= rtc.year + (rtc.century * 100) - 1900;
	tm->tm_yday	= rtc_year_days(tm->tm_mday, tm->tm_mon, tm->tm_year);

	/* Don't compute day of week, we don't need it. */
	tm->tm_wday = -1;

	return 0;
}

static int wilco_ec_rtc_write(struct device *dev, struct rtc_time *tm)
{
	struct wilco_ec_device *ec = dev_get_drvdata(dev->parent);
	struct ec_rtc_write rtc;
	struct wilco_ec_message msg = {
		.type = WILCO_EC_MSG_LEGACY,
		.flags = WILCO_EC_FLAG_RAW_RESPONSE,
		.command = EC_COMMAND_CMOS,
		.request_data = &rtc,
		.request_size = sizeof(rtc),
	};
	int year = tm->tm_year + 1900;
	/*
	 * Convert from 0=Sunday to 0=Saturday for the EC
	 * We DO need to set weekday because the EC controls battery charging
	 * schedules that depend on the day of the week.
	 */
	int wday = tm->tm_wday == 6 ? 0 : tm->tm_wday + 1;
	int ret;

	rtc.param	= EC_CMOS_TOD_WRITE;
	rtc.century	= bin2bcd(year / 100);
	rtc.year	= bin2bcd(year % 100);
	rtc.month	= bin2bcd(tm->tm_mon + 1);
	rtc.day		= bin2bcd(tm->tm_mday);
	rtc.hour	= bin2bcd(tm->tm_hour);
	rtc.minute	= bin2bcd(tm->tm_min);
	rtc.second	= bin2bcd(tm->tm_sec);
	rtc.weekday	= bin2bcd(wday);

	ret = wilco_ec_mailbox(ec, &msg);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct rtc_class_ops wilco_ec_rtc_ops = {
	.read_time = wilco_ec_rtc_read,
	.set_time = wilco_ec_rtc_write,
};

static int wilco_ec_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;

	rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	rtc->ops = &wilco_ec_rtc_ops;
	/* EC only supports this century */
	rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rtc->range_max = RTC_TIMESTAMP_END_2099;
	rtc->owner = THIS_MODULE;

	return rtc_register_device(rtc);
}

static struct platform_driver wilco_ec_rtc_driver = {
	.driver = {
		.name = "rtc-wilco-ec",
	},
	.probe = wilco_ec_rtc_probe,
};

module_platform_driver(wilco_ec_rtc_driver);

MODULE_ALIAS("platform:rtc-wilco-ec");
MODULE_AUTHOR("Nick Crews <ncrews@chromium.org>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Wilco EC RTC driver");

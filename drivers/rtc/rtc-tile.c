/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Tilera-specific RTC driver.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>

/* Platform device pointer. */
static struct platform_device *tile_rtc_platform_device;

/*
 * RTC read routine.  Gets time info from RTC chip via hypervisor syscall.
 */
static int read_rtc_time(struct device *dev, struct rtc_time *tm)
{
	HV_RTCTime hvtm = hv_get_rtc();

	tm->tm_sec = hvtm.tm_sec;
	tm->tm_min = hvtm.tm_min;
	tm->tm_hour = hvtm.tm_hour;
	tm->tm_mday = hvtm.tm_mday;
	tm->tm_mon = hvtm.tm_mon;
	tm->tm_year = hvtm.tm_year;
	tm->tm_wday = 0;
	tm->tm_yday = 0;
	tm->tm_isdst = 0;

	if (rtc_valid_tm(tm) < 0)
		dev_warn(dev, "Read invalid date/time from RTC\n");

	return 0;
}

/*
 * RTC write routine.  Sends time info to hypervisor via syscall, to be
 * written to RTC chip.
 */
static int set_rtc_time(struct device *dev, struct rtc_time *tm)
{
	HV_RTCTime hvtm;

	hvtm.tm_sec = tm->tm_sec;
	hvtm.tm_min = tm->tm_min;
	hvtm.tm_hour = tm->tm_hour;
	hvtm.tm_mday = tm->tm_mday;
	hvtm.tm_mon = tm->tm_mon;
	hvtm.tm_year = tm->tm_year;

	hv_set_rtc(hvtm);

	return 0;
}

/*
 * RTC read/write ops.
 */
static const struct rtc_class_ops tile_rtc_ops = {
	.read_time	= read_rtc_time,
	.set_time	= set_rtc_time,
};

/*
 * Device probe routine.
 */
static int tile_rtc_probe(struct platform_device *dev)
{
	struct rtc_device *rtc;

	rtc = devm_rtc_device_register(&dev->dev, "tile",
				&tile_rtc_ops, THIS_MODULE);

	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	platform_set_drvdata(dev, rtc);

	return 0;
}

static struct platform_driver tile_rtc_platform_driver = {
	.driver		= {
		.name	= "rtc-tile",
	},
	.probe		= tile_rtc_probe,
};

/*
 * Driver init routine.
 */
static int __init tile_rtc_driver_init(void)
{
	int err;

	err = platform_driver_register(&tile_rtc_platform_driver);
	if (err)
		return err;

	tile_rtc_platform_device = platform_device_alloc("rtc-tile", 0);
	if (tile_rtc_platform_device == NULL) {
		err = -ENOMEM;
		goto exit_driver_unregister;
	}

	err = platform_device_add(tile_rtc_platform_device);
	if (err)
		goto exit_device_put;

	return 0;

exit_device_put:
	platform_device_put(tile_rtc_platform_device);

exit_driver_unregister:
	platform_driver_unregister(&tile_rtc_platform_driver);
	return err;
}

/*
 * Driver cleanup routine.
 */
static void __exit tile_rtc_driver_exit(void)
{
	platform_device_unregister(tile_rtc_platform_device);
	platform_driver_unregister(&tile_rtc_platform_driver);
}

module_init(tile_rtc_driver_init);
module_exit(tile_rtc_driver_exit);

MODULE_DESCRIPTION("Tilera-specific Real Time Clock Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtc-tile");

// SPDX-License-Identifier: GPL-2.0
/*
 * PS3 RTC Driver
 *
 * Copyright 2009 Sony Corporation
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#include <asm/lv1call.h>
#include <asm/ps3.h>


static u64 read_rtc(void)
{
	int result;
	u64 rtc_val;
	u64 tb_val;

	result = lv1_get_rtc(&rtc_val, &tb_val);
	BUG_ON(result);

	return rtc_val;
}

static int ps3_get_time(struct device *dev, struct rtc_time *tm)
{
	rtc_time64_to_tm(read_rtc() + ps3_os_area_get_rtc_diff(), tm);
	return 0;
}

static int ps3_set_time(struct device *dev, struct rtc_time *tm)
{
	ps3_os_area_set_rtc_diff(rtc_tm_to_time64(tm) - read_rtc());
	return 0;
}

static const struct rtc_class_ops ps3_rtc_ops = {
	.read_time = ps3_get_time,
	.set_time = ps3_set_time,
};

static int __init ps3_rtc_probe(struct platform_device *dev)
{
	struct rtc_device *rtc;

	rtc = devm_rtc_allocate_device(&dev->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	rtc->ops = &ps3_rtc_ops;

	platform_set_drvdata(dev, rtc);

	return rtc_register_device(rtc);
}

static struct platform_driver ps3_rtc_driver = {
	.driver = {
		.name = "rtc-ps3",
	},
};

module_platform_driver_probe(ps3_rtc_driver, ps3_rtc_probe);

MODULE_AUTHOR("Sony Corporation");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ps3 RTC driver");
MODULE_ALIAS("platform:rtc-ps3");

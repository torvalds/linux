/*
 *  rtc-nintendo3ds.c
 *
 *  Copyright (C) 2016 Sergi Granell
 *  based on rtc-em3207.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/mfd/nintendo3ds-mcu.h>

struct nintendo3ds_rtc {
	struct nintendo3ds_mcu_dev *mcu;
	struct rtc_device *rtc_dev;
};

static struct platform_driver nintendo3ds_rtc_driver;

static int nintendo3ds_rtc_get_time(struct device *dev, struct rtc_time *tm)
{
	struct nintendo3ds_rtc *n3ds_rtc = dev_get_drvdata(dev);
	struct nintendo3ds_mcu_dev *mcu = n3ds_rtc->mcu;
	u8 buf[7];

	mcu->read_device(mcu, NINTENDO3DS_MCU_REG_RTC, sizeof(buf), buf);

	tm->tm_sec	= bcd2bin(buf[0]);
	tm->tm_min	= (bcd2bin(buf[1]) + 30) % 60;
	tm->tm_hour	= (bcd2bin(buf[2]) + 5) % 24;
	tm->tm_mday	= (bcd2bin(buf[4]) - 7) % 31;;
	tm->tm_mon	= (bcd2bin(buf[5]) + 1) % 12;
	tm->tm_year	= bcd2bin(buf[6]) + 110;

	return 0;
}

static const struct rtc_class_ops nintendo3ds_rtc_ops = {
	.read_time	= nintendo3ds_rtc_get_time
};

static int nintendo3ds_rtc_probe(struct platform_device *pdev)
{
	struct nintendo3ds_rtc *n3ds_rtc;
	struct nintendo3ds_mcu_dev *mcu_dev = dev_get_drvdata(pdev->dev.parent);

	n3ds_rtc = devm_kzalloc(&pdev->dev, sizeof(struct nintendo3ds_rtc),
				GFP_KERNEL);

	platform_set_drvdata(pdev, n3ds_rtc);

	n3ds_rtc->mcu = mcu_dev;
	n3ds_rtc->rtc_dev = devm_rtc_device_register(&pdev->dev,
		nintendo3ds_rtc_driver.driver.name,
		&nintendo3ds_rtc_ops, THIS_MODULE);
	if (IS_ERR(n3ds_rtc->rtc_dev))
		return PTR_ERR(n3ds_rtc->rtc_dev);

	return 0;
}

static struct platform_driver nintendo3ds_rtc_driver = {
	.driver = {
		.name = "nintendo3ds-rtc",
	},
	.probe = nintendo3ds_rtc_probe,
};
module_platform_driver(nintendo3ds_rtc_driver);

MODULE_DESCRIPTION("Nintendo 3DS I2C bus driver");
MODULE_AUTHOR("Sergi Granell, <xerpi.g.12@gmail.com>");
MODULE_LICENSE("GPL");

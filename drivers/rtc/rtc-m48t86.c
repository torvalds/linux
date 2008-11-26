/*
 * ST M48T86 / Dallas DS12887 RTC driver
 * Copyright (c) 2006 Tower Technologies
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This drivers only supports the clock running in BCD and 24H mode.
 * If it will be ever adapted to binary and 12H mode, care must be taken
 * to not introduce bugs.
 */

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <linux/m48t86.h>
#include <linux/bcd.h>

#define M48T86_REG_SEC		0x00
#define M48T86_REG_SECALRM	0x01
#define M48T86_REG_MIN		0x02
#define M48T86_REG_MINALRM	0x03
#define M48T86_REG_HOUR		0x04
#define M48T86_REG_HOURALRM	0x05
#define M48T86_REG_DOW		0x06 /* 1 = sunday */
#define M48T86_REG_DOM		0x07
#define M48T86_REG_MONTH	0x08 /* 1 - 12 */
#define M48T86_REG_YEAR		0x09 /* 0 - 99 */
#define M48T86_REG_A		0x0A
#define M48T86_REG_B		0x0B
#define M48T86_REG_C		0x0C
#define M48T86_REG_D		0x0D

#define M48T86_REG_B_H24	(1 << 1)
#define M48T86_REG_B_DM		(1 << 2)
#define M48T86_REG_B_SET	(1 << 7)
#define M48T86_REG_D_VRT	(1 << 7)

#define DRV_VERSION "0.1"


static int m48t86_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char reg;
	struct platform_device *pdev = to_platform_device(dev);
	struct m48t86_ops *ops = pdev->dev.platform_data;

	reg = ops->readbyte(M48T86_REG_B);

	if (reg & M48T86_REG_B_DM) {
		/* data (binary) mode */
		tm->tm_sec	= ops->readbyte(M48T86_REG_SEC);
		tm->tm_min	= ops->readbyte(M48T86_REG_MIN);
		tm->tm_hour	= ops->readbyte(M48T86_REG_HOUR) & 0x3F;
		tm->tm_mday	= ops->readbyte(M48T86_REG_DOM);
		/* tm_mon is 0-11 */
		tm->tm_mon	= ops->readbyte(M48T86_REG_MONTH) - 1;
		tm->tm_year	= ops->readbyte(M48T86_REG_YEAR) + 100;
		tm->tm_wday	= ops->readbyte(M48T86_REG_DOW);
	} else {
		/* bcd mode */
		tm->tm_sec	= bcd2bin(ops->readbyte(M48T86_REG_SEC));
		tm->tm_min	= bcd2bin(ops->readbyte(M48T86_REG_MIN));
		tm->tm_hour	= bcd2bin(ops->readbyte(M48T86_REG_HOUR) & 0x3F);
		tm->tm_mday	= bcd2bin(ops->readbyte(M48T86_REG_DOM));
		/* tm_mon is 0-11 */
		tm->tm_mon	= bcd2bin(ops->readbyte(M48T86_REG_MONTH)) - 1;
		tm->tm_year	= bcd2bin(ops->readbyte(M48T86_REG_YEAR)) + 100;
		tm->tm_wday	= bcd2bin(ops->readbyte(M48T86_REG_DOW));
	}

	/* correct the hour if the clock is in 12h mode */
	if (!(reg & M48T86_REG_B_H24))
		if (ops->readbyte(M48T86_REG_HOUR) & 0x80)
			tm->tm_hour += 12;

	return 0;
}

static int m48t86_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char reg;
	struct platform_device *pdev = to_platform_device(dev);
	struct m48t86_ops *ops = pdev->dev.platform_data;

	reg = ops->readbyte(M48T86_REG_B);

	/* update flag and 24h mode */
	reg |= M48T86_REG_B_SET | M48T86_REG_B_H24;
	ops->writebyte(reg, M48T86_REG_B);

	if (reg & M48T86_REG_B_DM) {
		/* data (binary) mode */
		ops->writebyte(tm->tm_sec, M48T86_REG_SEC);
		ops->writebyte(tm->tm_min, M48T86_REG_MIN);
		ops->writebyte(tm->tm_hour, M48T86_REG_HOUR);
		ops->writebyte(tm->tm_mday, M48T86_REG_DOM);
		ops->writebyte(tm->tm_mon + 1, M48T86_REG_MONTH);
		ops->writebyte(tm->tm_year % 100, M48T86_REG_YEAR);
		ops->writebyte(tm->tm_wday, M48T86_REG_DOW);
	} else {
		/* bcd mode */
		ops->writebyte(bin2bcd(tm->tm_sec), M48T86_REG_SEC);
		ops->writebyte(bin2bcd(tm->tm_min), M48T86_REG_MIN);
		ops->writebyte(bin2bcd(tm->tm_hour), M48T86_REG_HOUR);
		ops->writebyte(bin2bcd(tm->tm_mday), M48T86_REG_DOM);
		ops->writebyte(bin2bcd(tm->tm_mon + 1), M48T86_REG_MONTH);
		ops->writebyte(bin2bcd(tm->tm_year % 100), M48T86_REG_YEAR);
		ops->writebyte(bin2bcd(tm->tm_wday), M48T86_REG_DOW);
	}

	/* update ended */
	reg &= ~M48T86_REG_B_SET;
	ops->writebyte(reg, M48T86_REG_B);

	return 0;
}

static int m48t86_rtc_proc(struct device *dev, struct seq_file *seq)
{
	unsigned char reg;
	struct platform_device *pdev = to_platform_device(dev);
	struct m48t86_ops *ops = pdev->dev.platform_data;

	reg = ops->readbyte(M48T86_REG_B);

	seq_printf(seq, "mode\t\t: %s\n",
		 (reg & M48T86_REG_B_DM) ? "binary" : "bcd");

	reg = ops->readbyte(M48T86_REG_D);

	seq_printf(seq, "battery\t\t: %s\n",
		 (reg & M48T86_REG_D_VRT) ? "ok" : "exhausted");

	return 0;
}

static const struct rtc_class_ops m48t86_rtc_ops = {
	.read_time	= m48t86_rtc_read_time,
	.set_time	= m48t86_rtc_set_time,
	.proc		= m48t86_rtc_proc,
};

static int __devinit m48t86_rtc_probe(struct platform_device *dev)
{
	unsigned char reg;
	struct m48t86_ops *ops = dev->dev.platform_data;
	struct rtc_device *rtc = rtc_device_register("m48t86",
				&dev->dev, &m48t86_rtc_ops, THIS_MODULE);

	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	platform_set_drvdata(dev, rtc);

	/* read battery status */
	reg = ops->readbyte(M48T86_REG_D);
	dev_info(&dev->dev, "battery %s\n",
		(reg & M48T86_REG_D_VRT) ? "ok" : "exhausted");

	return 0;
}

static int __devexit m48t86_rtc_remove(struct platform_device *dev)
{
	struct rtc_device *rtc = platform_get_drvdata(dev);

 	if (rtc)
		rtc_device_unregister(rtc);

	platform_set_drvdata(dev, NULL);

	return 0;
}

static struct platform_driver m48t86_rtc_platform_driver = {
	.driver		= {
		.name	= "rtc-m48t86",
		.owner	= THIS_MODULE,
	},
	.probe		= m48t86_rtc_probe,
	.remove		= __devexit_p(m48t86_rtc_remove),
};

static int __init m48t86_rtc_init(void)
{
	return platform_driver_register(&m48t86_rtc_platform_driver);
}

static void __exit m48t86_rtc_exit(void)
{
	platform_driver_unregister(&m48t86_rtc_platform_driver);
}

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("M48T86 RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_ALIAS("platform:rtc-m48t86");

module_init(m48t86_rtc_init);
module_exit(m48t86_rtc_exit);

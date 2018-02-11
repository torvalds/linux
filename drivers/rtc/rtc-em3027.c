/*
 * An rtc/i2c driver for the EM Microelectronic EM3027
 * Copyright 2011 CompuLab, Ltd.
 *
 * Author: Mike Rapoport <mike@compulab.co.il>
 *
 * Based on rtc-ds1672.c by Alessandro Zummo <a.zummo@towertech.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/module.h>
#include <linux/of.h>

/* Registers */
#define EM3027_REG_ON_OFF_CTRL	0x00
#define EM3027_REG_IRQ_CTRL	0x01
#define EM3027_REG_IRQ_FLAGS	0x02
#define EM3027_REG_STATUS	0x03
#define EM3027_REG_RST_CTRL	0x04

#define EM3027_REG_WATCH_SEC	0x08
#define EM3027_REG_WATCH_MIN	0x09
#define EM3027_REG_WATCH_HOUR	0x0a
#define EM3027_REG_WATCH_DATE	0x0b
#define EM3027_REG_WATCH_DAY	0x0c
#define EM3027_REG_WATCH_MON	0x0d
#define EM3027_REG_WATCH_YEAR	0x0e

#define EM3027_REG_ALARM_SEC	0x10
#define EM3027_REG_ALARM_MIN	0x11
#define EM3027_REG_ALARM_HOUR	0x12
#define EM3027_REG_ALARM_DATE	0x13
#define EM3027_REG_ALARM_DAY	0x14
#define EM3027_REG_ALARM_MON	0x15
#define EM3027_REG_ALARM_YEAR	0x16

static struct i2c_driver em3027_driver;

static int em3027_get_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);

	unsigned char addr = EM3027_REG_WATCH_SEC;
	unsigned char buf[7];

	struct i2c_msg msgs[] = {
		{/* setup read addr */
			.addr = client->addr,
			.len = 1,
			.buf = &addr
		},
		{/* read time/date */
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 7,
			.buf = buf
		},
	};

	/* read time/date registers */
	if ((i2c_transfer(client->adapter, &msgs[0], 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __func__);
		return -EIO;
	}

	tm->tm_sec	= bcd2bin(buf[0]);
	tm->tm_min	= bcd2bin(buf[1]);
	tm->tm_hour	= bcd2bin(buf[2]);
	tm->tm_mday	= bcd2bin(buf[3]);
	tm->tm_wday	= bcd2bin(buf[4]);
	tm->tm_mon	= bcd2bin(buf[5]);
	tm->tm_year	= bcd2bin(buf[6]) + 100;

	return 0;
}

static int em3027_set_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char buf[8];

	struct i2c_msg msg = {
		.addr = client->addr,
		.len = 8,
		.buf = buf,	/* write time/date */
	};

	buf[0] = EM3027_REG_WATCH_SEC;
	buf[1] = bin2bcd(tm->tm_sec);
	buf[2] = bin2bcd(tm->tm_min);
	buf[3] = bin2bcd(tm->tm_hour);
	buf[4] = bin2bcd(tm->tm_mday);
	buf[5] = bin2bcd(tm->tm_wday);
	buf[6] = bin2bcd(tm->tm_mon);
	buf[7] = bin2bcd(tm->tm_year % 100);

	/* write time/date registers */
	if ((i2c_transfer(client->adapter, &msg, 1)) != 1) {
		dev_err(&client->dev, "%s: write error\n", __func__);
		return -EIO;
	}

	return 0;
}

static const struct rtc_class_ops em3027_rtc_ops = {
	.read_time = em3027_get_time,
	.set_time = em3027_set_time,
};

static int em3027_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct rtc_device *rtc;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	rtc = devm_rtc_device_register(&client->dev, em3027_driver.driver.name,
				  &em3027_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	i2c_set_clientdata(client, rtc);

	return 0;
}

static const struct i2c_device_id em3027_id[] = {
	{ "em3027", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, em3027_id);

#ifdef CONFIG_OF
static const struct of_device_id em3027_of_match[] = {
	{ .compatible = "emmicro,em3027", },
	{}
};
MODULE_DEVICE_TABLE(of, em3027_of_match);
#endif

static struct i2c_driver em3027_driver = {
	.driver = {
		   .name = "rtc-em3027",
		   .of_match_table = of_match_ptr(em3027_of_match),
	},
	.probe = &em3027_probe,
	.id_table = em3027_id,
};

module_i2c_driver(em3027_driver);

MODULE_AUTHOR("Mike Rapoport <mike@compulab.co.il>");
MODULE_DESCRIPTION("EM Microelectronic EM3027 RTC driver");
MODULE_LICENSE("GPL");

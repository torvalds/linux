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

#include <linux/i2c.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/module.h>
#include <linux/of.h>

#define NINTENDO3DS_RTC_REGISTER 0x30

static struct i2c_driver nintendo3ds_rtc_driver;

static int nintendo3ds_rtc_get_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	u8 buf[8];
	u8 reg = NINTENDO3DS_RTC_REGISTER;

	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.len = sizeof(reg),
			.buf = &reg
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = sizeof(buf),
			.buf = buf
		},
	};

	if ((i2c_transfer(client->adapter, &msgs[0], 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __func__);
		return -EIO;
	}

	tm->tm_sec	= bcd2bin(buf[0]);
	tm->tm_min	= (bcd2bin(buf[1]) + 30) % 60;
	tm->tm_hour	= (bcd2bin(buf[2]) + 5) % 24;
	tm->tm_mday	= 1; /* Hardcoded for now :( */
	tm->tm_wday	= bcd2bin(buf[5]);
	tm->tm_mon	= 0; /* Hardcoded for now :( */
	tm->tm_year	= bcd2bin(buf[6]) + 110;

	return 0;
}

static const struct rtc_class_ops nintendo3ds_rtc_ops = {
	.read_time	= nintendo3ds_rtc_get_time
};

static int nintendo3ds_rtc_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct rtc_device *rtc;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	rtc = devm_rtc_device_register(&client->dev,
		nintendo3ds_rtc_driver.driver.name,
		&nintendo3ds_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	i2c_set_clientdata(client, rtc);

	return 0;
}

static struct i2c_device_id nintendo3ds_rtc_id[] = {
	{ "nintendo3ds-rtc", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nintendo3ds_rtc_id);

#ifdef CONFIG_OF
static const struct of_device_id nintendo3ds_rtc_of_match[] = {
	{ .compatible = "nintendo3ds,nintendo3ds-rtc", },
	{}
};
MODULE_DEVICE_TABLE(of, nintendo3ds_rtc_of_match);
#endif

static struct i2c_driver nintendo3ds_rtc_driver = {
	.driver = {
		.name = "rtc-nintendo3ds",
		.of_match_table = of_match_ptr(nintendo3ds_rtc_of_match),
	},
	.probe		= nintendo3ds_rtc_probe,
	.id_table	= nintendo3ds_rtc_id,
};

module_i2c_driver(nintendo3ds_rtc_driver);

MODULE_DESCRIPTION("Nintendo 3DS I2C bus driver");
MODULE_AUTHOR("Sergi Granell, <xerpi.g.12@gmail.com>");
MODULE_LICENSE("GPL");

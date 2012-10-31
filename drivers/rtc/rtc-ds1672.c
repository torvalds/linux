/*
 * An rtc/i2c driver for the Dallas DS1672
 * Copyright 2005-06 Tower Technologies
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/rtc.h>
#include <linux/module.h>

#define DRV_VERSION "0.4"

/* Registers */

#define DS1672_REG_CNT_BASE	0
#define DS1672_REG_CONTROL	4
#define DS1672_REG_TRICKLE	5

#define DS1672_REG_CONTROL_EOSC	0x80

static struct i2c_driver ds1672_driver;

/*
 * In the routines that deal directly with the ds1672 hardware, we use
 * rtc_time -- month 0-11, hour 0-23, yr = calendar year-epoch
 * Epoch is initialized as 2000. Time is set to UTC.
 */
static int ds1672_get_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	unsigned long time;
	unsigned char addr = DS1672_REG_CNT_BASE;
	unsigned char buf[4];

	struct i2c_msg msgs[] = {
		{/* setup read ptr */
			.addr = client->addr,
			.len = 1,
			.buf = &addr
		},
		{/* read date */
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 4,
			.buf = buf
		},
	};

	/* read date registers */
	if ((i2c_transfer(client->adapter, &msgs[0], 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __func__);
		return -EIO;
	}

	dev_dbg(&client->dev,
		"%s: raw read data - counters=%02x,%02x,%02x,%02x\n",
		__func__, buf[0], buf[1], buf[2], buf[3]);

	time = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];

	rtc_time_to_tm(time, tm);

	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__, tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	return 0;
}

static int ds1672_set_mmss(struct i2c_client *client, unsigned long secs)
{
	int xfer;
	unsigned char buf[6];

	buf[0] = DS1672_REG_CNT_BASE;
	buf[1] = secs & 0x000000FF;
	buf[2] = (secs & 0x0000FF00) >> 8;
	buf[3] = (secs & 0x00FF0000) >> 16;
	buf[4] = (secs & 0xFF000000) >> 24;
	buf[5] = 0;		/* set control reg to enable counting */

	xfer = i2c_master_send(client, buf, 6);
	if (xfer != 6) {
		dev_err(&client->dev, "%s: send: %d\n", __func__, xfer);
		return -EIO;
	}

	return 0;
}

static int ds1672_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return ds1672_get_datetime(to_i2c_client(dev), tm);
}

static int ds1672_rtc_set_mmss(struct device *dev, unsigned long secs)
{
	return ds1672_set_mmss(to_i2c_client(dev), secs);
}

static int ds1672_get_control(struct i2c_client *client, u8 *status)
{
	unsigned char addr = DS1672_REG_CONTROL;

	struct i2c_msg msgs[] = {
		{/* setup read ptr */
			.addr = client->addr,
			.len = 1,
			.buf = &addr
		},
		{/* read control */
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = status
		},
	};

	/* read control register */
	if ((i2c_transfer(client->adapter, &msgs[0], 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __func__);
		return -EIO;
	}

	return 0;
}

/* following are the sysfs callback functions */
static ssize_t show_control(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	u8 control;
	int err;

	err = ds1672_get_control(client, &control);
	if (err)
		return err;

	return sprintf(buf, "%s\n", (control & DS1672_REG_CONTROL_EOSC)
		       ? "disabled" : "enabled");
}

static DEVICE_ATTR(control, S_IRUGO, show_control, NULL);

static const struct rtc_class_ops ds1672_rtc_ops = {
	.read_time = ds1672_rtc_read_time,
	.set_mmss = ds1672_rtc_set_mmss,
};

static int ds1672_remove(struct i2c_client *client)
{
	struct rtc_device *rtc = i2c_get_clientdata(client);

	if (rtc)
		rtc_device_unregister(rtc);

	return 0;
}

static int ds1672_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err = 0;
	u8 control;
	struct rtc_device *rtc;

	dev_dbg(&client->dev, "%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	dev_info(&client->dev, "chip found, driver version " DRV_VERSION "\n");

	rtc = rtc_device_register(ds1672_driver.driver.name, &client->dev,
				  &ds1672_rtc_ops, THIS_MODULE);

	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	i2c_set_clientdata(client, rtc);

	/* read control register */
	err = ds1672_get_control(client, &control);
	if (err)
		goto exit_devreg;

	if (control & DS1672_REG_CONTROL_EOSC)
		dev_warn(&client->dev, "Oscillator not enabled. "
			 "Set time to enable.\n");

	/* Register sysfs hooks */
	err = device_create_file(&client->dev, &dev_attr_control);
	if (err)
		goto exit_devreg;

	return 0;

 exit_devreg:
	rtc_device_unregister(rtc);
	return err;
}

static struct i2c_device_id ds1672_id[] = {
	{ "ds1672", 0 },
	{ }
};

static struct i2c_driver ds1672_driver = {
	.driver = {
		   .name = "rtc-ds1672",
		   },
	.probe = &ds1672_probe,
	.remove = &ds1672_remove,
	.id_table = ds1672_id,
};

module_i2c_driver(ds1672_driver);

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("Dallas/Maxim DS1672 timekeeper driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

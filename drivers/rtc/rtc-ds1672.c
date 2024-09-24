// SPDX-License-Identifier: GPL-2.0
/*
 * An rtc/i2c driver for the Dallas DS1672
 * Copyright 2005-06 Tower Technologies
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 */

#include <linux/i2c.h>
#include <linux/rtc.h>
#include <linux/module.h>

/* Registers */

#define DS1672_REG_CNT_BASE	0
#define DS1672_REG_CONTROL	4
#define DS1672_REG_TRICKLE	5

#define DS1672_REG_CONTROL_EOSC	0x80

/*
 * In the routines that deal directly with the ds1672 hardware, we use
 * rtc_time -- month 0-11, hour 0-23, yr = calendar year-epoch
 * Time is set to UTC.
 */
static int ds1672_read_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long time;
	unsigned char addr = DS1672_REG_CONTROL;
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
			.len = 1,
			.buf = buf
		},
	};

	/* read control register */
	if ((i2c_transfer(client->adapter, &msgs[0], 2)) != 2) {
		dev_warn(&client->dev, "Unable to read the control register\n");
		return -EIO;
	}

	if (buf[0] & DS1672_REG_CONTROL_EOSC) {
		dev_warn(&client->dev, "Oscillator not enabled. Set time to enable.\n");
		return -EINVAL;
	}

	addr = DS1672_REG_CNT_BASE;
	msgs[1].len = 4;

	/* read date registers */
	if ((i2c_transfer(client->adapter, &msgs[0], 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __func__);
		return -EIO;
	}

	dev_dbg(&client->dev,
		"%s: raw read data - counters=%02x,%02x,%02x,%02x\n",
		__func__, buf[0], buf[1], buf[2], buf[3]);

	time = ((unsigned long)buf[3] << 24) | (buf[2] << 16) |
	       (buf[1] << 8) | buf[0];

	rtc_time64_to_tm(time, tm);

	dev_dbg(&client->dev, "%s: tm is %ptR\n", __func__, tm);

	return 0;
}

static int ds1672_set_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	int xfer;
	unsigned char buf[6];
	unsigned long secs = rtc_tm_to_time64(tm);

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

static const struct rtc_class_ops ds1672_rtc_ops = {
	.read_time = ds1672_read_time,
	.set_time = ds1672_set_time,
};

static int ds1672_probe(struct i2c_client *client)
{
	int err = 0;
	struct rtc_device *rtc;

	dev_dbg(&client->dev, "%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	rtc = devm_rtc_allocate_device(&client->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	rtc->ops = &ds1672_rtc_ops;
	rtc->range_max = U32_MAX;

	err = devm_rtc_register_device(rtc);
	if (err)
		return err;

	i2c_set_clientdata(client, rtc);

	return 0;
}

static const struct i2c_device_id ds1672_id[] = {
	{ "ds1672" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ds1672_id);

static const __maybe_unused struct of_device_id ds1672_of_match[] = {
	{ .compatible = "dallas,ds1672" },
	{ }
};
MODULE_DEVICE_TABLE(of, ds1672_of_match);

static struct i2c_driver ds1672_driver = {
	.driver = {
		   .name = "rtc-ds1672",
		   .of_match_table = of_match_ptr(ds1672_of_match),
	},
	.probe = ds1672_probe,
	.id_table = ds1672_id,
};

module_i2c_driver(ds1672_driver);

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("Dallas/Maxim DS1672 timekeeper driver");
MODULE_LICENSE("GPL");

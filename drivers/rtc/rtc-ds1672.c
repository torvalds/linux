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

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/rtc.h>

#define DRV_VERSION "0.3"

/* Addresses to scan: none. This chip cannot be detected. */
static const unsigned short normal_i2c[] = { I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD;

/* Registers */

#define DS1672_REG_CNT_BASE	0
#define DS1672_REG_CONTROL	4
#define DS1672_REG_TRICKLE	5

#define DS1672_REG_CONTROL_EOSC	0x80

/* Prototypes */
static int ds1672_probe(struct i2c_adapter *adapter, int address, int kind);

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
		{ client->addr, 0, 1, &addr },		/* setup read ptr */
		{ client->addr, I2C_M_RD, 4, buf },	/* read date */
	};

	/* read date registers */
	if ((i2c_transfer(client->adapter, &msgs[0], 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __FUNCTION__);
		return -EIO;
	}

	dev_dbg(&client->dev,
		"%s: raw read data - counters=%02x,%02x,%02x,%02x\n",
		__FUNCTION__, buf[0], buf[1], buf[2], buf[3]);

	time = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];

	rtc_time_to_tm(time, tm);

	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__FUNCTION__, tm->tm_sec, tm->tm_min, tm->tm_hour,
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
	buf[5] = 0;	/* set control reg to enable counting */

	xfer = i2c_master_send(client, buf, 6);
	if (xfer != 6) {
		dev_err(&client->dev, "%s: send: %d\n", __FUNCTION__, xfer);
		return -EIO;
	}

	return 0;
}

static int ds1672_set_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	unsigned long secs;

	dev_dbg(&client->dev,
		"%s: secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__FUNCTION__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	rtc_tm_to_time(tm, &secs);

	return ds1672_set_mmss(client, secs);
}

static int ds1672_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return ds1672_get_datetime(to_i2c_client(dev), tm);
}

static int ds1672_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return ds1672_set_datetime(to_i2c_client(dev), tm);
}

static int ds1672_rtc_set_mmss(struct device *dev, unsigned long secs)
{
	return ds1672_set_mmss(to_i2c_client(dev), secs);
}

static int ds1672_get_control(struct i2c_client *client, u8 *status)
{
	unsigned char addr = DS1672_REG_CONTROL;

	struct i2c_msg msgs[] = {
		{ client->addr, 0, 1, &addr },		/* setup read ptr */
		{ client->addr, I2C_M_RD, 1, status },	/* read control */
	};

	/* read control register */
	if ((i2c_transfer(client->adapter, &msgs[0], 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __FUNCTION__);
		return -EIO;
	}

	return 0;
}

/* following are the sysfs callback functions */
static ssize_t show_control(struct device *dev, struct device_attribute *attr, char *buf)
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
	.read_time	= ds1672_rtc_read_time,
	.set_time	= ds1672_rtc_set_time,
	.set_mmss	= ds1672_rtc_set_mmss,
};

static int ds1672_attach(struct i2c_adapter *adapter)
{
	return i2c_probe(adapter, &addr_data, ds1672_probe);
}

static int ds1672_detach(struct i2c_client *client)
{
	int err;
	struct rtc_device *rtc = i2c_get_clientdata(client);

 	if (rtc)
		rtc_device_unregister(rtc);

	if ((err = i2c_detach_client(client)))
		return err;

	kfree(client);

	return 0;
}

static struct i2c_driver ds1672_driver = {
	.driver		= {
		.name	= "ds1672",
	},
	.id		= I2C_DRIVERID_DS1672,
	.attach_adapter = &ds1672_attach,
	.detach_client	= &ds1672_detach,
};

static int ds1672_probe(struct i2c_adapter *adapter, int address, int kind)
{
	int err = 0;
	u8 control;
	struct i2c_client *client;
	struct rtc_device *rtc;

	dev_dbg(&adapter->dev, "%s\n", __FUNCTION__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit;
	}

	if (!(client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	/* I2C client */
	client->addr = address;
	client->driver = &ds1672_driver;
	client->adapter	= adapter;

	strlcpy(client->name, ds1672_driver.driver.name, I2C_NAME_SIZE);

	/* Inform the i2c layer */
	if ((err = i2c_attach_client(client)))
		goto exit_kfree;

	dev_info(&client->dev, "chip found, driver version " DRV_VERSION "\n");

	rtc = rtc_device_register(ds1672_driver.driver.name, &client->dev,
				&ds1672_rtc_ops, THIS_MODULE);

	if (IS_ERR(rtc)) {
		err = PTR_ERR(rtc);
		goto exit_detach;
	}

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

exit_detach:
	i2c_detach_client(client);

exit_kfree:
	kfree(client);

exit:
	return err;
}

static int __init ds1672_init(void)
{
	return i2c_add_driver(&ds1672_driver);
}

static void __exit ds1672_exit(void)
{
	i2c_del_driver(&ds1672_driver);
}

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("Dallas/Maxim DS1672 timekeeper driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

module_init(ds1672_init);
module_exit(ds1672_exit);

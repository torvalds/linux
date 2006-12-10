/*
 * An I2C driver for the Ricoh RS5C372 RTC
 *
 * Copyright (C) 2005 Pavel Mironchik <pmironchik@optifacio.net>
 * Copyright (C) 2006 Tower Technologies
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/rtc.h>
#include <linux/bcd.h>

#define DRV_VERSION "0.3"

/* Addresses to scan */
static unsigned short normal_i2c[] = { /* 0x32,*/ I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD;

#define RS5C372_REG_SECS	0
#define RS5C372_REG_MINS	1
#define RS5C372_REG_HOURS	2
#define RS5C372_REG_WDAY	3
#define RS5C372_REG_DAY		4
#define RS5C372_REG_MONTH	5
#define RS5C372_REG_YEAR	6
#define RS5C372_REG_TRIM	7

#define RS5C372_TRIM_XSL	0x80
#define RS5C372_TRIM_MASK	0x7F

#define RS5C372_REG_BASE	0

static int rs5c372_attach(struct i2c_adapter *adapter);
static int rs5c372_detach(struct i2c_client *client);
static int rs5c372_probe(struct i2c_adapter *adapter, int address, int kind);

struct rs5c372 {
	u8 reg_addr;
	u8 regs[17];
	struct i2c_msg msg[1];
	struct i2c_client client;
	struct rtc_device *rtc;
};

static struct i2c_driver rs5c372_driver = {
	.driver		= {
		.name	= "rs5c372",
	},
	.attach_adapter	= &rs5c372_attach,
	.detach_client	= &rs5c372_detach,
};

static int rs5c372_get_datetime(struct i2c_client *client, struct rtc_time *tm)
{

	struct rs5c372 *rs5c372 = i2c_get_clientdata(client);
	u8 *buf = &(rs5c372->regs[1]);

	/* this implements the 3rd reading method, according
	 * to the datasheet. rs5c372 defaults to internal
	 * address 0xF, so 0x0 is in regs[1]
	 */

	if ((i2c_transfer(client->adapter, rs5c372->msg, 1)) != 1) {
		dev_err(&client->dev, "%s: read error\n", __FUNCTION__);
		return -EIO;
	}

	tm->tm_sec = BCD2BIN(buf[RS5C372_REG_SECS] & 0x7f);
	tm->tm_min = BCD2BIN(buf[RS5C372_REG_MINS] & 0x7f);
	tm->tm_hour = BCD2BIN(buf[RS5C372_REG_HOURS] & 0x3f);
	tm->tm_wday = BCD2BIN(buf[RS5C372_REG_WDAY] & 0x07);
	tm->tm_mday = BCD2BIN(buf[RS5C372_REG_DAY] & 0x3f);

	/* tm->tm_mon is zero-based */
	tm->tm_mon = BCD2BIN(buf[RS5C372_REG_MONTH] & 0x1f) - 1;

	/* year is 1900 + tm->tm_year */
	tm->tm_year = BCD2BIN(buf[RS5C372_REG_YEAR]) + 100;

	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__FUNCTION__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	return 0;
}

static int rs5c372_set_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	unsigned char buf[8] = { RS5C372_REG_BASE };

	dev_dbg(&client->dev,
		"%s: secs=%d, mins=%d, hours=%d "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__FUNCTION__, tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	buf[1] = BIN2BCD(tm->tm_sec);
	buf[2] = BIN2BCD(tm->tm_min);
	buf[3] = BIN2BCD(tm->tm_hour);
	buf[4] = BIN2BCD(tm->tm_wday);
	buf[5] = BIN2BCD(tm->tm_mday);
	buf[6] = BIN2BCD(tm->tm_mon + 1);
	buf[7] = BIN2BCD(tm->tm_year - 100);

	if ((i2c_master_send(client, buf, 8)) != 8) {
		dev_err(&client->dev, "%s: write error\n", __FUNCTION__);
		return -EIO;
	}

	return 0;
}

static int rs5c372_get_trim(struct i2c_client *client, int *osc, int *trim)
{
	struct rs5c372 *rs5c372 = i2c_get_clientdata(client);
	u8 tmp = rs5c372->regs[RS5C372_REG_TRIM + 1];

	if (osc)
		*osc = (tmp & RS5C372_TRIM_XSL) ? 32000 : 32768;

	if (trim) {
		*trim = tmp & RS5C372_TRIM_MASK;
		dev_dbg(&client->dev, "%s: raw trim=%x\n", __FUNCTION__, *trim);
	}

	return 0;
}

static int rs5c372_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return rs5c372_get_datetime(to_i2c_client(dev), tm);
}

static int rs5c372_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return rs5c372_set_datetime(to_i2c_client(dev), tm);
}

static int rs5c372_rtc_proc(struct device *dev, struct seq_file *seq)
{
	int err, osc, trim;

	err = rs5c372_get_trim(to_i2c_client(dev), &osc, &trim);
	if (err == 0) {
		seq_printf(seq, "%d.%03d KHz\n", osc / 1000, osc % 1000);
		seq_printf(seq, "trim\t: %d\n", trim);
	}

	return 0;
}

static const struct rtc_class_ops rs5c372_rtc_ops = {
	.proc		= rs5c372_rtc_proc,
	.read_time	= rs5c372_rtc_read_time,
	.set_time	= rs5c372_rtc_set_time,
};

static ssize_t rs5c372_sysfs_show_trim(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int err, trim;

	err = rs5c372_get_trim(to_i2c_client(dev), NULL, &trim);
	if (err)
		return err;

	return sprintf(buf, "0x%2x\n", trim);
}
static DEVICE_ATTR(trim, S_IRUGO, rs5c372_sysfs_show_trim, NULL);

static ssize_t rs5c372_sysfs_show_osc(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int err, osc;

	err = rs5c372_get_trim(to_i2c_client(dev), &osc, NULL);
	if (err)
		return err;

	return sprintf(buf, "%d.%03d KHz\n", osc / 1000, osc % 1000);
}
static DEVICE_ATTR(osc, S_IRUGO, rs5c372_sysfs_show_osc, NULL);

static int rs5c372_attach(struct i2c_adapter *adapter)
{
	return i2c_probe(adapter, &addr_data, rs5c372_probe);
}

static int rs5c372_probe(struct i2c_adapter *adapter, int address, int kind)
{
	int err = 0;
	struct i2c_client *client;
	struct rs5c372 *rs5c372;

	dev_dbg(adapter->class_dev.dev, "%s\n", __FUNCTION__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit;
	}

	if (!(rs5c372 = kzalloc(sizeof(struct rs5c372), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}
	client = &rs5c372->client;

	/* I2C client */
	client->addr = address;
	client->driver = &rs5c372_driver;
	client->adapter = adapter;

	strlcpy(client->name, rs5c372_driver.driver.name, I2C_NAME_SIZE);

	i2c_set_clientdata(client, rs5c372);

	rs5c372->msg[0].addr = address;
	rs5c372->msg[0].flags = I2C_M_RD;
	rs5c372->msg[0].len = sizeof(rs5c372->regs);
	rs5c372->msg[0].buf = rs5c372->regs;

	/* Inform the i2c layer */
	if ((err = i2c_attach_client(client)))
		goto exit_kfree;

	dev_info(&client->dev, "chip found, driver version " DRV_VERSION "\n");

	rs5c372->rtc = rtc_device_register(rs5c372_driver.driver.name,
				&client->dev, &rs5c372_rtc_ops, THIS_MODULE);

	if (IS_ERR(rs5c372->rtc)) {
		err = PTR_ERR(rs5c372->rtc);
		goto exit_detach;
	}

	err = device_create_file(&client->dev, &dev_attr_trim);
	if (err)
		goto exit_devreg;
	err = device_create_file(&client->dev, &dev_attr_osc);
	if (err)
		goto exit_trim;

	return 0;

exit_trim:
	device_remove_file(&client->dev, &dev_attr_trim);

exit_devreg:
	rtc_device_unregister(rs5c372->rtc);

exit_detach:
	i2c_detach_client(client);

exit_kfree:
	kfree(rs5c372);

exit:
	return err;
}

static int rs5c372_detach(struct i2c_client *client)
{
	int err;
	struct rs5c372 *rs5c372 = i2c_get_clientdata(client);

	if (rs5c372->rtc)
		rtc_device_unregister(rs5c372->rtc);

	if ((err = i2c_detach_client(client)))
		return err;

	kfree(rs5c372);
	return 0;
}

static __init int rs5c372_init(void)
{
	return i2c_add_driver(&rs5c372_driver);
}

static __exit void rs5c372_exit(void)
{
	i2c_del_driver(&rs5c372_driver);
}

module_init(rs5c372_init);
module_exit(rs5c372_exit);

MODULE_AUTHOR(
		"Pavel Mironchik <pmironchik@optifacio.net>, "
		"Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("Ricoh RS5C372 RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

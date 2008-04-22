/*
 * Seiko Instruments S-35390A RTC Driver
 *
 * Copyright (c) 2007 Byron Bradley
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/i2c.h>
#include <linux/bitrev.h>
#include <linux/bcd.h>
#include <linux/slab.h>

#define S35390A_CMD_STATUS1	0
#define S35390A_CMD_STATUS2	1
#define S35390A_CMD_TIME1	2

#define S35390A_BYTE_YEAR	0
#define S35390A_BYTE_MONTH	1
#define S35390A_BYTE_DAY	2
#define S35390A_BYTE_WDAY	3
#define S35390A_BYTE_HOURS	4
#define S35390A_BYTE_MINS	5
#define S35390A_BYTE_SECS	6

#define S35390A_FLAG_POC	0x01
#define S35390A_FLAG_BLD	0x02
#define S35390A_FLAG_24H	0x40
#define S35390A_FLAG_RESET	0x80
#define S35390A_FLAG_TEST	0x01

struct s35390a {
	struct i2c_client *client[8];
	struct rtc_device *rtc;
	int twentyfourhour;
};

static int s35390a_set_reg(struct s35390a *s35390a, int reg, char *buf, int len)
{
	struct i2c_client *client = s35390a->client[reg];
	struct i2c_msg msg[] = {
		{ client->addr, 0, len, buf },
	};

	if ((i2c_transfer(client->adapter, msg, 1)) != 1)
		return -EIO;

	return 0;
}

static int s35390a_get_reg(struct s35390a *s35390a, int reg, char *buf, int len)
{
	struct i2c_client *client = s35390a->client[reg];
	struct i2c_msg msg[] = {
		{ client->addr, I2C_M_RD, len, buf },
	};

	if ((i2c_transfer(client->adapter, msg, 1)) != 1)
		return -EIO;

	return 0;
}

static int s35390a_reset(struct s35390a *s35390a)
{
	char buf[1];

	if (s35390a_get_reg(s35390a, S35390A_CMD_STATUS1, buf, sizeof(buf)) < 0)
		return -EIO;

	if (!(buf[0] & (S35390A_FLAG_POC | S35390A_FLAG_BLD)))
		return 0;

	buf[0] |= (S35390A_FLAG_RESET | S35390A_FLAG_24H);
	buf[0] &= 0xf0;
	return s35390a_set_reg(s35390a, S35390A_CMD_STATUS1, buf, sizeof(buf));
}

static int s35390a_disable_test_mode(struct s35390a *s35390a)
{
	char buf[1];

	if (s35390a_get_reg(s35390a, S35390A_CMD_STATUS2, buf, sizeof(buf)) < 0)
		return -EIO;

	if (!(buf[0] & S35390A_FLAG_TEST))
		return 0;

	buf[0] &= ~S35390A_FLAG_TEST;
	return s35390a_set_reg(s35390a, S35390A_CMD_STATUS2, buf, sizeof(buf));
}

static char s35390a_hr2reg(struct s35390a *s35390a, int hour)
{
	if (s35390a->twentyfourhour)
		return BIN2BCD(hour);

	if (hour < 12)
		return BIN2BCD(hour);

	return 0x40 | BIN2BCD(hour - 12);
}

static int s35390a_reg2hr(struct s35390a *s35390a, char reg)
{
	unsigned hour;

	if (s35390a->twentyfourhour)
		return BCD2BIN(reg & 0x3f);

	hour = BCD2BIN(reg & 0x3f);
	if (reg & 0x40)
		hour += 12;

	return hour;
}

static int s35390a_set_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct s35390a	*s35390a = i2c_get_clientdata(client);
	int i, err;
	char buf[7];

	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d mday=%d, "
		"mon=%d, year=%d, wday=%d\n", __func__, tm->tm_sec,
		tm->tm_min, tm->tm_hour, tm->tm_mday, tm->tm_mon, tm->tm_year,
		tm->tm_wday);

	buf[S35390A_BYTE_YEAR] = BIN2BCD(tm->tm_year - 100);
	buf[S35390A_BYTE_MONTH] = BIN2BCD(tm->tm_mon + 1);
	buf[S35390A_BYTE_DAY] = BIN2BCD(tm->tm_mday);
	buf[S35390A_BYTE_WDAY] = BIN2BCD(tm->tm_wday);
	buf[S35390A_BYTE_HOURS] = s35390a_hr2reg(s35390a, tm->tm_hour);
	buf[S35390A_BYTE_MINS] = BIN2BCD(tm->tm_min);
	buf[S35390A_BYTE_SECS] = BIN2BCD(tm->tm_sec);

	/* This chip expects the bits of each byte to be in reverse order */
	for (i = 0; i < 7; ++i)
		buf[i] = bitrev8(buf[i]);

	err = s35390a_set_reg(s35390a, S35390A_CMD_TIME1, buf, sizeof(buf));

	return err;
}

static int s35390a_get_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct s35390a *s35390a = i2c_get_clientdata(client);
	char buf[7];
	int i, err;

	err = s35390a_get_reg(s35390a, S35390A_CMD_TIME1, buf, sizeof(buf));
	if (err < 0)
		return err;

	/* This chip returns the bits of each byte in reverse order */
	for (i = 0; i < 7; ++i)
		buf[i] = bitrev8(buf[i]);

	tm->tm_sec = BCD2BIN(buf[S35390A_BYTE_SECS]);
	tm->tm_min = BCD2BIN(buf[S35390A_BYTE_MINS]);
	tm->tm_hour = s35390a_reg2hr(s35390a, buf[S35390A_BYTE_HOURS]);
	tm->tm_wday = BCD2BIN(buf[S35390A_BYTE_WDAY]);
	tm->tm_mday = BCD2BIN(buf[S35390A_BYTE_DAY]);
	tm->tm_mon = BCD2BIN(buf[S35390A_BYTE_MONTH]) - 1;
	tm->tm_year = BCD2BIN(buf[S35390A_BYTE_YEAR]) + 100;

	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d, mday=%d, "
		"mon=%d, year=%d, wday=%d\n", __func__, tm->tm_sec,
		tm->tm_min, tm->tm_hour, tm->tm_mday, tm->tm_mon, tm->tm_year,
		tm->tm_wday);

	return rtc_valid_tm(tm);
}

static int s35390a_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return s35390a_get_datetime(to_i2c_client(dev), tm);
}

static int s35390a_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return s35390a_set_datetime(to_i2c_client(dev), tm);
}

static const struct rtc_class_ops s35390a_rtc_ops = {
	.read_time	= s35390a_rtc_read_time,
	.set_time	= s35390a_rtc_set_time,
};

static struct i2c_driver s35390a_driver;

static int s35390a_probe(struct i2c_client *client)
{
	int err;
	unsigned int i;
	struct s35390a *s35390a;
	struct rtc_time tm;
	char buf[1];

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit;
	}

	s35390a = kzalloc(sizeof(struct s35390a), GFP_KERNEL);
	if (!s35390a) {
		err = -ENOMEM;
		goto exit;
	}

	s35390a->client[0] = client;
	i2c_set_clientdata(client, s35390a);

	/* This chip uses multiple addresses, use dummy devices for them */
	for (i = 1; i < 8; ++i) {
		s35390a->client[i] = i2c_new_dummy(client->adapter,
					client->addr + i, "rtc-s35390a");
		if (!s35390a->client[i]) {
			dev_err(&client->dev, "Address %02x unavailable\n",
						client->addr + i);
			err = -EBUSY;
			goto exit_dummy;
		}
	}

	err = s35390a_reset(s35390a);
	if (err < 0) {
		dev_err(&client->dev, "error resetting chip\n");
		goto exit_dummy;
	}

	err = s35390a_disable_test_mode(s35390a);
	if (err < 0) {
		dev_err(&client->dev, "error disabling test mode\n");
		goto exit_dummy;
	}

	err = s35390a_get_reg(s35390a, S35390A_CMD_STATUS1, buf, sizeof(buf));
	if (err < 0) {
		dev_err(&client->dev, "error checking 12/24 hour mode\n");
		goto exit_dummy;
	}
	if (buf[0] & S35390A_FLAG_24H)
		s35390a->twentyfourhour = 1;
	else
		s35390a->twentyfourhour = 0;

	if (s35390a_get_datetime(client, &tm) < 0)
		dev_warn(&client->dev, "clock needs to be set\n");

	s35390a->rtc = rtc_device_register(s35390a_driver.driver.name,
				&client->dev, &s35390a_rtc_ops, THIS_MODULE);

	if (IS_ERR(s35390a->rtc)) {
		err = PTR_ERR(s35390a->rtc);
		goto exit_dummy;
	}
	return 0;

exit_dummy:
	for (i = 1; i < 8; ++i)
		if (s35390a->client[i])
			i2c_unregister_device(s35390a->client[i]);
	kfree(s35390a);
	i2c_set_clientdata(client, NULL);

exit:
	return err;
}

static int s35390a_remove(struct i2c_client *client)
{
	unsigned int i;

	struct s35390a *s35390a = i2c_get_clientdata(client);
	for (i = 1; i < 8; ++i)
		if (s35390a->client[i])
			i2c_unregister_device(s35390a->client[i]);

	rtc_device_unregister(s35390a->rtc);
	kfree(s35390a);
	i2c_set_clientdata(client, NULL);

	return 0;
}

static struct i2c_driver s35390a_driver = {
	.driver		= {
		.name	= "rtc-s35390a",
	},
	.probe		= s35390a_probe,
	.remove		= s35390a_remove,
};

static int __init s35390a_rtc_init(void)
{
	return i2c_add_driver(&s35390a_driver);
}

static void __exit s35390a_rtc_exit(void)
{
	i2c_del_driver(&s35390a_driver);
}

MODULE_AUTHOR("Byron Bradley <byron.bbradley@gmail.com>");
MODULE_DESCRIPTION("S35390A RTC driver");
MODULE_LICENSE("GPL");

module_init(s35390a_rtc_init);
module_exit(s35390a_rtc_exit);

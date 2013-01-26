/*
 * Copyright (C) 2012 Avionic Design GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bcd.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/of.h>

#define DRIVER_NAME "rtc-pcf8523"

#define REG_CONTROL1 0x00
#define REG_CONTROL1_CAP_SEL (1 << 7)
#define REG_CONTROL1_STOP    (1 << 5)

#define REG_CONTROL3 0x02
#define REG_CONTROL3_PM_BLD (1 << 7) /* battery low detection disabled */
#define REG_CONTROL3_PM_VDD (1 << 6) /* switch-over disabled */
#define REG_CONTROL3_PM_DSM (1 << 5) /* direct switching mode */
#define REG_CONTROL3_PM_MASK 0xe0

#define REG_SECONDS  0x03
#define REG_SECONDS_OS (1 << 7)

#define REG_MINUTES  0x04
#define REG_HOURS    0x05
#define REG_DAYS     0x06
#define REG_WEEKDAYS 0x07
#define REG_MONTHS   0x08
#define REG_YEARS    0x09

struct pcf8523 {
	struct rtc_device *rtc;
};

static int pcf8523_read(struct i2c_client *client, u8 reg, u8 *valuep)
{
	struct i2c_msg msgs[2];
	u8 value = 0;
	int err;

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = sizeof(reg);
	msgs[0].buf = &reg;

	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = sizeof(value);
	msgs[1].buf = &value;

	err = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (err < 0)
		return err;

	*valuep = value;

	return 0;
}

static int pcf8523_write(struct i2c_client *client, u8 reg, u8 value)
{
	u8 buffer[2] = { reg, value };
	struct i2c_msg msg;
	int err;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = sizeof(buffer);
	msg.buf = buffer;

	err = i2c_transfer(client->adapter, &msg, 1);
	if (err < 0)
		return err;

	return 0;
}

static int pcf8523_select_capacitance(struct i2c_client *client, bool high)
{
	u8 value;
	int err;

	err = pcf8523_read(client, REG_CONTROL1, &value);
	if (err < 0)
		return err;

	if (!high)
		value &= ~REG_CONTROL1_CAP_SEL;
	else
		value |= REG_CONTROL1_CAP_SEL;

	err = pcf8523_write(client, REG_CONTROL1, value);
	if (err < 0)
		return err;

	return err;
}

static int pcf8523_set_pm(struct i2c_client *client, u8 pm)
{
	u8 value;
	int err;

	err = pcf8523_read(client, REG_CONTROL3, &value);
	if (err < 0)
		return err;

	value = (value & ~REG_CONTROL3_PM_MASK) | pm;

	err = pcf8523_write(client, REG_CONTROL3, value);
	if (err < 0)
		return err;

	return 0;
}

static int pcf8523_stop_rtc(struct i2c_client *client)
{
	u8 value;
	int err;

	err = pcf8523_read(client, REG_CONTROL1, &value);
	if (err < 0)
		return err;

	value |= REG_CONTROL1_STOP;

	err = pcf8523_write(client, REG_CONTROL1, value);
	if (err < 0)
		return err;

	return 0;
}

static int pcf8523_start_rtc(struct i2c_client *client)
{
	u8 value;
	int err;

	err = pcf8523_read(client, REG_CONTROL1, &value);
	if (err < 0)
		return err;

	value &= ~REG_CONTROL1_STOP;

	err = pcf8523_write(client, REG_CONTROL1, value);
	if (err < 0)
		return err;

	return 0;
}

static int pcf8523_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	u8 start = REG_SECONDS, regs[7];
	struct i2c_msg msgs[2];
	int err;

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &start;

	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = sizeof(regs);
	msgs[1].buf = regs;

	err = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (err < 0)
		return err;

	if (regs[0] & REG_SECONDS_OS) {
		/*
		 * If the oscillator was stopped, try to clear the flag. Upon
		 * power-up the flag is always set, but if we cannot clear it
		 * the oscillator isn't running properly for some reason. The
		 * sensible thing therefore is to return an error, signalling
		 * that the clock cannot be assumed to be correct.
		 */

		regs[0] &= ~REG_SECONDS_OS;

		err = pcf8523_write(client, REG_SECONDS, regs[0]);
		if (err < 0)
			return err;

		err = pcf8523_read(client, REG_SECONDS, &regs[0]);
		if (err < 0)
			return err;

		if (regs[0] & REG_SECONDS_OS)
			return -EAGAIN;
	}

	tm->tm_sec = bcd2bin(regs[0] & 0x7f);
	tm->tm_min = bcd2bin(regs[1] & 0x7f);
	tm->tm_hour = bcd2bin(regs[2] & 0x3f);
	tm->tm_mday = bcd2bin(regs[3] & 0x3f);
	tm->tm_wday = regs[4] & 0x7;
	tm->tm_mon = bcd2bin(regs[5] & 0x1f);
	tm->tm_year = bcd2bin(regs[6]) + 100;

	return rtc_valid_tm(tm);
}

static int pcf8523_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_msg msg;
	u8 regs[8];
	int err;

	err = pcf8523_stop_rtc(client);
	if (err < 0)
		return err;

	regs[0] = REG_SECONDS;
	regs[1] = bin2bcd(tm->tm_sec);
	regs[2] = bin2bcd(tm->tm_min);
	regs[3] = bin2bcd(tm->tm_hour);
	regs[4] = bin2bcd(tm->tm_mday);
	regs[5] = tm->tm_wday;
	regs[6] = bin2bcd(tm->tm_mon);
	regs[7] = bin2bcd(tm->tm_year - 100);

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = sizeof(regs);
	msg.buf = regs;

	err = i2c_transfer(client->adapter, &msg, 1);
	if (err < 0) {
		/*
		 * If the time cannot be set, restart the RTC anyway. Note
		 * that errors are ignored if the RTC cannot be started so
		 * that we have a chance to propagate the original error.
		 */
		pcf8523_start_rtc(client);
		return err;
	}

	return pcf8523_start_rtc(client);
}

static const struct rtc_class_ops pcf8523_rtc_ops = {
	.read_time = pcf8523_rtc_read_time,
	.set_time = pcf8523_rtc_set_time,
};

static int pcf8523_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct pcf8523 *pcf;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	pcf = devm_kzalloc(&client->dev, sizeof(*pcf), GFP_KERNEL);
	if (!pcf)
		return -ENOMEM;

	err = pcf8523_select_capacitance(client, true);
	if (err < 0)
		return err;

	err = pcf8523_set_pm(client, 0);
	if (err < 0)
		return err;

	pcf->rtc = rtc_device_register(DRIVER_NAME, &client->dev,
				       &pcf8523_rtc_ops, THIS_MODULE);
	if (IS_ERR(pcf->rtc))
		return PTR_ERR(pcf->rtc);

	i2c_set_clientdata(client, pcf);

	return 0;
}

static int pcf8523_remove(struct i2c_client *client)
{
	struct pcf8523 *pcf = i2c_get_clientdata(client);

	rtc_device_unregister(pcf->rtc);

	return 0;
}

static const struct i2c_device_id pcf8523_id[] = {
	{ "pcf8523", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcf8523_id);

#ifdef CONFIG_OF
static const struct of_device_id pcf8523_of_match[] = {
	{ .compatible = "nxp,pcf8523" },
	{ }
};
MODULE_DEVICE_TABLE(of, pcf8523_of_match);
#endif

static struct i2c_driver pcf8523_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(pcf8523_of_match),
	},
	.probe = pcf8523_probe,
	.remove = pcf8523_remove,
	.id_table = pcf8523_id,
};
module_i2c_driver(pcf8523_driver);

MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_DESCRIPTION("NXP PCF8523 RTC driver");
MODULE_LICENSE("GPL v2");

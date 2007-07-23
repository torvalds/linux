/*
 * rtc-ds1307.c - RTC driver for some mostly-compatible I2C chips.
 *
 *  Copyright (C) 2005 James Chapman (ds1337 core)
 *  Copyright (C) 2006 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/string.h>
#include <linux/rtc.h>
#include <linux/bcd.h>



/* We can't determine type by probing, but if we expect pre-Linux code
 * to have set the chip up as a clock (turning on the oscillator and
 * setting the date and time), Linux can ignore the non-clock features.
 * That's a natural job for a factory or repair bench.
 *
 * This is currently a simple no-alarms driver.  If your board has the
 * alarm irq wired up on a ds1337 or ds1339, and you want to use that,
 * then look at the rtc-rs5c372 driver for code to steal...
 */
enum ds_type {
	ds_1307,
	ds_1337,
	ds_1338,
	ds_1339,
	ds_1340,
	m41t00,
	// rs5c372 too?  different address...
};


/* RTC registers don't differ much, except for the century flag */
#define DS1307_REG_SECS		0x00	/* 00-59 */
#	define DS1307_BIT_CH		0x80
#	define DS1340_BIT_nEOSC		0x80
#define DS1307_REG_MIN		0x01	/* 00-59 */
#define DS1307_REG_HOUR		0x02	/* 00-23, or 1-12{am,pm} */
#	define DS1307_BIT_12HR		0x40	/* in REG_HOUR */
#	define DS1307_BIT_PM		0x20	/* in REG_HOUR */
#	define DS1340_BIT_CENTURY_EN	0x80	/* in REG_HOUR */
#	define DS1340_BIT_CENTURY	0x40	/* in REG_HOUR */
#define DS1307_REG_WDAY		0x03	/* 01-07 */
#define DS1307_REG_MDAY		0x04	/* 01-31 */
#define DS1307_REG_MONTH	0x05	/* 01-12 */
#	define DS1337_BIT_CENTURY	0x80	/* in REG_MONTH */
#define DS1307_REG_YEAR		0x06	/* 00-99 */

/* Other registers (control, status, alarms, trickle charge, NVRAM, etc)
 * start at 7, and they differ a LOT. Only control and status matter for
 * basic RTC date and time functionality; be careful using them.
 */
#define DS1307_REG_CONTROL	0x07		/* or ds1338 */
#	define DS1307_BIT_OUT		0x80
#	define DS1338_BIT_OSF		0x20
#	define DS1307_BIT_SQWE		0x10
#	define DS1307_BIT_RS1		0x02
#	define DS1307_BIT_RS0		0x01
#define DS1337_REG_CONTROL	0x0e
#	define DS1337_BIT_nEOSC		0x80
#	define DS1337_BIT_RS2		0x10
#	define DS1337_BIT_RS1		0x08
#	define DS1337_BIT_INTCN		0x04
#	define DS1337_BIT_A2IE		0x02
#	define DS1337_BIT_A1IE		0x01
#define DS1340_REG_CONTROL	0x07
#	define DS1340_BIT_OUT		0x80
#	define DS1340_BIT_FT		0x40
#	define DS1340_BIT_CALIB_SIGN	0x20
#	define DS1340_M_CALIBRATION	0x1f
#define DS1340_REG_FLAG		0x09
#	define DS1340_BIT_OSF		0x80
#define DS1337_REG_STATUS	0x0f
#	define DS1337_BIT_OSF		0x80
#	define DS1337_BIT_A2I		0x02
#	define DS1337_BIT_A1I		0x01
#define DS1339_REG_TRICKLE	0x10



struct ds1307 {
	u8			reg_addr;
	u8			regs[8];
	enum ds_type		type;
	struct i2c_msg		msg[2];
	struct i2c_client	*client;
	struct i2c_client	dev;
	struct rtc_device	*rtc;
};

struct chip_desc {
	char			name[9];
	unsigned		nvram56:1;
	unsigned		alarm:1;
	enum ds_type		type;
};

static const struct chip_desc chips[] = { {
	.name		= "ds1307",
	.type		= ds_1307,
	.nvram56	= 1,
}, {
	.name		= "ds1337",
	.type		= ds_1337,
	.alarm		= 1,
}, {
	.name		= "ds1338",
	.type		= ds_1338,
	.nvram56	= 1,
}, {
	.name		= "ds1339",
	.type		= ds_1339,
	.alarm		= 1,
}, {
	.name		= "ds1340",
	.type		= ds_1340,
}, {
	.name		= "m41t00",
	.type		= m41t00,
}, };

static inline const struct chip_desc *find_chip(const char *s)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(chips); i++)
		if (strnicmp(s, chips[i].name, sizeof chips[i].name) == 0)
			return &chips[i];
	return NULL;
}

static int ds1307_get_time(struct device *dev, struct rtc_time *t)
{
	struct ds1307	*ds1307 = dev_get_drvdata(dev);
	int		tmp;

	/* read the RTC date and time registers all at once */
	ds1307->msg[1].flags = I2C_M_RD;
	ds1307->msg[1].len = 7;

	tmp = i2c_transfer(to_i2c_adapter(ds1307->client->dev.parent),
			ds1307->msg, 2);
	if (tmp != 2) {
		dev_err(dev, "%s error %d\n", "read", tmp);
		return -EIO;
	}

	dev_dbg(dev, "%s: %02x %02x %02x %02x %02x %02x %02x\n",
			"read",
			ds1307->regs[0], ds1307->regs[1],
			ds1307->regs[2], ds1307->regs[3],
			ds1307->regs[4], ds1307->regs[5],
			ds1307->regs[6]);

	t->tm_sec = BCD2BIN(ds1307->regs[DS1307_REG_SECS] & 0x7f);
	t->tm_min = BCD2BIN(ds1307->regs[DS1307_REG_MIN] & 0x7f);
	tmp = ds1307->regs[DS1307_REG_HOUR] & 0x3f;
	t->tm_hour = BCD2BIN(tmp);
	t->tm_wday = BCD2BIN(ds1307->regs[DS1307_REG_WDAY] & 0x07) - 1;
	t->tm_mday = BCD2BIN(ds1307->regs[DS1307_REG_MDAY] & 0x3f);
	tmp = ds1307->regs[DS1307_REG_MONTH] & 0x1f;
	t->tm_mon = BCD2BIN(tmp) - 1;

	/* assume 20YY not 19YY, and ignore DS1337_BIT_CENTURY */
	t->tm_year = BCD2BIN(ds1307->regs[DS1307_REG_YEAR]) + 100;

	dev_dbg(dev, "%s secs=%d, mins=%d, "
		"hours=%d, mday=%d, mon=%d, year=%d, wday=%d\n",
		"read", t->tm_sec, t->tm_min,
		t->tm_hour, t->tm_mday,
		t->tm_mon, t->tm_year, t->tm_wday);

	/* initial clock setting can be undefined */
	return rtc_valid_tm(t);
}

static int ds1307_set_time(struct device *dev, struct rtc_time *t)
{
	struct ds1307	*ds1307 = dev_get_drvdata(dev);
	int		result;
	int		tmp;
	u8		*buf = ds1307->regs;

	dev_dbg(dev, "%s secs=%d, mins=%d, "
		"hours=%d, mday=%d, mon=%d, year=%d, wday=%d\n",
		"write", t->tm_sec, t->tm_min,
		t->tm_hour, t->tm_mday,
		t->tm_mon, t->tm_year, t->tm_wday);

	*buf++ = 0;		/* first register addr */
	buf[DS1307_REG_SECS] = BIN2BCD(t->tm_sec);
	buf[DS1307_REG_MIN] = BIN2BCD(t->tm_min);
	buf[DS1307_REG_HOUR] = BIN2BCD(t->tm_hour);
	buf[DS1307_REG_WDAY] = BIN2BCD(t->tm_wday + 1);
	buf[DS1307_REG_MDAY] = BIN2BCD(t->tm_mday);
	buf[DS1307_REG_MONTH] = BIN2BCD(t->tm_mon + 1);

	/* assume 20YY not 19YY */
	tmp = t->tm_year - 100;
	buf[DS1307_REG_YEAR] = BIN2BCD(tmp);

	switch (ds1307->type) {
	case ds_1337:
	case ds_1339:
		buf[DS1307_REG_MONTH] |= DS1337_BIT_CENTURY;
		break;
	case ds_1340:
		buf[DS1307_REG_HOUR] |= DS1340_BIT_CENTURY_EN
				| DS1340_BIT_CENTURY;
		break;
	default:
		break;
	}

	ds1307->msg[1].flags = 0;
	ds1307->msg[1].len = 8;

	dev_dbg(dev, "%s: %02x %02x %02x %02x %02x %02x %02x\n",
		"write", buf[0], buf[1], buf[2], buf[3],
		buf[4], buf[5], buf[6]);

	result = i2c_transfer(to_i2c_adapter(ds1307->client->dev.parent),
			&ds1307->msg[1], 1);
	if (result != 1) {
		dev_err(dev, "%s error %d\n", "write", tmp);
		return -EIO;
	}
	return 0;
}

static const struct rtc_class_ops ds13xx_rtc_ops = {
	.read_time	= ds1307_get_time,
	.set_time	= ds1307_set_time,
};

static struct i2c_driver ds1307_driver;

static int __devinit ds1307_probe(struct i2c_client *client)
{
	struct ds1307		*ds1307;
	int			err = -ENODEV;
	int			tmp;
	const struct chip_desc	*chip;
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);

	chip = find_chip(client->name);
	if (!chip) {
		dev_err(&client->dev, "unknown chip type '%s'\n",
				client->name);
		return -ENODEV;
	}

	if (!i2c_check_functionality(adapter,
			I2C_FUNC_I2C | I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -EIO;

	if (!(ds1307 = kzalloc(sizeof(struct ds1307), GFP_KERNEL)))
		return -ENOMEM;

	ds1307->client = client;
	i2c_set_clientdata(client, ds1307);

	ds1307->msg[0].addr = client->addr;
	ds1307->msg[0].flags = 0;
	ds1307->msg[0].len = 1;
	ds1307->msg[0].buf = &ds1307->reg_addr;

	ds1307->msg[1].addr = client->addr;
	ds1307->msg[1].flags = I2C_M_RD;
	ds1307->msg[1].len = sizeof(ds1307->regs);
	ds1307->msg[1].buf = ds1307->regs;

	ds1307->type = chip->type;

	switch (ds1307->type) {
	case ds_1337:
	case ds_1339:
		ds1307->reg_addr = DS1337_REG_CONTROL;
		ds1307->msg[1].len = 2;

		/* get registers that the "rtc" read below won't read... */
		tmp = i2c_transfer(adapter, ds1307->msg, 2);
		if (tmp != 2) {
			pr_debug("read error %d\n", tmp);
			err = -EIO;
			goto exit_free;
		}

		ds1307->reg_addr = 0;
		ds1307->msg[1].len = sizeof(ds1307->regs);

		/* oscillator off?  turn it on, so clock can tick. */
		if (ds1307->regs[0] & DS1337_BIT_nEOSC)
			i2c_smbus_write_byte_data(client, DS1337_REG_CONTROL,
				ds1307->regs[0] & ~DS1337_BIT_nEOSC);

		/* oscillator fault?  clear flag, and warn */
		if (ds1307->regs[1] & DS1337_BIT_OSF) {
			i2c_smbus_write_byte_data(client, DS1337_REG_STATUS,
				ds1307->regs[1] & ~DS1337_BIT_OSF);
			dev_warn(&client->dev, "SET TIME!\n");
		}
		break;
	default:
		break;
	}

read_rtc:
	/* read RTC registers */

	tmp = i2c_transfer(adapter, ds1307->msg, 2);
	if (tmp != 2) {
		pr_debug("read error %d\n", tmp);
		err = -EIO;
		goto exit_free;
	}

	/* minimal sanity checking; some chips (like DS1340) don't
	 * specify the extra bits as must-be-zero, but there are
	 * still a few values that are clearly out-of-range.
	 */
	tmp = ds1307->regs[DS1307_REG_SECS];
	switch (ds1307->type) {
	case ds_1340:
		/* FIXME read register with DS1340_BIT_OSF, use that to
		 * trigger the "set time" warning (*after* restarting the
		 * oscillator!) instead of this weaker ds1307/m41t00 test.
		 */
	case ds_1307:
	case m41t00:
		/* clock halted?  turn it on, so clock can tick. */
		if (tmp & DS1307_BIT_CH) {
			i2c_smbus_write_byte_data(client, DS1307_REG_SECS, 0);
			dev_warn(&client->dev, "SET TIME!\n");
			goto read_rtc;
		}
		break;
	case ds_1338:
		/* clock halted?  turn it on, so clock can tick. */
		if (tmp & DS1307_BIT_CH)
			i2c_smbus_write_byte_data(client, DS1307_REG_SECS, 0);

		/* oscillator fault?  clear flag, and warn */
		if (ds1307->regs[DS1307_REG_CONTROL] & DS1338_BIT_OSF) {
			i2c_smbus_write_byte_data(client, DS1307_REG_CONTROL,
					ds1307->regs[DS1337_REG_CONTROL]
					& ~DS1338_BIT_OSF);
			dev_warn(&client->dev, "SET TIME!\n");
			goto read_rtc;
		}
		break;
	case ds_1337:
	case ds_1339:
		break;
	}

	tmp = ds1307->regs[DS1307_REG_SECS];
	tmp = BCD2BIN(tmp & 0x7f);
	if (tmp > 60)
		goto exit_bad;
	tmp = BCD2BIN(ds1307->regs[DS1307_REG_MIN] & 0x7f);
	if (tmp > 60)
		goto exit_bad;

	tmp = BCD2BIN(ds1307->regs[DS1307_REG_MDAY] & 0x3f);
	if (tmp == 0 || tmp > 31)
		goto exit_bad;

	tmp = BCD2BIN(ds1307->regs[DS1307_REG_MONTH] & 0x1f);
	if (tmp == 0 || tmp > 12)
		goto exit_bad;

	tmp = ds1307->regs[DS1307_REG_HOUR];
	switch (ds1307->type) {
	case ds_1340:
	case m41t00:
		/* NOTE: ignores century bits; fix before deploying
		 * systems that will run through year 2100.
		 */
		break;
	default:
		if (!(tmp & DS1307_BIT_12HR))
			break;

		/* Be sure we're in 24 hour mode.  Multi-master systems
		 * take note...
		 */
		tmp = BCD2BIN(tmp & 0x1f);
		if (tmp == 12)
			tmp = 0;
		if (ds1307->regs[DS1307_REG_HOUR] & DS1307_BIT_PM)
			tmp += 12;
		i2c_smbus_write_byte_data(client,
				DS1307_REG_HOUR,
				BIN2BCD(tmp));
	}

	ds1307->rtc = rtc_device_register(client->name, &client->dev,
				&ds13xx_rtc_ops, THIS_MODULE);
	if (IS_ERR(ds1307->rtc)) {
		err = PTR_ERR(ds1307->rtc);
		dev_err(&client->dev,
			"unable to register the class device\n");
		goto exit_free;
	}

	return 0;

exit_bad:
	dev_dbg(&client->dev, "%s: %02x %02x %02x %02x %02x %02x %02x\n",
			"bogus register",
			ds1307->regs[0], ds1307->regs[1],
			ds1307->regs[2], ds1307->regs[3],
			ds1307->regs[4], ds1307->regs[5],
			ds1307->regs[6]);

exit_free:
	kfree(ds1307);
	return err;
}

static int __devexit ds1307_remove(struct i2c_client *client)
{
	struct ds1307	*ds1307 = i2c_get_clientdata(client);

	rtc_device_unregister(ds1307->rtc);
	kfree(ds1307);
	return 0;
}

static struct i2c_driver ds1307_driver = {
	.driver = {
		.name	= "rtc-ds1307",
		.owner	= THIS_MODULE,
	},
	.probe		= ds1307_probe,
	.remove		= __devexit_p(ds1307_remove),
};

static int __init ds1307_init(void)
{
	return i2c_add_driver(&ds1307_driver);
}
module_init(ds1307_init);

static void __exit ds1307_exit(void)
{
	i2c_del_driver(&ds1307_driver);
}
module_exit(ds1307_exit);

MODULE_DESCRIPTION("RTC driver for DS1307 and similar chips");
MODULE_LICENSE("GPL");

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
 * If the I2C "force" mechanism is used, we assume the chip is a ds1337.
 * (Much better would be board-specific tables of I2C devices, along with
 * the platform_data drivers would use to sort such issues out.)
 */
enum ds_type {
	unknown = 0,
	ds_1307,		/* or ds1338, ... */
	ds_1337,		/* or ds1339, ... */
	ds_1340,		/* or st m41t00, ... */
	// rs5c372 too?  different address...
};

static unsigned short normal_i2c[] = { 0x68, I2C_CLIENT_END };

I2C_CLIENT_INSMOD;



/* RTC registers don't differ much, except for the century flag */
#define DS1307_REG_SECS		0x00	/* 00-59 */
#	define DS1307_BIT_CH		0x80
#define DS1307_REG_MIN		0x01	/* 00-59 */
#define DS1307_REG_HOUR		0x02	/* 00-23, or 1-12{am,pm} */
#	define DS1340_BIT_CENTURY_EN	0x80	/* in REG_HOUR */
#	define DS1340_BIT_CENTURY	0x40	/* in REG_HOUR */
#define DS1307_REG_WDAY		0x03	/* 01-07 */
#define DS1307_REG_MDAY		0x04	/* 01-31 */
#define DS1307_REG_MONTH	0x05	/* 01-12 */
#	define DS1337_BIT_CENTURY	0x80	/* in REG_MONTH */
#define DS1307_REG_YEAR		0x06	/* 00-99 */

/* Other registers (control, status, alarms, trickle charge, NVRAM, etc)
 * start at 7, and they differ a lot. Only control and status matter for RTC;
 * be careful using them.
 */
#define DS1307_REG_CONTROL	0x07
#	define DS1307_BIT_OUT		0x80
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
	struct i2c_client	client;
	struct rtc_device	*rtc;
};


static int ds1307_get_time(struct device *dev, struct rtc_time *t)
{
	struct ds1307	*ds1307 = dev_get_drvdata(dev);
	int		tmp;

	/* read the RTC registers all at once */
	ds1307->msg[1].flags = I2C_M_RD;
	ds1307->msg[1].len = 7;

	tmp = i2c_transfer(ds1307->client.adapter, ds1307->msg, 2);
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

	return 0;
}

static int ds1307_set_time(struct device *dev, struct rtc_time *t)
{
	struct ds1307	*ds1307 = dev_get_drvdata(dev);
	int		result;
	int		tmp;
	u8		*buf = ds1307->regs;

	dev_dbg(dev, "%s secs=%d, mins=%d, "
		"hours=%d, mday=%d, mon=%d, year=%d, wday=%d\n",
		"write", dt->tm_sec, dt->tm_min,
		dt->tm_hour, dt->tm_mday,
		dt->tm_mon, dt->tm_year, dt->tm_wday);

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

	if (ds1307->type == ds_1337)
		buf[DS1307_REG_MONTH] |= DS1337_BIT_CENTURY;
	else if (ds1307->type == ds_1340)
		buf[DS1307_REG_HOUR] |= DS1340_BIT_CENTURY_EN
				| DS1340_BIT_CENTURY;

	ds1307->msg[1].flags = 0;
	ds1307->msg[1].len = 8;

	dev_dbg(dev, "%s: %02x %02x %02x %02x %02x %02x %02x\n",
		"write", buf[0], buf[1], buf[2], buf[3],
		buf[4], buf[5], buf[6]);

	result = i2c_transfer(ds1307->client.adapter, &ds1307->msg[1], 1);
	if (result != 1) {
		dev_err(dev, "%s error %d\n", "write", tmp);
		return -EIO;
	}
	return 0;
}

static struct rtc_class_ops ds13xx_rtc_ops = {
	.read_time	= ds1307_get_time,
	.set_time	= ds1307_set_time,
};

static struct i2c_driver ds1307_driver;

static int __devinit
ds1307_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct ds1307		*ds1307;
	int			err = -ENODEV;
	struct i2c_client	*client;
	int			tmp;

	if (!(ds1307 = kzalloc(sizeof(struct ds1307), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	client = &ds1307->client;
	client->addr = address;
	client->adapter = adapter;
	client->driver = &ds1307_driver;
	client->flags = 0;

	i2c_set_clientdata(client, ds1307);

	ds1307->msg[0].addr = client->addr;
	ds1307->msg[0].flags = 0;
	ds1307->msg[0].len = 1;
	ds1307->msg[0].buf = &ds1307->reg_addr;

	ds1307->msg[1].addr = client->addr;
	ds1307->msg[1].flags = I2C_M_RD;
	ds1307->msg[1].len = sizeof(ds1307->regs);
	ds1307->msg[1].buf = ds1307->regs;

	/* HACK: "force" implies "needs ds1337-style-oscillator setup" */
	if (kind >= 0) {
		ds1307->type = ds_1337;

		ds1307->reg_addr = DS1337_REG_CONTROL;
		ds1307->msg[1].len = 2;

		tmp = i2c_transfer(client->adapter, ds1307->msg, 2);
		if (tmp != 2) {
			pr_debug("read error %d\n", tmp);
			err = -EIO;
			goto exit_free;
		}

		ds1307->reg_addr = 0;
		ds1307->msg[1].len = sizeof(ds1307->regs);

		/* oscillator is off; need to turn it on */
		if ((ds1307->regs[0] & DS1337_BIT_nEOSC)
				|| (ds1307->regs[1] & DS1337_BIT_OSF)) {
			printk(KERN_ERR "no ds1337 oscillator code\n");
			goto exit_free;
		}
	} else
		ds1307->type = ds_1307;

read_rtc:
	/* read RTC registers */

	tmp = i2c_transfer(client->adapter, ds1307->msg, 2);
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
	if (tmp & DS1307_BIT_CH) {
		if (ds1307->type && ds1307->type != ds_1307) {
			pr_debug("not a ds1307?\n");
			goto exit_free;
		}
		ds1307->type = ds_1307;

		/* this partial initialization should work for ds1307,
		 * ds1338, ds1340, st m41t00, and more.
		 */
		dev_warn(&client->dev, "oscillator started; SET TIME!\n");
		i2c_smbus_write_byte_data(client, 0, 0);
		goto read_rtc;
	}
	tmp = BCD2BIN(tmp & 0x7f);
	if (tmp > 60)
		goto exit_free;
	tmp = BCD2BIN(ds1307->regs[DS1307_REG_MIN] & 0x7f);
	if (tmp > 60)
		goto exit_free;

	tmp = BCD2BIN(ds1307->regs[DS1307_REG_MDAY] & 0x3f);
	if (tmp == 0 || tmp > 31)
		goto exit_free;

	tmp = BCD2BIN(ds1307->regs[DS1307_REG_MONTH] & 0x1f);
	if (tmp == 0 || tmp > 12)
		goto exit_free;

	/* force into in 24 hour mode (most chips) or
	 * disable century bit (ds1340)
	 */
	tmp = ds1307->regs[DS1307_REG_HOUR];
	if (tmp & (1 << 6)) {
		if (tmp & (1 << 5))
			tmp = BCD2BIN(tmp & 0x1f) + 12;
		else
			tmp = BCD2BIN(tmp);
		i2c_smbus_write_byte_data(client,
				DS1307_REG_HOUR,
				BIN2BCD(tmp));
	}

	/* FIXME chips like 1337 can generate alarm irqs too; those are
	 * worth exposing through the API (especially when the irq is
	 * wakeup-capable).
	 */

	switch (ds1307->type) {
	case unknown:
		strlcpy(client->name, "unknown", I2C_NAME_SIZE);
		break;
	case ds_1307:
		strlcpy(client->name, "ds1307", I2C_NAME_SIZE);
		break;
	case ds_1337:
		strlcpy(client->name, "ds1337", I2C_NAME_SIZE);
		break;
	case ds_1340:
		strlcpy(client->name, "ds1340", I2C_NAME_SIZE);
		break;
	}

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(client)))
		goto exit_free;

	ds1307->rtc = rtc_device_register(client->name, &client->dev,
				&ds13xx_rtc_ops, THIS_MODULE);
	if (IS_ERR(ds1307->rtc)) {
		err = PTR_ERR(ds1307->rtc);
		dev_err(&client->dev,
			"unable to register the class device\n");
		goto exit_detach;
	}

	return 0;

exit_detach:
	i2c_detach_client(client);
exit_free:
	kfree(ds1307);
exit:
	return err;
}

static int __devinit
ds1307_attach_adapter(struct i2c_adapter *adapter)
{
	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C))
		return 0;
	return i2c_probe(adapter, &addr_data, ds1307_detect);
}

static int __devexit ds1307_detach_client(struct i2c_client *client)
{
	int		err;
	struct ds1307	*ds1307 = i2c_get_clientdata(client);

	rtc_device_unregister(ds1307->rtc);
	if ((err = i2c_detach_client(client)))
		return err;
	kfree(ds1307);
	return 0;
}

static struct i2c_driver ds1307_driver = {
	.driver = {
		.name	= "ds1307",
		.owner	= THIS_MODULE,
	},
	.attach_adapter	= ds1307_attach_adapter,
	.detach_client	= __devexit_p(ds1307_detach_client),
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

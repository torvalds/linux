/*
 * An I2C driver for the Philips PCF8563 RTC
 * Copyright 2005-06 Tower Technologies
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 * Maintainers: http://www.nslu2-linux.org/
 *
 * based on the other drivers in this same directory.
 *
 * http://www.semiconductors.philips.com/acrobat/datasheets/PCF8563-04.pdf
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>

#define DRV_VERSION "0.4.3"

#define PCF8563_REG_ST1		0x00 /* status */
#define PCF8563_REG_ST2		0x01

#define PCF8563_REG_SC		0x02 /* datetime */
#define PCF8563_REG_MN		0x03
#define PCF8563_REG_HR		0x04
#define PCF8563_REG_DM		0x05
#define PCF8563_REG_DW		0x06
#define PCF8563_REG_MO		0x07
#define PCF8563_REG_YR		0x08

#define PCF8563_REG_AMN		0x09 /* alarm */
#define PCF8563_REG_AHR		0x0A
#define PCF8563_REG_ADM		0x0B
#define PCF8563_REG_ADW		0x0C

#define PCF8563_REG_CLKO	0x0D /* clock out */
#define PCF8563_REG_TMRC	0x0E /* timer control */
#define PCF8563_REG_TMR		0x0F /* timer */

#define PCF8563_SC_LV		0x80 /* low voltage */
#define PCF8563_MO_C		0x80 /* century */

static struct i2c_driver pcf8563_driver;

struct pcf8563 {
	struct rtc_device *rtc;
	/*
	 * The meaning of MO_C bit varies by the chip type.
	 * From PCF8563 datasheet: this bit is toggled when the years
	 * register overflows from 99 to 00
	 *   0 indicates the century is 20xx
	 *   1 indicates the century is 19xx
	 * From RTC8564 datasheet: this bit indicates change of
	 * century. When the year digit data overflows from 99 to 00,
	 * this bit is set. By presetting it to 0 while still in the
	 * 20th century, it will be set in year 2000, ...
	 * There seems no reliable way to know how the system use this
	 * bit.  So let's do it heuristically, assuming we are live in
	 * 1970...2069.
	 */
	int c_polarity;	/* 0: MO_C=1 means 19xx, otherwise MO_C=1 means 20xx */
};

/*
 * In the routines that deal directly with the pcf8563 hardware, we use
 * rtc_time -- month 0-11, hour 0-23, yr = calendar year-epoch.
 */
static int pcf8563_get_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct pcf8563 *pcf8563 = i2c_get_clientdata(client);
	unsigned char buf[13] = { PCF8563_REG_ST1 };

	struct i2c_msg msgs[] = {
		{ client->addr, 0, 1, buf },	/* setup read ptr */
		{ client->addr, I2C_M_RD, 13, buf },	/* read status + date */
	};

	/* read registers */
	if ((i2c_transfer(client->adapter, msgs, 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __func__);
		return -EIO;
	}

	if (buf[PCF8563_REG_SC] & PCF8563_SC_LV)
		dev_info(&client->dev,
			"low voltage detected, date/time is not reliable.\n");

	dev_dbg(&client->dev,
		"%s: raw data is st1=%02x, st2=%02x, sec=%02x, min=%02x, hr=%02x, "
		"mday=%02x, wday=%02x, mon=%02x, year=%02x\n",
		__func__,
		buf[0], buf[1], buf[2], buf[3],
		buf[4], buf[5], buf[6], buf[7],
		buf[8]);


	tm->tm_sec = BCD2BIN(buf[PCF8563_REG_SC] & 0x7F);
	tm->tm_min = BCD2BIN(buf[PCF8563_REG_MN] & 0x7F);
	tm->tm_hour = BCD2BIN(buf[PCF8563_REG_HR] & 0x3F); /* rtc hr 0-23 */
	tm->tm_mday = BCD2BIN(buf[PCF8563_REG_DM] & 0x3F);
	tm->tm_wday = buf[PCF8563_REG_DW] & 0x07;
	tm->tm_mon = BCD2BIN(buf[PCF8563_REG_MO] & 0x1F) - 1; /* rtc mn 1-12 */
	tm->tm_year = BCD2BIN(buf[PCF8563_REG_YR]);
	if (tm->tm_year < 70)
		tm->tm_year += 100;	/* assume we are in 1970...2069 */
	/* detect the polarity heuristically. see note above. */
	pcf8563->c_polarity = (buf[PCF8563_REG_MO] & PCF8563_MO_C) ?
		(tm->tm_year >= 100) : (tm->tm_year < 100);

	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	/* the clock can give out invalid datetime, but we cannot return
	 * -EINVAL otherwise hwclock will refuse to set the time on bootup.
	 */
	if (rtc_valid_tm(tm) < 0)
		dev_err(&client->dev, "retrieved date/time is not valid.\n");

	return 0;
}

static int pcf8563_set_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct pcf8563 *pcf8563 = i2c_get_clientdata(client);
	int i, err;
	unsigned char buf[9];

	dev_dbg(&client->dev, "%s: secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	/* hours, minutes and seconds */
	buf[PCF8563_REG_SC] = BIN2BCD(tm->tm_sec);
	buf[PCF8563_REG_MN] = BIN2BCD(tm->tm_min);
	buf[PCF8563_REG_HR] = BIN2BCD(tm->tm_hour);

	buf[PCF8563_REG_DM] = BIN2BCD(tm->tm_mday);

	/* month, 1 - 12 */
	buf[PCF8563_REG_MO] = BIN2BCD(tm->tm_mon + 1);

	/* year and century */
	buf[PCF8563_REG_YR] = BIN2BCD(tm->tm_year % 100);
	if (pcf8563->c_polarity ? (tm->tm_year >= 100) : (tm->tm_year < 100))
		buf[PCF8563_REG_MO] |= PCF8563_MO_C;

	buf[PCF8563_REG_DW] = tm->tm_wday & 0x07;

	/* write register's data */
	for (i = 0; i < 7; i++) {
		unsigned char data[2] = { PCF8563_REG_SC + i,
						buf[PCF8563_REG_SC + i] };

		err = i2c_master_send(client, data, sizeof(data));
		if (err != sizeof(data)) {
			dev_err(&client->dev,
				"%s: err=%d addr=%02x, data=%02x\n",
				__func__, err, data[0], data[1]);
			return -EIO;
		}
	};

	return 0;
}

struct pcf8563_limit
{
	unsigned char reg;
	unsigned char mask;
	unsigned char min;
	unsigned char max;
};

static int pcf8563_validate_client(struct i2c_client *client)
{
	int i;

	static const struct pcf8563_limit pattern[] = {
		/* register, mask, min, max */
		{ PCF8563_REG_SC,	0x7F,	0,	59	},
		{ PCF8563_REG_MN,	0x7F,	0,	59	},
		{ PCF8563_REG_HR,	0x3F,	0,	23	},
		{ PCF8563_REG_DM,	0x3F,	0,	31	},
		{ PCF8563_REG_MO,	0x1F,	0,	12	},
	};

	/* check limits (only registers with bcd values) */
	for (i = 0; i < ARRAY_SIZE(pattern); i++) {
		int xfer;
		unsigned char value;
		unsigned char buf = pattern[i].reg;

		struct i2c_msg msgs[] = {
			{ client->addr, 0, 1, &buf },
			{ client->addr, I2C_M_RD, 1, &buf },
		};

		xfer = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));

		if (xfer != ARRAY_SIZE(msgs)) {
			dev_err(&client->dev,
				"%s: could not read register 0x%02X\n",
				__func__, pattern[i].reg);

			return -EIO;
		}

		value = BCD2BIN(buf & pattern[i].mask);

		if (value > pattern[i].max ||
			value < pattern[i].min) {
			dev_dbg(&client->dev,
				"%s: pattern=%d, reg=%x, mask=0x%02x, min=%d, "
				"max=%d, value=%d, raw=0x%02X\n",
				__func__, i, pattern[i].reg, pattern[i].mask,
				pattern[i].min, pattern[i].max,
				value, buf);

			return -ENODEV;
		}
	}

	return 0;
}

static int pcf8563_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return pcf8563_get_datetime(to_i2c_client(dev), tm);
}

static int pcf8563_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return pcf8563_set_datetime(to_i2c_client(dev), tm);
}

static const struct rtc_class_ops pcf8563_rtc_ops = {
	.read_time	= pcf8563_rtc_read_time,
	.set_time	= pcf8563_rtc_set_time,
};

static int pcf8563_probe(struct i2c_client *client)
{
	struct pcf8563 *pcf8563;

	int err = 0;

	dev_dbg(&client->dev, "%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	pcf8563 = kzalloc(sizeof(struct pcf8563), GFP_KERNEL);
	if (!pcf8563)
		return -ENOMEM;

	/* Verify the chip is really an PCF8563 */
	if (pcf8563_validate_client(client) < 0) {
		err = -ENODEV;
		goto exit_kfree;
	}

	dev_info(&client->dev, "chip found, driver version " DRV_VERSION "\n");

	pcf8563->rtc = rtc_device_register(pcf8563_driver.driver.name,
				&client->dev, &pcf8563_rtc_ops, THIS_MODULE);

	if (IS_ERR(pcf8563->rtc)) {
		err = PTR_ERR(pcf8563->rtc);
		goto exit_kfree;
	}

	i2c_set_clientdata(client, pcf8563);

	return 0;

exit_kfree:
	kfree(pcf8563);

	return err;
}

static int pcf8563_remove(struct i2c_client *client)
{
	struct pcf8563 *pcf8563 = i2c_get_clientdata(client);

	if (pcf8563->rtc)
		rtc_device_unregister(pcf8563->rtc);

	kfree(pcf8563);

	return 0;
}

static struct i2c_driver pcf8563_driver = {
	.driver		= {
		.name	= "rtc-pcf8563",
	},
	.probe		= pcf8563_probe,
	.remove		= pcf8563_remove,
};

static int __init pcf8563_init(void)
{
	return i2c_add_driver(&pcf8563_driver);
}

static void __exit pcf8563_exit(void)
{
	i2c_del_driver(&pcf8563_driver);
}

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("Philips PCF8563/Epson RTC8564 RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

module_init(pcf8563_init);
module_exit(pcf8563_exit);

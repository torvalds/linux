/*
 * An I2C driver for the Epson RX8581 RTC
 *
 * Author: Martyn Welch <martyn.welch@ge.com>
 * Copyright 2008 GE Intelligent Platforms Embedded Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on: rtc-pcf8563.c (An I2C driver for the Philips PCF8563 RTC)
 * Copyright 2005-06 Tower Technologies
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/log2.h>

#define DRV_VERSION "0.1"

#define RX8581_REG_SC		0x00 /* Second in BCD */
#define RX8581_REG_MN		0x01 /* Minute in BCD */
#define RX8581_REG_HR		0x02 /* Hour in BCD */
#define RX8581_REG_DW		0x03 /* Day of Week */
#define RX8581_REG_DM		0x04 /* Day of Month in BCD */
#define RX8581_REG_MO		0x05 /* Month in BCD */
#define RX8581_REG_YR		0x06 /* Year in BCD */
#define RX8581_REG_RAM		0x07 /* RAM */
#define RX8581_REG_AMN		0x08 /* Alarm Min in BCD*/
#define RX8581_REG_AHR		0x09 /* Alarm Hour in BCD */
#define RX8581_REG_ADM		0x0A
#define RX8581_REG_ADW		0x0A
#define RX8581_REG_TMR0		0x0B
#define RX8581_REG_TMR1		0x0C
#define RX8581_REG_EXT		0x0D /* Extension Register */
#define RX8581_REG_FLAG		0x0E /* Flag Register */
#define RX8581_REG_CTRL		0x0F /* Control Register */


/* Flag Register bit definitions */
#define RX8581_FLAG_UF		0x20 /* Update */
#define RX8581_FLAG_TF		0x10 /* Timer */
#define RX8581_FLAG_AF		0x08 /* Alarm */
#define RX8581_FLAG_VLF		0x02 /* Voltage Low */

/* Control Register bit definitions */
#define RX8581_CTRL_UIE		0x20 /* Update Interrupt Enable */
#define RX8581_CTRL_TIE		0x10 /* Timer Interrupt Enable */
#define RX8581_CTRL_AIE		0x08 /* Alarm Interrupt Enable */
#define RX8581_CTRL_STOP	0x02 /* STOP bit */
#define RX8581_CTRL_RESET	0x01 /* RESET bit */

struct rx8581 {
	struct i2c_client	*client;
	struct rtc_device	*rtc;
	s32 (*read_block_data)(const struct i2c_client *client, u8 command,
				u8 length, u8 *values);
	s32 (*write_block_data)(const struct i2c_client *client, u8 command,
				u8 length, const u8 *values);
};

static struct i2c_driver rx8581_driver;

static int rx8581_read_block_data(const struct i2c_client *client, u8 command,
					u8 length, u8 *values)
{
	s32 i, data;

	for (i = 0; i < length; i++) {
		data = i2c_smbus_read_byte_data(client, command + i);
		if (data < 0)
			return data;
		values[i] = data;
	}
	return i;
}

static int rx8581_write_block_data(const struct i2c_client *client, u8 command,
					u8 length, const u8 *values)
{
	s32 i, ret;

	for (i = 0; i < length; i++) {
		ret = i2c_smbus_write_byte_data(client, command + i,
						values[i]);
		if (ret < 0)
			return ret;
	}
	return length;
}

/*
 * In the routines that deal directly with the rx8581 hardware, we use
 * rtc_time -- month 0-11, hour 0-23, yr = calendar year-epoch.
 */
static int rx8581_get_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	unsigned char date[7];
	int data, err;
	struct rx8581 *rx8581 = i2c_get_clientdata(client);

	/* First we ensure that the "update flag" is not set, we read the
	 * time and date then re-read the "update flag". If the update flag
	 * has been set, we know that the time has changed during the read so
	 * we repeat the whole process again.
	 */
	data = i2c_smbus_read_byte_data(client, RX8581_REG_FLAG);
	if (data < 0) {
		dev_err(&client->dev, "Unable to read device flags\n");
		return -EIO;
	}

	do {
		/* If update flag set, clear it */
		if (data & RX8581_FLAG_UF) {
			err = i2c_smbus_write_byte_data(client,
				RX8581_REG_FLAG, (data & ~RX8581_FLAG_UF));
			if (err != 0) {
				dev_err(&client->dev, "Unable to write device flags\n");
				return -EIO;
			}
		}

		/* Now read time and date */
		err = rx8581->read_block_data(client, RX8581_REG_SC,
			7, date);
		if (err < 0) {
			dev_err(&client->dev, "Unable to read date\n");
			return -EIO;
		}

		/* Check flag register */
		data = i2c_smbus_read_byte_data(client, RX8581_REG_FLAG);
		if (data < 0) {
			dev_err(&client->dev, "Unable to read device flags\n");
			return -EIO;
		}
	} while (data & RX8581_FLAG_UF);

	if (data & RX8581_FLAG_VLF)
		dev_info(&client->dev,
			"low voltage detected, date/time is not reliable.\n");

	dev_dbg(&client->dev,
		"%s: raw data is sec=%02x, min=%02x, hr=%02x, "
		"wday=%02x, mday=%02x, mon=%02x, year=%02x\n",
		__func__,
		date[0], date[1], date[2], date[3], date[4], date[5], date[6]);

	tm->tm_sec = bcd2bin(date[RX8581_REG_SC] & 0x7F);
	tm->tm_min = bcd2bin(date[RX8581_REG_MN] & 0x7F);
	tm->tm_hour = bcd2bin(date[RX8581_REG_HR] & 0x3F); /* rtc hr 0-23 */
	tm->tm_wday = ilog2(date[RX8581_REG_DW] & 0x7F);
	tm->tm_mday = bcd2bin(date[RX8581_REG_DM] & 0x3F);
	tm->tm_mon = bcd2bin(date[RX8581_REG_MO] & 0x1F) - 1; /* rtc mn 1-12 */
	tm->tm_year = bcd2bin(date[RX8581_REG_YR]);
	if (tm->tm_year < 70)
		tm->tm_year += 100;	/* assume we are in 1970...2069 */


	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	err = rtc_valid_tm(tm);
	if (err < 0)
		dev_err(&client->dev, "retrieved date/time is not valid.\n");

	return err;
}

static int rx8581_set_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	int data, err;
	unsigned char buf[7];
	struct rx8581 *rx8581 = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "%s: secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	/* hours, minutes and seconds */
	buf[RX8581_REG_SC] = bin2bcd(tm->tm_sec);
	buf[RX8581_REG_MN] = bin2bcd(tm->tm_min);
	buf[RX8581_REG_HR] = bin2bcd(tm->tm_hour);

	buf[RX8581_REG_DM] = bin2bcd(tm->tm_mday);

	/* month, 1 - 12 */
	buf[RX8581_REG_MO] = bin2bcd(tm->tm_mon + 1);

	/* year and century */
	buf[RX8581_REG_YR] = bin2bcd(tm->tm_year % 100);
	buf[RX8581_REG_DW] = (0x1 << tm->tm_wday);

	/* Stop the clock */
	data = i2c_smbus_read_byte_data(client, RX8581_REG_CTRL);
	if (data < 0) {
		dev_err(&client->dev, "Unable to read control register\n");
		return -EIO;
	}

	err = i2c_smbus_write_byte_data(client, RX8581_REG_CTRL,
		(data | RX8581_CTRL_STOP));
	if (err < 0) {
		dev_err(&client->dev, "Unable to write control register\n");
		return -EIO;
	}

	/* write register's data */
	err = rx8581->write_block_data(client, RX8581_REG_SC, 7, buf);
	if (err < 0) {
		dev_err(&client->dev, "Unable to write to date registers\n");
		return -EIO;
	}

	/* get VLF and clear it */
	data = i2c_smbus_read_byte_data(client, RX8581_REG_FLAG);
	if (data < 0) {
		dev_err(&client->dev, "Unable to read flag register\n");
		return -EIO;
	}

	err = i2c_smbus_write_byte_data(client, RX8581_REG_FLAG,
		(data & ~(RX8581_FLAG_VLF)));
	if (err != 0) {
		dev_err(&client->dev, "Unable to write flag register\n");
		return -EIO;
	}

	/* Restart the clock */
	data = i2c_smbus_read_byte_data(client, RX8581_REG_CTRL);
	if (data < 0) {
		dev_err(&client->dev, "Unable to read control register\n");
		return -EIO;
	}

	err = i2c_smbus_write_byte_data(client, RX8581_REG_CTRL,
		(data & ~(RX8581_CTRL_STOP)));
	if (err != 0) {
		dev_err(&client->dev, "Unable to write control register\n");
		return -EIO;
	}

	return 0;
}

static int rx8581_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return rx8581_get_datetime(to_i2c_client(dev), tm);
}

static int rx8581_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return rx8581_set_datetime(to_i2c_client(dev), tm);
}

static const struct rtc_class_ops rx8581_rtc_ops = {
	.read_time	= rx8581_rtc_read_time,
	.set_time	= rx8581_rtc_set_time,
};

static int rx8581_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct rx8581	  *rx8581;

	dev_dbg(&client->dev, "%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)
		&& !i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_I2C_BLOCK))
		return -EIO;

	rx8581 = devm_kzalloc(&client->dev, sizeof(struct rx8581), GFP_KERNEL);
	if (!rx8581)
		return -ENOMEM;

	i2c_set_clientdata(client, rx8581);
	rx8581->client = client;

	if (i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_I2C_BLOCK)) {
		rx8581->read_block_data = i2c_smbus_read_i2c_block_data;
		rx8581->write_block_data = i2c_smbus_write_i2c_block_data;
	} else {
		rx8581->read_block_data = rx8581_read_block_data;
		rx8581->write_block_data = rx8581_write_block_data;
	}

	dev_info(&client->dev, "chip found, driver version " DRV_VERSION "\n");

	rx8581->rtc = devm_rtc_device_register(&client->dev,
		rx8581_driver.driver.name, &rx8581_rtc_ops, THIS_MODULE);

	if (IS_ERR(rx8581->rtc)) {
		dev_err(&client->dev,
			"unable to register the class device\n");
		return PTR_ERR(rx8581->rtc);
	}

	return 0;
}

static const struct i2c_device_id rx8581_id[] = {
	{ "rx8581", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rx8581_id);

static struct i2c_driver rx8581_driver = {
	.driver		= {
		.name	= "rtc-rx8581",
		.owner	= THIS_MODULE,
	},
	.probe		= rx8581_probe,
	.id_table	= rx8581_id,
};

module_i2c_driver(rx8581_driver);

MODULE_AUTHOR("Martyn Welch <martyn.welch@ge.com>");
MODULE_DESCRIPTION("Epson RX-8581 RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

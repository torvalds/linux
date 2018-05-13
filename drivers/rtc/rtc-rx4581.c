/* drivers/rtc/rtc-rx4581.c
 *
 * written by Torben Hohn <torbenh@linutronix.de>
 *
 * Based on:
 * drivers/rtc/rtc-max6902.c
 *
 * Copyright (C) 2006 8D Technologies inc.
 * Copyright (C) 2004 Compulab Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Driver for MAX6902 spi RTC
 *
 * and based on:
 * drivers/rtc/rtc-rx8581.c
 *
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
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/rtc.h>
#include <linux/spi/spi.h>
#include <linux/bcd.h>

#define RX4581_REG_SC		0x00 /* Second in BCD */
#define RX4581_REG_MN		0x01 /* Minute in BCD */
#define RX4581_REG_HR		0x02 /* Hour in BCD */
#define RX4581_REG_DW		0x03 /* Day of Week */
#define RX4581_REG_DM		0x04 /* Day of Month in BCD */
#define RX4581_REG_MO		0x05 /* Month in BCD */
#define RX4581_REG_YR		0x06 /* Year in BCD */
#define RX4581_REG_RAM		0x07 /* RAM */
#define RX4581_REG_AMN		0x08 /* Alarm Min in BCD*/
#define RX4581_REG_AHR		0x09 /* Alarm Hour in BCD */
#define RX4581_REG_ADM		0x0A
#define RX4581_REG_ADW		0x0A
#define RX4581_REG_TMR0		0x0B
#define RX4581_REG_TMR1		0x0C
#define RX4581_REG_EXT		0x0D /* Extension Register */
#define RX4581_REG_FLAG		0x0E /* Flag Register */
#define RX4581_REG_CTRL		0x0F /* Control Register */


/* Flag Register bit definitions */
#define RX4581_FLAG_UF		0x20 /* Update */
#define RX4581_FLAG_TF		0x10 /* Timer */
#define RX4581_FLAG_AF		0x08 /* Alarm */
#define RX4581_FLAG_VLF		0x02 /* Voltage Low */

/* Control Register bit definitions */
#define RX4581_CTRL_UIE		0x20 /* Update Interrupt Enable */
#define RX4581_CTRL_TIE		0x10 /* Timer Interrupt Enable */
#define RX4581_CTRL_AIE		0x08 /* Alarm Interrupt Enable */
#define RX4581_CTRL_STOP	0x02 /* STOP bit */
#define RX4581_CTRL_RESET	0x01 /* RESET bit */

static int rx4581_set_reg(struct device *dev, unsigned char address,
				unsigned char data)
{
	struct spi_device *spi = to_spi_device(dev);
	unsigned char buf[2];

	/* high nibble must be '0' to write */
	buf[0] = address & 0x0f;
	buf[1] = data;

	return spi_write_then_read(spi, buf, 2, NULL, 0);
}

static int rx4581_get_reg(struct device *dev, unsigned char address,
				unsigned char *data)
{
	struct spi_device *spi = to_spi_device(dev);

	/* Set MSB to indicate read */
	*data = address | 0x80;

	return spi_write_then_read(spi, data, 1, data, 1);
}

/*
 * In the routines that deal directly with the rx8581 hardware, we use
 * rtc_time -- month 0-11, hour 0-23, yr = calendar year-epoch.
 */
static int rx4581_get_datetime(struct device *dev, struct rtc_time *tm)
{
	struct spi_device *spi = to_spi_device(dev);
	unsigned char date[7];
	unsigned char data;
	int err;

	/* First we ensure that the "update flag" is not set, we read the
	 * time and date then re-read the "update flag". If the update flag
	 * has been set, we know that the time has changed during the read so
	 * we repeat the whole process again.
	 */
	err = rx4581_get_reg(dev, RX4581_REG_FLAG, &data);
	if (err != 0) {
		dev_err(dev, "Unable to read device flags\n");
		return -EIO;
	}

	do {
		/* If update flag set, clear it */
		if (data & RX4581_FLAG_UF) {
			err = rx4581_set_reg(dev,
				RX4581_REG_FLAG, (data & ~RX4581_FLAG_UF));
			if (err != 0) {
				dev_err(dev, "Unable to write device "
					"flags\n");
				return -EIO;
			}
		}

		/* Now read time and date */
		date[0] = 0x80;
		err = spi_write_then_read(spi, date, 1, date, 7);
		if (err < 0) {
			dev_err(dev, "Unable to read date\n");
			return -EIO;
		}

		/* Check flag register */
		err = rx4581_get_reg(dev, RX4581_REG_FLAG, &data);
		if (err != 0) {
			dev_err(dev, "Unable to read device flags\n");
			return -EIO;
		}
	} while (data & RX4581_FLAG_UF);

	if (data & RX4581_FLAG_VLF)
		dev_info(dev,
			"low voltage detected, date/time is not reliable.\n");

	dev_dbg(dev,
		"%s: raw data is sec=%02x, min=%02x, hr=%02x, "
		"wday=%02x, mday=%02x, mon=%02x, year=%02x\n",
		__func__,
		date[0], date[1], date[2], date[3], date[4], date[5], date[6]);

	tm->tm_sec = bcd2bin(date[RX4581_REG_SC] & 0x7F);
	tm->tm_min = bcd2bin(date[RX4581_REG_MN] & 0x7F);
	tm->tm_hour = bcd2bin(date[RX4581_REG_HR] & 0x3F); /* rtc hr 0-23 */
	tm->tm_wday = ilog2(date[RX4581_REG_DW] & 0x7F);
	tm->tm_mday = bcd2bin(date[RX4581_REG_DM] & 0x3F);
	tm->tm_mon = bcd2bin(date[RX4581_REG_MO] & 0x1F) - 1; /* rtc mn 1-12 */
	tm->tm_year = bcd2bin(date[RX4581_REG_YR]);
	if (tm->tm_year < 70)
		tm->tm_year += 100;	/* assume we are in 1970...2069 */


	dev_dbg(dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	return 0;
}

static int rx4581_set_datetime(struct device *dev, struct rtc_time *tm)
{
	struct spi_device *spi = to_spi_device(dev);
	int err;
	unsigned char buf[8], data;

	dev_dbg(dev, "%s: secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	buf[0] = 0x00;
	/* hours, minutes and seconds */
	buf[RX4581_REG_SC+1] = bin2bcd(tm->tm_sec);
	buf[RX4581_REG_MN+1] = bin2bcd(tm->tm_min);
	buf[RX4581_REG_HR+1] = bin2bcd(tm->tm_hour);

	buf[RX4581_REG_DM+1] = bin2bcd(tm->tm_mday);

	/* month, 1 - 12 */
	buf[RX4581_REG_MO+1] = bin2bcd(tm->tm_mon + 1);

	/* year and century */
	buf[RX4581_REG_YR+1] = bin2bcd(tm->tm_year % 100);
	buf[RX4581_REG_DW+1] = (0x1 << tm->tm_wday);

	/* Stop the clock */
	err = rx4581_get_reg(dev, RX4581_REG_CTRL, &data);
	if (err != 0) {
		dev_err(dev, "Unable to read control register\n");
		return -EIO;
	}

	err = rx4581_set_reg(dev, RX4581_REG_CTRL,
		(data | RX4581_CTRL_STOP));
	if (err != 0) {
		dev_err(dev, "Unable to write control register\n");
		return -EIO;
	}

	/* write register's data */
	err = spi_write_then_read(spi, buf, 8, NULL, 0);
	if (err != 0) {
		dev_err(dev, "Unable to write to date registers\n");
		return -EIO;
	}

	/* get VLF and clear it */
	err = rx4581_get_reg(dev, RX4581_REG_FLAG, &data);
	if (err != 0) {
		dev_err(dev, "Unable to read flag register\n");
		return -EIO;
	}

	err = rx4581_set_reg(dev, RX4581_REG_FLAG,
		(data & ~(RX4581_FLAG_VLF)));
	if (err != 0) {
		dev_err(dev, "Unable to write flag register\n");
		return -EIO;
	}

	/* Restart the clock */
	err = rx4581_get_reg(dev, RX4581_REG_CTRL, &data);
	if (err != 0) {
		dev_err(dev, "Unable to read control register\n");
		return -EIO;
	}

	err = rx4581_set_reg(dev, RX4581_REG_CTRL,
		(data & ~(RX4581_CTRL_STOP)));
	if (err != 0) {
		dev_err(dev, "Unable to write control register\n");
		return -EIO;
	}

	return 0;
}

static const struct rtc_class_ops rx4581_rtc_ops = {
	.read_time	= rx4581_get_datetime,
	.set_time	= rx4581_set_datetime,
};

static int rx4581_probe(struct spi_device *spi)
{
	struct rtc_device *rtc;
	unsigned char tmp;
	int res;

	res = rx4581_get_reg(&spi->dev, RX4581_REG_SC, &tmp);
	if (res != 0)
		return res;

	rtc = devm_rtc_device_register(&spi->dev, "rx4581",
				&rx4581_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	spi_set_drvdata(spi, rtc);
	return 0;
}

static const struct spi_device_id rx4581_id[] = {
	{ "rx4581", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, rx4581_id);

static struct spi_driver rx4581_driver = {
	.driver = {
		.name	= "rtc-rx4581",
	},
	.probe	= rx4581_probe,
	.id_table = rx4581_id,
};

module_spi_driver(rx4581_driver);

MODULE_DESCRIPTION("rx4581 spi RTC driver");
MODULE_AUTHOR("Torben Hohn");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:rtc-rx4581");

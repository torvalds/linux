/*
 * Driver for ST M41T94 SPI RTC
 *
 * Copyright (C) 2008 Kim B. Heino
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/spi/spi.h>
#include <linux/bcd.h>

#define M41T94_REG_SECONDS	0x01
#define M41T94_REG_MINUTES	0x02
#define M41T94_REG_HOURS	0x03
#define M41T94_REG_WDAY		0x04
#define M41T94_REG_DAY		0x05
#define M41T94_REG_MONTH	0x06
#define M41T94_REG_YEAR		0x07
#define M41T94_REG_HT		0x0c

#define M41T94_BIT_HALT		0x40
#define M41T94_BIT_STOP		0x80
#define M41T94_BIT_CB		0x40
#define M41T94_BIT_CEB		0x80

static int m41t94_set_time(struct device *dev, struct rtc_time *tm)
{
	struct spi_device *spi = to_spi_device(dev);
	u8 buf[8]; /* write cmd + 7 registers */

	dev_dbg(dev, "%s secs=%d, mins=%d, "
		"hours=%d, mday=%d, mon=%d, year=%d, wday=%d\n",
		"write", tm->tm_sec, tm->tm_min,
		tm->tm_hour, tm->tm_mday,
		tm->tm_mon, tm->tm_year, tm->tm_wday);

	buf[0] = 0x80 | M41T94_REG_SECONDS; /* write time + date */
	buf[M41T94_REG_SECONDS] = bin2bcd(tm->tm_sec);
	buf[M41T94_REG_MINUTES] = bin2bcd(tm->tm_min);
	buf[M41T94_REG_HOURS]   = bin2bcd(tm->tm_hour);
	buf[M41T94_REG_WDAY]    = bin2bcd(tm->tm_wday + 1);
	buf[M41T94_REG_DAY]     = bin2bcd(tm->tm_mday);
	buf[M41T94_REG_MONTH]   = bin2bcd(tm->tm_mon + 1);

	buf[M41T94_REG_HOURS] |= M41T94_BIT_CEB;
	if (tm->tm_year >= 100)
		buf[M41T94_REG_HOURS] |= M41T94_BIT_CB;
	buf[M41T94_REG_YEAR] = bin2bcd(tm->tm_year % 100);

	return spi_write(spi, buf, 8);
}

static int m41t94_read_time(struct device *dev, struct rtc_time *tm)
{
	struct spi_device *spi = to_spi_device(dev);
	u8 buf[2];
	int ret, hour;

	/* clear halt update bit */
	ret = spi_w8r8(spi, M41T94_REG_HT);
	if (ret < 0)
		return ret;
	if (ret & M41T94_BIT_HALT) {
		buf[0] = 0x80 | M41T94_REG_HT;
		buf[1] = ret & ~M41T94_BIT_HALT;
		spi_write(spi, buf, 2);
	}

	/* clear stop bit */
	ret = spi_w8r8(spi, M41T94_REG_SECONDS);
	if (ret < 0)
		return ret;
	if (ret & M41T94_BIT_STOP) {
		buf[0] = 0x80 | M41T94_REG_SECONDS;
		buf[1] = ret & ~M41T94_BIT_STOP;
		spi_write(spi, buf, 2);
	}

	tm->tm_sec  = bcd2bin(spi_w8r8(spi, M41T94_REG_SECONDS));
	tm->tm_min  = bcd2bin(spi_w8r8(spi, M41T94_REG_MINUTES));
	hour = spi_w8r8(spi, M41T94_REG_HOURS);
	tm->tm_hour = bcd2bin(hour & 0x3f);
	tm->tm_wday = bcd2bin(spi_w8r8(spi, M41T94_REG_WDAY)) - 1;
	tm->tm_mday = bcd2bin(spi_w8r8(spi, M41T94_REG_DAY));
	tm->tm_mon  = bcd2bin(spi_w8r8(spi, M41T94_REG_MONTH)) - 1;
	tm->tm_year = bcd2bin(spi_w8r8(spi, M41T94_REG_YEAR));
	if ((hour & M41T94_BIT_CB) || !(hour & M41T94_BIT_CEB))
		tm->tm_year += 100;

	dev_dbg(dev, "%s secs=%d, mins=%d, "
		"hours=%d, mday=%d, mon=%d, year=%d, wday=%d\n",
		"read", tm->tm_sec, tm->tm_min,
		tm->tm_hour, tm->tm_mday,
		tm->tm_mon, tm->tm_year, tm->tm_wday);

	return 0;
}

static const struct rtc_class_ops m41t94_rtc_ops = {
	.read_time	= m41t94_read_time,
	.set_time	= m41t94_set_time,
};

static struct spi_driver m41t94_driver;

static int m41t94_probe(struct spi_device *spi)
{
	struct rtc_device *rtc;
	int res;

	spi->bits_per_word = 8;
	spi_setup(spi);

	res = spi_w8r8(spi, M41T94_REG_SECONDS);
	if (res < 0) {
		dev_err(&spi->dev, "not found.\n");
		return res;
	}

	rtc = devm_rtc_device_register(&spi->dev, m41t94_driver.driver.name,
					&m41t94_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	spi_set_drvdata(spi, rtc);

	return 0;
}

static struct spi_driver m41t94_driver = {
	.driver = {
		.name	= "rtc-m41t94",
	},
	.probe	= m41t94_probe,
};

module_spi_driver(m41t94_driver);

MODULE_AUTHOR("Kim B. Heino <Kim.Heino@bluegiga.com>");
MODULE_DESCRIPTION("Driver for ST M41T94 SPI RTC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:rtc-m41t94");

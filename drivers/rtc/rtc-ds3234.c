/* rtc-ds3234.c
 *
 * Driver for Dallas Semiconductor (DS3234) SPI RTC with Integrated Crystal
 * and SRAM.
 *
 * Copyright (C) 2008 MIMOMax Wireless Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/spi/spi.h>
#include <linux/bcd.h>

#define DS3234_REG_SECONDS	0x00
#define DS3234_REG_MINUTES	0x01
#define DS3234_REG_HOURS	0x02
#define DS3234_REG_DAY		0x03
#define DS3234_REG_DATE		0x04
#define DS3234_REG_MONTH	0x05
#define DS3234_REG_YEAR		0x06
#define DS3234_REG_CENTURY	(1 << 7) /* Bit 7 of the Month register */

#define DS3234_REG_CONTROL	0x0E
#define DS3234_REG_CONT_STAT	0x0F

static int ds3234_set_reg(struct device *dev, unsigned char address,
				unsigned char data)
{
	struct spi_device *spi = to_spi_device(dev);
	unsigned char buf[2];

	/* MSB must be '1' to indicate write */
	buf[0] = address | 0x80;
	buf[1] = data;

	return spi_write_then_read(spi, buf, 2, NULL, 0);
}

static int ds3234_get_reg(struct device *dev, unsigned char address,
				unsigned char *data)
{
	struct spi_device *spi = to_spi_device(dev);

	*data = address & 0x7f;

	return spi_write_then_read(spi, data, 1, data, 1);
}

static int ds3234_read_time(struct device *dev, struct rtc_time *dt)
{
	int err;
	unsigned char buf[8];
	struct spi_device *spi = to_spi_device(dev);

	buf[0] = 0x00; /* Start address */

	err = spi_write_then_read(spi, buf, 1, buf, 8);
	if (err != 0)
		return err;

	/* Seconds, Minutes, Hours, Day, Date, Month, Year */
	dt->tm_sec	= bcd2bin(buf[0]);
	dt->tm_min	= bcd2bin(buf[1]);
	dt->tm_hour	= bcd2bin(buf[2] & 0x3f);
	dt->tm_wday	= bcd2bin(buf[3]) - 1; /* 0 = Sun */
	dt->tm_mday	= bcd2bin(buf[4]);
	dt->tm_mon	= bcd2bin(buf[5] & 0x1f) - 1; /* 0 = Jan */
	dt->tm_year 	= bcd2bin(buf[6] & 0xff) + 100; /* Assume 20YY */

	return rtc_valid_tm(dt);
}

static int ds3234_set_time(struct device *dev, struct rtc_time *dt)
{
	ds3234_set_reg(dev, DS3234_REG_SECONDS, bin2bcd(dt->tm_sec));
	ds3234_set_reg(dev, DS3234_REG_MINUTES, bin2bcd(dt->tm_min));
	ds3234_set_reg(dev, DS3234_REG_HOURS, bin2bcd(dt->tm_hour) & 0x3f);

	/* 0 = Sun */
	ds3234_set_reg(dev, DS3234_REG_DAY, bin2bcd(dt->tm_wday + 1));
	ds3234_set_reg(dev, DS3234_REG_DATE, bin2bcd(dt->tm_mday));

	/* 0 = Jan */
	ds3234_set_reg(dev, DS3234_REG_MONTH, bin2bcd(dt->tm_mon + 1));

	/* Assume 20YY although we just want to make sure not to go negative. */
	if (dt->tm_year > 100)
		dt->tm_year -= 100;

	ds3234_set_reg(dev, DS3234_REG_YEAR, bin2bcd(dt->tm_year));

	return 0;
}

static const struct rtc_class_ops ds3234_rtc_ops = {
	.read_time	= ds3234_read_time,
	.set_time	= ds3234_set_time,
};

static int __devinit ds3234_probe(struct spi_device *spi)
{
	struct rtc_device *rtc;
	unsigned char tmp;
	int res;

	spi->mode = SPI_MODE_3;
	spi->bits_per_word = 8;
	spi_setup(spi);

	res = ds3234_get_reg(&spi->dev, DS3234_REG_SECONDS, &tmp);
	if (res != 0)
		return res;

	/* Control settings
	 *
	 * CONTROL_REG
	 * BIT 7	6	5	4	3	2	1	0
	 *     EOSC	BBSQW	CONV	RS2	RS1	INTCN	A2IE	A1IE
	 *
	 *     0	0	0	1	1	1	0	0
	 *
	 * CONTROL_STAT_REG
	 * BIT 7	6	5	4	3	2	1	0
	 *     OSF	BB32kHz	CRATE1	CRATE0	EN32kHz	BSY	A2F	A1F
	 *
	 *     1	0	0	0	1	0	0	0
	 */
	ds3234_get_reg(&spi->dev, DS3234_REG_CONTROL, &tmp);
	ds3234_set_reg(&spi->dev, DS3234_REG_CONTROL, tmp & 0x1c);

	ds3234_get_reg(&spi->dev, DS3234_REG_CONT_STAT, &tmp);
	ds3234_set_reg(&spi->dev, DS3234_REG_CONT_STAT, tmp & 0x88);

	/* Print our settings */
	ds3234_get_reg(&spi->dev, DS3234_REG_CONTROL, &tmp);
	dev_info(&spi->dev, "Control Reg: 0x%02x\n", tmp);

	ds3234_get_reg(&spi->dev, DS3234_REG_CONT_STAT, &tmp);
	dev_info(&spi->dev, "Ctrl/Stat Reg: 0x%02x\n", tmp);

	rtc = rtc_device_register("ds3234",
				&spi->dev, &ds3234_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	dev_set_drvdata(&spi->dev, rtc);

	return 0;
}

static int __devexit ds3234_remove(struct spi_device *spi)
{
	struct rtc_device *rtc = spi_get_drvdata(spi);

	rtc_device_unregister(rtc);
	return 0;
}

static struct spi_driver ds3234_driver = {
	.driver = {
		.name	 = "ds3234",
		.owner	= THIS_MODULE,
	},
	.probe	 = ds3234_probe,
	.remove = __devexit_p(ds3234_remove),
};

module_spi_driver(ds3234_driver);

MODULE_DESCRIPTION("DS3234 SPI RTC driver");
MODULE_AUTHOR("Dennis Aberilla <denzzzhome@yahoo.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:ds3234");

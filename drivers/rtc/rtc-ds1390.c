/*
 * rtc-ds1390.c -- driver for the Dallas/Maxim DS1390/93/94 SPI RTC
 *
 * Copyright (C) 2008 Mercury IMC Ltd
 * Written by Mark Jackson <mpfj@mimc.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NOTE: Currently this driver only supports the bare minimum for read
 * and write the RTC. The extra features provided by the chip family
 * (alarms, trickle charger, different control registers) are unavailable.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/spi/spi.h>
#include <linux/bcd.h>
#include <linux/slab.h>

#define DS1390_REG_100THS		0x00
#define DS1390_REG_SECONDS		0x01
#define DS1390_REG_MINUTES		0x02
#define DS1390_REG_HOURS		0x03
#define DS1390_REG_DAY			0x04
#define DS1390_REG_DATE			0x05
#define DS1390_REG_MONTH_CENT		0x06
#define DS1390_REG_YEAR			0x07

#define DS1390_REG_ALARM_100THS		0x08
#define DS1390_REG_ALARM_SECONDS	0x09
#define DS1390_REG_ALARM_MINUTES	0x0A
#define DS1390_REG_ALARM_HOURS		0x0B
#define DS1390_REG_ALARM_DAY_DATE	0x0C

#define DS1390_REG_CONTROL		0x0D
#define DS1390_REG_STATUS		0x0E
#define DS1390_REG_TRICKLE		0x0F

struct ds1390 {
	struct rtc_device *rtc;
	u8 txrx_buf[9];	/* cmd + 8 registers */
};

static int ds1390_get_reg(struct device *dev, unsigned char address,
				unsigned char *data)
{
	struct spi_device *spi = to_spi_device(dev);
	struct ds1390 *chip = dev_get_drvdata(dev);
	int status;

	if (!data)
		return -EINVAL;

	/* Clear MSB to indicate read */
	chip->txrx_buf[0] = address & 0x7f;
	/* do the i/o */
	status = spi_write_then_read(spi, chip->txrx_buf, 1, chip->txrx_buf, 1);
	if (status != 0)
		return status;

	*data = chip->txrx_buf[1];

	return 0;
}

static int ds1390_read_time(struct device *dev, struct rtc_time *dt)
{
	struct spi_device *spi = to_spi_device(dev);
	struct ds1390 *chip = dev_get_drvdata(dev);
	int status;

	/* build the message */
	chip->txrx_buf[0] = DS1390_REG_SECONDS;

	/* do the i/o */
	status = spi_write_then_read(spi, chip->txrx_buf, 1, chip->txrx_buf, 8);
	if (status != 0)
		return status;

	/* The chip sends data in this order:
	 * Seconds, Minutes, Hours, Day, Date, Month / Century, Year */
	dt->tm_sec	= bcd2bin(chip->txrx_buf[0]);
	dt->tm_min	= bcd2bin(chip->txrx_buf[1]);
	dt->tm_hour	= bcd2bin(chip->txrx_buf[2]);
	dt->tm_wday	= bcd2bin(chip->txrx_buf[3]);
	dt->tm_mday	= bcd2bin(chip->txrx_buf[4]);
	/* mask off century bit */
	dt->tm_mon	= bcd2bin(chip->txrx_buf[5] & 0x7f) - 1;
	/* adjust for century bit */
	dt->tm_year = bcd2bin(chip->txrx_buf[6]) + ((chip->txrx_buf[5] & 0x80) ? 100 : 0);

	return rtc_valid_tm(dt);
}

static int ds1390_set_time(struct device *dev, struct rtc_time *dt)
{
	struct spi_device *spi = to_spi_device(dev);
	struct ds1390 *chip = dev_get_drvdata(dev);

	/* build the message */
	chip->txrx_buf[0] = DS1390_REG_SECONDS | 0x80;
	chip->txrx_buf[1] = bin2bcd(dt->tm_sec);
	chip->txrx_buf[2] = bin2bcd(dt->tm_min);
	chip->txrx_buf[3] = bin2bcd(dt->tm_hour);
	chip->txrx_buf[4] = bin2bcd(dt->tm_wday);
	chip->txrx_buf[5] = bin2bcd(dt->tm_mday);
	chip->txrx_buf[6] = bin2bcd(dt->tm_mon + 1) |
				((dt->tm_year > 99) ? 0x80 : 0x00);
	chip->txrx_buf[7] = bin2bcd(dt->tm_year % 100);

	/* do the i/o */
	return spi_write_then_read(spi, chip->txrx_buf, 8, NULL, 0);
}

static const struct rtc_class_ops ds1390_rtc_ops = {
	.read_time	= ds1390_read_time,
	.set_time	= ds1390_set_time,
};

static int ds1390_probe(struct spi_device *spi)
{
	unsigned char tmp;
	struct ds1390 *chip;
	int res;

	spi->mode = SPI_MODE_3;
	spi->bits_per_word = 8;
	spi_setup(spi);

	chip = devm_kzalloc(&spi->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	spi_set_drvdata(spi, chip);

	res = ds1390_get_reg(&spi->dev, DS1390_REG_SECONDS, &tmp);
	if (res != 0) {
		dev_err(&spi->dev, "unable to read device\n");
		return res;
	}

	chip->rtc = devm_rtc_device_register(&spi->dev, "ds1390",
					&ds1390_rtc_ops, THIS_MODULE);
	if (IS_ERR(chip->rtc)) {
		dev_err(&spi->dev, "unable to register device\n");
		res = PTR_ERR(chip->rtc);
	}

	return res;
}

static struct spi_driver ds1390_driver = {
	.driver = {
		.name	= "rtc-ds1390",
		.owner	= THIS_MODULE,
	},
	.probe	= ds1390_probe,
};

module_spi_driver(ds1390_driver);

MODULE_DESCRIPTION("Dallas/Maxim DS1390/93/94 SPI RTC driver");
MODULE_AUTHOR("Mark Jackson <mpfj@mimc.co.uk>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:rtc-ds1390");

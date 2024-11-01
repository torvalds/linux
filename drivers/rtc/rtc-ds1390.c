// SPDX-License-Identifier: GPL-2.0-only
/*
 * rtc-ds1390.c -- driver for the Dallas/Maxim DS1390/93/94 SPI RTC
 *
 * Copyright (C) 2008 Mercury IMC Ltd
 * Written by Mark Jackson <mpfj@mimc.co.uk>
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
#include <linux/of.h>

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

#define DS1390_TRICKLE_CHARGER_ENABLE	0xA0
#define DS1390_TRICKLE_CHARGER_250_OHM	0x01
#define DS1390_TRICKLE_CHARGER_2K_OHM	0x02
#define DS1390_TRICKLE_CHARGER_4K_OHM	0x03
#define DS1390_TRICKLE_CHARGER_NO_DIODE	0x04
#define DS1390_TRICKLE_CHARGER_DIODE	0x08

struct ds1390 {
	struct rtc_device *rtc;
	u8 txrx_buf[9];	/* cmd + 8 registers */
};

static void ds1390_set_reg(struct device *dev, unsigned char address,
			   unsigned char data)
{
	struct spi_device *spi = to_spi_device(dev);
	unsigned char buf[2];

	/* MSB must be '1' to write */
	buf[0] = address | 0x80;
	buf[1] = data;

	spi_write(spi, buf, 2);
}

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

	*data = chip->txrx_buf[0];

	return 0;
}

static void ds1390_trickle_of_init(struct spi_device *spi)
{
	u32 ohms = 0;
	u8 value;

	if (of_property_read_u32(spi->dev.of_node, "trickle-resistor-ohms",
				 &ohms))
		goto out;

	/* Enable charger */
	value = DS1390_TRICKLE_CHARGER_ENABLE;
	if (of_property_read_bool(spi->dev.of_node, "trickle-diode-disable"))
		value |= DS1390_TRICKLE_CHARGER_NO_DIODE;
	else
		value |= DS1390_TRICKLE_CHARGER_DIODE;

	/* Resistor select */
	switch (ohms) {
	case 250:
		value |= DS1390_TRICKLE_CHARGER_250_OHM;
		break;
	case 2000:
		value |= DS1390_TRICKLE_CHARGER_2K_OHM;
		break;
	case 4000:
		value |= DS1390_TRICKLE_CHARGER_4K_OHM;
		break;
	default:
		dev_warn(&spi->dev,
			 "Unsupported ohm value %02ux in dt\n", ohms);
		return;
	}

	ds1390_set_reg(&spi->dev, DS1390_REG_TRICKLE, value);

out:
	return;
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

	return 0;
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

	if (spi->dev.of_node)
		ds1390_trickle_of_init(spi);

	chip->rtc = devm_rtc_device_register(&spi->dev, "ds1390",
					&ds1390_rtc_ops, THIS_MODULE);
	if (IS_ERR(chip->rtc)) {
		dev_err(&spi->dev, "unable to register device\n");
		res = PTR_ERR(chip->rtc);
	}

	return res;
}

static const struct of_device_id ds1390_of_match[] = {
	{ .compatible = "dallas,ds1390" },
	{}
};
MODULE_DEVICE_TABLE(of, ds1390_of_match);

static const struct spi_device_id ds1390_spi_ids[] = {
	{ .name = "ds1390" },
	{}
};
MODULE_DEVICE_TABLE(spi, ds1390_spi_ids);

static struct spi_driver ds1390_driver = {
	.driver = {
		.name	= "rtc-ds1390",
		.of_match_table = of_match_ptr(ds1390_of_match),
	},
	.probe	= ds1390_probe,
	.id_table = ds1390_spi_ids,
};

module_spi_driver(ds1390_driver);

MODULE_DESCRIPTION("Dallas/Maxim DS1390/93/94 SPI RTC driver");
MODULE_AUTHOR("Mark Jackson <mpfj@mimc.co.uk>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:rtc-ds1390");

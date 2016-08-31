/* rtc-ds1347.c
 *
 * Driver for Dallas Semiconductor DS1347 Low Current, SPI Compatible
 * Real Time Clock
 *
 * Author : Raghavendra Chandra Ganiga <ravi23ganiga@gmail.com>
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
#include <linux/regmap.h>

/* Registers in ds1347 rtc */

#define DS1347_SECONDS_REG	0x01
#define DS1347_MINUTES_REG	0x03
#define DS1347_HOURS_REG	0x05
#define DS1347_DATE_REG		0x07
#define DS1347_MONTH_REG	0x09
#define DS1347_DAY_REG		0x0B
#define DS1347_YEAR_REG		0x0D
#define DS1347_CONTROL_REG	0x0F
#define DS1347_STATUS_REG	0x17
#define DS1347_CLOCK_BURST	0x3F

static const struct regmap_range ds1347_ranges[] = {
	{
		.range_min = DS1347_SECONDS_REG,
		.range_max = DS1347_STATUS_REG,
	},
};

static const struct regmap_access_table ds1347_access_table = {
	.yes_ranges = ds1347_ranges,
	.n_yes_ranges = ARRAY_SIZE(ds1347_ranges),
};

static int ds1347_read_time(struct device *dev, struct rtc_time *dt)
{
	struct spi_device *spi = to_spi_device(dev);
	struct regmap *map;
	int err;
	unsigned char buf[8];

	map = spi_get_drvdata(spi);

	err = regmap_bulk_read(map, DS1347_CLOCK_BURST, buf, 8);
	if (err)
		return err;

	dt->tm_sec = bcd2bin(buf[0]);
	dt->tm_min = bcd2bin(buf[1]);
	dt->tm_hour = bcd2bin(buf[2] & 0x3F);
	dt->tm_mday = bcd2bin(buf[3]);
	dt->tm_mon = bcd2bin(buf[4]) - 1;
	dt->tm_wday = bcd2bin(buf[5]) - 1;
	dt->tm_year = bcd2bin(buf[6]) + 100;

	return rtc_valid_tm(dt);
}

static int ds1347_set_time(struct device *dev, struct rtc_time *dt)
{
	struct spi_device *spi = to_spi_device(dev);
	struct regmap *map;
	unsigned char buf[8];

	map = spi_get_drvdata(spi);

	buf[0] = bin2bcd(dt->tm_sec);
	buf[1] = bin2bcd(dt->tm_min);
	buf[2] = (bin2bcd(dt->tm_hour) & 0x3F);
	buf[3] = bin2bcd(dt->tm_mday);
	buf[4] = bin2bcd(dt->tm_mon + 1);
	buf[5] = bin2bcd(dt->tm_wday + 1);

	/* year in linux is from 1900 i.e in range of 100
	in rtc it is from 00 to 99 */
	dt->tm_year = dt->tm_year % 100;

	buf[6] = bin2bcd(dt->tm_year);
	buf[7] = bin2bcd(0x00);

	/* write the rtc settings */
	return regmap_bulk_write(map, DS1347_CLOCK_BURST, buf, 8);
}

static const struct rtc_class_ops ds1347_rtc_ops = {
	.read_time = ds1347_read_time,
	.set_time = ds1347_set_time,
};

static int ds1347_probe(struct spi_device *spi)
{
	struct rtc_device *rtc;
	struct regmap_config config;
	struct regmap *map;
	unsigned int data;
	int res;

	memset(&config, 0, sizeof(config));
	config.reg_bits = 8;
	config.val_bits = 8;
	config.read_flag_mask = 0x80;
	config.max_register = 0x3F;
	config.wr_table = &ds1347_access_table;

	/* spi setup with ds1347 in mode 3 and bits per word as 8 */
	spi->mode = SPI_MODE_3;
	spi->bits_per_word = 8;
	spi_setup(spi);

	map = devm_regmap_init_spi(spi, &config);

	if (IS_ERR(map)) {
		dev_err(&spi->dev, "ds1347 regmap init spi failed\n");
		return PTR_ERR(map);
	}

	spi_set_drvdata(spi, map);

	/* RTC Settings */
	res = regmap_read(map, DS1347_SECONDS_REG, &data);
	if (res)
		return res;

	/* Disable the write protect of rtc */
	regmap_read(map, DS1347_CONTROL_REG, &data);
	data = data & ~(1<<7);
	regmap_write(map, DS1347_CONTROL_REG, data);

	/* Enable the oscillator , disable the oscillator stop flag,
	 and glitch filter to reduce current consumption */
	regmap_read(map, DS1347_STATUS_REG, &data);
	data = data & 0x1B;
	regmap_write(map, DS1347_STATUS_REG, data);

	/* display the settings */
	regmap_read(map, DS1347_CONTROL_REG, &data);
	dev_info(&spi->dev, "DS1347 RTC CTRL Reg = 0x%02x\n", data);

	regmap_read(map, DS1347_STATUS_REG, &data);
	dev_info(&spi->dev, "DS1347 RTC Status Reg = 0x%02x\n", data);

	rtc = devm_rtc_device_register(&spi->dev, "ds1347",
				&ds1347_rtc_ops, THIS_MODULE);

	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	return 0;
}

static struct spi_driver ds1347_driver = {
	.driver = {
		.name = "ds1347",
	},
	.probe = ds1347_probe,
};

module_spi_driver(ds1347_driver);

MODULE_DESCRIPTION("DS1347 SPI RTC DRIVER");
MODULE_AUTHOR("Raghavendra C Ganiga <ravi23ganiga@gmail.com>");
MODULE_LICENSE("GPL v2");

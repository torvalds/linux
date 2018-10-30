/* rtc-max6916.c
 *
 * Driver for MAXIM  max6916 Low Current, SPI Compatible
 * Real Time Clock
 *
 * Author : Venkat Prashanth B U <venkat.prashanth2498@gmail.com>
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

/* Registers in max6916 rtc */

#define MAX6916_SECONDS_REG	0x01
#define MAX6916_MINUTES_REG	0x02
#define MAX6916_HOURS_REG	0x03
#define MAX6916_DATE_REG	0x04
#define MAX6916_MONTH_REG	0x05
#define MAX6916_DAY_REG	0x06
#define MAX6916_YEAR_REG	0x07
#define MAX6916_CONTROL_REG	0x08
#define MAX6916_STATUS_REG	0x0C
#define MAX6916_CLOCK_BURST	0x3F

static int max6916_read_reg(struct device *dev, unsigned char address,
			    unsigned char *data)
{
	struct spi_device *spi = to_spi_device(dev);

	*data = address | 0x80;

	return spi_write_then_read(spi, data, 1, data, 1);
}

static int max6916_write_reg(struct device *dev, unsigned char address,
			     unsigned char data)
{
	struct spi_device *spi = to_spi_device(dev);
	unsigned char buf[2];

	buf[0] = address & 0x7F;
	buf[1] = data;

	return spi_write_then_read(spi, buf, 2, NULL, 0);
}

static int max6916_read_time(struct device *dev, struct rtc_time *dt)
{
	struct spi_device *spi = to_spi_device(dev);
	int err;
	unsigned char buf[8];

	buf[0] = MAX6916_CLOCK_BURST | 0x80;

	err = spi_write_then_read(spi, buf, 1, buf, 8);

	if (err)
		return err;

	dt->tm_sec = bcd2bin(buf[0]);
	dt->tm_min = bcd2bin(buf[1]);
	dt->tm_hour = bcd2bin(buf[2] & 0x3F);
	dt->tm_mday = bcd2bin(buf[3]);
	dt->tm_mon = bcd2bin(buf[4]) - 1;
	dt->tm_wday = bcd2bin(buf[5]) - 1;
	dt->tm_year = bcd2bin(buf[6]) + 100;

	return 0;
}

static int max6916_set_time(struct device *dev, struct rtc_time *dt)
{
	struct spi_device *spi = to_spi_device(dev);
	unsigned char buf[9];

	if (dt->tm_year < 100 || dt->tm_year > 199) {
		dev_err(&spi->dev, "Year must be between 2000 and 2099. It's %d.\n",
			dt->tm_year + 1900);
	return -EINVAL;
	}

	buf[0] = MAX6916_CLOCK_BURST & 0x7F;
	buf[1] = bin2bcd(dt->tm_sec);
	buf[2] = bin2bcd(dt->tm_min);
	buf[3] = (bin2bcd(dt->tm_hour) & 0X3F);
	buf[4] = bin2bcd(dt->tm_mday);
	buf[5] = bin2bcd(dt->tm_mon + 1);
	buf[6] = bin2bcd(dt->tm_wday + 1);
	buf[7] = bin2bcd(dt->tm_year % 100);
	buf[8] = bin2bcd(0x00);

	/* write the rtc settings */
	return spi_write_then_read(spi, buf, 9, NULL, 0);
}

static const struct rtc_class_ops max6916_rtc_ops = {
	.read_time = max6916_read_time,
	.set_time = max6916_set_time,
};

static int max6916_probe(struct spi_device *spi)
{
	struct rtc_device *rtc;
	unsigned char data;
	int res;

	/* spi setup with max6916 in mode 3 and bits per word as 8 */
	spi->mode = SPI_MODE_3;
	spi->bits_per_word = 8;
	spi_setup(spi);

	/* RTC Settings */
	res = max6916_read_reg(&spi->dev, MAX6916_SECONDS_REG, &data);
	if (res)
		return res;

	/* Disable the write protect of rtc */
	max6916_read_reg(&spi->dev, MAX6916_CONTROL_REG, &data);
	data = data & ~(1 << 7);
	max6916_write_reg(&spi->dev, MAX6916_CONTROL_REG, data);

	/*Enable oscillator,disable oscillator stop flag, glitch filter*/
	max6916_read_reg(&spi->dev, MAX6916_STATUS_REG, &data);
	data = data & 0x1B;
	max6916_write_reg(&spi->dev, MAX6916_STATUS_REG, data);

	/* display the settings */
	max6916_read_reg(&spi->dev, MAX6916_CONTROL_REG, &data);
	dev_info(&spi->dev, "MAX6916 RTC CTRL Reg = 0x%02x\n", data);

	max6916_read_reg(&spi->dev, MAX6916_STATUS_REG, &data);
	dev_info(&spi->dev, "MAX6916 RTC Status Reg = 0x%02x\n", data);

	rtc = devm_rtc_device_register(&spi->dev, "max6916",
				       &max6916_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	spi_set_drvdata(spi, rtc);

	return 0;
}

static struct spi_driver max6916_driver = {
	.driver = {
		.name = "max6916",
	},
	.probe = max6916_probe,
};
module_spi_driver(max6916_driver);

MODULE_DESCRIPTION("MAX6916 SPI RTC DRIVER");
MODULE_AUTHOR("Venkat Prashanth B U <venkat.prashanth2498@gmail.com>");
MODULE_LICENSE("GPL v2");

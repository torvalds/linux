/* drivers/rtc/rtc-ds3234.c
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
 * Changelog:
 *
 * 07-May-2008: Dennis Aberilla <denzzzhome@yahoo.com>
 *		- Created based on the max6902 code. Only implements the
 *		  date/time keeping functions; no SRAM yet.
 */

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

#undef DS3234_DEBUG

struct ds3234 {
	struct rtc_device *rtc;
	u8 buf[8]; /* Burst read: addr + 7 regs */
	u8 tx_buf[2];
	u8 rx_buf[2];
};

static void ds3234_set_reg(struct device *dev, unsigned char address,
				unsigned char data)
{
	struct spi_device *spi = to_spi_device(dev);
	unsigned char buf[2];

	/* MSB must be '1' to indicate write */
	buf[0] = address | 0x80;
	buf[1] = data;

	spi_write(spi, buf, 2);
}

static int ds3234_get_reg(struct device *dev, unsigned char address,
				unsigned char *data)
{
	struct spi_device *spi = to_spi_device(dev);
	struct ds3234 *chip = dev_get_drvdata(dev);
	struct spi_message message;
	struct spi_transfer xfer;
	int status;

	if (!data)
		return -EINVAL;

	/* Build our spi message */
	spi_message_init(&message);
	memset(&xfer, 0, sizeof(xfer));

	/* Address + dummy tx byte */
	xfer.len = 2;
	xfer.tx_buf = chip->tx_buf;
	xfer.rx_buf = chip->rx_buf;

	chip->tx_buf[0] = address;
	chip->tx_buf[1] = 0xff;

	spi_message_add_tail(&xfer, &message);

	/* do the i/o */
	status = spi_sync(spi, &message);
	if (status == 0)
		status = message.status;
	else
		return status;

	*data = chip->rx_buf[1];

	return status;
}

static int ds3234_get_datetime(struct device *dev, struct rtc_time *dt)
{
	struct spi_device *spi = to_spi_device(dev);
	struct ds3234 *chip = dev_get_drvdata(dev);
	struct spi_message message;
	struct spi_transfer xfer;
	int status;

	/* build the message */
	spi_message_init(&message);
	memset(&xfer, 0, sizeof(xfer));
	xfer.len = 1 + 7;	/* Addr + 7 registers */
	xfer.tx_buf = chip->buf;
	xfer.rx_buf = chip->buf;
	chip->buf[0] = 0x00;	/* Start address */
	spi_message_add_tail(&xfer, &message);

	/* do the i/o */
	status = spi_sync(spi, &message);
	if (status == 0)
		status = message.status;
	else
		return status;

	/* Seconds, Minutes, Hours, Day, Date, Month, Year */
	dt->tm_sec	= bcd2bin(chip->buf[1]);
	dt->tm_min	= bcd2bin(chip->buf[2]);
	dt->tm_hour	= bcd2bin(chip->buf[3] & 0x3f);
	dt->tm_wday	= bcd2bin(chip->buf[4]) - 1; /* 0 = Sun */
	dt->tm_mday	= bcd2bin(chip->buf[5]);
	dt->tm_mon	= bcd2bin(chip->buf[6] & 0x1f) - 1; /* 0 = Jan */
	dt->tm_year 	= bcd2bin(chip->buf[7] & 0xff) + 100; /* Assume 20YY */

#ifdef DS3234_DEBUG
	dev_dbg(dev, "\n%s : Read RTC values\n", __func__);
	dev_dbg(dev, "tm_hour: %i\n", dt->tm_hour);
	dev_dbg(dev, "tm_min : %i\n", dt->tm_min);
	dev_dbg(dev, "tm_sec : %i\n", dt->tm_sec);
	dev_dbg(dev, "tm_wday: %i\n", dt->tm_wday);
	dev_dbg(dev, "tm_mday: %i\n", dt->tm_mday);
	dev_dbg(dev, "tm_mon : %i\n", dt->tm_mon);
	dev_dbg(dev, "tm_year: %i\n", dt->tm_year);
#endif

	return 0;
}

static int ds3234_set_datetime(struct device *dev, struct rtc_time *dt)
{
#ifdef DS3234_DEBUG
	dev_dbg(dev, "\n%s : Setting RTC values\n", __func__);
	dev_dbg(dev, "tm_sec : %i\n", dt->tm_sec);
	dev_dbg(dev, "tm_min : %i\n", dt->tm_min);
	dev_dbg(dev, "tm_hour: %i\n", dt->tm_hour);
	dev_dbg(dev, "tm_wday: %i\n", dt->tm_wday);
	dev_dbg(dev, "tm_mday: %i\n", dt->tm_mday);
	dev_dbg(dev, "tm_mon : %i\n", dt->tm_mon);
	dev_dbg(dev, "tm_year: %i\n", dt->tm_year);
#endif

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

static int ds3234_read_time(struct device *dev, struct rtc_time *tm)
{
	return ds3234_get_datetime(dev, tm);
}

static int ds3234_set_time(struct device *dev, struct rtc_time *tm)
{
	return ds3234_set_datetime(dev, tm);
}

static const struct rtc_class_ops ds3234_rtc_ops = {
	.read_time	= ds3234_read_time,
	.set_time	= ds3234_set_time,
};

static int __devinit ds3234_probe(struct spi_device *spi)
{
	struct rtc_device *rtc;
	unsigned char tmp;
	struct ds3234 *chip;
	int res;

	rtc = rtc_device_register("ds3234",
				&spi->dev, &ds3234_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	spi->mode = SPI_MODE_3;
	spi->bits_per_word = 8;
	spi_setup(spi);

	chip = kzalloc(sizeof(struct ds3234), GFP_KERNEL);
	if (!chip) {
		rtc_device_unregister(rtc);
		return -ENOMEM;
	}
	chip->rtc = rtc;
	dev_set_drvdata(&spi->dev, chip);

	res = ds3234_get_reg(&spi->dev, DS3234_REG_SECONDS, &tmp);
	if (res) {
		rtc_device_unregister(rtc);
		return res;
	}

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

	return 0;
}

static int __devexit ds3234_remove(struct spi_device *spi)
{
	struct ds3234 *chip = platform_get_drvdata(spi);
	struct rtc_device *rtc = chip->rtc;

	if (rtc)
		rtc_device_unregister(rtc);

	kfree(chip);

	return 0;
}

static struct spi_driver ds3234_driver = {
	.driver = {
		.name	 = "ds3234",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe	 = ds3234_probe,
	.remove = __devexit_p(ds3234_remove),
};

static __init int ds3234_init(void)
{
	printk(KERN_INFO "DS3234 SPI RTC Driver\n");
	return spi_register_driver(&ds3234_driver);
}
module_init(ds3234_init);

static __exit void ds3234_exit(void)
{
	spi_unregister_driver(&ds3234_driver);
}
module_exit(ds3234_exit);

MODULE_DESCRIPTION("DS3234 SPI RTC driver");
MODULE_AUTHOR("Dennis Aberilla <denzzzhome@yahoo.com>");
MODULE_LICENSE("GPL");

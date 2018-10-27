/*
 * A SPI driver for the Ricoh RS5C348 RTC
 *
 * Copyright (C) 2006 Atsushi Nemoto <anemo@mba.ocn.ne.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The board specific init code should provide characteristics of this
 * device:
 *     Mode 1 (High-Active, Shift-Then-Sample), High Avtive CS
 */

#include <linux/bcd.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/workqueue.h>
#include <linux/spi/spi.h>
#include <linux/module.h>

#define RS5C348_REG_SECS	0
#define RS5C348_REG_MINS	1
#define RS5C348_REG_HOURS	2
#define RS5C348_REG_WDAY	3
#define RS5C348_REG_DAY	4
#define RS5C348_REG_MONTH	5
#define RS5C348_REG_YEAR	6
#define RS5C348_REG_CTL1	14
#define RS5C348_REG_CTL2	15

#define RS5C348_SECS_MASK	0x7f
#define RS5C348_MINS_MASK	0x7f
#define RS5C348_HOURS_MASK	0x3f
#define RS5C348_WDAY_MASK	0x03
#define RS5C348_DAY_MASK	0x3f
#define RS5C348_MONTH_MASK	0x1f

#define RS5C348_BIT_PM	0x20	/* REG_HOURS */
#define RS5C348_BIT_Y2K	0x80	/* REG_MONTH */
#define RS5C348_BIT_24H	0x20	/* REG_CTL1 */
#define RS5C348_BIT_XSTP	0x10	/* REG_CTL2 */
#define RS5C348_BIT_VDET	0x40	/* REG_CTL2 */

#define RS5C348_CMD_W(addr)	(((addr) << 4) | 0x08)	/* single write */
#define RS5C348_CMD_R(addr)	(((addr) << 4) | 0x0c)	/* single read */
#define RS5C348_CMD_MW(addr)	(((addr) << 4) | 0x00)	/* burst write */
#define RS5C348_CMD_MR(addr)	(((addr) << 4) | 0x04)	/* burst read */

struct rs5c348_plat_data {
	struct rtc_device *rtc;
	int rtc_24h;
};

static int
rs5c348_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct spi_device *spi = to_spi_device(dev);
	struct rs5c348_plat_data *pdata = dev_get_platdata(&spi->dev);
	u8 txbuf[5+7], *txp;
	int ret;

	ret = spi_w8r8(spi, RS5C348_CMD_R(RS5C348_REG_CTL2));
	if (ret < 0)
		return ret;
	if (ret & RS5C348_BIT_XSTP) {
		txbuf[0] = RS5C348_CMD_W(RS5C348_REG_CTL2);
		txbuf[1] = 0;
		ret = spi_write_then_read(spi, txbuf, 2, NULL, 0);
		if (ret < 0)
			return ret;
	}

	/* Transfer 5 bytes before writing SEC.  This gives 31us for carry. */
	txp = txbuf;
	txbuf[0] = RS5C348_CMD_R(RS5C348_REG_CTL2); /* cmd, ctl2 */
	txbuf[1] = 0;	/* dummy */
	txbuf[2] = RS5C348_CMD_R(RS5C348_REG_CTL2); /* cmd, ctl2 */
	txbuf[3] = 0;	/* dummy */
	txbuf[4] = RS5C348_CMD_MW(RS5C348_REG_SECS); /* cmd, sec, ... */
	txp = &txbuf[5];
	txp[RS5C348_REG_SECS] = bin2bcd(tm->tm_sec);
	txp[RS5C348_REG_MINS] = bin2bcd(tm->tm_min);
	if (pdata->rtc_24h) {
		txp[RS5C348_REG_HOURS] = bin2bcd(tm->tm_hour);
	} else {
		/* hour 0 is AM12, noon is PM12 */
		txp[RS5C348_REG_HOURS] = bin2bcd((tm->tm_hour + 11) % 12 + 1) |
			(tm->tm_hour >= 12 ? RS5C348_BIT_PM : 0);
	}
	txp[RS5C348_REG_WDAY] = bin2bcd(tm->tm_wday);
	txp[RS5C348_REG_DAY] = bin2bcd(tm->tm_mday);
	txp[RS5C348_REG_MONTH] = bin2bcd(tm->tm_mon + 1) |
		(tm->tm_year >= 100 ? RS5C348_BIT_Y2K : 0);
	txp[RS5C348_REG_YEAR] = bin2bcd(tm->tm_year % 100);
	/* write in one transfer to avoid data inconsistency */
	ret = spi_write_then_read(spi, txbuf, sizeof(txbuf), NULL, 0);
	udelay(62);	/* Tcsr 62us */
	return ret;
}

static int
rs5c348_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct spi_device *spi = to_spi_device(dev);
	struct rs5c348_plat_data *pdata = dev_get_platdata(&spi->dev);
	u8 txbuf[5], rxbuf[7];
	int ret;

	ret = spi_w8r8(spi, RS5C348_CMD_R(RS5C348_REG_CTL2));
	if (ret < 0)
		return ret;
	if (ret & RS5C348_BIT_VDET)
		dev_warn(&spi->dev, "voltage-low detected.\n");
	if (ret & RS5C348_BIT_XSTP) {
		dev_warn(&spi->dev, "oscillator-stop detected.\n");
		return -EINVAL;
	}

	/* Transfer 5 byte befores reading SEC.  This gives 31us for carry. */
	txbuf[0] = RS5C348_CMD_R(RS5C348_REG_CTL2); /* cmd, ctl2 */
	txbuf[1] = 0;	/* dummy */
	txbuf[2] = RS5C348_CMD_R(RS5C348_REG_CTL2); /* cmd, ctl2 */
	txbuf[3] = 0;	/* dummy */
	txbuf[4] = RS5C348_CMD_MR(RS5C348_REG_SECS); /* cmd, sec, ... */

	/* read in one transfer to avoid data inconsistency */
	ret = spi_write_then_read(spi, txbuf, sizeof(txbuf),
				  rxbuf, sizeof(rxbuf));
	udelay(62);	/* Tcsr 62us */
	if (ret < 0)
		return ret;

	tm->tm_sec = bcd2bin(rxbuf[RS5C348_REG_SECS] & RS5C348_SECS_MASK);
	tm->tm_min = bcd2bin(rxbuf[RS5C348_REG_MINS] & RS5C348_MINS_MASK);
	tm->tm_hour = bcd2bin(rxbuf[RS5C348_REG_HOURS] & RS5C348_HOURS_MASK);
	if (!pdata->rtc_24h) {
		if (rxbuf[RS5C348_REG_HOURS] & RS5C348_BIT_PM) {
			tm->tm_hour -= 20;
			tm->tm_hour %= 12;
			tm->tm_hour += 12;
		} else
			tm->tm_hour %= 12;
	}
	tm->tm_wday = bcd2bin(rxbuf[RS5C348_REG_WDAY] & RS5C348_WDAY_MASK);
	tm->tm_mday = bcd2bin(rxbuf[RS5C348_REG_DAY] & RS5C348_DAY_MASK);
	tm->tm_mon =
		bcd2bin(rxbuf[RS5C348_REG_MONTH] & RS5C348_MONTH_MASK) - 1;
	/* year is 1900 + tm->tm_year */
	tm->tm_year = bcd2bin(rxbuf[RS5C348_REG_YEAR]) +
		((rxbuf[RS5C348_REG_MONTH] & RS5C348_BIT_Y2K) ? 100 : 0);

	return 0;
}

static const struct rtc_class_ops rs5c348_rtc_ops = {
	.read_time	= rs5c348_rtc_read_time,
	.set_time	= rs5c348_rtc_set_time,
};

static int rs5c348_probe(struct spi_device *spi)
{
	int ret;
	struct rtc_device *rtc;
	struct rs5c348_plat_data *pdata;

	pdata = devm_kzalloc(&spi->dev, sizeof(struct rs5c348_plat_data),
				GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	spi->dev.platform_data = pdata;

	/* Check D7 of SECOND register */
	ret = spi_w8r8(spi, RS5C348_CMD_R(RS5C348_REG_SECS));
	if (ret < 0 || (ret & 0x80)) {
		dev_err(&spi->dev, "not found.\n");
		return ret;
	}

	dev_info(&spi->dev, "spiclk %u KHz.\n",
		 (spi->max_speed_hz + 500) / 1000);

	ret = spi_w8r8(spi, RS5C348_CMD_R(RS5C348_REG_CTL1));
	if (ret < 0)
		return ret;
	if (ret & RS5C348_BIT_24H)
		pdata->rtc_24h = 1;

	rtc = devm_rtc_allocate_device(&spi->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	pdata->rtc = rtc;

	rtc->ops = &rs5c348_rtc_ops;

	return rtc_register_device(rtc);
}

static struct spi_driver rs5c348_driver = {
	.driver = {
		.name	= "rtc-rs5c348",
	},
	.probe	= rs5c348_probe,
};

module_spi_driver(rs5c348_driver);

MODULE_AUTHOR("Atsushi Nemoto <anemo@mba.ocn.ne.jp>");
MODULE_DESCRIPTION("Ricoh RS5C348 RTC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:rtc-rs5c348");

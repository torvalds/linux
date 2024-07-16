// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Epson RTC-9701JE
 *
 * Copyright (C) 2008 Magnus Damm
 *
 * Based on rtc-max6902.c
 *
 * Copyright (C) 2006 8D Technologies inc.
 * Copyright (C) 2004 Compulab Ltd.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/rtc.h>
#include <linux/spi/spi.h>
#include <linux/bcd.h>
#include <linux/delay.h>
#include <linux/bitops.h>

#define RSECCNT	0x00	/* Second Counter */
#define RMINCNT	0x01	/* Minute Counter */
#define RHRCNT	0x02	/* Hour Counter */
#define RWKCNT	0x03	/* Week Counter */
#define RDAYCNT	0x04	/* Day Counter */
#define RMONCNT	0x05	/* Month Counter */
#define RYRCNT	0x06	/* Year Counter */
#define R100CNT	0x07	/* Y100 Counter */
#define RMINAR	0x08	/* Minute Alarm */
#define RHRAR	0x09	/* Hour Alarm */
#define RWKAR	0x0a	/* Week/Day Alarm */
#define RTIMCNT	0x0c	/* Interval Timer */
#define REXT	0x0d	/* Extension Register */
#define RFLAG	0x0e	/* RTC Flag Register */
#define RCR	0x0f	/* RTC Control Register */

static int write_reg(struct device *dev, int address, unsigned char data)
{
	struct spi_device *spi = to_spi_device(dev);
	unsigned char buf[2];

	buf[0] = address & 0x7f;
	buf[1] = data;

	return spi_write(spi, buf, ARRAY_SIZE(buf));
}

static int read_regs(struct device *dev, unsigned char *regs, int no_regs)
{
	struct spi_device *spi = to_spi_device(dev);
	u8 txbuf[1], rxbuf[1];
	int k, ret;

	ret = 0;

	for (k = 0; ret == 0 && k < no_regs; k++) {
		txbuf[0] = 0x80 | regs[k];
		ret = spi_write_then_read(spi, txbuf, 1, rxbuf, 1);
		regs[k] = rxbuf[0];
	}

	return ret;
}

static int r9701_get_datetime(struct device *dev, struct rtc_time *dt)
{
	int ret;
	unsigned char buf[] = { RSECCNT, RMINCNT, RHRCNT,
				RDAYCNT, RMONCNT, RYRCNT };

	ret = read_regs(dev, buf, ARRAY_SIZE(buf));
	if (ret)
		return ret;

	dt->tm_sec = bcd2bin(buf[0]); /* RSECCNT */
	dt->tm_min = bcd2bin(buf[1]); /* RMINCNT */
	dt->tm_hour = bcd2bin(buf[2]); /* RHRCNT */

	dt->tm_mday = bcd2bin(buf[3]); /* RDAYCNT */
	dt->tm_mon = bcd2bin(buf[4]) - 1; /* RMONCNT */
	dt->tm_year = bcd2bin(buf[5]) + 100; /* RYRCNT */

	return 0;
}

static int r9701_set_datetime(struct device *dev, struct rtc_time *dt)
{
	int ret;

	ret = write_reg(dev, RHRCNT, bin2bcd(dt->tm_hour));
	ret = ret ? ret : write_reg(dev, RMINCNT, bin2bcd(dt->tm_min));
	ret = ret ? ret : write_reg(dev, RSECCNT, bin2bcd(dt->tm_sec));
	ret = ret ? ret : write_reg(dev, RDAYCNT, bin2bcd(dt->tm_mday));
	ret = ret ? ret : write_reg(dev, RMONCNT, bin2bcd(dt->tm_mon + 1));
	ret = ret ? ret : write_reg(dev, RYRCNT, bin2bcd(dt->tm_year - 100));

	return ret;
}

static const struct rtc_class_ops r9701_rtc_ops = {
	.read_time	= r9701_get_datetime,
	.set_time	= r9701_set_datetime,
};

static int r9701_probe(struct spi_device *spi)
{
	struct rtc_device *rtc;
	unsigned char tmp;
	int res;

	tmp = R100CNT;
	res = read_regs(&spi->dev, &tmp, 1);
	if (res || tmp != 0x20) {
		dev_err(&spi->dev, "cannot read RTC register\n");
		return -ENODEV;
	}

	rtc = devm_rtc_allocate_device(&spi->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	spi_set_drvdata(spi, rtc);
	rtc->ops = &r9701_rtc_ops;
	rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rtc->range_max = RTC_TIMESTAMP_END_2099;

	return devm_rtc_register_device(rtc);
}

static struct spi_driver r9701_driver = {
	.driver = {
		.name	= "rtc-r9701",
	},
	.probe	= r9701_probe,
};

module_spi_driver(r9701_driver);

MODULE_DESCRIPTION("r9701 spi RTC driver");
MODULE_AUTHOR("Magnus Damm <damm@opensource.se>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:rtc-r9701");

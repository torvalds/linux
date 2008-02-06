/* drivers/rtc/rtc-r9701.c
 *
 * Driver for Epson RTC-9701JE
 *
 * Copyright (C) 2008 Magnus Damm
 *
 * Based on drivers/rtc/rtc-max6902.c
 *
 * Copyright (C) 2006 8D Technologies inc.
 * Copyright (C) 2004 Compulab Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/version.h>
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
	struct spi_device *spi = to_spi_device(dev);
	unsigned long time;
	int ret;
	unsigned char buf[] = { RSECCNT, RMINCNT, RHRCNT,
				RDAYCNT, RMONCNT, RYRCNT };

	ret = read_regs(&spi->dev, buf, ARRAY_SIZE(buf));
	if (ret)
		return ret;

	memset(dt, 0, sizeof(*dt));

	dt->tm_sec = BCD2BIN(buf[0]); /* RSECCNT */
	dt->tm_min = BCD2BIN(buf[1]); /* RMINCNT */
	dt->tm_hour = BCD2BIN(buf[2]); /* RHRCNT */

	dt->tm_mday = BCD2BIN(buf[3]); /* RDAYCNT */
	dt->tm_mon = BCD2BIN(buf[4]) - 1; /* RMONCNT */
	dt->tm_year = BCD2BIN(buf[5]) + 100; /* RYRCNT */

	/* the rtc device may contain illegal values on power up
	 * according to the data sheet. make sure they are valid.
	 */

	ret = rtc_valid_tm(dt);
	if (ret)
		return ret;

	/* don't bother with yday, wday and isdst.
	 * let the rtc core calculate them for us.
	 */

	rtc_tm_to_time(dt, &time);
	rtc_time_to_tm(time, dt);
	return 0;
}

static int r9701_set_datetime(struct device *dev, struct rtc_time *dt)
{
	int ret, year;

	year = dt->tm_year + 1900;
	if (year >= 2100 || year < 2000)
		return -EINVAL;

	ret = write_reg(dev, RHRCNT, BIN2BCD(dt->tm_hour));
	ret |= write_reg(dev, RMINCNT, BIN2BCD(dt->tm_min));
	ret |= write_reg(dev, RSECCNT, BIN2BCD(dt->tm_sec));
	ret |= write_reg(dev, RDAYCNT, BIN2BCD(dt->tm_mday));
	ret |= write_reg(dev, RMONCNT, BIN2BCD(dt->tm_mon + 1));
	ret |= write_reg(dev, RYRCNT, BIN2BCD(dt->tm_year - 100));
	ret |= write_reg(dev, RWKCNT, 1 << dt->tm_wday);

	return ret;
}

static const struct rtc_class_ops r9701_rtc_ops = {
	.read_time	= r9701_get_datetime,
	.set_time	= r9701_set_datetime,
};

static int __devinit r9701_probe(struct spi_device *spi)
{
	struct rtc_device *rtc;
	unsigned char tmp;
	int res;

	rtc = rtc_device_register("r9701",
				&spi->dev, &r9701_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	dev_set_drvdata(&spi->dev, rtc);

	spi->mode = SPI_MODE_3;
	spi->bits_per_word = 8;
	spi_setup(spi);

	tmp = R100CNT;
	res = read_regs(&spi->dev, &tmp, 1);
	if (res || tmp != 0x20) {
		rtc_device_unregister(rtc);
		return res;
	}

	return 0;
}

static int __devexit r9701_remove(struct spi_device *spi)
{
	struct rtc_device *rtc = dev_get_drvdata(&spi->dev);

	rtc_device_unregister(rtc);
	return 0;
}

static struct spi_driver r9701_driver = {
	.driver = {
		.name	= "rtc-r9701",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe	= r9701_probe,
	.remove = __devexit_p(r9701_remove),
};

static __init int r9701_init(void)
{
	return spi_register_driver(&r9701_driver);
}
module_init(r9701_init);

static __exit void r9701_exit(void)
{
	spi_unregister_driver(&r9701_driver);
}
module_exit(r9701_exit);

MODULE_DESCRIPTION("r9701 spi RTC driver");
MODULE_AUTHOR("Magnus Damm <damm@opensource.se>");
MODULE_LICENSE("GPL");

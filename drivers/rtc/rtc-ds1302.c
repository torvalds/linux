/*
 * Dallas DS1302 RTC Support
 *
 *  Copyright (C) 2002 David McCullough
 *  Copyright (C) 2003 - 2007 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License version 2. See the file "COPYING" in the main directory of
 * this archive for more details.
 */

#include <linux/bcd.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/rtc.h>
#include <linux/spi/spi.h>

#define DRV_NAME	"rtc-ds1302"

#define	RTC_CMD_READ	0x81		/* Read command */
#define	RTC_CMD_WRITE	0x80		/* Write command */

#define	RTC_CMD_WRITE_ENABLE	0x00		/* Write enable */
#define	RTC_CMD_WRITE_DISABLE	0x80		/* Write disable */

#define RTC_ADDR_RAM0	0x20		/* Address of RAM0 */
#define RTC_ADDR_TCR	0x08		/* Address of trickle charge register */
#define RTC_CLCK_BURST	0x1F		/* Address of clock burst */
#define	RTC_CLCK_LEN	0x08		/* Size of clock burst */
#define	RTC_ADDR_CTRL	0x07		/* Address of control register */
#define	RTC_ADDR_YEAR	0x06		/* Address of year register */
#define	RTC_ADDR_DAY	0x05		/* Address of day of week register */
#define	RTC_ADDR_MON	0x04		/* Address of month register */
#define	RTC_ADDR_DATE	0x03		/* Address of day of month register */
#define	RTC_ADDR_HOUR	0x02		/* Address of hour register */
#define	RTC_ADDR_MIN	0x01		/* Address of minute register */
#define	RTC_ADDR_SEC	0x00		/* Address of second register */

static int ds1302_rtc_set_time(struct device *dev, struct rtc_time *time)
{
	struct spi_device	*spi = dev_get_drvdata(dev);
	u8		buf[1 + RTC_CLCK_LEN];
	u8		*bp = buf;
	int		status;

	/* Enable writing */
	bp = buf;
	*bp++ = RTC_ADDR_CTRL << 1 | RTC_CMD_WRITE;
	*bp++ = RTC_CMD_WRITE_ENABLE;

	status = spi_write_then_read(spi, buf, 2,
			NULL, 0);
	if (status)
		return status;

	/* Write registers starting at the first time/date address. */
	bp = buf;
	*bp++ = RTC_CLCK_BURST << 1 | RTC_CMD_WRITE;

	*bp++ = bin2bcd(time->tm_sec);
	*bp++ = bin2bcd(time->tm_min);
	*bp++ = bin2bcd(time->tm_hour);
	*bp++ = bin2bcd(time->tm_mday);
	*bp++ = bin2bcd(time->tm_mon + 1);
	*bp++ = time->tm_wday + 1;
	*bp++ = bin2bcd(time->tm_year % 100);
	*bp++ = RTC_CMD_WRITE_DISABLE;

	/* use write-then-read since dma from stack is nonportable */
	return spi_write_then_read(spi, buf, sizeof(buf),
			NULL, 0);
}

static int ds1302_rtc_get_time(struct device *dev, struct rtc_time *time)
{
	struct spi_device	*spi = dev_get_drvdata(dev);
	u8		addr = RTC_CLCK_BURST << 1 | RTC_CMD_READ;
	u8		buf[RTC_CLCK_LEN - 1];
	int		status;

	/* Use write-then-read to get all the date/time registers
	 * since dma from stack is nonportable
	 */
	status = spi_write_then_read(spi, &addr, sizeof(addr),
			buf, sizeof(buf));
	if (status < 0)
		return status;

	/* Decode the registers */
	time->tm_sec = bcd2bin(buf[RTC_ADDR_SEC]);
	time->tm_min = bcd2bin(buf[RTC_ADDR_MIN]);
	time->tm_hour = bcd2bin(buf[RTC_ADDR_HOUR]);
	time->tm_wday = buf[RTC_ADDR_DAY] - 1;
	time->tm_mday = bcd2bin(buf[RTC_ADDR_DATE]);
	time->tm_mon = bcd2bin(buf[RTC_ADDR_MON]) - 1;
	time->tm_year = bcd2bin(buf[RTC_ADDR_YEAR]) + 100;

	/* Time may not be set */
	return rtc_valid_tm(time);
}

static const struct rtc_class_ops ds1302_rtc_ops = {
	.read_time	= ds1302_rtc_get_time,
	.set_time	= ds1302_rtc_set_time,
};

static int ds1302_probe(struct spi_device *spi)
{
	struct rtc_device	*rtc;
	u8		addr;
	u8		buf[4];
	u8		*bp = buf;
	int		status;

	/* Sanity check board setup data.  This may be hooked up
	 * in 3wire mode, but we don't care.  Note that unless
	 * there's an inverter in place, this needs SPI_CS_HIGH!
	 */
	if (spi->bits_per_word && (spi->bits_per_word != 8)) {
		dev_err(&spi->dev, "bad word length\n");
		return -EINVAL;
	} else if (spi->max_speed_hz > 2000000) {
		dev_err(&spi->dev, "speed is too high\n");
		return -EINVAL;
	} else if (spi->mode & SPI_CPHA) {
		dev_err(&spi->dev, "bad mode\n");
		return -EINVAL;
	}

	addr = RTC_ADDR_CTRL << 1 | RTC_CMD_READ;
	status = spi_write_then_read(spi, &addr, sizeof(addr), buf, 1);
	if (status < 0) {
		dev_err(&spi->dev, "control register read error %d\n",
				status);
		return status;
	}

	if ((buf[0] & ~RTC_CMD_WRITE_DISABLE) != 0) {
		status = spi_write_then_read(spi, &addr, sizeof(addr), buf, 1);
		if (status < 0) {
			dev_err(&spi->dev, "control register read error %d\n",
					status);
			return status;
		}

		if ((buf[0] & ~RTC_CMD_WRITE_DISABLE) != 0) {
			dev_err(&spi->dev, "junk in control register\n");
			return -ENODEV;
		}
	}
	if (buf[0] == 0) {
		bp = buf;
		*bp++ = RTC_ADDR_CTRL << 1 | RTC_CMD_WRITE;
		*bp++ = RTC_CMD_WRITE_DISABLE;

		status = spi_write_then_read(spi, buf, 2, NULL, 0);
		if (status < 0) {
			dev_err(&spi->dev, "control register write error %d\n",
					status);
			return status;
		}

		addr = RTC_ADDR_CTRL << 1 | RTC_CMD_READ;
		status = spi_write_then_read(spi, &addr, sizeof(addr), buf, 1);
		if (status < 0) {
			dev_err(&spi->dev,
					"error %d reading control register\n",
					status);
			return status;
		}

		if (buf[0] != RTC_CMD_WRITE_DISABLE) {
			dev_err(&spi->dev, "failed to detect chip\n");
			return -ENODEV;
		}
	}

	spi_set_drvdata(spi, spi);

	rtc = devm_rtc_device_register(&spi->dev, "ds1302",
			&ds1302_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		status = PTR_ERR(rtc);
		dev_err(&spi->dev, "error %d registering rtc\n", status);
		return status;
	}

	return 0;
}

static int ds1302_remove(struct spi_device *spi)
{
	spi_set_drvdata(spi, NULL);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id ds1302_dt_ids[] = {
	{ .compatible = "maxim,ds1302", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ds1302_dt_ids);
#endif

static struct spi_driver ds1302_driver = {
	.driver.name	= "rtc-ds1302",
	.driver.of_match_table = of_match_ptr(ds1302_dt_ids),
	.probe		= ds1302_probe,
	.remove		= ds1302_remove,
};

module_spi_driver(ds1302_driver);

MODULE_DESCRIPTION("Dallas DS1302 RTC driver");
MODULE_AUTHOR("Paul Mundt, David McCullough");
MODULE_LICENSE("GPL v2");

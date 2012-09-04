/*
 * An SPI driver for the Philips PCF2123 RTC
 * Copyright 2009 Cyber Switching, Inc.
 *
 * Author: Chris Verges <chrisv@cyberswitching.com>
 * Maintainers: http://www.cyberswitching.com
 *
 * based on the RS5C348 driver in this same directory.
 *
 * Thanks to Christian Pellegrin <chripell@fsfe.org> for
 * the sysfs contributions to this driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Please note that the CS is active high, so platform data
 * should look something like:
 *
 * static struct spi_board_info ek_spi_devices[] = {
 * 	...
 * 	{
 * 		.modalias		= "rtc-pcf2123",
 * 		.chip_select		= 1,
 * 		.controller_data	= (void *)AT91_PIN_PA10,
 *		.max_speed_hz		= 1000 * 1000,
 *		.mode			= SPI_CS_HIGH,
 *		.bus_num		= 0,
 *	},
 *	...
 *};
 *
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
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/sysfs.h>

#define DRV_VERSION "0.6"

#define PCF2123_REG_CTRL1	(0x00)	/* Control Register 1 */
#define PCF2123_REG_CTRL2	(0x01)	/* Control Register 2 */
#define PCF2123_REG_SC		(0x02)	/* datetime */
#define PCF2123_REG_MN		(0x03)
#define PCF2123_REG_HR		(0x04)
#define PCF2123_REG_DM		(0x05)
#define PCF2123_REG_DW		(0x06)
#define PCF2123_REG_MO		(0x07)
#define PCF2123_REG_YR		(0x08)

#define PCF2123_SUBADDR		(1 << 4)
#define PCF2123_WRITE		((0 << 7) | PCF2123_SUBADDR)
#define PCF2123_READ		((1 << 7) | PCF2123_SUBADDR)

static struct spi_driver pcf2123_driver;

struct pcf2123_sysfs_reg {
	struct device_attribute attr;
	char name[2];
};

struct pcf2123_plat_data {
	struct rtc_device *rtc;
	struct pcf2123_sysfs_reg regs[16];
};

/*
 * Causes a 30 nanosecond delay to ensure that the PCF2123 chip select
 * is released properly after an SPI write.  This function should be
 * called after EVERY read/write call over SPI.
 */
static inline void pcf2123_delay_trec(void)
{
	ndelay(30);
}

static ssize_t pcf2123_show(struct device *dev, struct device_attribute *attr,
			    char *buffer)
{
	struct spi_device *spi = to_spi_device(dev);
	struct pcf2123_sysfs_reg *r;
	u8 txbuf[1], rxbuf[1];
	unsigned long reg;
	int ret;

	r = container_of(attr, struct pcf2123_sysfs_reg, attr);

	if (strict_strtoul(r->name, 16, &reg))
		return -EINVAL;

	txbuf[0] = PCF2123_READ | reg;
	ret = spi_write_then_read(spi, txbuf, 1, rxbuf, 1);
	if (ret < 0)
		return -EIO;
	pcf2123_delay_trec();
	return sprintf(buffer, "0x%x\n", rxbuf[0]);
}

static ssize_t pcf2123_store(struct device *dev, struct device_attribute *attr,
			     const char *buffer, size_t count) {
	struct spi_device *spi = to_spi_device(dev);
	struct pcf2123_sysfs_reg *r;
	u8 txbuf[2];
	unsigned long reg;
	unsigned long val;

	int ret;

	r = container_of(attr, struct pcf2123_sysfs_reg, attr);

	if (strict_strtoul(r->name, 16, &reg)
		|| strict_strtoul(buffer, 10, &val))
		return -EINVAL;

	txbuf[0] = PCF2123_WRITE | reg;
	txbuf[1] = val;
	ret = spi_write(spi, txbuf, sizeof(txbuf));
	if (ret < 0)
		return -EIO;
	pcf2123_delay_trec();
	return count;
}

static int pcf2123_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct spi_device *spi = to_spi_device(dev);
	u8 txbuf[1], rxbuf[7];
	int ret;

	txbuf[0] = PCF2123_READ | PCF2123_REG_SC;
	ret = spi_write_then_read(spi, txbuf, sizeof(txbuf),
			rxbuf, sizeof(rxbuf));
	if (ret < 0)
		return ret;
	pcf2123_delay_trec();

	tm->tm_sec = bcd2bin(rxbuf[0] & 0x7F);
	tm->tm_min = bcd2bin(rxbuf[1] & 0x7F);
	tm->tm_hour = bcd2bin(rxbuf[2] & 0x3F); /* rtc hr 0-23 */
	tm->tm_mday = bcd2bin(rxbuf[3] & 0x3F);
	tm->tm_wday = rxbuf[4] & 0x07;
	tm->tm_mon = bcd2bin(rxbuf[5] & 0x1F) - 1; /* rtc mn 1-12 */
	tm->tm_year = bcd2bin(rxbuf[6]);
	if (tm->tm_year < 70)
		tm->tm_year += 100;	/* assume we are in 1970...2069 */

	dev_dbg(dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
			"mday=%d, mon=%d, year=%d, wday=%d\n",
			__func__,
			tm->tm_sec, tm->tm_min, tm->tm_hour,
			tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	/* the clock can give out invalid datetime, but we cannot return
	 * -EINVAL otherwise hwclock will refuse to set the time on bootup.
	 */
	if (rtc_valid_tm(tm) < 0)
		dev_err(dev, "retrieved date/time is not valid.\n");

	return 0;
}

static int pcf2123_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct spi_device *spi = to_spi_device(dev);
	u8 txbuf[8];
	int ret;

	dev_dbg(dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
			"mday=%d, mon=%d, year=%d, wday=%d\n",
			__func__,
			tm->tm_sec, tm->tm_min, tm->tm_hour,
			tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	/* Stop the counter first */
	txbuf[0] = PCF2123_WRITE | PCF2123_REG_CTRL1;
	txbuf[1] = 0x20;
	ret = spi_write(spi, txbuf, 2);
	if (ret < 0)
		return ret;
	pcf2123_delay_trec();

	/* Set the new time */
	txbuf[0] = PCF2123_WRITE | PCF2123_REG_SC;
	txbuf[1] = bin2bcd(tm->tm_sec & 0x7F);
	txbuf[2] = bin2bcd(tm->tm_min & 0x7F);
	txbuf[3] = bin2bcd(tm->tm_hour & 0x3F);
	txbuf[4] = bin2bcd(tm->tm_mday & 0x3F);
	txbuf[5] = tm->tm_wday & 0x07;
	txbuf[6] = bin2bcd((tm->tm_mon + 1) & 0x1F); /* rtc mn 1-12 */
	txbuf[7] = bin2bcd(tm->tm_year < 100 ? tm->tm_year : tm->tm_year - 100);

	ret = spi_write(spi, txbuf, sizeof(txbuf));
	if (ret < 0)
		return ret;
	pcf2123_delay_trec();

	/* Start the counter */
	txbuf[0] = PCF2123_WRITE | PCF2123_REG_CTRL1;
	txbuf[1] = 0x00;
	ret = spi_write(spi, txbuf, 2);
	if (ret < 0)
		return ret;
	pcf2123_delay_trec();

	return 0;
}

static const struct rtc_class_ops pcf2123_rtc_ops = {
	.read_time	= pcf2123_rtc_read_time,
	.set_time	= pcf2123_rtc_set_time,
};

static int __devinit pcf2123_probe(struct spi_device *spi)
{
	struct rtc_device *rtc;
	struct pcf2123_plat_data *pdata;
	u8 txbuf[2], rxbuf[2];
	int ret, i;

	pdata = kzalloc(sizeof(struct pcf2123_plat_data), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	spi->dev.platform_data = pdata;

	/* Send a software reset command */
	txbuf[0] = PCF2123_WRITE | PCF2123_REG_CTRL1;
	txbuf[1] = 0x58;
	dev_dbg(&spi->dev, "resetting RTC (0x%02X 0x%02X)\n",
			txbuf[0], txbuf[1]);
	ret = spi_write(spi, txbuf, 2 * sizeof(u8));
	if (ret < 0)
		goto kfree_exit;
	pcf2123_delay_trec();

	/* Stop the counter */
	txbuf[0] = PCF2123_WRITE | PCF2123_REG_CTRL1;
	txbuf[1] = 0x20;
	dev_dbg(&spi->dev, "stopping RTC (0x%02X 0x%02X)\n",
			txbuf[0], txbuf[1]);
	ret = spi_write(spi, txbuf, 2 * sizeof(u8));
	if (ret < 0)
		goto kfree_exit;
	pcf2123_delay_trec();

	/* See if the counter was actually stopped */
	txbuf[0] = PCF2123_READ | PCF2123_REG_CTRL1;
	dev_dbg(&spi->dev, "checking for presence of RTC (0x%02X)\n",
			txbuf[0]);
	ret = spi_write_then_read(spi, txbuf, 1 * sizeof(u8),
					rxbuf, 2 * sizeof(u8));
	dev_dbg(&spi->dev, "received data from RTC (0x%02X 0x%02X)\n",
			rxbuf[0], rxbuf[1]);
	if (ret < 0)
		goto kfree_exit;
	pcf2123_delay_trec();

	if (!(rxbuf[0] & 0x20)) {
		dev_err(&spi->dev, "chip not found\n");
		goto kfree_exit;
	}

	dev_info(&spi->dev, "chip found, driver version " DRV_VERSION "\n");
	dev_info(&spi->dev, "spiclk %u KHz.\n",
			(spi->max_speed_hz + 500) / 1000);

	/* Start the counter */
	txbuf[0] = PCF2123_WRITE | PCF2123_REG_CTRL1;
	txbuf[1] = 0x00;
	ret = spi_write(spi, txbuf, sizeof(txbuf));
	if (ret < 0)
		goto kfree_exit;
	pcf2123_delay_trec();

	/* Finalize the initialization */
	rtc = rtc_device_register(pcf2123_driver.driver.name, &spi->dev,
			&pcf2123_rtc_ops, THIS_MODULE);

	if (IS_ERR(rtc)) {
		dev_err(&spi->dev, "failed to register.\n");
		ret = PTR_ERR(rtc);
		goto kfree_exit;
	}

	pdata->rtc = rtc;

	for (i = 0; i < 16; i++) {
		sysfs_attr_init(&pdata->regs[i].attr.attr);
		sprintf(pdata->regs[i].name, "%1x", i);
		pdata->regs[i].attr.attr.mode = S_IRUGO | S_IWUSR;
		pdata->regs[i].attr.attr.name = pdata->regs[i].name;
		pdata->regs[i].attr.show = pcf2123_show;
		pdata->regs[i].attr.store = pcf2123_store;
		ret = device_create_file(&spi->dev, &pdata->regs[i].attr);
		if (ret) {
			dev_err(&spi->dev, "Unable to create sysfs %s\n",
				pdata->regs[i].name);
			goto sysfs_exit;
		}
	}

	return 0;

sysfs_exit:
	for (i--; i >= 0; i--)
		device_remove_file(&spi->dev, &pdata->regs[i].attr);

kfree_exit:
	kfree(pdata);
	spi->dev.platform_data = NULL;
	return ret;
}

static int __devexit pcf2123_remove(struct spi_device *spi)
{
	struct pcf2123_plat_data *pdata = spi->dev.platform_data;
	int i;

	if (pdata) {
		struct rtc_device *rtc = pdata->rtc;

		if (rtc)
			rtc_device_unregister(rtc);
		for (i = 0; i < 16; i++)
			if (pdata->regs[i].name[0])
				device_remove_file(&spi->dev,
						   &pdata->regs[i].attr);
		kfree(pdata);
	}

	return 0;
}

static struct spi_driver pcf2123_driver = {
	.driver	= {
			.name	= "rtc-pcf2123",
			.owner	= THIS_MODULE,
	},
	.probe	= pcf2123_probe,
	.remove	= __devexit_p(pcf2123_remove),
};

module_spi_driver(pcf2123_driver);

MODULE_AUTHOR("Chris Verges <chrisv@cyberswitching.com>");
MODULE_DESCRIPTION("NXP PCF2123 RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

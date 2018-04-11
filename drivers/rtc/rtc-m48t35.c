/*
 * Driver for the SGS-Thomson M48T35 Timekeeper RAM chip
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Written by Ulf Carlsson (ulfc@engr.sgi.com)
 *
 * Copyright (C) 2008 Thomas Bogendoerfer
 *
 * Based on code written by Paul Gortmaker.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/bcd.h>
#include <linux/io.h>
#include <linux/err.h>

struct m48t35_rtc {
	u8	pad[0x7ff8];    /* starts at 0x7ff8 */
	u8	control;
	u8	sec;
	u8	min;
	u8	hour;
	u8	day;
	u8	date;
	u8	month;
	u8	year;
};

#define M48T35_RTC_SET		0x80
#define M48T35_RTC_READ		0x40

struct m48t35_priv {
	struct rtc_device *rtc;
	struct m48t35_rtc __iomem *reg;
	size_t size;
	unsigned long baseaddr;
	spinlock_t lock;
};

static int m48t35_read_time(struct device *dev, struct rtc_time *tm)
{
	struct m48t35_priv *priv = dev_get_drvdata(dev);
	u8 control;

	/*
	 * Only the values that we read from the RTC are set. We leave
	 * tm_wday, tm_yday and tm_isdst untouched. Even though the
	 * RTC has RTC_DAY_OF_WEEK, we ignore it, as it is only updated
	 * by the RTC when initially set to a non-zero value.
	 */
	spin_lock_irq(&priv->lock);
	control = readb(&priv->reg->control);
	writeb(control | M48T35_RTC_READ, &priv->reg->control);
	tm->tm_sec = readb(&priv->reg->sec);
	tm->tm_min = readb(&priv->reg->min);
	tm->tm_hour = readb(&priv->reg->hour);
	tm->tm_mday = readb(&priv->reg->date);
	tm->tm_mon = readb(&priv->reg->month);
	tm->tm_year = readb(&priv->reg->year);
	writeb(control, &priv->reg->control);
	spin_unlock_irq(&priv->lock);

	tm->tm_sec = bcd2bin(tm->tm_sec);
	tm->tm_min = bcd2bin(tm->tm_min);
	tm->tm_hour = bcd2bin(tm->tm_hour);
	tm->tm_mday = bcd2bin(tm->tm_mday);
	tm->tm_mon = bcd2bin(tm->tm_mon);
	tm->tm_year = bcd2bin(tm->tm_year);

	/*
	 * Account for differences between how the RTC uses the values
	 * and how they are defined in a struct rtc_time;
	 */
	tm->tm_year += 70;
	if (tm->tm_year <= 69)
		tm->tm_year += 100;

	tm->tm_mon--;
	return 0;
}

static int m48t35_set_time(struct device *dev, struct rtc_time *tm)
{
	struct m48t35_priv *priv = dev_get_drvdata(dev);
	unsigned char mon, day, hrs, min, sec;
	unsigned int yrs;
	u8 control;

	yrs = tm->tm_year + 1900;
	mon = tm->tm_mon + 1;   /* tm_mon starts at zero */
	day = tm->tm_mday;
	hrs = tm->tm_hour;
	min = tm->tm_min;
	sec = tm->tm_sec;

	if (yrs < 1970)
		return -EINVAL;

	yrs -= 1970;
	if (yrs > 255)    /* They are unsigned */
		return -EINVAL;

	if (yrs > 169)
		return -EINVAL;

	if (yrs >= 100)
		yrs -= 100;

	sec = bin2bcd(sec);
	min = bin2bcd(min);
	hrs = bin2bcd(hrs);
	day = bin2bcd(day);
	mon = bin2bcd(mon);
	yrs = bin2bcd(yrs);

	spin_lock_irq(&priv->lock);
	control = readb(&priv->reg->control);
	writeb(control | M48T35_RTC_SET, &priv->reg->control);
	writeb(yrs, &priv->reg->year);
	writeb(mon, &priv->reg->month);
	writeb(day, &priv->reg->date);
	writeb(hrs, &priv->reg->hour);
	writeb(min, &priv->reg->min);
	writeb(sec, &priv->reg->sec);
	writeb(control, &priv->reg->control);
	spin_unlock_irq(&priv->lock);
	return 0;
}

static const struct rtc_class_ops m48t35_ops = {
	.read_time	= m48t35_read_time,
	.set_time	= m48t35_set_time,
};

static int m48t35_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct m48t35_priv *priv;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;
	priv = devm_kzalloc(&pdev->dev, sizeof(struct m48t35_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->size = resource_size(res);
	/*
	 * kludge: remove the #ifndef after ioc3 resource
	 * conflicts are resolved
	 */
#ifndef CONFIG_SGI_IP27
	if (!devm_request_mem_region(&pdev->dev, res->start, priv->size,
				     pdev->name))
		return -EBUSY;
#endif
	priv->baseaddr = res->start;
	priv->reg = devm_ioremap(&pdev->dev, priv->baseaddr, priv->size);
	if (!priv->reg)
		return -ENOMEM;

	spin_lock_init(&priv->lock);

	platform_set_drvdata(pdev, priv);

	priv->rtc = devm_rtc_device_register(&pdev->dev, "m48t35",
				  &m48t35_ops, THIS_MODULE);
	return PTR_ERR_OR_ZERO(priv->rtc);
}

static struct platform_driver m48t35_platform_driver = {
	.driver		= {
		.name	= "rtc-m48t35",
	},
	.probe		= m48t35_probe,
};

module_platform_driver(m48t35_platform_driver);

MODULE_AUTHOR("Thomas Bogendoerfer <tsbogend@alpha.franken.de>");
MODULE_DESCRIPTION("M48T35 RTC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtc-m48t35");

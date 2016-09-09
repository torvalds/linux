/*
 * An rtc driver for the Dallas DS1553
 *
 * Copyright (C) 2006 Atsushi Nemoto <anemo@mba.ocn.ne.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bcd.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/module.h>

#define RTC_REG_SIZE		0x2000
#define RTC_OFFSET		0x1ff0

#define RTC_FLAGS		(RTC_OFFSET + 0)
#define RTC_SECONDS_ALARM	(RTC_OFFSET + 2)
#define RTC_MINUTES_ALARM	(RTC_OFFSET + 3)
#define RTC_HOURS_ALARM		(RTC_OFFSET + 4)
#define RTC_DATE_ALARM		(RTC_OFFSET + 5)
#define RTC_INTERRUPTS		(RTC_OFFSET + 6)
#define RTC_WATCHDOG		(RTC_OFFSET + 7)
#define RTC_CONTROL		(RTC_OFFSET + 8)
#define RTC_CENTURY		(RTC_OFFSET + 8)
#define RTC_SECONDS		(RTC_OFFSET + 9)
#define RTC_MINUTES		(RTC_OFFSET + 10)
#define RTC_HOURS		(RTC_OFFSET + 11)
#define RTC_DAY			(RTC_OFFSET + 12)
#define RTC_DATE		(RTC_OFFSET + 13)
#define RTC_MONTH		(RTC_OFFSET + 14)
#define RTC_YEAR		(RTC_OFFSET + 15)

#define RTC_CENTURY_MASK	0x3f
#define RTC_SECONDS_MASK	0x7f
#define RTC_DAY_MASK		0x07

/* Bits in the Control/Century register */
#define RTC_WRITE		0x80
#define RTC_READ		0x40

/* Bits in the Seconds register */
#define RTC_STOP		0x80

/* Bits in the Flags register */
#define RTC_FLAGS_AF		0x40
#define RTC_FLAGS_BLF		0x10

/* Bits in the Interrupts register */
#define RTC_INTS_AE		0x80

struct rtc_plat_data {
	struct rtc_device *rtc;
	void __iomem *ioaddr;
	unsigned long last_jiffies;
	int irq;
	unsigned int irqen;
	int alrm_sec;
	int alrm_min;
	int alrm_hour;
	int alrm_mday;
	spinlock_t lock;
};

static int ds1553_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;
	u8 century;

	century = bin2bcd((tm->tm_year + 1900) / 100);

	writeb(RTC_WRITE, pdata->ioaddr + RTC_CONTROL);

	writeb(bin2bcd(tm->tm_year % 100), ioaddr + RTC_YEAR);
	writeb(bin2bcd(tm->tm_mon + 1), ioaddr + RTC_MONTH);
	writeb(bin2bcd(tm->tm_wday) & RTC_DAY_MASK, ioaddr + RTC_DAY);
	writeb(bin2bcd(tm->tm_mday), ioaddr + RTC_DATE);
	writeb(bin2bcd(tm->tm_hour), ioaddr + RTC_HOURS);
	writeb(bin2bcd(tm->tm_min), ioaddr + RTC_MINUTES);
	writeb(bin2bcd(tm->tm_sec) & RTC_SECONDS_MASK, ioaddr + RTC_SECONDS);

	/* RTC_CENTURY and RTC_CONTROL share same register */
	writeb(RTC_WRITE | (century & RTC_CENTURY_MASK), ioaddr + RTC_CENTURY);
	writeb(century & RTC_CENTURY_MASK, ioaddr + RTC_CONTROL);
	return 0;
}

static int ds1553_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;
	unsigned int year, month, day, hour, minute, second, week;
	unsigned int century;

	/* give enough time to update RTC in case of continuous read */
	if (pdata->last_jiffies == jiffies)
		msleep(1);
	pdata->last_jiffies = jiffies;
	writeb(RTC_READ, ioaddr + RTC_CONTROL);
	second = readb(ioaddr + RTC_SECONDS) & RTC_SECONDS_MASK;
	minute = readb(ioaddr + RTC_MINUTES);
	hour = readb(ioaddr + RTC_HOURS);
	day = readb(ioaddr + RTC_DATE);
	week = readb(ioaddr + RTC_DAY) & RTC_DAY_MASK;
	month = readb(ioaddr + RTC_MONTH);
	year = readb(ioaddr + RTC_YEAR);
	century = readb(ioaddr + RTC_CENTURY) & RTC_CENTURY_MASK;
	writeb(0, ioaddr + RTC_CONTROL);
	tm->tm_sec = bcd2bin(second);
	tm->tm_min = bcd2bin(minute);
	tm->tm_hour = bcd2bin(hour);
	tm->tm_mday = bcd2bin(day);
	tm->tm_wday = bcd2bin(week);
	tm->tm_mon = bcd2bin(month) - 1;
	/* year is 1900 + tm->tm_year */
	tm->tm_year = bcd2bin(year) + bcd2bin(century) * 100 - 1900;

	if (rtc_valid_tm(tm) < 0) {
		dev_err(dev, "retrieved date/time is not valid.\n");
		rtc_time_to_tm(0, tm);
	}
	return 0;
}

static void ds1553_rtc_update_alarm(struct rtc_plat_data *pdata)
{
	void __iomem *ioaddr = pdata->ioaddr;
	unsigned long flags;

	spin_lock_irqsave(&pdata->lock, flags);
	writeb(pdata->alrm_mday < 0 || (pdata->irqen & RTC_UF) ?
	       0x80 : bin2bcd(pdata->alrm_mday),
	       ioaddr + RTC_DATE_ALARM);
	writeb(pdata->alrm_hour < 0 || (pdata->irqen & RTC_UF) ?
	       0x80 : bin2bcd(pdata->alrm_hour),
	       ioaddr + RTC_HOURS_ALARM);
	writeb(pdata->alrm_min < 0 || (pdata->irqen & RTC_UF) ?
	       0x80 : bin2bcd(pdata->alrm_min),
	       ioaddr + RTC_MINUTES_ALARM);
	writeb(pdata->alrm_sec < 0 || (pdata->irqen & RTC_UF) ?
	       0x80 : bin2bcd(pdata->alrm_sec),
	       ioaddr + RTC_SECONDS_ALARM);
	writeb(pdata->irqen ? RTC_INTS_AE : 0, ioaddr + RTC_INTERRUPTS);
	readb(ioaddr + RTC_FLAGS);	/* clear interrupts */
	spin_unlock_irqrestore(&pdata->lock, flags);
}

static int ds1553_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);

	if (pdata->irq <= 0)
		return -EINVAL;
	pdata->alrm_mday = alrm->time.tm_mday;
	pdata->alrm_hour = alrm->time.tm_hour;
	pdata->alrm_min = alrm->time.tm_min;
	pdata->alrm_sec = alrm->time.tm_sec;
	if (alrm->enabled)
		pdata->irqen |= RTC_AF;
	ds1553_rtc_update_alarm(pdata);
	return 0;
}

static int ds1553_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);

	if (pdata->irq <= 0)
		return -EINVAL;
	alrm->time.tm_mday = pdata->alrm_mday < 0 ? 0 : pdata->alrm_mday;
	alrm->time.tm_hour = pdata->alrm_hour < 0 ? 0 : pdata->alrm_hour;
	alrm->time.tm_min = pdata->alrm_min < 0 ? 0 : pdata->alrm_min;
	alrm->time.tm_sec = pdata->alrm_sec < 0 ? 0 : pdata->alrm_sec;
	alrm->enabled = (pdata->irqen & RTC_AF) ? 1 : 0;
	return 0;
}

static irqreturn_t ds1553_rtc_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;
	unsigned long events = 0;

	spin_lock(&pdata->lock);
	/* read and clear interrupt */
	if (readb(ioaddr + RTC_FLAGS) & RTC_FLAGS_AF) {
		events = RTC_IRQF;
		if (readb(ioaddr + RTC_SECONDS_ALARM) & 0x80)
			events |= RTC_UF;
		else
			events |= RTC_AF;
		rtc_update_irq(pdata->rtc, 1, events);
	}
	spin_unlock(&pdata->lock);
	return events ? IRQ_HANDLED : IRQ_NONE;
}

static int ds1553_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);

	if (pdata->irq <= 0)
		return -EINVAL;
	if (enabled)
		pdata->irqen |= RTC_AF;
	else
		pdata->irqen &= ~RTC_AF;
	ds1553_rtc_update_alarm(pdata);
	return 0;
}

static const struct rtc_class_ops ds1553_rtc_ops = {
	.read_time		= ds1553_rtc_read_time,
	.set_time		= ds1553_rtc_set_time,
	.read_alarm		= ds1553_rtc_read_alarm,
	.set_alarm		= ds1553_rtc_set_alarm,
	.alarm_irq_enable	= ds1553_rtc_alarm_irq_enable,
};

static ssize_t ds1553_nvram_read(struct file *filp, struct kobject *kobj,
				 struct bin_attribute *bin_attr,
				 char *buf, loff_t pos, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;
	ssize_t count;

	for (count = 0; count < size; count++)
		*buf++ = readb(ioaddr + pos++);
	return count;
}

static ssize_t ds1553_nvram_write(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *bin_attr,
				  char *buf, loff_t pos, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;
	ssize_t count;

	for (count = 0; count < size; count++)
		writeb(*buf++, ioaddr + pos++);
	return count;
}

static struct bin_attribute ds1553_nvram_attr = {
	.attr = {
		.name = "nvram",
		.mode = S_IRUGO | S_IWUSR,
	},
	.size = RTC_OFFSET,
	.read = ds1553_nvram_read,
	.write = ds1553_nvram_write,
};

static int ds1553_rtc_probe(struct platform_device *pdev)
{
	struct resource *res;
	unsigned int cen, sec;
	struct rtc_plat_data *pdata;
	void __iomem *ioaddr;
	int ret = 0;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ioaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ioaddr))
		return PTR_ERR(ioaddr);
	pdata->ioaddr = ioaddr;
	pdata->irq = platform_get_irq(pdev, 0);

	/* turn RTC on if it was not on */
	sec = readb(ioaddr + RTC_SECONDS);
	if (sec & RTC_STOP) {
		sec &= RTC_SECONDS_MASK;
		cen = readb(ioaddr + RTC_CENTURY) & RTC_CENTURY_MASK;
		writeb(RTC_WRITE, ioaddr + RTC_CONTROL);
		writeb(sec, ioaddr + RTC_SECONDS);
		writeb(cen & RTC_CENTURY_MASK, ioaddr + RTC_CONTROL);
	}
	if (readb(ioaddr + RTC_FLAGS) & RTC_FLAGS_BLF)
		dev_warn(&pdev->dev, "voltage-low detected.\n");

	spin_lock_init(&pdata->lock);
	pdata->last_jiffies = jiffies;
	platform_set_drvdata(pdev, pdata);

	pdata->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
				  &ds1553_rtc_ops, THIS_MODULE);
	if (IS_ERR(pdata->rtc))
		return PTR_ERR(pdata->rtc);

	if (pdata->irq > 0) {
		writeb(0, ioaddr + RTC_INTERRUPTS);
		if (devm_request_irq(&pdev->dev, pdata->irq,
				ds1553_rtc_interrupt,
				0, pdev->name, pdev) < 0) {
			dev_warn(&pdev->dev, "interrupt not available.\n");
			pdata->irq = 0;
		}
	}

	ret = sysfs_create_bin_file(&pdev->dev.kobj, &ds1553_nvram_attr);
	if (ret)
		dev_err(&pdev->dev, "unable to create sysfs file: %s\n",
			ds1553_nvram_attr.attr.name);

	return 0;
}

static int ds1553_rtc_remove(struct platform_device *pdev)
{
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);

	sysfs_remove_bin_file(&pdev->dev.kobj, &ds1553_nvram_attr);
	if (pdata->irq > 0)
		writeb(0, pdata->ioaddr + RTC_INTERRUPTS);
	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:rtc-ds1553");

static struct platform_driver ds1553_rtc_driver = {
	.probe		= ds1553_rtc_probe,
	.remove		= ds1553_rtc_remove,
	.driver		= {
		.name	= "rtc-ds1553",
	},
};

module_platform_driver(ds1553_rtc_driver);

MODULE_AUTHOR("Atsushi Nemoto <anemo@mba.ocn.ne.jp>");
MODULE_DESCRIPTION("Dallas DS1553 RTC driver");
MODULE_LICENSE("GPL");

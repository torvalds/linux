/*
 * A driver for the RTC embedded in the Cirrus Logic EP93XX processors
 * Copyright (c) 2006 Tower Technologies
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <asm/hardware.h>

#define EP93XX_RTC_REG(x)	(EP93XX_RTC_BASE + (x))
#define EP93XX_RTC_DATA		EP93XX_RTC_REG(0x0000)
#define EP93XX_RTC_LOAD		EP93XX_RTC_REG(0x000C)
#define EP93XX_RTC_SWCOMP	EP93XX_RTC_REG(0x0108)

#define DRV_VERSION "0.2"

static int ep93xx_get_swcomp(struct device *dev, unsigned short *preload,
				unsigned short *delete)
{
	unsigned short comp = __raw_readl(EP93XX_RTC_SWCOMP);

	if (preload)
		*preload = comp & 0xffff;

	if (delete)
		*delete = (comp >> 16) & 0x1f;

	return 0;
}

static int ep93xx_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long time = __raw_readl(EP93XX_RTC_DATA);

	rtc_time_to_tm(time, tm);
	return 0;
}

static int ep93xx_rtc_set_mmss(struct device *dev, unsigned long secs)
{
	__raw_writel(secs + 1, EP93XX_RTC_LOAD);
	return 0;
}

static int ep93xx_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	int err;
	unsigned long secs;

	err = rtc_tm_to_time(tm, &secs);
	if (err != 0)
		return err;

	return ep93xx_rtc_set_mmss(dev, secs);
}

static int ep93xx_rtc_proc(struct device *dev, struct seq_file *seq)
{
	unsigned short preload, delete;

	ep93xx_get_swcomp(dev, &preload, &delete);

	seq_printf(seq, "24hr\t\t: yes\n");
	seq_printf(seq, "preload\t\t: %d\n", preload);
	seq_printf(seq, "delete\t\t: %d\n", delete);

	return 0;
}

static struct rtc_class_ops ep93xx_rtc_ops = {
	.read_time	= ep93xx_rtc_read_time,
	.set_time	= ep93xx_rtc_set_time,
	.set_mmss	= ep93xx_rtc_set_mmss,
	.proc		= ep93xx_rtc_proc,
};

static ssize_t ep93xx_sysfs_show_comp_preload(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	unsigned short preload;

	ep93xx_get_swcomp(dev, &preload, NULL);

	return sprintf(buf, "%d\n", preload);
}
static DEVICE_ATTR(comp_preload, S_IRUGO, ep93xx_sysfs_show_comp_preload, NULL);

static ssize_t ep93xx_sysfs_show_comp_delete(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	unsigned short delete;

	ep93xx_get_swcomp(dev, NULL, &delete);

	return sprintf(buf, "%d\n", delete);
}
static DEVICE_ATTR(comp_delete, S_IRUGO, ep93xx_sysfs_show_comp_delete, NULL);


static int __devinit ep93xx_rtc_probe(struct platform_device *dev)
{
	struct rtc_device *rtc = rtc_device_register("ep93xx",
				&dev->dev, &ep93xx_rtc_ops, THIS_MODULE);

	if (IS_ERR(rtc)) {
		dev_err(&dev->dev, "unable to register\n");
		return PTR_ERR(rtc);
	}

	platform_set_drvdata(dev, rtc);

	device_create_file(&dev->dev, &dev_attr_comp_preload);
	device_create_file(&dev->dev, &dev_attr_comp_delete);

	return 0;
}

static int __devexit ep93xx_rtc_remove(struct platform_device *dev)
{
	struct rtc_device *rtc = platform_get_drvdata(dev);

 	if (rtc)
		rtc_device_unregister(rtc);

	platform_set_drvdata(dev, NULL);

	return 0;
}

static struct platform_driver ep93xx_rtc_platform_driver = {
	.driver		= {
		.name	= "ep93xx-rtc",
		.owner	= THIS_MODULE,
	},
	.probe		= ep93xx_rtc_probe,
	.remove		= __devexit_p(ep93xx_rtc_remove),
};

static int __init ep93xx_rtc_init(void)
{
	return platform_driver_register(&ep93xx_rtc_platform_driver);
}

static void __exit ep93xx_rtc_exit(void)
{
	platform_driver_unregister(&ep93xx_rtc_platform_driver);
}

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("EP93XX RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

module_init(ep93xx_rtc_init);
module_exit(ep93xx_rtc_exit);

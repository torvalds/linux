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
#include <linux/io.h>
#include <linux/gfp.h>

#define EP93XX_RTC_DATA			0x000
#define EP93XX_RTC_MATCH		0x004
#define EP93XX_RTC_STATUS		0x008
#define  EP93XX_RTC_STATUS_INTR		 (1<<0)
#define EP93XX_RTC_LOAD			0x00C
#define EP93XX_RTC_CONTROL		0x010
#define  EP93XX_RTC_CONTROL_MIE		 (1<<0)
#define EP93XX_RTC_SWCOMP		0x108
#define  EP93XX_RTC_SWCOMP_DEL_MASK	 0x001f0000
#define  EP93XX_RTC_SWCOMP_DEL_SHIFT	 16
#define  EP93XX_RTC_SWCOMP_INT_MASK	 0x0000ffff
#define  EP93XX_RTC_SWCOMP_INT_SHIFT	 0

#define DRV_VERSION "0.3"

/*
 * struct device dev.platform_data is used to store our private data
 * because struct rtc_device does not have a variable to hold it.
 */
struct ep93xx_rtc {
	void __iomem	*mmio_base;
	struct rtc_device *rtc;
};

static int ep93xx_rtc_get_swcomp(struct device *dev, unsigned short *preload,
				unsigned short *delete)
{
	struct ep93xx_rtc *ep93xx_rtc = dev->platform_data;
	unsigned long comp;

	comp = __raw_readl(ep93xx_rtc->mmio_base + EP93XX_RTC_SWCOMP);

	if (preload)
		*preload = (comp & EP93XX_RTC_SWCOMP_INT_MASK)
				>> EP93XX_RTC_SWCOMP_INT_SHIFT;

	if (delete)
		*delete = (comp & EP93XX_RTC_SWCOMP_DEL_MASK)
				>> EP93XX_RTC_SWCOMP_DEL_SHIFT;

	return 0;
}

static int ep93xx_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct ep93xx_rtc *ep93xx_rtc = dev->platform_data;
	unsigned long time;

	 time = __raw_readl(ep93xx_rtc->mmio_base + EP93XX_RTC_DATA);

	rtc_time_to_tm(time, tm);
	return 0;
}

static int ep93xx_rtc_set_mmss(struct device *dev, unsigned long secs)
{
	struct ep93xx_rtc *ep93xx_rtc = dev->platform_data;

	__raw_writel(secs + 1, ep93xx_rtc->mmio_base + EP93XX_RTC_LOAD);
	return 0;
}

static int ep93xx_rtc_proc(struct device *dev, struct seq_file *seq)
{
	unsigned short preload, delete;

	ep93xx_rtc_get_swcomp(dev, &preload, &delete);

	seq_printf(seq, "preload\t\t: %d\n", preload);
	seq_printf(seq, "delete\t\t: %d\n", delete);

	return 0;
}

static const struct rtc_class_ops ep93xx_rtc_ops = {
	.read_time	= ep93xx_rtc_read_time,
	.set_mmss	= ep93xx_rtc_set_mmss,
	.proc		= ep93xx_rtc_proc,
};

static ssize_t ep93xx_rtc_show_comp_preload(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	unsigned short preload;

	ep93xx_rtc_get_swcomp(dev, &preload, NULL);

	return sprintf(buf, "%d\n", preload);
}
static DEVICE_ATTR(comp_preload, S_IRUGO, ep93xx_rtc_show_comp_preload, NULL);

static ssize_t ep93xx_rtc_show_comp_delete(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	unsigned short delete;

	ep93xx_rtc_get_swcomp(dev, NULL, &delete);

	return sprintf(buf, "%d\n", delete);
}
static DEVICE_ATTR(comp_delete, S_IRUGO, ep93xx_rtc_show_comp_delete, NULL);

static struct attribute *ep93xx_rtc_attrs[] = {
	&dev_attr_comp_preload.attr,
	&dev_attr_comp_delete.attr,
	NULL
};

static const struct attribute_group ep93xx_rtc_sysfs_files = {
	.attrs	= ep93xx_rtc_attrs,
};

static int __devinit ep93xx_rtc_probe(struct platform_device *pdev)
{
	struct ep93xx_rtc *ep93xx_rtc;
	struct resource *res;
	int err;

	ep93xx_rtc = devm_kzalloc(&pdev->dev, sizeof(*ep93xx_rtc), GFP_KERNEL);
	if (!ep93xx_rtc)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENXIO;

	if (!devm_request_mem_region(&pdev->dev, res->start,
				     resource_size(res), pdev->name))
		return -EBUSY;

	ep93xx_rtc->mmio_base = devm_ioremap(&pdev->dev, res->start,
					     resource_size(res));
	if (!ep93xx_rtc->mmio_base)
		return -ENXIO;

	pdev->dev.platform_data = ep93xx_rtc;
	platform_set_drvdata(pdev, ep93xx_rtc);

	ep93xx_rtc->rtc = rtc_device_register(pdev->name,
				&pdev->dev, &ep93xx_rtc_ops, THIS_MODULE);
	if (IS_ERR(ep93xx_rtc->rtc)) {
		err = PTR_ERR(ep93xx_rtc->rtc);
		goto exit;
	}

	err = sysfs_create_group(&pdev->dev.kobj, &ep93xx_rtc_sysfs_files);
	if (err)
		goto fail;

	return 0;

fail:
	rtc_device_unregister(ep93xx_rtc->rtc);
exit:
	platform_set_drvdata(pdev, NULL);
	pdev->dev.platform_data = NULL;
	return err;
}

static int __devexit ep93xx_rtc_remove(struct platform_device *pdev)
{
	struct ep93xx_rtc *ep93xx_rtc = platform_get_drvdata(pdev);

	sysfs_remove_group(&pdev->dev.kobj, &ep93xx_rtc_sysfs_files);
	platform_set_drvdata(pdev, NULL);
	rtc_device_unregister(ep93xx_rtc->rtc);
	pdev->dev.platform_data = NULL;

	return 0;
}

static struct platform_driver ep93xx_rtc_driver = {
	.driver		= {
		.name	= "ep93xx-rtc",
		.owner	= THIS_MODULE,
	},
	.probe		= ep93xx_rtc_probe,
	.remove		= __devexit_p(ep93xx_rtc_remove),
};

module_platform_driver(ep93xx_rtc_driver);

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("EP93XX RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_ALIAS("platform:ep93xx-rtc");

/*
 *  linux/drivers/rtc/rtc-pl030.c
 *
 *  Copyright (C) 2000-2001 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/amba/bus.h>
#include <linux/io.h>

#define RTC_DR		(0)
#define RTC_MR		(4)
#define RTC_STAT	(8)
#define RTC_EOI		(8)
#define RTC_LR		(12)
#define RTC_CR		(16)
#define RTC_CR_MIE	(1 << 0)

struct pl030_rtc {
	struct rtc_device	*rtc;
	void __iomem		*base;
};

static irqreturn_t pl030_interrupt(int irq, void *dev_id)
{
	struct pl030_rtc *rtc = dev_id;
	writel(0, rtc->base + RTC_EOI);
	return IRQ_HANDLED;
}

static int pl030_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}

static int pl030_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct pl030_rtc *rtc = dev_get_drvdata(dev);

	rtc_time_to_tm(readl(rtc->base + RTC_MR), &alrm->time);
	return 0;
}

static int pl030_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct pl030_rtc *rtc = dev_get_drvdata(dev);
	unsigned long time;
	int ret;

	/*
	 * At the moment, we can only deal with non-wildcarded alarm times.
	 */
	ret = rtc_valid_tm(&alrm->time);
	if (ret == 0)
		ret = rtc_tm_to_time(&alrm->time, &time);
	if (ret == 0)
		writel(time, rtc->base + RTC_MR);
	return ret;
}

static int pl030_read_time(struct device *dev, struct rtc_time *tm)
{
	struct pl030_rtc *rtc = dev_get_drvdata(dev);

	rtc_time_to_tm(readl(rtc->base + RTC_DR), tm);

	return 0;
}

/*
 * Set the RTC time.  Unfortunately, we can't accurately set
 * the point at which the counter updates.
 *
 * Also, since RTC_LR is transferred to RTC_CR on next rising
 * edge of the 1Hz clock, we must write the time one second
 * in advance.
 */
static int pl030_set_time(struct device *dev, struct rtc_time *tm)
{
	struct pl030_rtc *rtc = dev_get_drvdata(dev);
	unsigned long time;
	int ret;

	ret = rtc_tm_to_time(tm, &time);
	if (ret == 0)
		writel(time + 1, rtc->base + RTC_LR);

	return ret;
}

static const struct rtc_class_ops pl030_ops = {
	.ioctl		= pl030_ioctl,
	.read_time	= pl030_read_time,
	.set_time	= pl030_set_time,
	.read_alarm	= pl030_read_alarm,
	.set_alarm	= pl030_set_alarm,
};

static int pl030_probe(struct amba_device *dev, void *id)
{
	struct pl030_rtc *rtc;
	int ret;

	ret = amba_request_regions(dev, NULL);
	if (ret)
		goto err_req;

	rtc = kmalloc(sizeof(*rtc), GFP_KERNEL);
	if (!rtc) {
		ret = -ENOMEM;
		goto err_rtc;
	}

	rtc->base = ioremap(dev->res.start, SZ_4K);
	if (!rtc->base) {
		ret = -ENOMEM;
		goto err_map;
	}

	__raw_writel(0, rtc->base + RTC_CR);
	__raw_writel(0, rtc->base + RTC_EOI);

	amba_set_drvdata(dev, rtc);

	ret = request_irq(dev->irq[0], pl030_interrupt, IRQF_DISABLED,
			  "rtc-pl030", rtc);
	if (ret)
		goto err_irq;

	rtc->rtc = rtc_device_register("pl030", &dev->dev, &pl030_ops,
				       THIS_MODULE);
	if (IS_ERR(rtc->rtc)) {
		ret = PTR_ERR(rtc->rtc);
		goto err_reg;
	}

	return 0;

 err_reg:
	free_irq(dev->irq[0], rtc);
 err_irq:
	iounmap(rtc->base);
 err_map:
	kfree(rtc);
 err_rtc:
	amba_release_regions(dev);
 err_req:
	return ret;
}

static int pl030_remove(struct amba_device *dev)
{
	struct pl030_rtc *rtc = amba_get_drvdata(dev);

	amba_set_drvdata(dev, NULL);

	writel(0, rtc->base + RTC_CR);

	free_irq(dev->irq[0], rtc);
	rtc_device_unregister(rtc->rtc);
	iounmap(rtc->base);
	kfree(rtc);
	amba_release_regions(dev);

	return 0;
}

static struct amba_id pl030_ids[] = {
	{
		.id	= 0x00041030,
		.mask	= 0x000fffff,
	},
	{ 0, 0 },
};

static struct amba_driver pl030_driver = {
	.drv		= {
		.name	= "rtc-pl030",
	},
	.probe		= pl030_probe,
	.remove		= pl030_remove,
	.id_table	= pl030_ids,
};

static int __init pl030_init(void)
{
	return amba_driver_register(&pl030_driver);
}

static void __exit pl030_exit(void)
{
	amba_driver_unregister(&pl030_driver);
}

module_init(pl030_init);
module_exit(pl030_exit);

MODULE_AUTHOR("Russell King <rmk@arm.linux.org.uk>");
MODULE_DESCRIPTION("ARM AMBA PL030 RTC Driver");
MODULE_LICENSE("GPL");

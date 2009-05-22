/*
 * drivers/rtc/rtc-pl031.c
 *
 * Real Time Clock interface for ARM AMBA PrimeCell 031 RTC
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2006 (c) MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/amba/bus.h>
#include <linux/io.h>

/*
 * Register definitions
 */
#define	RTC_DR		0x00	/* Data read register */
#define	RTC_MR		0x04	/* Match register */
#define	RTC_LR		0x08	/* Data load register */
#define	RTC_CR		0x0c	/* Control register */
#define	RTC_IMSC	0x10	/* Interrupt mask and set register */
#define	RTC_RIS		0x14	/* Raw interrupt status register */
#define	RTC_MIS		0x18	/* Masked interrupt status register */
#define	RTC_ICR		0x1c	/* Interrupt clear register */

struct pl031_local {
	struct rtc_device *rtc;
	void __iomem *base;
};

static irqreturn_t pl031_interrupt(int irq, void *dev_id)
{
	struct rtc_device *rtc = dev_id;

	rtc_update_irq(rtc, 1, RTC_AF);

	return IRQ_HANDLED;
}

static int pl031_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct pl031_local *ldata = dev_get_drvdata(dev);

	switch (cmd) {
	case RTC_AIE_OFF:
		__raw_writel(1, ldata->base + RTC_MIS);
		return 0;
	case RTC_AIE_ON:
		__raw_writel(0, ldata->base + RTC_MIS);
		return 0;
	}

	return -ENOIOCTLCMD;
}

static int pl031_read_time(struct device *dev, struct rtc_time *tm)
{
	struct pl031_local *ldata = dev_get_drvdata(dev);

	rtc_time_to_tm(__raw_readl(ldata->base + RTC_DR), tm);

	return 0;
}

static int pl031_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long time;
	struct pl031_local *ldata = dev_get_drvdata(dev);

	rtc_tm_to_time(tm, &time);
	__raw_writel(time, ldata->base + RTC_LR);

	return 0;
}

static int pl031_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct pl031_local *ldata = dev_get_drvdata(dev);

	rtc_time_to_tm(__raw_readl(ldata->base + RTC_MR), &alarm->time);
	alarm->pending = __raw_readl(ldata->base + RTC_RIS);
	alarm->enabled = __raw_readl(ldata->base + RTC_IMSC);

	return 0;
}

static int pl031_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct pl031_local *ldata = dev_get_drvdata(dev);
	unsigned long time;

	rtc_tm_to_time(&alarm->time, &time);

	__raw_writel(time, ldata->base + RTC_MR);
	__raw_writel(!alarm->enabled, ldata->base + RTC_MIS);

	return 0;
}

static const struct rtc_class_ops pl031_ops = {
	.ioctl = pl031_ioctl,
	.read_time = pl031_read_time,
	.set_time = pl031_set_time,
	.read_alarm = pl031_read_alarm,
	.set_alarm = pl031_set_alarm,
};

static int pl031_remove(struct amba_device *adev)
{
	struct pl031_local *ldata = dev_get_drvdata(&adev->dev);

	amba_set_drvdata(adev, NULL);
	free_irq(adev->irq[0], ldata->rtc);
	rtc_device_unregister(ldata->rtc);
	iounmap(ldata->base);
	kfree(ldata);
	amba_release_regions(adev);

	return 0;
}

static int pl031_probe(struct amba_device *adev, struct amba_id *id)
{
	int ret;
	struct pl031_local *ldata;

	ret = amba_request_regions(adev, NULL);
	if (ret)
		goto err_req;

	ldata = kmalloc(sizeof(struct pl031_local), GFP_KERNEL);
	if (!ldata) {
		ret = -ENOMEM;
		goto out;
	}

	ldata->base = ioremap(adev->res.start,
			      adev->res.end - adev->res.start + 1);
	if (!ldata->base) {
		ret = -ENOMEM;
		goto out_no_remap;
	}

	amba_set_drvdata(adev, ldata);

	if (request_irq(adev->irq[0], pl031_interrupt, IRQF_DISABLED,
			"rtc-pl031", ldata->rtc)) {
		ret = -EIO;
		goto out_no_irq;
	}

	ldata->rtc = rtc_device_register("pl031", &adev->dev, &pl031_ops,
					 THIS_MODULE);
	if (IS_ERR(ldata->rtc)) {
		ret = PTR_ERR(ldata->rtc);
		goto out_no_rtc;
	}

	return 0;

out_no_rtc:
	free_irq(adev->irq[0], ldata->rtc);
out_no_irq:
	iounmap(ldata->base);
	amba_set_drvdata(adev, NULL);
out_no_remap:
	kfree(ldata);
out:
	amba_release_regions(adev);
err_req:
	return ret;
}

static struct amba_id pl031_ids[] __initdata = {
	{
		 .id = 0x00041031,
	 	.mask = 0x000fffff, },
	{0, 0},
};

static struct amba_driver pl031_driver = {
	.drv = {
		.name = "rtc-pl031",
	},
	.id_table = pl031_ids,
	.probe = pl031_probe,
	.remove = pl031_remove,
};

static int __init pl031_init(void)
{
	return amba_driver_register(&pl031_driver);
}

static void __exit pl031_exit(void)
{
	amba_driver_unregister(&pl031_driver);
}

module_init(pl031_init);
module_exit(pl031_exit);

MODULE_AUTHOR("Deepak Saxena <dsaxena@plexity.net");
MODULE_DESCRIPTION("ARM AMBA PL031 RTC Driver");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/rtc/rtc-pl030.c
 *
 *  Copyright (C) 2000-2001 Deep Blue Solutions Ltd.
 */
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/amba/bus.h>
#include <linux/io.h>
#include <linux/slab.h>

#define RTC_DR		(0)
#define RTC_MR		(4)
#define RTC_STAT	(8)
#define RTC_EOI		(8)
#define RTC_LR		(12)
#define RTC_CR		(16)
#define RTC_CR_MIE	(1 << 0)

struct pl030_rtc {
	void __iomem		*base;
};

static irqreturn_t pl030_interrupt(int irq, void *dev_id)
{
	struct pl030_rtc *rtc = dev_id;
	writel(0, rtc->base + RTC_EOI);
	return IRQ_HANDLED;
}

static int pl030_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct pl030_rtc *rtc = dev_get_drvdata(dev);

	rtc_time64_to_tm(readl(rtc->base + RTC_MR), &alrm->time);
	return 0;
}

static int pl030_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct pl030_rtc *rtc = dev_get_drvdata(dev);

	writel(rtc_tm_to_time64(&alrm->time), rtc->base + RTC_MR);

	return 0;
}

static int pl030_read_time(struct device *dev, struct rtc_time *tm)
{
	struct pl030_rtc *rtc = dev_get_drvdata(dev);

	rtc_time64_to_tm(readl(rtc->base + RTC_DR), tm);

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

	writel(rtc_tm_to_time64(tm) + 1, rtc->base + RTC_LR);

	return 0;
}

static const struct rtc_class_ops pl030_ops = {
	.read_time	= pl030_read_time,
	.set_time	= pl030_set_time,
	.read_alarm	= pl030_read_alarm,
	.set_alarm	= pl030_set_alarm,
};

static int pl030_probe(struct amba_device *dev, const struct amba_id *id)
{
	struct pl030_rtc *rtc;
	int ret;
	struct rtc_device *rtc_dev;

	ret = amba_request_regions(dev, NULL);
	if (ret)
		goto err_req;

	rtc = devm_kzalloc(&dev->dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc) {
		ret = -ENOMEM;
		goto err_rtc;
	}

	rtc_dev = devm_rtc_allocate_device(&dev->dev);
	if (IS_ERR(rtc_dev)) {
		ret = PTR_ERR(rtc_dev);
		goto err_rtc;
	}

	rtc_dev->ops = &pl030_ops;
	rtc_dev->range_max = U32_MAX;
	rtc->base = ioremap(dev->res.start, resource_size(&dev->res));
	if (!rtc->base) {
		ret = -ENOMEM;
		goto err_rtc;
	}

	__raw_writel(0, rtc->base + RTC_CR);
	__raw_writel(0, rtc->base + RTC_EOI);

	amba_set_drvdata(dev, rtc);

	ret = request_irq(dev->irq[0], pl030_interrupt, 0,
			  "rtc-pl030", rtc);
	if (ret)
		goto err_irq;

	ret = devm_rtc_register_device(rtc_dev);
	if (ret)
		goto err_reg;

	return 0;

 err_reg:
	free_irq(dev->irq[0], rtc);
 err_irq:
	iounmap(rtc->base);
 err_rtc:
	amba_release_regions(dev);
 err_req:
	return ret;
}

static void pl030_remove(struct amba_device *dev)
{
	struct pl030_rtc *rtc = amba_get_drvdata(dev);

	writel(0, rtc->base + RTC_CR);

	free_irq(dev->irq[0], rtc);
	iounmap(rtc->base);
	amba_release_regions(dev);
}

static const struct amba_id pl030_ids[] = {
	{
		.id	= 0x00041030,
		.mask	= 0x000fffff,
	},
	{ 0, 0 },
};

MODULE_DEVICE_TABLE(amba, pl030_ids);

static struct amba_driver pl030_driver = {
	.drv		= {
		.name	= "rtc-pl030",
	},
	.probe		= pl030_probe,
	.remove		= pl030_remove,
	.id_table	= pl030_ids,
};

module_amba_driver(pl030_driver);

MODULE_AUTHOR("Russell King <rmk@arm.linux.org.uk>");
MODULE_DESCRIPTION("ARM AMBA PL030 RTC Driver");
MODULE_LICENSE("GPL");

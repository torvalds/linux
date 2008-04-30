/*
 *  linux/arch/arm/mach-integrator/time.c
 *
 *  Copyright (C) 2000-2001 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/mc146818rtc.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/amba/bus.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/rtc.h>

#include <asm/mach/time.h>

#define RTC_DR		(0)
#define RTC_MR		(4)
#define RTC_STAT	(8)
#define RTC_EOI		(8)
#define RTC_LR		(12)
#define RTC_CR		(16)
#define RTC_CR_MIE	(1 << 0)

extern int (*set_rtc)(void);
static void __iomem *rtc_base;

static int integrator_set_rtc(void)
{
	__raw_writel(xtime.tv_sec, rtc_base + RTC_LR);
	return 1;
}

static int integrator_rtc_read_alarm(struct rtc_wkalrm *alrm)
{
	rtc_time_to_tm(readl(rtc_base + RTC_MR), &alrm->time);
	return 0;
}

static inline int integrator_rtc_set_alarm(struct rtc_wkalrm *alrm)
{
	unsigned long time;
	int ret;

	/*
	 * At the moment, we can only deal with non-wildcarded alarm times.
	 */
	ret = rtc_valid_tm(&alrm->time);
	if (ret == 0)
		ret = rtc_tm_to_time(&alrm->time, &time);
	if (ret == 0)
		writel(time, rtc_base + RTC_MR);
	return ret;
}

static int integrator_rtc_read_time(struct rtc_time *tm)
{
	rtc_time_to_tm(readl(rtc_base + RTC_DR), tm);
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
static inline int integrator_rtc_set_time(struct rtc_time *tm)
{
	unsigned long time;
	int ret;

	ret = rtc_tm_to_time(tm, &time);
	if (ret == 0)
		writel(time + 1, rtc_base + RTC_LR);

	return ret;
}

static struct rtc_ops rtc_ops = {
	.owner		= THIS_MODULE,
	.read_time	= integrator_rtc_read_time,
	.set_time	= integrator_rtc_set_time,
	.read_alarm	= integrator_rtc_read_alarm,
	.set_alarm	= integrator_rtc_set_alarm,
};

static irqreturn_t arm_rtc_interrupt(int irq, void *dev_id)
{
	writel(0, rtc_base + RTC_EOI);
	return IRQ_HANDLED;
}

static int rtc_probe(struct amba_device *dev, void *id)
{
	int ret;

	if (rtc_base)
		return -EBUSY;

	ret = amba_request_regions(dev, NULL);
	if (ret)
		goto out;

	rtc_base = ioremap(dev->res.start, SZ_4K);
	if (!rtc_base) {
		ret = -ENOMEM;
		goto res_out;
	}

	__raw_writel(0, rtc_base + RTC_CR);
	__raw_writel(0, rtc_base + RTC_EOI);

	xtime.tv_sec = __raw_readl(rtc_base + RTC_DR);

	/* note that 'dev' is merely used for irq disambiguation;
	 * it is not actually referenced in the irq handler
	 */
	ret = request_irq(dev->irq[0], arm_rtc_interrupt, IRQF_DISABLED,
			  "rtc-pl030", dev);
	if (ret)
		goto map_out;

	ret = register_rtc(&rtc_ops);
	if (ret)
		goto irq_out;

	set_rtc = integrator_set_rtc;
	return 0;

 irq_out:
	free_irq(dev->irq[0], dev);
 map_out:
	iounmap(rtc_base);
	rtc_base = NULL;
 res_out:
	amba_release_regions(dev);
 out:
	return ret;
}

static int rtc_remove(struct amba_device *dev)
{
	set_rtc = NULL;

	writel(0, rtc_base + RTC_CR);

	free_irq(dev->irq[0], dev);
	unregister_rtc(&rtc_ops);

	iounmap(rtc_base);
	rtc_base = NULL;
	amba_release_regions(dev);

	return 0;
}

static struct timespec rtc_delta;

static int rtc_suspend(struct amba_device *dev, pm_message_t state)
{
	struct timespec rtc;

	rtc.tv_sec = readl(rtc_base + RTC_DR);
	rtc.tv_nsec = 0;
	save_time_delta(&rtc_delta, &rtc);

	return 0;
}

static int rtc_resume(struct amba_device *dev)
{
	struct timespec rtc;

	rtc.tv_sec = readl(rtc_base + RTC_DR);
	rtc.tv_nsec = 0;
	restore_time_delta(&rtc_delta, &rtc);

	return 0;
}

static struct amba_id rtc_ids[] = {
	{
		.id	= 0x00041030,
		.mask	= 0x000fffff,
	},
	{ 0, 0 },
};

static struct amba_driver rtc_driver = {
	.drv		= {
		.name	= "rtc-pl030",
	},
	.probe		= rtc_probe,
	.remove		= rtc_remove,
	.suspend	= rtc_suspend,
	.resume		= rtc_resume,
	.id_table	= rtc_ids,
};

static int __init integrator_rtc_init(void)
{
	return amba_driver_register(&rtc_driver);
}

static void __exit integrator_rtc_exit(void)
{
	amba_driver_unregister(&rtc_driver);
}

module_init(integrator_rtc_init);
module_exit(integrator_rtc_exit);

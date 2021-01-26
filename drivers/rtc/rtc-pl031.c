// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/rtc/rtc-pl031.c
 *
 * Real Time Clock interface for ARM AMBA PrimeCell 031 RTC
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2006 (c) MontaVista Software, Inc.
 *
 * Author: Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>
 * Copyright 2010 (c) ST-Ericsson AB
 */
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/amba/bus.h>
#include <linux/io.h>
#include <linux/bcd.h>
#include <linux/delay.h>
#include <linux/pm_wakeirq.h>
#include <linux/slab.h>

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
/* ST variants have additional timer functionality */
#define RTC_TDR		0x20	/* Timer data read register */
#define RTC_TLR		0x24	/* Timer data load register */
#define RTC_TCR		0x28	/* Timer control register */
#define RTC_YDR		0x30	/* Year data read register */
#define RTC_YMR		0x34	/* Year match register */
#define RTC_YLR		0x38	/* Year data load register */

#define RTC_CR_EN	(1 << 0)	/* counter enable bit */
#define RTC_CR_CWEN	(1 << 26)	/* Clockwatch enable bit */

#define RTC_TCR_EN	(1 << 1) /* Periodic timer enable bit */

/* Common bit definitions for Interrupt status and control registers */
#define RTC_BIT_AI	(1 << 0) /* Alarm interrupt bit */
#define RTC_BIT_PI	(1 << 1) /* Periodic interrupt bit. ST variants only. */

/* Common bit definations for ST v2 for reading/writing time */
#define RTC_SEC_SHIFT 0
#define RTC_SEC_MASK (0x3F << RTC_SEC_SHIFT) /* Second [0-59] */
#define RTC_MIN_SHIFT 6
#define RTC_MIN_MASK (0x3F << RTC_MIN_SHIFT) /* Minute [0-59] */
#define RTC_HOUR_SHIFT 12
#define RTC_HOUR_MASK (0x1F << RTC_HOUR_SHIFT) /* Hour [0-23] */
#define RTC_WDAY_SHIFT 17
#define RTC_WDAY_MASK (0x7 << RTC_WDAY_SHIFT) /* Day of Week [1-7] 1=Sunday */
#define RTC_MDAY_SHIFT 20
#define RTC_MDAY_MASK (0x1F << RTC_MDAY_SHIFT) /* Day of Month [1-31] */
#define RTC_MON_SHIFT 25
#define RTC_MON_MASK (0xF << RTC_MON_SHIFT) /* Month [1-12] 1=January */

#define RTC_TIMER_FREQ 32768

/**
 * struct pl031_vendor_data - per-vendor variations
 * @ops: the vendor-specific operations used on this silicon version
 * @clockwatch: if this is an ST Microelectronics silicon version with a
 *	clockwatch function
 * @st_weekday: if this is an ST Microelectronics silicon version that need
 *	the weekday fix
 * @irqflags: special IRQ flags per variant
 */
struct pl031_vendor_data {
	struct rtc_class_ops ops;
	bool clockwatch;
	bool st_weekday;
	unsigned long irqflags;
	time64_t range_min;
	timeu64_t range_max;
};

struct pl031_local {
	struct pl031_vendor_data *vendor;
	struct rtc_device *rtc;
	void __iomem *base;
};

static int pl031_alarm_irq_enable(struct device *dev,
	unsigned int enabled)
{
	struct pl031_local *ldata = dev_get_drvdata(dev);
	unsigned long imsc;

	/* Clear any pending alarm interrupts. */
	writel(RTC_BIT_AI, ldata->base + RTC_ICR);

	imsc = readl(ldata->base + RTC_IMSC);

	if (enabled == 1)
		writel(imsc | RTC_BIT_AI, ldata->base + RTC_IMSC);
	else
		writel(imsc & ~RTC_BIT_AI, ldata->base + RTC_IMSC);

	return 0;
}

/*
 * Convert Gregorian date to ST v2 RTC format.
 */
static int pl031_stv2_tm_to_time(struct device *dev,
				 struct rtc_time *tm, unsigned long *st_time,
	unsigned long *bcd_year)
{
	int year = tm->tm_year + 1900;
	int wday = tm->tm_wday;

	/* wday masking is not working in hardware so wday must be valid */
	if (wday < -1 || wday > 6) {
		dev_err(dev, "invalid wday value %d\n", tm->tm_wday);
		return -EINVAL;
	} else if (wday == -1) {
		/* wday is not provided, calculate it here */
		struct rtc_time calc_tm;

		rtc_time64_to_tm(rtc_tm_to_time64(tm), &calc_tm);
		wday = calc_tm.tm_wday;
	}

	*bcd_year = (bin2bcd(year % 100) | bin2bcd(year / 100) << 8);

	*st_time = ((tm->tm_mon + 1) << RTC_MON_SHIFT)
			|	(tm->tm_mday << RTC_MDAY_SHIFT)
			|	((wday + 1) << RTC_WDAY_SHIFT)
			|	(tm->tm_hour << RTC_HOUR_SHIFT)
			|	(tm->tm_min << RTC_MIN_SHIFT)
			|	(tm->tm_sec << RTC_SEC_SHIFT);

	return 0;
}

/*
 * Convert ST v2 RTC format to Gregorian date.
 */
static int pl031_stv2_time_to_tm(unsigned long st_time, unsigned long bcd_year,
	struct rtc_time *tm)
{
	tm->tm_year = bcd2bin(bcd_year) + (bcd2bin(bcd_year >> 8) * 100);
	tm->tm_mon  = ((st_time & RTC_MON_MASK) >> RTC_MON_SHIFT) - 1;
	tm->tm_mday = ((st_time & RTC_MDAY_MASK) >> RTC_MDAY_SHIFT);
	tm->tm_wday = ((st_time & RTC_WDAY_MASK) >> RTC_WDAY_SHIFT) - 1;
	tm->tm_hour = ((st_time & RTC_HOUR_MASK) >> RTC_HOUR_SHIFT);
	tm->tm_min  = ((st_time & RTC_MIN_MASK) >> RTC_MIN_SHIFT);
	tm->tm_sec  = ((st_time & RTC_SEC_MASK) >> RTC_SEC_SHIFT);

	tm->tm_yday = rtc_year_days(tm->tm_mday, tm->tm_mon, tm->tm_year);
	tm->tm_year -= 1900;

	return 0;
}

static int pl031_stv2_read_time(struct device *dev, struct rtc_time *tm)
{
	struct pl031_local *ldata = dev_get_drvdata(dev);

	pl031_stv2_time_to_tm(readl(ldata->base + RTC_DR),
			readl(ldata->base + RTC_YDR), tm);

	return 0;
}

static int pl031_stv2_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long time;
	unsigned long bcd_year;
	struct pl031_local *ldata = dev_get_drvdata(dev);
	int ret;

	ret = pl031_stv2_tm_to_time(dev, tm, &time, &bcd_year);
	if (ret == 0) {
		writel(bcd_year, ldata->base + RTC_YLR);
		writel(time, ldata->base + RTC_LR);
	}

	return ret;
}

static int pl031_stv2_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct pl031_local *ldata = dev_get_drvdata(dev);
	int ret;

	ret = pl031_stv2_time_to_tm(readl(ldata->base + RTC_MR),
			readl(ldata->base + RTC_YMR), &alarm->time);

	alarm->pending = readl(ldata->base + RTC_RIS) & RTC_BIT_AI;
	alarm->enabled = readl(ldata->base + RTC_IMSC) & RTC_BIT_AI;

	return ret;
}

static int pl031_stv2_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct pl031_local *ldata = dev_get_drvdata(dev);
	unsigned long time;
	unsigned long bcd_year;
	int ret;

	ret = pl031_stv2_tm_to_time(dev, &alarm->time,
				    &time, &bcd_year);
	if (ret == 0) {
		writel(bcd_year, ldata->base + RTC_YMR);
		writel(time, ldata->base + RTC_MR);

		pl031_alarm_irq_enable(dev, alarm->enabled);
	}

	return ret;
}

static irqreturn_t pl031_interrupt(int irq, void *dev_id)
{
	struct pl031_local *ldata = dev_id;
	unsigned long rtcmis;
	unsigned long events = 0;

	rtcmis = readl(ldata->base + RTC_MIS);
	if (rtcmis & RTC_BIT_AI) {
		writel(RTC_BIT_AI, ldata->base + RTC_ICR);
		events |= (RTC_AF | RTC_IRQF);
		rtc_update_irq(ldata->rtc, 1, events);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int pl031_read_time(struct device *dev, struct rtc_time *tm)
{
	struct pl031_local *ldata = dev_get_drvdata(dev);

	rtc_time64_to_tm(readl(ldata->base + RTC_DR), tm);

	return 0;
}

static int pl031_set_time(struct device *dev, struct rtc_time *tm)
{
	struct pl031_local *ldata = dev_get_drvdata(dev);

	writel(rtc_tm_to_time64(tm), ldata->base + RTC_LR);

	return 0;
}

static int pl031_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct pl031_local *ldata = dev_get_drvdata(dev);

	rtc_time64_to_tm(readl(ldata->base + RTC_MR), &alarm->time);

	alarm->pending = readl(ldata->base + RTC_RIS) & RTC_BIT_AI;
	alarm->enabled = readl(ldata->base + RTC_IMSC) & RTC_BIT_AI;

	return 0;
}

static int pl031_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct pl031_local *ldata = dev_get_drvdata(dev);

	writel(rtc_tm_to_time64(&alarm->time), ldata->base + RTC_MR);
	pl031_alarm_irq_enable(dev, alarm->enabled);

	return 0;
}

static void pl031_remove(struct amba_device *adev)
{
	struct pl031_local *ldata = dev_get_drvdata(&adev->dev);

	dev_pm_clear_wake_irq(&adev->dev);
	device_init_wakeup(&adev->dev, false);
	if (adev->irq[0])
		free_irq(adev->irq[0], ldata);
	amba_release_regions(adev);
}

static int pl031_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret;
	struct pl031_local *ldata;
	struct pl031_vendor_data *vendor = id->data;
	struct rtc_class_ops *ops;
	unsigned long time, data;

	ret = amba_request_regions(adev, NULL);
	if (ret)
		goto err_req;

	ldata = devm_kzalloc(&adev->dev, sizeof(struct pl031_local),
			     GFP_KERNEL);
	ops = devm_kmemdup(&adev->dev, &vendor->ops, sizeof(vendor->ops),
			   GFP_KERNEL);
	if (!ldata || !ops) {
		ret = -ENOMEM;
		goto out;
	}

	ldata->vendor = vendor;
	ldata->base = devm_ioremap(&adev->dev, adev->res.start,
				   resource_size(&adev->res));
	if (!ldata->base) {
		ret = -ENOMEM;
		goto out;
	}

	amba_set_drvdata(adev, ldata);

	dev_dbg(&adev->dev, "designer ID = 0x%02x\n", amba_manf(adev));
	dev_dbg(&adev->dev, "revision = 0x%01x\n", amba_rev(adev));

	data = readl(ldata->base + RTC_CR);
	/* Enable the clockwatch on ST Variants */
	if (vendor->clockwatch)
		data |= RTC_CR_CWEN;
	else
		data |= RTC_CR_EN;
	writel(data, ldata->base + RTC_CR);

	/*
	 * On ST PL031 variants, the RTC reset value does not provide correct
	 * weekday for 2000-01-01. Correct the erroneous sunday to saturday.
	 */
	if (vendor->st_weekday) {
		if (readl(ldata->base + RTC_YDR) == 0x2000) {
			time = readl(ldata->base + RTC_DR);
			if ((time &
			     (RTC_MON_MASK | RTC_MDAY_MASK | RTC_WDAY_MASK))
			    == 0x02120000) {
				time = time | (0x7 << RTC_WDAY_SHIFT);
				writel(0x2000, ldata->base + RTC_YLR);
				writel(time, ldata->base + RTC_LR);
			}
		}
	}

	if (!adev->irq[0]) {
		/* When there's no interrupt, no point in exposing the alarm */
		ops->read_alarm = NULL;
		ops->set_alarm = NULL;
		ops->alarm_irq_enable = NULL;
	}

	device_init_wakeup(&adev->dev, true);
	ldata->rtc = devm_rtc_allocate_device(&adev->dev);
	if (IS_ERR(ldata->rtc)) {
		ret = PTR_ERR(ldata->rtc);
		goto out;
	}

	ldata->rtc->ops = ops;
	ldata->rtc->range_min = vendor->range_min;
	ldata->rtc->range_max = vendor->range_max;

	ret = rtc_register_device(ldata->rtc);
	if (ret)
		goto out;

	if (adev->irq[0]) {
		ret = request_irq(adev->irq[0], pl031_interrupt,
				  vendor->irqflags, "rtc-pl031", ldata);
		if (ret)
			goto out;
		dev_pm_set_wake_irq(&adev->dev, adev->irq[0]);
	}
	return 0;

out:
	amba_release_regions(adev);
err_req:

	return ret;
}

/* Operations for the original ARM version */
static struct pl031_vendor_data arm_pl031 = {
	.ops = {
		.read_time = pl031_read_time,
		.set_time = pl031_set_time,
		.read_alarm = pl031_read_alarm,
		.set_alarm = pl031_set_alarm,
		.alarm_irq_enable = pl031_alarm_irq_enable,
	},
	.range_max = U32_MAX,
};

/* The First ST derivative */
static struct pl031_vendor_data stv1_pl031 = {
	.ops = {
		.read_time = pl031_read_time,
		.set_time = pl031_set_time,
		.read_alarm = pl031_read_alarm,
		.set_alarm = pl031_set_alarm,
		.alarm_irq_enable = pl031_alarm_irq_enable,
	},
	.clockwatch = true,
	.st_weekday = true,
	.range_max = U32_MAX,
};

/* And the second ST derivative */
static struct pl031_vendor_data stv2_pl031 = {
	.ops = {
		.read_time = pl031_stv2_read_time,
		.set_time = pl031_stv2_set_time,
		.read_alarm = pl031_stv2_read_alarm,
		.set_alarm = pl031_stv2_set_alarm,
		.alarm_irq_enable = pl031_alarm_irq_enable,
	},
	.clockwatch = true,
	.st_weekday = true,
	/*
	 * This variant shares the IRQ with another block and must not
	 * suspend that IRQ line.
	 * TODO check if it shares with IRQF_NO_SUSPEND user, else we can
	 * remove IRQF_COND_SUSPEND
	 */
	.irqflags = IRQF_SHARED | IRQF_COND_SUSPEND,
	.range_min = RTC_TIMESTAMP_BEGIN_0000,
	.range_max = RTC_TIMESTAMP_END_9999,
};

static const struct amba_id pl031_ids[] = {
	{
		.id = 0x00041031,
		.mask = 0x000fffff,
		.data = &arm_pl031,
	},
	/* ST Micro variants */
	{
		.id = 0x00180031,
		.mask = 0x00ffffff,
		.data = &stv1_pl031,
	},
	{
		.id = 0x00280031,
		.mask = 0x00ffffff,
		.data = &stv2_pl031,
	},
	{0, 0},
};

MODULE_DEVICE_TABLE(amba, pl031_ids);

static struct amba_driver pl031_driver = {
	.drv = {
		.name = "rtc-pl031",
	},
	.id_table = pl031_ids,
	.probe = pl031_probe,
	.remove = pl031_remove,
};

module_amba_driver(pl031_driver);

MODULE_AUTHOR("Deepak Saxena <dsaxena@plexity.net>");
MODULE_DESCRIPTION("ARM AMBA PL031 RTC Driver");
MODULE_LICENSE("GPL");

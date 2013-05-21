/*
 * TI LP8788 MFD - rtc driver
 *
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/err.h>
#include <linux/irqdomain.h>
#include <linux/mfd/lp8788.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/slab.h>

/* register address */
#define LP8788_INTEN_3			0x05
#define LP8788_RTC_UNLOCK		0x64
#define LP8788_RTC_SEC			0x70
#define LP8788_ALM1_SEC			0x77
#define LP8788_ALM1_EN			0x7D
#define LP8788_ALM2_SEC			0x7E
#define LP8788_ALM2_EN			0x84

/* mask/shift bits */
#define LP8788_INT_RTC_ALM1_M		BIT(1)	/* Addr 05h */
#define LP8788_INT_RTC_ALM1_S		1
#define LP8788_INT_RTC_ALM2_M		BIT(2)	/* Addr 05h */
#define LP8788_INT_RTC_ALM2_S		2
#define LP8788_ALM_EN_M			BIT(7)	/* Addr 7Dh or 84h */
#define LP8788_ALM_EN_S			7

#define DEFAULT_ALARM_SEL		LP8788_ALARM_1
#define LP8788_MONTH_OFFSET		1
#define LP8788_BASE_YEAR		2000
#define MAX_WDAY_BITS			7
#define LP8788_WDAY_SET			1
#define RTC_UNLOCK			0x1
#define RTC_LATCH			0x2
#define ALARM_IRQ_FLAG			(RTC_IRQF | RTC_AF)

enum lp8788_time {
	LPTIME_SEC,
	LPTIME_MIN,
	LPTIME_HOUR,
	LPTIME_MDAY,
	LPTIME_MON,
	LPTIME_YEAR,
	LPTIME_WDAY,
	LPTIME_MAX,
};

struct lp8788_rtc {
	struct lp8788 *lp;
	struct rtc_device *rdev;
	enum lp8788_alarm_sel alarm;
	int irq;
};

static const u8 addr_alarm_sec[LP8788_ALARM_MAX] = {
	LP8788_ALM1_SEC,
	LP8788_ALM2_SEC,
};

static const u8 addr_alarm_en[LP8788_ALARM_MAX] = {
	LP8788_ALM1_EN,
	LP8788_ALM2_EN,
};

static const u8 mask_alarm_en[LP8788_ALARM_MAX] = {
	LP8788_INT_RTC_ALM1_M,
	LP8788_INT_RTC_ALM2_M,
};

static const u8 shift_alarm_en[LP8788_ALARM_MAX] = {
	LP8788_INT_RTC_ALM1_S,
	LP8788_INT_RTC_ALM2_S,
};

static int _to_tm_wday(u8 lp8788_wday)
{
	int i;

	if (lp8788_wday == 0)
		return 0;

	/* lookup defined weekday from read register value */
	for (i = 0; i < MAX_WDAY_BITS; i++) {
		if ((lp8788_wday >> i) == LP8788_WDAY_SET)
			break;
	}

	return i + 1;
}

static inline int _to_lp8788_wday(int tm_wday)
{
	return LP8788_WDAY_SET << (tm_wday - 1);
}

static void lp8788_rtc_unlock(struct lp8788 *lp)
{
	lp8788_write_byte(lp, LP8788_RTC_UNLOCK, RTC_UNLOCK);
	lp8788_write_byte(lp, LP8788_RTC_UNLOCK, RTC_LATCH);
}

static int lp8788_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct lp8788_rtc *rtc = dev_get_drvdata(dev);
	struct lp8788 *lp = rtc->lp;
	u8 data[LPTIME_MAX];
	int ret;

	lp8788_rtc_unlock(lp);

	ret = lp8788_read_multi_bytes(lp, LP8788_RTC_SEC, data,	LPTIME_MAX);
	if (ret)
		return ret;

	tm->tm_sec  = data[LPTIME_SEC];
	tm->tm_min  = data[LPTIME_MIN];
	tm->tm_hour = data[LPTIME_HOUR];
	tm->tm_mday = data[LPTIME_MDAY];
	tm->tm_mon  = data[LPTIME_MON] - LP8788_MONTH_OFFSET;
	tm->tm_year = data[LPTIME_YEAR] + LP8788_BASE_YEAR - 1900;
	tm->tm_wday = _to_tm_wday(data[LPTIME_WDAY]);

	return 0;
}

static int lp8788_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct lp8788_rtc *rtc = dev_get_drvdata(dev);
	struct lp8788 *lp = rtc->lp;
	u8 data[LPTIME_MAX - 1];
	int ret, i, year;

	year = tm->tm_year + 1900 - LP8788_BASE_YEAR;
	if (year < 0) {
		dev_err(lp->dev, "invalid year: %d\n", year);
		return -EINVAL;
	}

	/* because rtc weekday is a readonly register, do not update */
	data[LPTIME_SEC]  = tm->tm_sec;
	data[LPTIME_MIN]  = tm->tm_min;
	data[LPTIME_HOUR] = tm->tm_hour;
	data[LPTIME_MDAY] = tm->tm_mday;
	data[LPTIME_MON]  = tm->tm_mon + LP8788_MONTH_OFFSET;
	data[LPTIME_YEAR] = year;

	for (i = 0; i < ARRAY_SIZE(data); i++) {
		ret = lp8788_write_byte(lp, LP8788_RTC_SEC + i, data[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int lp8788_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct lp8788_rtc *rtc = dev_get_drvdata(dev);
	struct lp8788 *lp = rtc->lp;
	struct rtc_time *tm = &alarm->time;
	u8 addr, data[LPTIME_MAX];
	int ret;

	addr = addr_alarm_sec[rtc->alarm];
	ret = lp8788_read_multi_bytes(lp, addr, data, LPTIME_MAX);
	if (ret)
		return ret;

	tm->tm_sec  = data[LPTIME_SEC];
	tm->tm_min  = data[LPTIME_MIN];
	tm->tm_hour = data[LPTIME_HOUR];
	tm->tm_mday = data[LPTIME_MDAY];
	tm->tm_mon  = data[LPTIME_MON] - LP8788_MONTH_OFFSET;
	tm->tm_year = data[LPTIME_YEAR] + LP8788_BASE_YEAR - 1900;
	tm->tm_wday = _to_tm_wday(data[LPTIME_WDAY]);
	alarm->enabled = data[LPTIME_WDAY] & LP8788_ALM_EN_M;

	return 0;
}

static int lp8788_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct lp8788_rtc *rtc = dev_get_drvdata(dev);
	struct lp8788 *lp = rtc->lp;
	struct rtc_time *tm = &alarm->time;
	u8 addr, data[LPTIME_MAX];
	int ret, i, year;

	year = tm->tm_year + 1900 - LP8788_BASE_YEAR;
	if (year < 0) {
		dev_err(lp->dev, "invalid year: %d\n", year);
		return -EINVAL;
	}

	data[LPTIME_SEC]  = tm->tm_sec;
	data[LPTIME_MIN]  = tm->tm_min;
	data[LPTIME_HOUR] = tm->tm_hour;
	data[LPTIME_MDAY] = tm->tm_mday;
	data[LPTIME_MON]  = tm->tm_mon + LP8788_MONTH_OFFSET;
	data[LPTIME_YEAR] = year;
	data[LPTIME_WDAY] = _to_lp8788_wday(tm->tm_wday);

	for (i = 0; i < ARRAY_SIZE(data); i++) {
		addr = addr_alarm_sec[rtc->alarm] + i;
		ret = lp8788_write_byte(lp, addr, data[i]);
		if (ret)
			return ret;
	}

	alarm->enabled = 1;
	addr = addr_alarm_en[rtc->alarm];

	return lp8788_update_bits(lp, addr, LP8788_ALM_EN_M,
				alarm->enabled << LP8788_ALM_EN_S);
}

static int lp8788_alarm_irq_enable(struct device *dev, unsigned int enable)
{
	struct lp8788_rtc *rtc = dev_get_drvdata(dev);
	struct lp8788 *lp = rtc->lp;
	u8 mask, shift;

	if (!rtc->irq)
		return -EIO;

	mask = mask_alarm_en[rtc->alarm];
	shift = shift_alarm_en[rtc->alarm];

	return lp8788_update_bits(lp, LP8788_INTEN_3, mask, enable << shift);
}

static const struct rtc_class_ops lp8788_rtc_ops = {
	.read_time = lp8788_rtc_read_time,
	.set_time = lp8788_rtc_set_time,
	.read_alarm = lp8788_read_alarm,
	.set_alarm = lp8788_set_alarm,
	.alarm_irq_enable = lp8788_alarm_irq_enable,
};

static irqreturn_t lp8788_alarm_irq_handler(int irq, void *ptr)
{
	struct lp8788_rtc *rtc = ptr;

	rtc_update_irq(rtc->rdev, 1, ALARM_IRQ_FLAG);
	return IRQ_HANDLED;
}

static int lp8788_alarm_irq_register(struct platform_device *pdev,
				struct lp8788_rtc *rtc)
{
	struct resource *r;
	struct lp8788 *lp = rtc->lp;
	struct irq_domain *irqdm = lp->irqdm;
	int irq;

	rtc->irq = 0;

	/* even the alarm IRQ number is not specified, rtc time should work */
	r = platform_get_resource_byname(pdev, IORESOURCE_IRQ, LP8788_ALM_IRQ);
	if (!r)
		return 0;

	if (rtc->alarm == LP8788_ALARM_1)
		irq = r->start;
	else
		irq = r->end;

	rtc->irq = irq_create_mapping(irqdm, irq);

	return devm_request_threaded_irq(&pdev->dev, rtc->irq, NULL,
				lp8788_alarm_irq_handler,
				0, LP8788_ALM_IRQ, rtc);
}

static int lp8788_rtc_probe(struct platform_device *pdev)
{
	struct lp8788 *lp = dev_get_drvdata(pdev->dev.parent);
	struct lp8788_rtc *rtc;
	struct device *dev = &pdev->dev;

	rtc = devm_kzalloc(dev, sizeof(struct lp8788_rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->lp = lp;
	rtc->alarm = lp->pdata ? lp->pdata->alarm_sel : DEFAULT_ALARM_SEL;
	platform_set_drvdata(pdev, rtc);

	device_init_wakeup(dev, 1);

	rtc->rdev = devm_rtc_device_register(dev, "lp8788_rtc",
					&lp8788_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc->rdev)) {
		dev_err(dev, "can not register rtc device\n");
		return PTR_ERR(rtc->rdev);
	}

	if (lp8788_alarm_irq_register(pdev, rtc))
		dev_warn(lp->dev, "no rtc irq handler\n");

	return 0;
}

static int lp8788_rtc_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver lp8788_rtc_driver = {
	.probe = lp8788_rtc_probe,
	.remove = lp8788_rtc_remove,
	.driver = {
		.name = LP8788_DEV_RTC,
		.owner = THIS_MODULE,
	},
};
module_platform_driver(lp8788_rtc_driver);

MODULE_DESCRIPTION("Texas Instruments LP8788 RTC Driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:lp8788-rtc");

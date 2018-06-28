/*
 * Copyright (c) 2011 Zhao Zhang <zhzhl555@gmail.com>
 *
 * Derived from driver/rtc/rtc-au1xxx.c
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/io.h>
#include <loongson1.h>

#define LS1X_RTC_REG_OFFSET	(LS1X_RTC_BASE + 0x20)
#define LS1X_RTC_REGS(x) \
		((void __iomem *)KSEG1ADDR(LS1X_RTC_REG_OFFSET + (x)))

/*RTC programmable counters 0 and 1*/
#define SYS_COUNTER_CNTRL		(LS1X_RTC_REGS(0x20))
#define SYS_CNTRL_ERS			(1 << 23)
#define SYS_CNTRL_RTS			(1 << 20)
#define SYS_CNTRL_RM2			(1 << 19)
#define SYS_CNTRL_RM1			(1 << 18)
#define SYS_CNTRL_RM0			(1 << 17)
#define SYS_CNTRL_RS			(1 << 16)
#define SYS_CNTRL_BP			(1 << 14)
#define SYS_CNTRL_REN			(1 << 13)
#define SYS_CNTRL_BRT			(1 << 12)
#define SYS_CNTRL_TEN			(1 << 11)
#define SYS_CNTRL_BTT			(1 << 10)
#define SYS_CNTRL_E0			(1 << 8)
#define SYS_CNTRL_ETS			(1 << 7)
#define SYS_CNTRL_32S			(1 << 5)
#define SYS_CNTRL_TTS			(1 << 4)
#define SYS_CNTRL_TM2			(1 << 3)
#define SYS_CNTRL_TM1			(1 << 2)
#define SYS_CNTRL_TM0			(1 << 1)
#define SYS_CNTRL_TS			(1 << 0)

/* Programmable Counter 0 Registers */
#define SYS_TOYTRIM		(LS1X_RTC_REGS(0))
#define SYS_TOYWRITE0		(LS1X_RTC_REGS(4))
#define SYS_TOYWRITE1		(LS1X_RTC_REGS(8))
#define SYS_TOYREAD0		(LS1X_RTC_REGS(0xC))
#define SYS_TOYREAD1		(LS1X_RTC_REGS(0x10))
#define SYS_TOYMATCH0		(LS1X_RTC_REGS(0x14))
#define SYS_TOYMATCH1		(LS1X_RTC_REGS(0x18))
#define SYS_TOYMATCH2		(LS1X_RTC_REGS(0x1C))

/* Programmable Counter 1 Registers */
#define SYS_RTCTRIM		(LS1X_RTC_REGS(0x40))
#define SYS_RTCWRITE0		(LS1X_RTC_REGS(0x44))
#define SYS_RTCREAD0		(LS1X_RTC_REGS(0x48))
#define SYS_RTCMATCH0		(LS1X_RTC_REGS(0x4C))
#define SYS_RTCMATCH1		(LS1X_RTC_REGS(0x50))
#define SYS_RTCMATCH2		(LS1X_RTC_REGS(0x54))

#define LS1X_SEC_OFFSET		(4)
#define LS1X_MIN_OFFSET		(10)
#define LS1X_HOUR_OFFSET	(16)
#define LS1X_DAY_OFFSET		(21)
#define LS1X_MONTH_OFFSET	(26)


#define LS1X_SEC_MASK		(0x3f)
#define LS1X_MIN_MASK		(0x3f)
#define LS1X_HOUR_MASK		(0x1f)
#define LS1X_DAY_MASK		(0x1f)
#define LS1X_MONTH_MASK		(0x3f)
#define LS1X_YEAR_MASK		(0xffffffff)

#define ls1x_get_sec(t)		(((t) >> LS1X_SEC_OFFSET) & LS1X_SEC_MASK)
#define ls1x_get_min(t)		(((t) >> LS1X_MIN_OFFSET) & LS1X_MIN_MASK)
#define ls1x_get_hour(t)	(((t) >> LS1X_HOUR_OFFSET) & LS1X_HOUR_MASK)
#define ls1x_get_day(t)		(((t) >> LS1X_DAY_OFFSET) & LS1X_DAY_MASK)
#define ls1x_get_month(t)	(((t) >> LS1X_MONTH_OFFSET) & LS1X_MONTH_MASK)

#define RTC_CNTR_OK (SYS_CNTRL_E0 | SYS_CNTRL_32S)

static int ls1x_rtc_read_time(struct device *dev, struct rtc_time *rtm)
{
	unsigned long v;
	time64_t t;

	v = readl(SYS_TOYREAD0);
	t = readl(SYS_TOYREAD1);

	memset(rtm, 0, sizeof(struct rtc_time));
	t  = mktime64((t & LS1X_YEAR_MASK), ls1x_get_month(v),
			ls1x_get_day(v), ls1x_get_hour(v),
			ls1x_get_min(v), ls1x_get_sec(v));
	rtc_time64_to_tm(t, rtm);

	return 0;
}

static int ls1x_rtc_set_time(struct device *dev, struct  rtc_time *rtm)
{
	unsigned long v, t, c;
	int ret = -ETIMEDOUT;

	v = ((rtm->tm_mon + 1)  << LS1X_MONTH_OFFSET)
		| (rtm->tm_mday << LS1X_DAY_OFFSET)
		| (rtm->tm_hour << LS1X_HOUR_OFFSET)
		| (rtm->tm_min  << LS1X_MIN_OFFSET)
		| (rtm->tm_sec  << LS1X_SEC_OFFSET);

	writel(v, SYS_TOYWRITE0);
	c = 0x10000;
	/* add timeout check counter, for more safe */
	while ((readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_TS) && --c)
		usleep_range(1000, 3000);

	if (!c) {
		dev_err(dev, "set time timeout!\n");
		goto err;
	}

	t = rtm->tm_year + 1900;
	writel(t, SYS_TOYWRITE1);
	c = 0x10000;
	while ((readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_TS) && --c)
		usleep_range(1000, 3000);

	if (!c) {
		dev_err(dev, "set time timeout!\n");
		goto err;
	}
	return 0;
err:
	return ret;
}

static const struct rtc_class_ops  ls1x_rtc_ops = {
	.read_time	= ls1x_rtc_read_time,
	.set_time	= ls1x_rtc_set_time,
};

static int ls1x_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtcdev;
	unsigned long v;

	v = readl(SYS_COUNTER_CNTRL);
	if (!(v & RTC_CNTR_OK)) {
		dev_err(&pdev->dev, "rtc counters not working\n");
		return -ENODEV;
	}

	/* set to 1 HZ if needed */
	if (readl(SYS_TOYTRIM) != 32767) {
		v = 0x100000;
		while ((readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_TTS) && --v)
			usleep_range(1000, 3000);

		if (!v) {
			dev_err(&pdev->dev, "time out\n");
			return -ETIMEDOUT;
		}
		writel(32767, SYS_TOYTRIM);
	}
	/* this loop coundn't be endless */
	while (readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_TTS)
		usleep_range(1000, 3000);

	rtcdev = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtcdev))
		return PTR_ERR(rtcdev);

	platform_set_drvdata(pdev, rtcdev);
	rtcdev->ops = &ls1x_rtc_ops;
	rtcdev->range_min = RTC_TIMESTAMP_BEGIN_1900;
	rtcdev->range_max = RTC_TIMESTAMP_END_2099;

	return rtc_register_device(rtcdev);
}

static struct platform_driver  ls1x_rtc_driver = {
	.driver		= {
		.name	= "ls1x-rtc",
	},
	.probe		= ls1x_rtc_probe,
};

module_platform_driver(ls1x_rtc_driver);

MODULE_AUTHOR("zhao zhang <zhzhl555@gmail.com>");
MODULE_LICENSE("GPL");

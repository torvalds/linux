/*
 *  Oki MSM6242 RTC Driver
 *
 *  Copyright 2009 Geert Uytterhoeven
 *
 *  Based on the A2000 TOD code in arch/m68k/amiga/config.c
 *  Copyright (C) 1993 Hamish Macdonald
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/slab.h>


enum {
	MSM6242_SECOND1		= 0x0,	/* 1-second digit register */
	MSM6242_SECOND10	= 0x1,	/* 10-second digit register */
	MSM6242_MINUTE1		= 0x2,	/* 1-minute digit register */
	MSM6242_MINUTE10	= 0x3,	/* 10-minute digit register */
	MSM6242_HOUR1		= 0x4,	/* 1-hour digit register */
	MSM6242_HOUR10		= 0x5,	/* PM/AM, 10-hour digit register */
	MSM6242_DAY1		= 0x6,	/* 1-day digit register */
	MSM6242_DAY10		= 0x7,	/* 10-day digit register */
	MSM6242_MONTH1		= 0x8,	/* 1-month digit register */
	MSM6242_MONTH10		= 0x9,	/* 10-month digit register */
	MSM6242_YEAR1		= 0xa,	/* 1-year digit register */
	MSM6242_YEAR10		= 0xb,	/* 10-year digit register */
	MSM6242_WEEK		= 0xc,	/* Week register */
	MSM6242_CD		= 0xd,	/* Control Register D */
	MSM6242_CE		= 0xe,	/* Control Register E */
	MSM6242_CF		= 0xf,	/* Control Register F */
};

#define MSM6242_HOUR10_AM	(0 << 2)
#define MSM6242_HOUR10_PM	(1 << 2)
#define MSM6242_HOUR10_HR_MASK	(3 << 0)

#define MSM6242_WEEK_SUNDAY	0
#define MSM6242_WEEK_MONDAY	1
#define MSM6242_WEEK_TUESDAY	2
#define MSM6242_WEEK_WEDNESDAY	3
#define MSM6242_WEEK_THURSDAY	4
#define MSM6242_WEEK_FRIDAY	5
#define MSM6242_WEEK_SATURDAY	6

#define MSM6242_CD_30_S_ADJ	(1 << 3)	/* 30-second adjustment */
#define MSM6242_CD_IRQ_FLAG	(1 << 2)
#define MSM6242_CD_BUSY		(1 << 1)
#define MSM6242_CD_HOLD		(1 << 0)

#define MSM6242_CE_T_MASK	(3 << 2)
#define MSM6242_CE_T_64HZ	(0 << 2)	/* period 1/64 second */
#define MSM6242_CE_T_1HZ	(1 << 2)	/* period 1 second */
#define MSM6242_CE_T_1MINUTE	(2 << 2)	/* period 1 minute */
#define MSM6242_CE_T_1HOUR	(3 << 2)	/* period 1 hour */

#define MSM6242_CE_ITRPT_STND	(1 << 1)
#define MSM6242_CE_MASK		(1 << 0)	/* STD.P output control */

#define MSM6242_CF_TEST		(1 << 3)
#define MSM6242_CF_12H		(0 << 2)
#define MSM6242_CF_24H		(1 << 2)
#define MSM6242_CF_STOP		(1 << 1)
#define MSM6242_CF_REST		(1 << 0)	/* reset */


struct msm6242_priv {
	u32 __iomem *regs;
	struct rtc_device *rtc;
};

static inline unsigned int msm6242_read(struct msm6242_priv *priv,
				       unsigned int reg)
{
	return __raw_readl(&priv->regs[reg]) & 0xf;
}

static inline void msm6242_write(struct msm6242_priv *priv, unsigned int val,
				unsigned int reg)
{
	__raw_writel(val, &priv->regs[reg]);
}

static inline void msm6242_set(struct msm6242_priv *priv, unsigned int val,
			       unsigned int reg)
{
	msm6242_write(priv, msm6242_read(priv, reg) | val, reg);
}

static inline void msm6242_clear(struct msm6242_priv *priv, unsigned int val,
				 unsigned int reg)
{
	msm6242_write(priv, msm6242_read(priv, reg) & ~val, reg);
}

static void msm6242_lock(struct msm6242_priv *priv)
{
	int cnt = 5;

	msm6242_set(priv, MSM6242_CD_HOLD, MSM6242_CD);

	while ((msm6242_read(priv, MSM6242_CD) & MSM6242_CD_BUSY) && cnt) {
		msm6242_clear(priv, MSM6242_CD_HOLD, MSM6242_CD);
		udelay(70);
		msm6242_set(priv, MSM6242_CD_HOLD, MSM6242_CD);
		cnt--;
	}

	if (!cnt)
		pr_warn("timed out waiting for RTC (0x%x)\n",
			msm6242_read(priv, MSM6242_CD));
}

static void msm6242_unlock(struct msm6242_priv *priv)
{
	msm6242_clear(priv, MSM6242_CD_HOLD, MSM6242_CD);
}

static int msm6242_read_time(struct device *dev, struct rtc_time *tm)
{
	struct msm6242_priv *priv = dev_get_drvdata(dev);

	msm6242_lock(priv);

	tm->tm_sec  = msm6242_read(priv, MSM6242_SECOND10) * 10 +
		      msm6242_read(priv, MSM6242_SECOND1);
	tm->tm_min  = msm6242_read(priv, MSM6242_MINUTE10) * 10 +
		      msm6242_read(priv, MSM6242_MINUTE1);
	tm->tm_hour = (msm6242_read(priv, MSM6242_HOUR10 & 3)) * 10 +
		      msm6242_read(priv, MSM6242_HOUR1);
	tm->tm_mday = msm6242_read(priv, MSM6242_DAY10) * 10 +
		      msm6242_read(priv, MSM6242_DAY1);
	tm->tm_wday = msm6242_read(priv, MSM6242_WEEK);
	tm->tm_mon  = msm6242_read(priv, MSM6242_MONTH10) * 10 +
		      msm6242_read(priv, MSM6242_MONTH1) - 1;
	tm->tm_year = msm6242_read(priv, MSM6242_YEAR10) * 10 +
		      msm6242_read(priv, MSM6242_YEAR1);
	if (tm->tm_year <= 69)
		tm->tm_year += 100;

	if (!(msm6242_read(priv, MSM6242_CF) & MSM6242_CF_24H)) {
		unsigned int pm = msm6242_read(priv, MSM6242_HOUR10) &
				  MSM6242_HOUR10_PM;
		if (!pm && tm->tm_hour == 12)
			tm->tm_hour = 0;
		else if (pm && tm->tm_hour != 12)
			tm->tm_hour += 12;
	}

	msm6242_unlock(priv);

	return rtc_valid_tm(tm);
}

static int msm6242_set_time(struct device *dev, struct rtc_time *tm)
{
	struct msm6242_priv *priv = dev_get_drvdata(dev);

	msm6242_lock(priv);

	msm6242_write(priv, tm->tm_sec / 10, MSM6242_SECOND10);
	msm6242_write(priv, tm->tm_sec % 10, MSM6242_SECOND1);
	msm6242_write(priv, tm->tm_min / 10, MSM6242_MINUTE10);
	msm6242_write(priv, tm->tm_min % 10, MSM6242_MINUTE1);
	if (msm6242_read(priv, MSM6242_CF) & MSM6242_CF_24H)
		msm6242_write(priv, tm->tm_hour / 10, MSM6242_HOUR10);
	else if (tm->tm_hour >= 12)
		msm6242_write(priv, MSM6242_HOUR10_PM + (tm->tm_hour - 12) / 10,
			      MSM6242_HOUR10);
	else
		msm6242_write(priv, tm->tm_hour / 10, MSM6242_HOUR10);
	msm6242_write(priv, tm->tm_hour % 10, MSM6242_HOUR1);
	msm6242_write(priv, tm->tm_mday / 10, MSM6242_DAY10);
	msm6242_write(priv, tm->tm_mday % 10, MSM6242_DAY1);
	if (tm->tm_wday != -1)
		msm6242_write(priv, tm->tm_wday, MSM6242_WEEK);
	msm6242_write(priv, (tm->tm_mon + 1) / 10, MSM6242_MONTH10);
	msm6242_write(priv, (tm->tm_mon + 1) % 10, MSM6242_MONTH1);
	if (tm->tm_year >= 100)
		tm->tm_year -= 100;
	msm6242_write(priv, tm->tm_year / 10, MSM6242_YEAR10);
	msm6242_write(priv, tm->tm_year % 10, MSM6242_YEAR1);

	msm6242_unlock(priv);
	return 0;
}

static const struct rtc_class_ops msm6242_rtc_ops = {
	.read_time	= msm6242_read_time,
	.set_time	= msm6242_set_time,
};

static int __init msm6242_rtc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct msm6242_priv *priv;
	struct rtc_device *rtc;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regs = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!priv->regs)
		return -ENOMEM;
	platform_set_drvdata(pdev, priv);

	rtc = devm_rtc_device_register(&pdev->dev, "rtc-msm6242",
				&msm6242_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	priv->rtc = rtc;
	return 0;
}

static struct platform_driver msm6242_rtc_driver = {
	.driver	= {
		.name	= "rtc-msm6242",
	},
};

module_platform_driver_probe(msm6242_rtc_driver, msm6242_rtc_probe);

MODULE_AUTHOR("Geert Uytterhoeven <geert@linux-m68k.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Oki MSM6242 RTC driver");
MODULE_ALIAS("platform:rtc-msm6242");

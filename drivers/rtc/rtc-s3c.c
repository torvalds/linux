// SPDX-License-Identifier: GPL-2.0-only
/* drivers/rtc/rtc-s3c.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Copyright (c) 2004,2006 Simtec Electronics
 *	Ben Dooks, <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C2410/S3C2440/S3C24XX Internal RTC Driver
*/

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/clk.h>
#include <linux/log2.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#include <asm/irq.h>
#include "rtc-s3c.h"

struct s3c_rtc {
	struct device *dev;
	struct rtc_device *rtc;

	void __iomem *base;
	struct clk *rtc_clk;
	struct clk *rtc_src_clk;
	bool alarm_enabled;

	const struct s3c_rtc_data *data;

	int irq_alarm;
	spinlock_t alarm_lock;

	bool wake_en;
};

struct s3c_rtc_data {
	bool needs_src_clk;

	void (*irq_handler) (struct s3c_rtc *info, int mask);
	void (*enable) (struct s3c_rtc *info);
	void (*disable) (struct s3c_rtc *info);
};

static int s3c_rtc_enable_clk(struct s3c_rtc *info)
{
	int ret;

	ret = clk_enable(info->rtc_clk);
	if (ret)
		return ret;

	if (info->data->needs_src_clk) {
		ret = clk_enable(info->rtc_src_clk);
		if (ret) {
			clk_disable(info->rtc_clk);
			return ret;
		}
	}
	return 0;
}

static void s3c_rtc_disable_clk(struct s3c_rtc *info)
{
	if (info->data->needs_src_clk)
		clk_disable(info->rtc_src_clk);
	clk_disable(info->rtc_clk);
}

/* IRQ Handler */
static irqreturn_t s3c_rtc_alarmirq(int irq, void *id)
{
	struct s3c_rtc *info = (struct s3c_rtc *)id;

	if (info->data->irq_handler)
		info->data->irq_handler(info, S3C2410_INTP_ALM);

	return IRQ_HANDLED;
}

/* Update control registers */
static int s3c_rtc_setaie(struct device *dev, unsigned int enabled)
{
	struct s3c_rtc *info = dev_get_drvdata(dev);
	unsigned long flags;
	unsigned int tmp;
	int ret;

	dev_dbg(info->dev, "%s: aie=%d\n", __func__, enabled);

	ret = s3c_rtc_enable_clk(info);
	if (ret)
		return ret;

	tmp = readb(info->base + S3C2410_RTCALM) & ~S3C2410_RTCALM_ALMEN;

	if (enabled)
		tmp |= S3C2410_RTCALM_ALMEN;

	writeb(tmp, info->base + S3C2410_RTCALM);

	spin_lock_irqsave(&info->alarm_lock, flags);

	if (info->alarm_enabled && !enabled)
		s3c_rtc_disable_clk(info);
	else if (!info->alarm_enabled && enabled)
		ret = s3c_rtc_enable_clk(info);

	info->alarm_enabled = enabled;
	spin_unlock_irqrestore(&info->alarm_lock, flags);

	s3c_rtc_disable_clk(info);

	return ret;
}

/* Read time from RTC and convert it from BCD */
static int s3c_rtc_read_time(struct s3c_rtc *info, struct rtc_time *tm)
{
	unsigned int have_retried = 0;
	int ret;

	ret = s3c_rtc_enable_clk(info);
	if (ret)
		return ret;

retry_get_time:
	tm->tm_min  = readb(info->base + S3C2410_RTCMIN);
	tm->tm_hour = readb(info->base + S3C2410_RTCHOUR);
	tm->tm_mday = readb(info->base + S3C2410_RTCDATE);
	tm->tm_mon  = readb(info->base + S3C2410_RTCMON);
	tm->tm_year = readb(info->base + S3C2410_RTCYEAR);
	tm->tm_sec  = readb(info->base + S3C2410_RTCSEC);

	/*
	 * The only way to work out whether the system was mid-update
	 * when we read it is to check the second counter, and if it
	 * is zero, then we re-try the entire read
	 */
	if (tm->tm_sec == 0 && !have_retried) {
		have_retried = 1;
		goto retry_get_time;
	}

	s3c_rtc_disable_clk(info);

	tm->tm_sec  = bcd2bin(tm->tm_sec);
	tm->tm_min  = bcd2bin(tm->tm_min);
	tm->tm_hour = bcd2bin(tm->tm_hour);
	tm->tm_mday = bcd2bin(tm->tm_mday);
	tm->tm_mon  = bcd2bin(tm->tm_mon);
	tm->tm_year = bcd2bin(tm->tm_year);

	return 0;
}

/* Convert time to BCD and write it to RTC */
static int s3c_rtc_write_time(struct s3c_rtc *info, const struct rtc_time *tm)
{
	int ret;

	ret = s3c_rtc_enable_clk(info);
	if (ret)
		return ret;

	writeb(bin2bcd(tm->tm_sec),  info->base + S3C2410_RTCSEC);
	writeb(bin2bcd(tm->tm_min),  info->base + S3C2410_RTCMIN);
	writeb(bin2bcd(tm->tm_hour), info->base + S3C2410_RTCHOUR);
	writeb(bin2bcd(tm->tm_mday), info->base + S3C2410_RTCDATE);
	writeb(bin2bcd(tm->tm_mon),  info->base + S3C2410_RTCMON);
	writeb(bin2bcd(tm->tm_year), info->base + S3C2410_RTCYEAR);

	s3c_rtc_disable_clk(info);

	return 0;
}

static int s3c_rtc_gettime(struct device *dev, struct rtc_time *tm)
{
	struct s3c_rtc *info = dev_get_drvdata(dev);
	int ret;

	ret = s3c_rtc_read_time(info, tm);
	if (ret)
		return ret;

	/* Convert internal representation to actual date/time */
	tm->tm_year += 100;
	tm->tm_mon -= 1;

	dev_dbg(dev, "read time %ptR\n", tm);
	return 0;
}

static int s3c_rtc_settime(struct device *dev, struct rtc_time *tm)
{
	struct s3c_rtc *info = dev_get_drvdata(dev);
	struct rtc_time rtc_tm = *tm;

	dev_dbg(dev, "set time %ptR\n", tm);

	/*
	 * Convert actual date/time to internal representation.
	 * We get around Y2K by simply not supporting it.
	 */
	rtc_tm.tm_year -= 100;
	rtc_tm.tm_mon += 1;

	return s3c_rtc_write_time(info, &rtc_tm);
}

static int s3c_rtc_getalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct s3c_rtc *info = dev_get_drvdata(dev);
	struct rtc_time *alm_tm = &alrm->time;
	unsigned int alm_en;
	int ret;

	ret = s3c_rtc_enable_clk(info);
	if (ret)
		return ret;

	alm_tm->tm_sec  = readb(info->base + S3C2410_ALMSEC);
	alm_tm->tm_min  = readb(info->base + S3C2410_ALMMIN);
	alm_tm->tm_hour = readb(info->base + S3C2410_ALMHOUR);
	alm_tm->tm_mon  = readb(info->base + S3C2410_ALMMON);
	alm_tm->tm_mday = readb(info->base + S3C2410_ALMDATE);
	alm_tm->tm_year = readb(info->base + S3C2410_ALMYEAR);

	alm_en = readb(info->base + S3C2410_RTCALM);

	s3c_rtc_disable_clk(info);

	alrm->enabled = (alm_en & S3C2410_RTCALM_ALMEN) ? 1 : 0;

	dev_dbg(dev, "read alarm %d, %ptR\n", alm_en, alm_tm);

	/* decode the alarm enable field */
	if (alm_en & S3C2410_RTCALM_SECEN)
		alm_tm->tm_sec = bcd2bin(alm_tm->tm_sec);

	if (alm_en & S3C2410_RTCALM_MINEN)
		alm_tm->tm_min = bcd2bin(alm_tm->tm_min);

	if (alm_en & S3C2410_RTCALM_HOUREN)
		alm_tm->tm_hour = bcd2bin(alm_tm->tm_hour);

	if (alm_en & S3C2410_RTCALM_DAYEN)
		alm_tm->tm_mday = bcd2bin(alm_tm->tm_mday);

	if (alm_en & S3C2410_RTCALM_MONEN) {
		alm_tm->tm_mon = bcd2bin(alm_tm->tm_mon);
		alm_tm->tm_mon -= 1;
	}

	if (alm_en & S3C2410_RTCALM_YEAREN)
		alm_tm->tm_year = bcd2bin(alm_tm->tm_year);

	return 0;
}

static int s3c_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct s3c_rtc *info = dev_get_drvdata(dev);
	struct rtc_time *tm = &alrm->time;
	unsigned int alrm_en;
	int ret;

	dev_dbg(dev, "s3c_rtc_setalarm: %d, %ptR\n", alrm->enabled, tm);

	ret = s3c_rtc_enable_clk(info);
	if (ret)
		return ret;

	alrm_en = readb(info->base + S3C2410_RTCALM) & S3C2410_RTCALM_ALMEN;
	writeb(0x00, info->base + S3C2410_RTCALM);

	if (tm->tm_sec < 60 && tm->tm_sec >= 0) {
		alrm_en |= S3C2410_RTCALM_SECEN;
		writeb(bin2bcd(tm->tm_sec), info->base + S3C2410_ALMSEC);
	}

	if (tm->tm_min < 60 && tm->tm_min >= 0) {
		alrm_en |= S3C2410_RTCALM_MINEN;
		writeb(bin2bcd(tm->tm_min), info->base + S3C2410_ALMMIN);
	}

	if (tm->tm_hour < 24 && tm->tm_hour >= 0) {
		alrm_en |= S3C2410_RTCALM_HOUREN;
		writeb(bin2bcd(tm->tm_hour), info->base + S3C2410_ALMHOUR);
	}

	if (tm->tm_mon < 12 && tm->tm_mon >= 0) {
		alrm_en |= S3C2410_RTCALM_MONEN;
		writeb(bin2bcd(tm->tm_mon + 1), info->base + S3C2410_ALMMON);
	}

	if (tm->tm_mday <= 31 && tm->tm_mday >= 1) {
		alrm_en |= S3C2410_RTCALM_DAYEN;
		writeb(bin2bcd(tm->tm_mday), info->base + S3C2410_ALMDATE);
	}

	dev_dbg(dev, "setting S3C2410_RTCALM to %08x\n", alrm_en);

	writeb(alrm_en, info->base + S3C2410_RTCALM);

	s3c_rtc_setaie(dev, alrm->enabled);

	s3c_rtc_disable_clk(info);

	return 0;
}

static const struct rtc_class_ops s3c_rtcops = {
	.read_time	= s3c_rtc_gettime,
	.set_time	= s3c_rtc_settime,
	.read_alarm	= s3c_rtc_getalarm,
	.set_alarm	= s3c_rtc_setalarm,
	.alarm_irq_enable = s3c_rtc_setaie,
};

static void s3c24xx_rtc_enable(struct s3c_rtc *info)
{
	unsigned int con, tmp;

	con = readw(info->base + S3C2410_RTCCON);
	/* re-enable the device, and check it is ok */
	if ((con & S3C2410_RTCCON_RTCEN) == 0) {
		dev_info(info->dev, "rtc disabled, re-enabling\n");

		tmp = readw(info->base + S3C2410_RTCCON);
		writew(tmp | S3C2410_RTCCON_RTCEN, info->base + S3C2410_RTCCON);
	}

	if (con & S3C2410_RTCCON_CNTSEL) {
		dev_info(info->dev, "removing RTCCON_CNTSEL\n");

		tmp = readw(info->base + S3C2410_RTCCON);
		writew(tmp & ~S3C2410_RTCCON_CNTSEL,
		       info->base + S3C2410_RTCCON);
	}

	if (con & S3C2410_RTCCON_CLKRST) {
		dev_info(info->dev, "removing RTCCON_CLKRST\n");

		tmp = readw(info->base + S3C2410_RTCCON);
		writew(tmp & ~S3C2410_RTCCON_CLKRST,
		       info->base + S3C2410_RTCCON);
	}
}

static void s3c24xx_rtc_disable(struct s3c_rtc *info)
{
	unsigned int con;

	con = readw(info->base + S3C2410_RTCCON);
	con &= ~S3C2410_RTCCON_RTCEN;
	writew(con, info->base + S3C2410_RTCCON);

	con = readb(info->base + S3C2410_TICNT);
	con &= ~S3C2410_TICNT_ENABLE;
	writeb(con, info->base + S3C2410_TICNT);
}

static void s3c6410_rtc_disable(struct s3c_rtc *info)
{
	unsigned int con;

	con = readw(info->base + S3C2410_RTCCON);
	con &= ~S3C64XX_RTCCON_TICEN;
	con &= ~S3C2410_RTCCON_RTCEN;
	writew(con, info->base + S3C2410_RTCCON);
}

static void s3c_rtc_remove(struct platform_device *pdev)
{
	struct s3c_rtc *info = platform_get_drvdata(pdev);

	s3c_rtc_setaie(info->dev, 0);

	if (info->data->needs_src_clk)
		clk_unprepare(info->rtc_src_clk);
	clk_unprepare(info->rtc_clk);
}

static int s3c_rtc_probe(struct platform_device *pdev)
{
	struct s3c_rtc *info = NULL;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	info->data = of_device_get_match_data(&pdev->dev);
	if (!info->data) {
		dev_err(&pdev->dev, "failed getting s3c_rtc_data\n");
		return -EINVAL;
	}
	spin_lock_init(&info->alarm_lock);

	platform_set_drvdata(pdev, info);

	info->irq_alarm = platform_get_irq(pdev, 0);
	if (info->irq_alarm < 0)
		return info->irq_alarm;

	dev_dbg(&pdev->dev, "s3c2410_rtc: alarm irq %d\n", info->irq_alarm);

	/* get the memory region */
	info->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(info->base))
		return PTR_ERR(info->base);

	info->rtc_clk = devm_clk_get(&pdev->dev, "rtc");
	if (IS_ERR(info->rtc_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(info->rtc_clk),
				     "failed to find rtc clock\n");
	ret = clk_prepare_enable(info->rtc_clk);
	if (ret)
		return ret;

	if (info->data->needs_src_clk) {
		info->rtc_src_clk = devm_clk_get(&pdev->dev, "rtc_src");
		if (IS_ERR(info->rtc_src_clk)) {
			ret = dev_err_probe(&pdev->dev, PTR_ERR(info->rtc_src_clk),
					    "failed to find rtc source clock\n");
			goto err_src_clk;
		}
		ret = clk_prepare_enable(info->rtc_src_clk);
		if (ret)
			goto err_src_clk;
	}

	/* disable RTC enable bits potentially set by the bootloader */
	if (info->data->disable)
		info->data->disable(info);

	/* check to see if everything is setup correctly */
	if (info->data->enable)
		info->data->enable(info);

	dev_dbg(&pdev->dev, "s3c2410_rtc: RTCCON=%02x\n",
		readw(info->base + S3C2410_RTCCON));

	device_init_wakeup(&pdev->dev, true);

	info->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(info->rtc)) {
		ret = PTR_ERR(info->rtc);
		goto err_nortc;
	}

	info->rtc->ops = &s3c_rtcops;
	info->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	info->rtc->range_max = RTC_TIMESTAMP_END_2099;

	ret = devm_rtc_register_device(info->rtc);
	if (ret)
		goto err_nortc;

	ret = devm_request_irq(&pdev->dev, info->irq_alarm, s3c_rtc_alarmirq,
			       0, "s3c2410-rtc alarm", info);
	if (ret) {
		dev_err(&pdev->dev, "IRQ%d error %d\n", info->irq_alarm, ret);
		goto err_nortc;
	}

	s3c_rtc_disable_clk(info);

	return 0;

err_nortc:
	if (info->data->disable)
		info->data->disable(info);

	if (info->data->needs_src_clk)
		clk_disable_unprepare(info->rtc_src_clk);
err_src_clk:
	clk_disable_unprepare(info->rtc_clk);

	return ret;
}

#ifdef CONFIG_PM_SLEEP

static int s3c_rtc_suspend(struct device *dev)
{
	struct s3c_rtc *info = dev_get_drvdata(dev);
	int ret;

	ret = s3c_rtc_enable_clk(info);
	if (ret)
		return ret;

	if (info->data->disable)
		info->data->disable(info);

	if (device_may_wakeup(dev) && !info->wake_en) {
		if (enable_irq_wake(info->irq_alarm) == 0)
			info->wake_en = true;
		else
			dev_err(dev, "enable_irq_wake failed\n");
	}

	return 0;
}

static int s3c_rtc_resume(struct device *dev)
{
	struct s3c_rtc *info = dev_get_drvdata(dev);

	if (info->data->enable)
		info->data->enable(info);

	s3c_rtc_disable_clk(info);

	if (device_may_wakeup(dev) && info->wake_en) {
		disable_irq_wake(info->irq_alarm);
		info->wake_en = false;
	}

	return 0;
}
#endif
static SIMPLE_DEV_PM_OPS(s3c_rtc_pm_ops, s3c_rtc_suspend, s3c_rtc_resume);

static void s3c24xx_rtc_irq(struct s3c_rtc *info, int mask)
{
	rtc_update_irq(info->rtc, 1, RTC_AF | RTC_IRQF);
}

static void s3c6410_rtc_irq(struct s3c_rtc *info, int mask)
{
	rtc_update_irq(info->rtc, 1, RTC_AF | RTC_IRQF);
	writeb(mask, info->base + S3C2410_INTP);
}

static struct s3c_rtc_data const s3c2410_rtc_data = {
	.irq_handler		= s3c24xx_rtc_irq,
	.enable			= s3c24xx_rtc_enable,
	.disable		= s3c24xx_rtc_disable,
};

static struct s3c_rtc_data const s3c2416_rtc_data = {
	.irq_handler		= s3c24xx_rtc_irq,
	.enable			= s3c24xx_rtc_enable,
	.disable		= s3c24xx_rtc_disable,
};

static struct s3c_rtc_data const s3c2443_rtc_data = {
	.irq_handler		= s3c24xx_rtc_irq,
	.enable			= s3c24xx_rtc_enable,
	.disable		= s3c24xx_rtc_disable,
};

static struct s3c_rtc_data const s3c6410_rtc_data = {
	.needs_src_clk		= true,
	.irq_handler		= s3c6410_rtc_irq,
	.enable			= s3c24xx_rtc_enable,
	.disable		= s3c6410_rtc_disable,
};

static const __maybe_unused struct of_device_id s3c_rtc_dt_match[] = {
	{
		.compatible = "samsung,s3c2410-rtc",
		.data = &s3c2410_rtc_data,
	}, {
		.compatible = "samsung,s3c2416-rtc",
		.data = &s3c2416_rtc_data,
	}, {
		.compatible = "samsung,s3c2443-rtc",
		.data = &s3c2443_rtc_data,
	}, {
		.compatible = "samsung,s3c6410-rtc",
		.data = &s3c6410_rtc_data,
	}, {
		.compatible = "samsung,exynos3250-rtc",
		.data = &s3c6410_rtc_data,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, s3c_rtc_dt_match);

static struct platform_driver s3c_rtc_driver = {
	.probe		= s3c_rtc_probe,
	.remove		= s3c_rtc_remove,
	.driver		= {
		.name	= "s3c-rtc",
		.pm	= &s3c_rtc_pm_ops,
		.of_match_table	= of_match_ptr(s3c_rtc_dt_match),
	},
};
module_platform_driver(s3c_rtc_driver);

MODULE_DESCRIPTION("Samsung S3C RTC Driver");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:s3c2410-rtc");

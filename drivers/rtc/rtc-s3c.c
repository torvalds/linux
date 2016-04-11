/* drivers/rtc/rtc-s3c.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Copyright (c) 2004,2006 Simtec Electronics
 *	Ben Dooks, <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
	bool clk_disabled;

	struct s3c_rtc_data *data;

	int irq_alarm;
	int irq_tick;

	spinlock_t pie_lock;
	spinlock_t alarm_clk_lock;

	int ticnt_save, ticnt_en_save;
	bool wake_en;
};

struct s3c_rtc_data {
	int max_user_freq;
	bool needs_src_clk;

	void (*irq_handler) (struct s3c_rtc *info, int mask);
	void (*set_freq) (struct s3c_rtc *info, int freq);
	void (*enable_tick) (struct s3c_rtc *info, struct seq_file *seq);
	void (*select_tick_clk) (struct s3c_rtc *info);
	void (*save_tick_cnt) (struct s3c_rtc *info);
	void (*restore_tick_cnt) (struct s3c_rtc *info);
	void (*enable) (struct s3c_rtc *info);
	void (*disable) (struct s3c_rtc *info);
};

static void s3c_rtc_enable_clk(struct s3c_rtc *info)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&info->alarm_clk_lock, irq_flags);
	if (info->clk_disabled) {
		clk_enable(info->rtc_clk);
		if (info->data->needs_src_clk)
			clk_enable(info->rtc_src_clk);
		info->clk_disabled = false;
	}
	spin_unlock_irqrestore(&info->alarm_clk_lock, irq_flags);
}

static void s3c_rtc_disable_clk(struct s3c_rtc *info)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&info->alarm_clk_lock, irq_flags);
	if (!info->clk_disabled) {
		if (info->data->needs_src_clk)
			clk_disable(info->rtc_src_clk);
		clk_disable(info->rtc_clk);
		info->clk_disabled = true;
	}
	spin_unlock_irqrestore(&info->alarm_clk_lock, irq_flags);
}

/* IRQ Handlers */
static irqreturn_t s3c_rtc_tickirq(int irq, void *id)
{
	struct s3c_rtc *info = (struct s3c_rtc *)id;

	if (info->data->irq_handler)
		info->data->irq_handler(info, S3C2410_INTP_TIC);

	return IRQ_HANDLED;
}

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
	unsigned int tmp;

	dev_dbg(info->dev, "%s: aie=%d\n", __func__, enabled);

	s3c_rtc_enable_clk(info);

	tmp = readb(info->base + S3C2410_RTCALM) & ~S3C2410_RTCALM_ALMEN;

	if (enabled)
		tmp |= S3C2410_RTCALM_ALMEN;

	writeb(tmp, info->base + S3C2410_RTCALM);

	s3c_rtc_disable_clk(info);

	if (enabled)
		s3c_rtc_enable_clk(info);
	else
		s3c_rtc_disable_clk(info);

	return 0;
}

/* Set RTC frequency */
static int s3c_rtc_setfreq(struct s3c_rtc *info, int freq)
{
	if (!is_power_of_2(freq))
		return -EINVAL;

	spin_lock_irq(&info->pie_lock);

	if (info->data->set_freq)
		info->data->set_freq(info, freq);

	spin_unlock_irq(&info->pie_lock);

	return 0;
}

/* Time read/write */
static int s3c_rtc_gettime(struct device *dev, struct rtc_time *rtc_tm)
{
	struct s3c_rtc *info = dev_get_drvdata(dev);
	unsigned int have_retried = 0;

	s3c_rtc_enable_clk(info);

 retry_get_time:
	rtc_tm->tm_min  = readb(info->base + S3C2410_RTCMIN);
	rtc_tm->tm_hour = readb(info->base + S3C2410_RTCHOUR);
	rtc_tm->tm_mday = readb(info->base + S3C2410_RTCDATE);
	rtc_tm->tm_mon  = readb(info->base + S3C2410_RTCMON);
	rtc_tm->tm_year = readb(info->base + S3C2410_RTCYEAR);
	rtc_tm->tm_sec  = readb(info->base + S3C2410_RTCSEC);

	/* the only way to work out whether the system was mid-update
	 * when we read it is to check the second counter, and if it
	 * is zero, then we re-try the entire read
	 */

	if (rtc_tm->tm_sec == 0 && !have_retried) {
		have_retried = 1;
		goto retry_get_time;
	}

	rtc_tm->tm_sec = bcd2bin(rtc_tm->tm_sec);
	rtc_tm->tm_min = bcd2bin(rtc_tm->tm_min);
	rtc_tm->tm_hour = bcd2bin(rtc_tm->tm_hour);
	rtc_tm->tm_mday = bcd2bin(rtc_tm->tm_mday);
	rtc_tm->tm_mon = bcd2bin(rtc_tm->tm_mon);
	rtc_tm->tm_year = bcd2bin(rtc_tm->tm_year);

	s3c_rtc_disable_clk(info);

	rtc_tm->tm_year += 100;

	dev_dbg(dev, "read time %04d.%02d.%02d %02d:%02d:%02d\n",
		 1900 + rtc_tm->tm_year, rtc_tm->tm_mon, rtc_tm->tm_mday,
		 rtc_tm->tm_hour, rtc_tm->tm_min, rtc_tm->tm_sec);

	rtc_tm->tm_mon -= 1;

	return rtc_valid_tm(rtc_tm);
}

static int s3c_rtc_settime(struct device *dev, struct rtc_time *tm)
{
	struct s3c_rtc *info = dev_get_drvdata(dev);
	int year = tm->tm_year - 100;

	dev_dbg(dev, "set time %04d.%02d.%02d %02d:%02d:%02d\n",
		 1900 + tm->tm_year, tm->tm_mon, tm->tm_mday,
		 tm->tm_hour, tm->tm_min, tm->tm_sec);

	/* we get around y2k by simply not supporting it */

	if (year < 0 || year >= 100) {
		dev_err(dev, "rtc only supports 100 years\n");
		return -EINVAL;
	}

	s3c_rtc_enable_clk(info);

	writeb(bin2bcd(tm->tm_sec),  info->base + S3C2410_RTCSEC);
	writeb(bin2bcd(tm->tm_min),  info->base + S3C2410_RTCMIN);
	writeb(bin2bcd(tm->tm_hour), info->base + S3C2410_RTCHOUR);
	writeb(bin2bcd(tm->tm_mday), info->base + S3C2410_RTCDATE);
	writeb(bin2bcd(tm->tm_mon + 1), info->base + S3C2410_RTCMON);
	writeb(bin2bcd(year), info->base + S3C2410_RTCYEAR);

	s3c_rtc_disable_clk(info);

	return 0;
}

static int s3c_rtc_getalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct s3c_rtc *info = dev_get_drvdata(dev);
	struct rtc_time *alm_tm = &alrm->time;
	unsigned int alm_en;

	s3c_rtc_enable_clk(info);

	alm_tm->tm_sec  = readb(info->base + S3C2410_ALMSEC);
	alm_tm->tm_min  = readb(info->base + S3C2410_ALMMIN);
	alm_tm->tm_hour = readb(info->base + S3C2410_ALMHOUR);
	alm_tm->tm_mon  = readb(info->base + S3C2410_ALMMON);
	alm_tm->tm_mday = readb(info->base + S3C2410_ALMDATE);
	alm_tm->tm_year = readb(info->base + S3C2410_ALMYEAR);

	alm_en = readb(info->base + S3C2410_RTCALM);

	s3c_rtc_disable_clk(info);

	alrm->enabled = (alm_en & S3C2410_RTCALM_ALMEN) ? 1 : 0;

	dev_dbg(dev, "read alarm %d, %04d.%02d.%02d %02d:%02d:%02d\n",
		 alm_en,
		 1900 + alm_tm->tm_year, alm_tm->tm_mon, alm_tm->tm_mday,
		 alm_tm->tm_hour, alm_tm->tm_min, alm_tm->tm_sec);

	/* decode the alarm enable field */
	if (alm_en & S3C2410_RTCALM_SECEN)
		alm_tm->tm_sec = bcd2bin(alm_tm->tm_sec);
	else
		alm_tm->tm_sec = -1;

	if (alm_en & S3C2410_RTCALM_MINEN)
		alm_tm->tm_min = bcd2bin(alm_tm->tm_min);
	else
		alm_tm->tm_min = -1;

	if (alm_en & S3C2410_RTCALM_HOUREN)
		alm_tm->tm_hour = bcd2bin(alm_tm->tm_hour);
	else
		alm_tm->tm_hour = -1;

	if (alm_en & S3C2410_RTCALM_DAYEN)
		alm_tm->tm_mday = bcd2bin(alm_tm->tm_mday);
	else
		alm_tm->tm_mday = -1;

	if (alm_en & S3C2410_RTCALM_MONEN) {
		alm_tm->tm_mon = bcd2bin(alm_tm->tm_mon);
		alm_tm->tm_mon -= 1;
	} else {
		alm_tm->tm_mon = -1;
	}

	if (alm_en & S3C2410_RTCALM_YEAREN)
		alm_tm->tm_year = bcd2bin(alm_tm->tm_year);
	else
		alm_tm->tm_year = -1;

	return 0;
}

static int s3c_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct s3c_rtc *info = dev_get_drvdata(dev);
	struct rtc_time *tm = &alrm->time;
	unsigned int alrm_en;
	int year = tm->tm_year - 100;

	dev_dbg(dev, "s3c_rtc_setalarm: %d, %04d.%02d.%02d %02d:%02d:%02d\n",
		 alrm->enabled,
		 1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday,
		 tm->tm_hour, tm->tm_min, tm->tm_sec);

	s3c_rtc_enable_clk(info);

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

	if (year < 100 && year >= 0) {
		alrm_en |= S3C2410_RTCALM_YEAREN;
		writeb(bin2bcd(year), info->base + S3C2410_ALMYEAR);
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

	s3c_rtc_disable_clk(info);

	s3c_rtc_setaie(dev, alrm->enabled);

	return 0;
}

static int s3c_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct s3c_rtc *info = dev_get_drvdata(dev);

	s3c_rtc_enable_clk(info);

	if (info->data->enable_tick)
		info->data->enable_tick(info, seq);

	s3c_rtc_disable_clk(info);

	return 0;
}

static const struct rtc_class_ops s3c_rtcops = {
	.read_time	= s3c_rtc_gettime,
	.set_time	= s3c_rtc_settime,
	.read_alarm	= s3c_rtc_getalarm,
	.set_alarm	= s3c_rtc_setalarm,
	.proc		= s3c_rtc_proc,
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
		writew(tmp | S3C2410_RTCCON_RTCEN,
			info->base + S3C2410_RTCCON);
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

static int s3c_rtc_remove(struct platform_device *pdev)
{
	struct s3c_rtc *info = platform_get_drvdata(pdev);

	s3c_rtc_setaie(info->dev, 0);

	if (info->data->needs_src_clk)
		clk_unprepare(info->rtc_src_clk);
	clk_unprepare(info->rtc_clk);

	return 0;
}

static const struct of_device_id s3c_rtc_dt_match[];

static struct s3c_rtc_data *s3c_rtc_get_data(struct platform_device *pdev)
{
	const struct of_device_id *match;

	match = of_match_node(s3c_rtc_dt_match, pdev->dev.of_node);
	return (struct s3c_rtc_data *)match->data;
}

static int s3c_rtc_probe(struct platform_device *pdev)
{
	struct s3c_rtc *info = NULL;
	struct rtc_time rtc_tm;
	struct resource *res;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	/* find the IRQs */
	info->irq_tick = platform_get_irq(pdev, 1);
	if (info->irq_tick < 0) {
		dev_err(&pdev->dev, "no irq for rtc tick\n");
		return info->irq_tick;
	}

	info->dev = &pdev->dev;
	info->data = s3c_rtc_get_data(pdev);
	if (!info->data) {
		dev_err(&pdev->dev, "failed getting s3c_rtc_data\n");
		return -EINVAL;
	}
	spin_lock_init(&info->pie_lock);
	spin_lock_init(&info->alarm_clk_lock);

	platform_set_drvdata(pdev, info);

	info->irq_alarm = platform_get_irq(pdev, 0);
	if (info->irq_alarm < 0) {
		dev_err(&pdev->dev, "no irq for alarm\n");
		return info->irq_alarm;
	}

	dev_dbg(&pdev->dev, "s3c2410_rtc: tick irq %d, alarm irq %d\n",
		 info->irq_tick, info->irq_alarm);

	/* get the memory region */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(info->base))
		return PTR_ERR(info->base);

	info->rtc_clk = devm_clk_get(&pdev->dev, "rtc");
	if (IS_ERR(info->rtc_clk)) {
		ret = PTR_ERR(info->rtc_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to find rtc clock\n");
		else
			dev_dbg(&pdev->dev, "probe deferred due to missing rtc clk\n");
		return ret;
	}
	clk_prepare_enable(info->rtc_clk);

	if (info->data->needs_src_clk) {
		info->rtc_src_clk = devm_clk_get(&pdev->dev, "rtc_src");
		if (IS_ERR(info->rtc_src_clk)) {
			ret = PTR_ERR(info->rtc_src_clk);
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev,
					"failed to find rtc source clock\n");
			else
				dev_dbg(&pdev->dev,
					"probe deferred due to missing rtc src clk\n");
			clk_disable_unprepare(info->rtc_clk);
			return ret;
		}
		clk_prepare_enable(info->rtc_src_clk);
	}

	/* check to see if everything is setup correctly */
	if (info->data->enable)
		info->data->enable(info);

	dev_dbg(&pdev->dev, "s3c2410_rtc: RTCCON=%02x\n",
		 readw(info->base + S3C2410_RTCCON));

	device_init_wakeup(&pdev->dev, 1);

	/* Check RTC Time */
	if (s3c_rtc_gettime(&pdev->dev, &rtc_tm)) {
		rtc_tm.tm_year	= 100;
		rtc_tm.tm_mon	= 0;
		rtc_tm.tm_mday	= 1;
		rtc_tm.tm_hour	= 0;
		rtc_tm.tm_min	= 0;
		rtc_tm.tm_sec	= 0;

		s3c_rtc_settime(&pdev->dev, &rtc_tm);

		dev_warn(&pdev->dev, "warning: invalid RTC value so initializing it\n");
	}

	/* register RTC and exit */
	info->rtc = devm_rtc_device_register(&pdev->dev, "s3c", &s3c_rtcops,
				  THIS_MODULE);
	if (IS_ERR(info->rtc)) {
		dev_err(&pdev->dev, "cannot attach rtc\n");
		ret = PTR_ERR(info->rtc);
		goto err_nortc;
	}

	ret = devm_request_irq(&pdev->dev, info->irq_alarm, s3c_rtc_alarmirq,
			  0,  "s3c2410-rtc alarm", info);
	if (ret) {
		dev_err(&pdev->dev, "IRQ%d error %d\n", info->irq_alarm, ret);
		goto err_nortc;
	}

	ret = devm_request_irq(&pdev->dev, info->irq_tick, s3c_rtc_tickirq,
			  0,  "s3c2410-rtc tick", info);
	if (ret) {
		dev_err(&pdev->dev, "IRQ%d error %d\n", info->irq_tick, ret);
		goto err_nortc;
	}

	if (info->data->select_tick_clk)
		info->data->select_tick_clk(info);

	s3c_rtc_setfreq(info, 1);

	s3c_rtc_disable_clk(info);

	return 0;

 err_nortc:
	if (info->data->disable)
		info->data->disable(info);

	if (info->data->needs_src_clk)
		clk_disable_unprepare(info->rtc_src_clk);
	clk_disable_unprepare(info->rtc_clk);

	return ret;
}

#ifdef CONFIG_PM_SLEEP

static int s3c_rtc_suspend(struct device *dev)
{
	struct s3c_rtc *info = dev_get_drvdata(dev);

	s3c_rtc_enable_clk(info);

	/* save TICNT for anyone using periodic interrupts */
	if (info->data->save_tick_cnt)
		info->data->save_tick_cnt(info);

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

	if (info->data->restore_tick_cnt)
		info->data->restore_tick_cnt(info);

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

static void s3c2410_rtc_setfreq(struct s3c_rtc *info, int freq)
{
	unsigned int tmp = 0;
	int val;

	tmp = readb(info->base + S3C2410_TICNT);
	tmp &= S3C2410_TICNT_ENABLE;

	val = (info->rtc->max_user_freq / freq) - 1;
	tmp |= val;

	writel(tmp, info->base + S3C2410_TICNT);
}

static void s3c2416_rtc_setfreq(struct s3c_rtc *info, int freq)
{
	unsigned int tmp = 0;
	int val;

	tmp = readb(info->base + S3C2410_TICNT);
	tmp &= S3C2410_TICNT_ENABLE;

	val = (info->rtc->max_user_freq / freq) - 1;

	tmp |= S3C2443_TICNT_PART(val);
	writel(S3C2443_TICNT1_PART(val), info->base + S3C2443_TICNT1);

	writel(S3C2416_TICNT2_PART(val), info->base + S3C2416_TICNT2);

	writel(tmp, info->base + S3C2410_TICNT);
}

static void s3c2443_rtc_setfreq(struct s3c_rtc *info, int freq)
{
	unsigned int tmp = 0;
	int val;

	tmp = readb(info->base + S3C2410_TICNT);
	tmp &= S3C2410_TICNT_ENABLE;

	val = (info->rtc->max_user_freq / freq) - 1;

	tmp |= S3C2443_TICNT_PART(val);
	writel(S3C2443_TICNT1_PART(val), info->base + S3C2443_TICNT1);

	writel(tmp, info->base + S3C2410_TICNT);
}

static void s3c6410_rtc_setfreq(struct s3c_rtc *info, int freq)
{
	int val;

	val = (info->rtc->max_user_freq / freq) - 1;
	writel(val, info->base + S3C2410_TICNT);
}

static void s3c24xx_rtc_enable_tick(struct s3c_rtc *info, struct seq_file *seq)
{
	unsigned int ticnt;

	ticnt = readb(info->base + S3C2410_TICNT);
	ticnt &= S3C2410_TICNT_ENABLE;

	seq_printf(seq, "periodic_IRQ\t: %s\n", ticnt  ? "yes" : "no");
}

static void s3c2416_rtc_select_tick_clk(struct s3c_rtc *info)
{
	unsigned int con;

	con = readw(info->base + S3C2410_RTCCON);
	con |= S3C2443_RTCCON_TICSEL;
	writew(con, info->base + S3C2410_RTCCON);
}

static void s3c6410_rtc_enable_tick(struct s3c_rtc *info, struct seq_file *seq)
{
	unsigned int ticnt;

	ticnt = readw(info->base + S3C2410_RTCCON);
	ticnt &= S3C64XX_RTCCON_TICEN;

	seq_printf(seq, "periodic_IRQ\t: %s\n", ticnt  ? "yes" : "no");
}

static void s3c24xx_rtc_save_tick_cnt(struct s3c_rtc *info)
{
	info->ticnt_save = readb(info->base + S3C2410_TICNT);
}

static void s3c24xx_rtc_restore_tick_cnt(struct s3c_rtc *info)
{
	writeb(info->ticnt_save, info->base + S3C2410_TICNT);
}

static void s3c6410_rtc_save_tick_cnt(struct s3c_rtc *info)
{
	info->ticnt_en_save = readw(info->base + S3C2410_RTCCON);
	info->ticnt_en_save &= S3C64XX_RTCCON_TICEN;
	info->ticnt_save = readl(info->base + S3C2410_TICNT);
}

static void s3c6410_rtc_restore_tick_cnt(struct s3c_rtc *info)
{
	unsigned int con;

	writel(info->ticnt_save, info->base + S3C2410_TICNT);
	if (info->ticnt_en_save) {
		con = readw(info->base + S3C2410_RTCCON);
		writew(con | info->ticnt_en_save,
				info->base + S3C2410_RTCCON);
	}
}

static struct s3c_rtc_data const s3c2410_rtc_data = {
	.max_user_freq		= 128,
	.irq_handler		= s3c24xx_rtc_irq,
	.set_freq		= s3c2410_rtc_setfreq,
	.enable_tick		= s3c24xx_rtc_enable_tick,
	.save_tick_cnt		= s3c24xx_rtc_save_tick_cnt,
	.restore_tick_cnt	= s3c24xx_rtc_restore_tick_cnt,
	.enable			= s3c24xx_rtc_enable,
	.disable		= s3c24xx_rtc_disable,
};

static struct s3c_rtc_data const s3c2416_rtc_data = {
	.max_user_freq		= 32768,
	.irq_handler		= s3c24xx_rtc_irq,
	.set_freq		= s3c2416_rtc_setfreq,
	.enable_tick		= s3c24xx_rtc_enable_tick,
	.select_tick_clk	= s3c2416_rtc_select_tick_clk,
	.save_tick_cnt		= s3c24xx_rtc_save_tick_cnt,
	.restore_tick_cnt	= s3c24xx_rtc_restore_tick_cnt,
	.enable			= s3c24xx_rtc_enable,
	.disable		= s3c24xx_rtc_disable,
};

static struct s3c_rtc_data const s3c2443_rtc_data = {
	.max_user_freq		= 32768,
	.irq_handler		= s3c24xx_rtc_irq,
	.set_freq		= s3c2443_rtc_setfreq,
	.enable_tick		= s3c24xx_rtc_enable_tick,
	.select_tick_clk	= s3c2416_rtc_select_tick_clk,
	.save_tick_cnt		= s3c24xx_rtc_save_tick_cnt,
	.restore_tick_cnt	= s3c24xx_rtc_restore_tick_cnt,
	.enable			= s3c24xx_rtc_enable,
	.disable		= s3c24xx_rtc_disable,
};

static struct s3c_rtc_data const s3c6410_rtc_data = {
	.max_user_freq		= 32768,
	.needs_src_clk		= true,
	.irq_handler		= s3c6410_rtc_irq,
	.set_freq		= s3c6410_rtc_setfreq,
	.enable_tick		= s3c6410_rtc_enable_tick,
	.save_tick_cnt		= s3c6410_rtc_save_tick_cnt,
	.restore_tick_cnt	= s3c6410_rtc_restore_tick_cnt,
	.enable			= s3c24xx_rtc_enable,
	.disable		= s3c6410_rtc_disable,
};

static const struct of_device_id s3c_rtc_dt_match[] = {
	{
		.compatible = "samsung,s3c2410-rtc",
		.data = (void *)&s3c2410_rtc_data,
	}, {
		.compatible = "samsung,s3c2416-rtc",
		.data = (void *)&s3c2416_rtc_data,
	}, {
		.compatible = "samsung,s3c2443-rtc",
		.data = (void *)&s3c2443_rtc_data,
	}, {
		.compatible = "samsung,s3c6410-rtc",
		.data = (void *)&s3c6410_rtc_data,
	}, {
		.compatible = "samsung,exynos3250-rtc",
		.data = (void *)&s3c6410_rtc_data,
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

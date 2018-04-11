/*
 * PIC32 RTC driver
 *
 * Joshua Henderson <joshua.henderson@microchip.com>
 * Copyright (C) 2016 Microchip Technology Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/rtc.h>
#include <linux/bcd.h>

#include <asm/mach-pic32/pic32.h>

#define PIC32_RTCCON		0x00
#define PIC32_RTCCON_ON		BIT(15)
#define PIC32_RTCCON_SIDL	BIT(13)
#define PIC32_RTCCON_RTCCLKSEL	(3 << 9)
#define PIC32_RTCCON_RTCCLKON	BIT(6)
#define PIC32_RTCCON_RTCWREN	BIT(3)
#define PIC32_RTCCON_RTCSYNC	BIT(2)
#define PIC32_RTCCON_HALFSEC	BIT(1)
#define PIC32_RTCCON_RTCOE	BIT(0)

#define PIC32_RTCALRM		0x10
#define PIC32_RTCALRM_ALRMEN	BIT(15)
#define PIC32_RTCALRM_CHIME	BIT(14)
#define PIC32_RTCALRM_PIV	BIT(13)
#define PIC32_RTCALRM_ALARMSYNC	BIT(12)
#define PIC32_RTCALRM_AMASK	0x0F00
#define PIC32_RTCALRM_ARPT	0xFF

#define PIC32_RTCHOUR		0x23
#define PIC32_RTCMIN		0x22
#define PIC32_RTCSEC		0x21
#define PIC32_RTCYEAR		0x33
#define PIC32_RTCMON		0x32
#define PIC32_RTCDAY		0x31

#define PIC32_ALRMTIME		0x40
#define PIC32_ALRMDATE		0x50

#define PIC32_ALRMHOUR		0x43
#define PIC32_ALRMMIN		0x42
#define PIC32_ALRMSEC		0x41
#define PIC32_ALRMYEAR		0x53
#define PIC32_ALRMMON		0x52
#define PIC32_ALRMDAY		0x51

struct pic32_rtc_dev {
	struct rtc_device	*rtc;
	void __iomem		*reg_base;
	struct clk		*clk;
	spinlock_t		alarm_lock;
	int			alarm_irq;
	bool			alarm_clk_enabled;
};

static void pic32_rtc_alarm_clk_enable(struct pic32_rtc_dev *pdata,
				       bool enable)
{
	unsigned long flags;

	spin_lock_irqsave(&pdata->alarm_lock, flags);
	if (enable) {
		if (!pdata->alarm_clk_enabled) {
			clk_enable(pdata->clk);
			pdata->alarm_clk_enabled = true;
		}
	} else {
		if (pdata->alarm_clk_enabled) {
			clk_disable(pdata->clk);
			pdata->alarm_clk_enabled = false;
		}
	}
	spin_unlock_irqrestore(&pdata->alarm_lock, flags);
}

static irqreturn_t pic32_rtc_alarmirq(int irq, void *id)
{
	struct pic32_rtc_dev *pdata = (struct pic32_rtc_dev *)id;

	clk_enable(pdata->clk);
	rtc_update_irq(pdata->rtc, 1, RTC_AF | RTC_IRQF);
	clk_disable(pdata->clk);

	pic32_rtc_alarm_clk_enable(pdata, false);

	return IRQ_HANDLED;
}

static int pic32_rtc_setaie(struct device *dev, unsigned int enabled)
{
	struct pic32_rtc_dev *pdata = dev_get_drvdata(dev);
	void __iomem *base = pdata->reg_base;

	clk_enable(pdata->clk);

	writel(PIC32_RTCALRM_ALRMEN,
	       base + (enabled ? PIC32_SET(PIC32_RTCALRM) :
		       PIC32_CLR(PIC32_RTCALRM)));

	clk_disable(pdata->clk);

	pic32_rtc_alarm_clk_enable(pdata, enabled);

	return 0;
}

static int pic32_rtc_setfreq(struct device *dev, int freq)
{
	struct pic32_rtc_dev *pdata = dev_get_drvdata(dev);
	void __iomem *base = pdata->reg_base;

	clk_enable(pdata->clk);

	writel(PIC32_RTCALRM_AMASK, base + PIC32_CLR(PIC32_RTCALRM));
	writel(freq << 8, base + PIC32_SET(PIC32_RTCALRM));
	writel(PIC32_RTCALRM_CHIME, base + PIC32_SET(PIC32_RTCALRM));

	clk_disable(pdata->clk);

	return 0;
}

static int pic32_rtc_gettime(struct device *dev, struct rtc_time *rtc_tm)
{
	struct pic32_rtc_dev *pdata = dev_get_drvdata(dev);
	void __iomem *base = pdata->reg_base;
	unsigned int tries = 0;

	clk_enable(pdata->clk);

	do {
		rtc_tm->tm_hour = readb(base + PIC32_RTCHOUR);
		rtc_tm->tm_min = readb(base + PIC32_RTCMIN);
		rtc_tm->tm_mon  = readb(base + PIC32_RTCMON);
		rtc_tm->tm_mday = readb(base + PIC32_RTCDAY);
		rtc_tm->tm_year = readb(base + PIC32_RTCYEAR);
		rtc_tm->tm_sec  = readb(base + PIC32_RTCSEC);

		/*
		 * The only way to work out whether the system was mid-update
		 * when we read it is to check the second counter, and if it
		 * is zero, then we re-try the entire read.
		 */
		tries += 1;
	} while (rtc_tm->tm_sec == 0 && tries < 2);

	rtc_tm->tm_sec = bcd2bin(rtc_tm->tm_sec);
	rtc_tm->tm_min = bcd2bin(rtc_tm->tm_min);
	rtc_tm->tm_hour = bcd2bin(rtc_tm->tm_hour);
	rtc_tm->tm_mday = bcd2bin(rtc_tm->tm_mday);
	rtc_tm->tm_mon = bcd2bin(rtc_tm->tm_mon) - 1;
	rtc_tm->tm_year = bcd2bin(rtc_tm->tm_year);

	rtc_tm->tm_year += 100;

	dev_dbg(dev, "read time %04d.%02d.%02d %02d:%02d:%02d\n",
		1900 + rtc_tm->tm_year, rtc_tm->tm_mon, rtc_tm->tm_mday,
		rtc_tm->tm_hour, rtc_tm->tm_min, rtc_tm->tm_sec);

	clk_disable(pdata->clk);
	return 0;
}

static int pic32_rtc_settime(struct device *dev, struct rtc_time *tm)
{
	struct pic32_rtc_dev *pdata = dev_get_drvdata(dev);
	void __iomem *base = pdata->reg_base;
	int year = tm->tm_year - 100;

	dev_dbg(dev, "set time %04d.%02d.%02d %02d:%02d:%02d\n",
		1900 + tm->tm_year, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	if (year < 0 || year >= 100) {
		dev_err(dev, "rtc only supports 100 years\n");
		return -EINVAL;
	}

	clk_enable(pdata->clk);
	writeb(bin2bcd(tm->tm_sec),  base + PIC32_RTCSEC);
	writeb(bin2bcd(tm->tm_min),  base + PIC32_RTCMIN);
	writeb(bin2bcd(tm->tm_hour), base + PIC32_RTCHOUR);
	writeb(bin2bcd(tm->tm_mday), base + PIC32_RTCDAY);
	writeb(bin2bcd(tm->tm_mon + 1), base + PIC32_RTCMON);
	writeb(bin2bcd(year), base + PIC32_RTCYEAR);
	clk_disable(pdata->clk);

	return 0;
}

static int pic32_rtc_getalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct pic32_rtc_dev *pdata = dev_get_drvdata(dev);
	struct rtc_time *alm_tm = &alrm->time;
	void __iomem *base = pdata->reg_base;
	unsigned int alm_en;

	clk_enable(pdata->clk);
	alm_tm->tm_sec  = readb(base + PIC32_ALRMSEC);
	alm_tm->tm_min  = readb(base + PIC32_ALRMMIN);
	alm_tm->tm_hour = readb(base + PIC32_ALRMHOUR);
	alm_tm->tm_mon  = readb(base + PIC32_ALRMMON);
	alm_tm->tm_mday = readb(base + PIC32_ALRMDAY);
	alm_tm->tm_year = readb(base + PIC32_ALRMYEAR);

	alm_en = readb(base + PIC32_RTCALRM);

	alrm->enabled = (alm_en & PIC32_RTCALRM_ALRMEN) ? 1 : 0;

	dev_dbg(dev, "getalarm: %d, %04d.%02d.%02d %02d:%02d:%02d\n",
		alm_en,
		1900 + alm_tm->tm_year, alm_tm->tm_mon, alm_tm->tm_mday,
		alm_tm->tm_hour, alm_tm->tm_min, alm_tm->tm_sec);

	alm_tm->tm_sec = bcd2bin(alm_tm->tm_sec);
	alm_tm->tm_min = bcd2bin(alm_tm->tm_min);
	alm_tm->tm_hour = bcd2bin(alm_tm->tm_hour);
	alm_tm->tm_mday = bcd2bin(alm_tm->tm_mday);
	alm_tm->tm_mon = bcd2bin(alm_tm->tm_mon) - 1;
	alm_tm->tm_year = bcd2bin(alm_tm->tm_year);

	clk_disable(pdata->clk);
	return 0;
}

static int pic32_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct pic32_rtc_dev *pdata = dev_get_drvdata(dev);
	struct rtc_time *tm = &alrm->time;
	void __iomem *base = pdata->reg_base;

	clk_enable(pdata->clk);
	dev_dbg(dev, "setalarm: %d, %04d.%02d.%02d %02d:%02d:%02d\n",
		alrm->enabled,
		1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	writel(0x00, base + PIC32_ALRMTIME);
	writel(0x00, base + PIC32_ALRMDATE);

	pic32_rtc_setaie(dev, alrm->enabled);

	clk_disable(pdata->clk);
	return 0;
}

static int pic32_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct pic32_rtc_dev *pdata = dev_get_drvdata(dev);
	void __iomem *base = pdata->reg_base;
	unsigned int repeat;

	clk_enable(pdata->clk);

	repeat = readw(base + PIC32_RTCALRM);
	repeat &= PIC32_RTCALRM_ARPT;
	seq_printf(seq, "periodic_IRQ\t: %s\n", repeat  ? "yes" : "no");

	clk_disable(pdata->clk);
	return 0;
}

static const struct rtc_class_ops pic32_rtcops = {
	.read_time	  = pic32_rtc_gettime,
	.set_time	  = pic32_rtc_settime,
	.read_alarm	  = pic32_rtc_getalarm,
	.set_alarm	  = pic32_rtc_setalarm,
	.proc		  = pic32_rtc_proc,
	.alarm_irq_enable = pic32_rtc_setaie,
};

static void pic32_rtc_enable(struct pic32_rtc_dev *pdata, int en)
{
	void __iomem *base = pdata->reg_base;

	if (!base)
		return;

	clk_enable(pdata->clk);
	if (!en) {
		writel(PIC32_RTCCON_ON, base + PIC32_CLR(PIC32_RTCCON));
	} else {
		pic32_syskey_unlock();

		writel(PIC32_RTCCON_RTCWREN, base + PIC32_SET(PIC32_RTCCON));
		writel(3 << 9, base + PIC32_CLR(PIC32_RTCCON));

		if (!(readl(base + PIC32_RTCCON) & PIC32_RTCCON_ON))
			writel(PIC32_RTCCON_ON, base + PIC32_SET(PIC32_RTCCON));
	}
	clk_disable(pdata->clk);
}

static int pic32_rtc_remove(struct platform_device *pdev)
{
	struct pic32_rtc_dev *pdata = platform_get_drvdata(pdev);

	pic32_rtc_setaie(&pdev->dev, 0);
	clk_unprepare(pdata->clk);
	pdata->clk = NULL;

	return 0;
}

static int pic32_rtc_probe(struct platform_device *pdev)
{
	struct pic32_rtc_dev *pdata;
	struct resource *res;
	int ret;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	platform_set_drvdata(pdev, pdata);

	pdata->alarm_irq = platform_get_irq(pdev, 0);
	if (pdata->alarm_irq < 0) {
		dev_err(&pdev->dev, "no irq for alarm\n");
		return pdata->alarm_irq;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pdata->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pdata->reg_base))
		return PTR_ERR(pdata->reg_base);

	pdata->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pdata->clk)) {
		dev_err(&pdev->dev, "failed to find rtc clock source\n");
		ret = PTR_ERR(pdata->clk);
		pdata->clk = NULL;
		return ret;
	}

	spin_lock_init(&pdata->alarm_lock);

	clk_prepare_enable(pdata->clk);

	pic32_rtc_enable(pdata, 1);

	device_init_wakeup(&pdev->dev, 1);

	pdata->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
						 &pic32_rtcops,
						 THIS_MODULE);
	if (IS_ERR(pdata->rtc)) {
		ret = PTR_ERR(pdata->rtc);
		goto err_nortc;
	}

	pdata->rtc->max_user_freq = 128;

	pic32_rtc_setfreq(&pdev->dev, 1);
	ret = devm_request_irq(&pdev->dev, pdata->alarm_irq,
			       pic32_rtc_alarmirq, 0,
			       dev_name(&pdev->dev), pdata);
	if (ret) {
		dev_err(&pdev->dev,
			"IRQ %d error %d\n", pdata->alarm_irq, ret);
		goto err_nortc;
	}

	clk_disable(pdata->clk);

	return 0;

err_nortc:
	pic32_rtc_enable(pdata, 0);
	clk_disable_unprepare(pdata->clk);

	return ret;
}

static const struct of_device_id pic32_rtc_dt_ids[] = {
	{ .compatible = "microchip,pic32mzda-rtc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pic32_rtc_dt_ids);

static struct platform_driver pic32_rtc_driver = {
	.probe		= pic32_rtc_probe,
	.remove		= pic32_rtc_remove,
	.driver		= {
		.name	= "pic32-rtc",
		.of_match_table	= of_match_ptr(pic32_rtc_dt_ids),
	},
};
module_platform_driver(pic32_rtc_driver);

MODULE_DESCRIPTION("Microchip PIC32 RTC Driver");
MODULE_AUTHOR("Joshua Henderson <joshua.henderson@microchip.com>");
MODULE_LICENSE("GPL");

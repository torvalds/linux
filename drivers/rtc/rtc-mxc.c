// SPDX-License-Identifier: GPL-2.0+
//
// Copyright 2004-2008 Freescale Semiconductor, Inc. All Rights Reserved.

#include <linux/io.h>
#include <linux/rtc.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>

#define RTC_INPUT_CLK_32768HZ	(0x00 << 5)
#define RTC_INPUT_CLK_32000HZ	(0x01 << 5)
#define RTC_INPUT_CLK_38400HZ	(0x02 << 5)

#define RTC_SW_BIT      (1 << 0)
#define RTC_ALM_BIT     (1 << 2)
#define RTC_1HZ_BIT     (1 << 4)
#define RTC_2HZ_BIT     (1 << 7)
#define RTC_SAM0_BIT    (1 << 8)
#define RTC_SAM1_BIT    (1 << 9)
#define RTC_SAM2_BIT    (1 << 10)
#define RTC_SAM3_BIT    (1 << 11)
#define RTC_SAM4_BIT    (1 << 12)
#define RTC_SAM5_BIT    (1 << 13)
#define RTC_SAM6_BIT    (1 << 14)
#define RTC_SAM7_BIT    (1 << 15)
#define PIT_ALL_ON      (RTC_2HZ_BIT | RTC_SAM0_BIT | RTC_SAM1_BIT | \
			 RTC_SAM2_BIT | RTC_SAM3_BIT | RTC_SAM4_BIT | \
			 RTC_SAM5_BIT | RTC_SAM6_BIT | RTC_SAM7_BIT)

#define RTC_ENABLE_BIT  (1 << 7)

#define MAX_PIE_NUM     9
#define MAX_PIE_FREQ    512

#define MXC_RTC_TIME	0
#define MXC_RTC_ALARM	1

#define RTC_HOURMIN	0x00	/*  32bit rtc hour/min counter reg */
#define RTC_SECOND	0x04	/*  32bit rtc seconds counter reg */
#define RTC_ALRM_HM	0x08	/*  32bit rtc alarm hour/min reg */
#define RTC_ALRM_SEC	0x0C	/*  32bit rtc alarm seconds reg */
#define RTC_RTCCTL	0x10	/*  32bit rtc control reg */
#define RTC_RTCISR	0x14	/*  32bit rtc interrupt status reg */
#define RTC_RTCIENR	0x18	/*  32bit rtc interrupt enable reg */
#define RTC_STPWCH	0x1C	/*  32bit rtc stopwatch min reg */
#define RTC_DAYR	0x20	/*  32bit rtc days counter reg */
#define RTC_DAYALARM	0x24	/*  32bit rtc day alarm reg */
#define RTC_TEST1	0x28	/*  32bit rtc test reg 1 */
#define RTC_TEST2	0x2C	/*  32bit rtc test reg 2 */
#define RTC_TEST3	0x30	/*  32bit rtc test reg 3 */

enum imx_rtc_type {
	IMX1_RTC,
	IMX21_RTC,
};

struct rtc_plat_data {
	struct rtc_device *rtc;
	void __iomem *ioaddr;
	int irq;
	struct clk *clk_ref;
	struct clk *clk_ipg;
	struct rtc_time g_rtc_alarm;
	enum imx_rtc_type devtype;
};

static const struct of_device_id imx_rtc_dt_ids[] = {
	{ .compatible = "fsl,imx1-rtc", .data = (const void *)IMX1_RTC },
	{ .compatible = "fsl,imx21-rtc", .data = (const void *)IMX21_RTC },
	{}
};
MODULE_DEVICE_TABLE(of, imx_rtc_dt_ids);

static inline int is_imx1_rtc(struct rtc_plat_data *data)
{
	return data->devtype == IMX1_RTC;
}

/*
 * This function is used to obtain the RTC time or the alarm value in
 * second.
 */
static time64_t get_alarm_or_time(struct device *dev, int time_alarm)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);
	void __iomem *ioaddr = pdata->ioaddr;
	u32 day = 0, hr = 0, min = 0, sec = 0, hr_min = 0;

	switch (time_alarm) {
	case MXC_RTC_TIME:
		day = readw(ioaddr + RTC_DAYR);
		hr_min = readw(ioaddr + RTC_HOURMIN);
		sec = readw(ioaddr + RTC_SECOND);
		break;
	case MXC_RTC_ALARM:
		day = readw(ioaddr + RTC_DAYALARM);
		hr_min = readw(ioaddr + RTC_ALRM_HM) & 0xffff;
		sec = readw(ioaddr + RTC_ALRM_SEC);
		break;
	}

	hr = hr_min >> 8;
	min = hr_min & 0xff;

	return ((((time64_t)day * 24 + hr) * 60) + min) * 60 + sec;
}

/*
 * This function sets the RTC alarm value or the time value.
 */
static void set_alarm_or_time(struct device *dev, int time_alarm, time64_t time)
{
	u32 tod, day, hr, min, sec, temp;
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);
	void __iomem *ioaddr = pdata->ioaddr;

	day = div_s64_rem(time, 86400, &tod);

	/* time is within a day now */
	hr = tod / 3600;
	tod -= hr * 3600;

	/* time is within an hour now */
	min = tod / 60;
	sec = tod - min * 60;

	temp = (hr << 8) + min;

	switch (time_alarm) {
	case MXC_RTC_TIME:
		writew(day, ioaddr + RTC_DAYR);
		writew(sec, ioaddr + RTC_SECOND);
		writew(temp, ioaddr + RTC_HOURMIN);
		break;
	case MXC_RTC_ALARM:
		writew(day, ioaddr + RTC_DAYALARM);
		writew(sec, ioaddr + RTC_ALRM_SEC);
		writew(temp, ioaddr + RTC_ALRM_HM);
		break;
	}
}

/*
 * This function updates the RTC alarm registers and then clears all the
 * interrupt status bits.
 */
static void rtc_update_alarm(struct device *dev, struct rtc_time *alrm)
{
	time64_t time;
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);
	void __iomem *ioaddr = pdata->ioaddr;

	time = rtc_tm_to_time64(alrm);

	/* clear all the interrupt status bits */
	writew(readw(ioaddr + RTC_RTCISR), ioaddr + RTC_RTCISR);
	set_alarm_or_time(dev, MXC_RTC_ALARM, time);
}

static void mxc_rtc_irq_enable(struct device *dev, unsigned int bit,
				unsigned int enabled)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);
	void __iomem *ioaddr = pdata->ioaddr;
	u32 reg;
	unsigned long flags;

	spin_lock_irqsave(&pdata->rtc->irq_lock, flags);
	reg = readw(ioaddr + RTC_RTCIENR);

	if (enabled)
		reg |= bit;
	else
		reg &= ~bit;

	writew(reg, ioaddr + RTC_RTCIENR);
	spin_unlock_irqrestore(&pdata->rtc->irq_lock, flags);
}

/* This function is the RTC interrupt service routine. */
static irqreturn_t mxc_rtc_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;
	u32 status;
	u32 events = 0;

	spin_lock(&pdata->rtc->irq_lock);
	status = readw(ioaddr + RTC_RTCISR) & readw(ioaddr + RTC_RTCIENR);
	/* clear interrupt sources */
	writew(status, ioaddr + RTC_RTCISR);

	/* update irq data & counter */
	if (status & RTC_ALM_BIT) {
		events |= (RTC_AF | RTC_IRQF);
		/* RTC alarm should be one-shot */
		mxc_rtc_irq_enable(&pdev->dev, RTC_ALM_BIT, 0);
	}

	if (status & PIT_ALL_ON)
		events |= (RTC_PF | RTC_IRQF);

	rtc_update_irq(pdata->rtc, 1, events);
	spin_unlock(&pdata->rtc->irq_lock);

	return IRQ_HANDLED;
}

static int mxc_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	mxc_rtc_irq_enable(dev, RTC_ALM_BIT, enabled);
	return 0;
}

/*
 * This function reads the current RTC time into tm in Gregorian date.
 */
static int mxc_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	time64_t val;

	/* Avoid roll-over from reading the different registers */
	do {
		val = get_alarm_or_time(dev, MXC_RTC_TIME);
	} while (val != get_alarm_or_time(dev, MXC_RTC_TIME));

	rtc_time64_to_tm(val, tm);

	return 0;
}

/*
 * This function sets the internal RTC time based on tm in Gregorian date.
 */
static int mxc_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	time64_t time = rtc_tm_to_time64(tm);

	/* Avoid roll-over from reading the different registers */
	do {
		set_alarm_or_time(dev, MXC_RTC_TIME, time);
	} while (time != get_alarm_or_time(dev, MXC_RTC_TIME));

	return 0;
}

/*
 * This function reads the current alarm value into the passed in 'alrm'
 * argument. It updates the alrm's pending field value based on the whether
 * an alarm interrupt occurs or not.
 */
static int mxc_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);
	void __iomem *ioaddr = pdata->ioaddr;

	rtc_time64_to_tm(get_alarm_or_time(dev, MXC_RTC_ALARM), &alrm->time);
	alrm->pending = ((readw(ioaddr + RTC_RTCISR) & RTC_ALM_BIT)) ? 1 : 0;

	return 0;
}

/*
 * This function sets the RTC alarm based on passed in alrm.
 */
static int mxc_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);

	rtc_update_alarm(dev, &alrm->time);

	memcpy(&pdata->g_rtc_alarm, &alrm->time, sizeof(struct rtc_time));
	mxc_rtc_irq_enable(dev, RTC_ALM_BIT, alrm->enabled);

	return 0;
}

/* RTC layer */
static const struct rtc_class_ops mxc_rtc_ops = {
	.read_time		= mxc_rtc_read_time,
	.set_time		= mxc_rtc_set_time,
	.read_alarm		= mxc_rtc_read_alarm,
	.set_alarm		= mxc_rtc_set_alarm,
	.alarm_irq_enable	= mxc_rtc_alarm_irq_enable,
};

static int mxc_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;
	struct rtc_plat_data *pdata = NULL;
	u32 reg;
	unsigned long rate;
	int ret;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->devtype = (uintptr_t)of_device_get_match_data(&pdev->dev);

	pdata->ioaddr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pdata->ioaddr))
		return PTR_ERR(pdata->ioaddr);

	rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	pdata->rtc = rtc;
	rtc->ops = &mxc_rtc_ops;
	if (is_imx1_rtc(pdata)) {
		struct rtc_time tm;

		/* 9bit days + hours minutes seconds */
		rtc->range_max = (1 << 9) * 86400 - 1;

		/*
		 * Set the start date as beginning of the current year. This can
		 * be overridden using device tree.
		 */
		rtc_time64_to_tm(ktime_get_real_seconds(), &tm);
		rtc->start_secs =  mktime64(tm.tm_year, 1, 1, 0, 0, 0);
		rtc->set_start_time = true;
	} else {
		/* 16bit days + hours minutes seconds */
		rtc->range_max = (1 << 16) * 86400ULL - 1;
	}

	pdata->clk_ipg = devm_clk_get_enabled(&pdev->dev, "ipg");
	if (IS_ERR(pdata->clk_ipg)) {
		dev_err(&pdev->dev, "unable to get ipg clock!\n");
		return PTR_ERR(pdata->clk_ipg);
	}

	pdata->clk_ref = devm_clk_get_enabled(&pdev->dev, "ref");
	if (IS_ERR(pdata->clk_ref)) {
		dev_err(&pdev->dev, "unable to get ref clock!\n");
		return PTR_ERR(pdata->clk_ref);
	}

	rate = clk_get_rate(pdata->clk_ref);

	if (rate == 32768)
		reg = RTC_INPUT_CLK_32768HZ;
	else if (rate == 32000)
		reg = RTC_INPUT_CLK_32000HZ;
	else if (rate == 38400)
		reg = RTC_INPUT_CLK_38400HZ;
	else {
		dev_err(&pdev->dev, "rtc clock is not valid (%lu)\n", rate);
		return -EINVAL;
	}

	reg |= RTC_ENABLE_BIT;
	writew(reg, (pdata->ioaddr + RTC_RTCCTL));
	if (((readw(pdata->ioaddr + RTC_RTCCTL)) & RTC_ENABLE_BIT) == 0) {
		dev_err(&pdev->dev, "hardware module can't be enabled!\n");
		return -EIO;
	}

	platform_set_drvdata(pdev, pdata);

	/* Configure and enable the RTC */
	pdata->irq = platform_get_irq(pdev, 0);

	if (pdata->irq >= 0 &&
	    devm_request_irq(&pdev->dev, pdata->irq, mxc_rtc_interrupt,
			     IRQF_SHARED, pdev->name, pdev) < 0) {
		dev_warn(&pdev->dev, "interrupt not available.\n");
		pdata->irq = -1;
	}

	if (pdata->irq >= 0) {
		device_init_wakeup(&pdev->dev, 1);
		ret = dev_pm_set_wake_irq(&pdev->dev, pdata->irq);
		if (ret)
			dev_err(&pdev->dev, "failed to enable irq wake\n");
	}

	ret = devm_rtc_register_device(rtc);

	return ret;
}

static struct platform_driver mxc_rtc_driver = {
	.driver = {
		   .name	= "mxc_rtc",
		   .of_match_table = imx_rtc_dt_ids,
	},
	.probe = mxc_rtc_probe,
};

module_platform_driver(mxc_rtc_driver)

MODULE_AUTHOR("Daniel Mack <daniel@caiaq.de>");
MODULE_DESCRIPTION("RTC driver for Freescale MXC");
MODULE_LICENSE("GPL");


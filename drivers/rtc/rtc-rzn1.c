// SPDX-License-Identifier: GPL-2.0+
/*
 * Renesas RZ/N1 Real Time Clock interface for Linux
 *
 * Copyright:
 * - 2014 Renesas Electronics Europe Limited
 * - 2022 Schneider Electric
 *
 * Authors:
 * - Michel Pollet <michel.pollet@bp.renesas.com>, <buserror@gmail.com>
 * - Miquel Raynal <miquel.raynal@bootlin.com>
 */

#include <linux/bcd.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/rtc.h>

#define RZN1_RTC_CTL0 0x00
#define   RZN1_RTC_CTL0_SLSB_SUBU 0
#define   RZN1_RTC_CTL0_SLSB_SCMP BIT(4)
#define   RZN1_RTC_CTL0_AMPM BIT(5)
#define   RZN1_RTC_CTL0_CE BIT(7)

#define RZN1_RTC_CTL1 0x04
#define   RZN1_RTC_CTL1_ALME BIT(4)

#define RZN1_RTC_CTL2 0x08
#define   RZN1_RTC_CTL2_WAIT BIT(0)
#define   RZN1_RTC_CTL2_WST BIT(1)
#define   RZN1_RTC_CTL2_WUST BIT(5)
#define   RZN1_RTC_CTL2_STOPPED (RZN1_RTC_CTL2_WAIT | RZN1_RTC_CTL2_WST)

#define RZN1_RTC_SEC 0x14
#define RZN1_RTC_MIN 0x18
#define RZN1_RTC_HOUR 0x1c
#define RZN1_RTC_WEEK 0x20
#define RZN1_RTC_DAY 0x24
#define RZN1_RTC_MONTH 0x28
#define RZN1_RTC_YEAR 0x2c

#define RZN1_RTC_SUBU 0x38
#define   RZN1_RTC_SUBU_DEV BIT(7)
#define   RZN1_RTC_SUBU_DECR BIT(6)

#define RZN1_RTC_ALM 0x40
#define RZN1_RTC_ALH 0x44
#define RZN1_RTC_ALW 0x48

#define RZN1_RTC_SECC 0x4c
#define RZN1_RTC_MINC 0x50
#define RZN1_RTC_HOURC 0x54
#define RZN1_RTC_WEEKC 0x58
#define RZN1_RTC_DAYC 0x5c
#define RZN1_RTC_MONTHC 0x60
#define RZN1_RTC_YEARC 0x64

struct rzn1_rtc {
	struct rtc_device *rtcdev;
	void __iomem *base;
};

static void rzn1_rtc_get_time_snapshot(struct rzn1_rtc *rtc, struct rtc_time *tm)
{
	tm->tm_sec = readl(rtc->base + RZN1_RTC_SECC);
	tm->tm_min = readl(rtc->base + RZN1_RTC_MINC);
	tm->tm_hour = readl(rtc->base + RZN1_RTC_HOURC);
	tm->tm_wday = readl(rtc->base + RZN1_RTC_WEEKC);
	tm->tm_mday = readl(rtc->base + RZN1_RTC_DAYC);
	tm->tm_mon = readl(rtc->base + RZN1_RTC_MONTHC);
	tm->tm_year = readl(rtc->base + RZN1_RTC_YEARC);
}

static unsigned int rzn1_rtc_tm_to_wday(struct rtc_time *tm)
{
	time64_t time;
	unsigned int days;
	u32 secs;

	time = rtc_tm_to_time64(tm);
	days = div_s64_rem(time, 86400, &secs);

	/* day of the week, 1970-01-01 was a Thursday */
	return (days + 4) % 7;
}

static int rzn1_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rzn1_rtc *rtc = dev_get_drvdata(dev);
	u32 val, secs;

	/*
	 * The RTC was not started or is stopped and thus does not carry the
	 * proper time/date.
	 */
	val = readl(rtc->base + RZN1_RTC_CTL2);
	if (val & RZN1_RTC_CTL2_STOPPED)
		return -EINVAL;

	rzn1_rtc_get_time_snapshot(rtc, tm);
	secs = readl(rtc->base + RZN1_RTC_SECC);
	if (tm->tm_sec != secs)
		rzn1_rtc_get_time_snapshot(rtc, tm);

	tm->tm_sec = bcd2bin(tm->tm_sec);
	tm->tm_min = bcd2bin(tm->tm_min);
	tm->tm_hour = bcd2bin(tm->tm_hour);
	tm->tm_wday = bcd2bin(tm->tm_wday);
	tm->tm_mday = bcd2bin(tm->tm_mday);
	tm->tm_mon = bcd2bin(tm->tm_mon);
	tm->tm_year = bcd2bin(tm->tm_year);

	return 0;
}

static int rzn1_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rzn1_rtc *rtc = dev_get_drvdata(dev);
	u32 val;
	int ret;

	tm->tm_sec = bin2bcd(tm->tm_sec);
	tm->tm_min = bin2bcd(tm->tm_min);
	tm->tm_hour = bin2bcd(tm->tm_hour);
	tm->tm_wday = bin2bcd(rzn1_rtc_tm_to_wday(tm));
	tm->tm_mday = bin2bcd(tm->tm_mday);
	tm->tm_mon = bin2bcd(tm->tm_mon);
	tm->tm_year = bin2bcd(tm->tm_year);

	val = readl(rtc->base + RZN1_RTC_CTL2);
	if (!(val & RZN1_RTC_CTL2_STOPPED)) {
		/* Hold the counter if it was counting up */
		writel(RZN1_RTC_CTL2_WAIT, rtc->base + RZN1_RTC_CTL2);

		/* Wait for the counter to stop: two 32k clock cycles */
		usleep_range(61, 100);
		ret = readl_poll_timeout(rtc->base + RZN1_RTC_CTL2, val,
					 val & RZN1_RTC_CTL2_WST, 0, 100);
		if (ret)
			return ret;
	}

	writel(tm->tm_sec, rtc->base + RZN1_RTC_SEC);
	writel(tm->tm_min, rtc->base + RZN1_RTC_MIN);
	writel(tm->tm_hour, rtc->base + RZN1_RTC_HOUR);
	writel(tm->tm_wday, rtc->base + RZN1_RTC_WEEK);
	writel(tm->tm_mday, rtc->base + RZN1_RTC_DAY);
	writel(tm->tm_mon, rtc->base + RZN1_RTC_MONTH);
	writel(tm->tm_year, rtc->base + RZN1_RTC_YEAR);
	writel(0, rtc->base + RZN1_RTC_CTL2);

	return 0;
}

static irqreturn_t rzn1_rtc_alarm_irq(int irq, void *dev_id)
{
	struct rzn1_rtc *rtc = dev_id;

	rtc_update_irq(rtc->rtcdev, 1, RTC_AF | RTC_IRQF);

	return IRQ_HANDLED;
}

static int rzn1_rtc_alarm_irq_enable(struct device *dev, unsigned int enable)
{
	struct rzn1_rtc *rtc = dev_get_drvdata(dev);
	u32 ctl1 = readl(rtc->base + RZN1_RTC_CTL1);

	if (enable)
		ctl1 |= RZN1_RTC_CTL1_ALME;
	else
		ctl1 &= ~RZN1_RTC_CTL1_ALME;

	writel(ctl1, rtc->base + RZN1_RTC_CTL1);

	return 0;
}

static int rzn1_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rzn1_rtc *rtc = dev_get_drvdata(dev);
	struct rtc_time *tm = &alrm->time;
	unsigned int min, hour, wday, delta_days;
	time64_t alarm;
	u32 ctl1;
	int ret;

	ret = rzn1_rtc_read_time(dev, tm);
	if (ret)
		return ret;

	min = readl(rtc->base + RZN1_RTC_ALM);
	hour = readl(rtc->base + RZN1_RTC_ALH);
	wday = readl(rtc->base + RZN1_RTC_ALW);

	tm->tm_sec = 0;
	tm->tm_min = bcd2bin(min);
	tm->tm_hour = bcd2bin(hour);
	delta_days = ((fls(wday) - 1) - tm->tm_wday + 7) % 7;
	tm->tm_wday = fls(wday) - 1;

	if (delta_days) {
		alarm = rtc_tm_to_time64(tm) + (delta_days * 86400);
		rtc_time64_to_tm(alarm, tm);
	}

	ctl1 = readl(rtc->base + RZN1_RTC_CTL1);
	alrm->enabled = !!(ctl1 & RZN1_RTC_CTL1_ALME);

	return 0;
}

static int rzn1_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rzn1_rtc *rtc = dev_get_drvdata(dev);
	struct rtc_time *tm = &alrm->time, tm_now;
	unsigned long alarm, farest;
	unsigned int days_ahead, wday;
	int ret;

	ret = rzn1_rtc_read_time(dev, &tm_now);
	if (ret)
		return ret;

	/* We cannot set alarms more than one week ahead */
	farest = rtc_tm_to_time64(&tm_now) + rtc->rtcdev->alarm_offset_max;
	alarm = rtc_tm_to_time64(tm);
	if (time_after(alarm, farest))
		return -ERANGE;

	/* Convert alarm day into week day */
	days_ahead = tm->tm_mday - tm_now.tm_mday;
	wday = (tm_now.tm_wday + days_ahead) % 7;

	writel(bin2bcd(tm->tm_min), rtc->base + RZN1_RTC_ALM);
	writel(bin2bcd(tm->tm_hour), rtc->base + RZN1_RTC_ALH);
	writel(BIT(wday), rtc->base + RZN1_RTC_ALW);

	rzn1_rtc_alarm_irq_enable(dev, alrm->enabled);

	return 0;
}

static int rzn1_rtc_read_offset(struct device *dev, long *offset)
{
	struct rzn1_rtc *rtc = dev_get_drvdata(dev);
	unsigned int ppb_per_step;
	bool subtract;
	u32 val;

	val = readl(rtc->base + RZN1_RTC_SUBU);
	ppb_per_step = val & RZN1_RTC_SUBU_DEV ? 1017 : 3051;
	subtract = val & RZN1_RTC_SUBU_DECR;
	val &= 0x3F;

	if (!val)
		*offset = 0;
	else if (subtract)
		*offset = -(((~val) & 0x3F) + 1) * ppb_per_step;
	else
		*offset = (val - 1) * ppb_per_step;

	return 0;
}

static int rzn1_rtc_set_offset(struct device *dev, long offset)
{
	struct rzn1_rtc *rtc = dev_get_drvdata(dev);
	int stepsh, stepsl, steps;
	u32 subu = 0, ctl2;
	int ret;

	/*
	 * Check which resolution mode (every 20 or 60s) can be used.
	 * Between 2 and 124 clock pulses can be added or substracted.
	 *
	 * In 20s mode, the minimum resolution is 2 / (32768 * 20) which is
	 * close to 3051 ppb. In 60s mode, the resolution is closer to 1017.
	 */
	stepsh = DIV_ROUND_CLOSEST(offset, 1017);
	stepsl = DIV_ROUND_CLOSEST(offset, 3051);

	if (stepsh >= -0x3E && stepsh <= 0x3E) {
		/* 1017 ppb per step */
		steps = stepsh;
		subu |= RZN1_RTC_SUBU_DEV;
	} else if (stepsl >= -0x3E && stepsl <= 0x3E) {
		/* 3051 ppb per step */
		steps = stepsl;
	} else {
		return -ERANGE;
	}

	if (!steps)
		return 0;

	if (steps > 0) {
		subu |= steps + 1;
	} else {
		subu |= RZN1_RTC_SUBU_DECR;
		subu |= (~(-steps - 1)) & 0x3F;
	}

	ret = readl_poll_timeout(rtc->base + RZN1_RTC_CTL2, ctl2,
				 !(ctl2 & RZN1_RTC_CTL2_WUST), 100, 2000000);
	if (ret)
		return ret;

	writel(subu, rtc->base + RZN1_RTC_SUBU);

	return 0;
}

static const struct rtc_class_ops rzn1_rtc_ops = {
	.read_time = rzn1_rtc_read_time,
	.set_time = rzn1_rtc_set_time,
	.read_alarm = rzn1_rtc_read_alarm,
	.set_alarm = rzn1_rtc_set_alarm,
	.alarm_irq_enable = rzn1_rtc_alarm_irq_enable,
	.read_offset = rzn1_rtc_read_offset,
	.set_offset = rzn1_rtc_set_offset,
};

static int rzn1_rtc_probe(struct platform_device *pdev)
{
	struct rzn1_rtc *rtc;
	int alarm_irq;
	int ret;

	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	platform_set_drvdata(pdev, rtc);

	rtc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rtc->base))
		return dev_err_probe(&pdev->dev, PTR_ERR(rtc->base), "Missing reg\n");

	alarm_irq = platform_get_irq(pdev, 0);
	if (alarm_irq < 0)
		return alarm_irq;

	rtc->rtcdev = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc->rtcdev))
		return PTR_ERR(rtc->rtcdev);

	rtc->rtcdev->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rtc->rtcdev->range_max = RTC_TIMESTAMP_END_2099;
	rtc->rtcdev->alarm_offset_max = 7 * 86400;
	rtc->rtcdev->ops = &rzn1_rtc_ops;
	set_bit(RTC_FEATURE_ALARM_RES_MINUTE, rtc->rtcdev->features);
	clear_bit(RTC_FEATURE_UPDATE_INTERRUPT, rtc->rtcdev->features);

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret < 0)
		return ret;
	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0)
		return ret;

	/*
	 * Ensure the clock counter is enabled.
	 * Set 24-hour mode and possible oscillator offset compensation in SUBU mode.
	 */
	writel(RZN1_RTC_CTL0_CE | RZN1_RTC_CTL0_AMPM | RZN1_RTC_CTL0_SLSB_SUBU,
	       rtc->base + RZN1_RTC_CTL0);

	/* Disable all interrupts */
	writel(0, rtc->base + RZN1_RTC_CTL1);

	ret = devm_request_irq(&pdev->dev, alarm_irq, rzn1_rtc_alarm_irq, 0,
			       dev_name(&pdev->dev), rtc);
	if (ret) {
		dev_err(&pdev->dev, "RTC timer interrupt not available\n");
		goto dis_runtime_pm;
	}

	ret = devm_rtc_register_device(rtc->rtcdev);
	if (ret)
		goto dis_runtime_pm;

	return 0;

dis_runtime_pm:
	pm_runtime_put(&pdev->dev);

	return ret;
}

static void rzn1_rtc_remove(struct platform_device *pdev)
{
	pm_runtime_put(&pdev->dev);
}

static const struct of_device_id rzn1_rtc_of_match[] = {
	{ .compatible	= "renesas,rzn1-rtc" },
	{},
};
MODULE_DEVICE_TABLE(of, rzn1_rtc_of_match);

static struct platform_driver rzn1_rtc_driver = {
	.probe = rzn1_rtc_probe,
	.remove_new = rzn1_rtc_remove,
	.driver = {
		.name	= "rzn1-rtc",
		.of_match_table = rzn1_rtc_of_match,
	},
};
module_platform_driver(rzn1_rtc_driver);

MODULE_AUTHOR("Michel Pollet <Michel.Pollet@bp.renesas.com");
MODULE_AUTHOR("Miquel Raynal <miquel.raynal@bootlin.com");
MODULE_DESCRIPTION("RZ/N1 RTC driver");
MODULE_LICENSE("GPL");

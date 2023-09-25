// SPDX-License-Identifier: GPL-2.0
/*
 * RTC driver for Nuvoton MA35D1
 *
 * Copyright (C) 2023 Nuvoton Technology Corp.
 */

#include <linux/bcd.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

/* MA35D1 RTC Control Registers */
#define MA35_REG_RTC_INIT	0x00
#define MA35_REG_RTC_SINFASTS	0x04
#define MA35_REG_RTC_FREQADJ	0x08
#define MA35_REG_RTC_TIME	0x0c
#define MA35_REG_RTC_CAL	0x10
#define MA35_REG_RTC_CLKFMT	0x14
#define MA35_REG_RTC_WEEKDAY	0x18
#define MA35_REG_RTC_TALM	0x1c
#define MA35_REG_RTC_CALM	0x20
#define MA35_REG_RTC_LEAPYEAR	0x24
#define MA35_REG_RTC_INTEN	0x28
#define MA35_REG_RTC_INTSTS	0x2c

/* register MA35_REG_RTC_INIT */
#define RTC_INIT_ACTIVE		BIT(0)
#define RTC_INIT_MAGIC_CODE	0xa5eb1357

/* register MA35_REG_RTC_CLKFMT */
#define RTC_CLKFMT_24HEN	BIT(0)
#define RTC_CLKFMT_DCOMPEN	BIT(16)

/* register MA35_REG_RTC_INTEN */
#define RTC_INTEN_ALMIEN	BIT(0)
#define RTC_INTEN_UIEN		BIT(1)
#define RTC_INTEN_CLKFIEN	BIT(24)
#define RTC_INTEN_CLKSTIEN	BIT(25)

/* register MA35_REG_RTC_INTSTS */
#define RTC_INTSTS_ALMIF	BIT(0)
#define RTC_INTSTS_UIF		BIT(1)
#define RTC_INTSTS_CLKFIF	BIT(24)
#define RTC_INTSTS_CLKSTIF	BIT(25)

#define RTC_INIT_TIMEOUT	250

struct ma35_rtc {
	int irq_num;
	void __iomem *rtc_reg;
	struct rtc_device *rtcdev;
};

static u32 rtc_reg_read(struct ma35_rtc *p, u32 offset)
{
	return __raw_readl(p->rtc_reg + offset);
}

static inline void rtc_reg_write(struct ma35_rtc *p, u32 offset, u32 value)
{
	__raw_writel(value, p->rtc_reg + offset);
}

static irqreturn_t ma35d1_rtc_interrupt(int irq, void *data)
{
	struct ma35_rtc *rtc = (struct ma35_rtc *)data;
	unsigned long events = 0, rtc_irq;

	rtc_irq = rtc_reg_read(rtc, MA35_REG_RTC_INTSTS);

	if (rtc_irq & RTC_INTSTS_ALMIF) {
		rtc_reg_write(rtc, MA35_REG_RTC_INTSTS, RTC_INTSTS_ALMIF);
		events |= RTC_AF | RTC_IRQF;
	}

	if (rtc_irq & RTC_INTSTS_UIF) {
		rtc_reg_write(rtc, MA35_REG_RTC_INTSTS, RTC_INTSTS_UIF);
		events |= RTC_UF | RTC_IRQF;
	}

	rtc_update_irq(rtc->rtcdev, 1, events);

	return IRQ_HANDLED;
}

static int ma35d1_rtc_init(struct ma35_rtc *rtc, u32 ms_timeout)
{
	const unsigned long timeout = jiffies + msecs_to_jiffies(ms_timeout);

	do {
		if (rtc_reg_read(rtc, MA35_REG_RTC_INIT) & RTC_INIT_ACTIVE)
			return 0;

		rtc_reg_write(rtc, MA35_REG_RTC_INIT, RTC_INIT_MAGIC_CODE);

		mdelay(1);

	} while (time_before(jiffies, timeout));

	return -ETIMEDOUT;
}

static int ma35d1_alarm_irq_enable(struct device *dev, u32 enabled)
{
	struct ma35_rtc *rtc = dev_get_drvdata(dev);
	u32 reg_ien;

	reg_ien = rtc_reg_read(rtc, MA35_REG_RTC_INTEN);

	if (enabled)
		rtc_reg_write(rtc, MA35_REG_RTC_INTEN, reg_ien | RTC_INTEN_ALMIEN);
	else
		rtc_reg_write(rtc, MA35_REG_RTC_INTEN, reg_ien & ~RTC_INTEN_ALMIEN);

	return 0;
}

static int ma35d1_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct ma35_rtc *rtc = dev_get_drvdata(dev);
	u32 time, cal, wday;

	do {
		time = rtc_reg_read(rtc, MA35_REG_RTC_TIME);
		cal  = rtc_reg_read(rtc, MA35_REG_RTC_CAL);
		wday = rtc_reg_read(rtc, MA35_REG_RTC_WEEKDAY);
	} while (time != rtc_reg_read(rtc, MA35_REG_RTC_TIME) ||
		 cal != rtc_reg_read(rtc, MA35_REG_RTC_CAL));

	tm->tm_mday = bcd2bin(cal >> 0);
	tm->tm_wday = wday;
	tm->tm_mon = bcd2bin(cal >> 8);
	tm->tm_mon = tm->tm_mon - 1;
	tm->tm_year = bcd2bin(cal >> 16) + 100;

	tm->tm_sec = bcd2bin(time >> 0);
	tm->tm_min = bcd2bin(time >> 8);
	tm->tm_hour = bcd2bin(time >> 16);

	return rtc_valid_tm(tm);
}

static int ma35d1_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct ma35_rtc *rtc = dev_get_drvdata(dev);
	u32 val;

	val = bin2bcd(tm->tm_mday) << 0 | bin2bcd(tm->tm_mon + 1) << 8 |
	      bin2bcd(tm->tm_year - 100) << 16;
	rtc_reg_write(rtc, MA35_REG_RTC_CAL, val);

	val = bin2bcd(tm->tm_sec) << 0 | bin2bcd(tm->tm_min) << 8 |
	      bin2bcd(tm->tm_hour) << 16;
	rtc_reg_write(rtc, MA35_REG_RTC_TIME, val);

	val = tm->tm_wday;
	rtc_reg_write(rtc, MA35_REG_RTC_WEEKDAY, val);

	return 0;
}

static int ma35d1_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct ma35_rtc *rtc = dev_get_drvdata(dev);
	u32 talm, calm;

	talm = rtc_reg_read(rtc, MA35_REG_RTC_TALM);
	calm = rtc_reg_read(rtc, MA35_REG_RTC_CALM);

	alrm->time.tm_mday = bcd2bin(calm >> 0);
	alrm->time.tm_mon = bcd2bin(calm >> 8);
	alrm->time.tm_mon = alrm->time.tm_mon - 1;

	alrm->time.tm_year = bcd2bin(calm >> 16) + 100;

	alrm->time.tm_sec = bcd2bin(talm >> 0);
	alrm->time.tm_min = bcd2bin(talm >> 8);
	alrm->time.tm_hour = bcd2bin(talm >> 16);

	return rtc_valid_tm(&alrm->time);
}

static int ma35d1_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct ma35_rtc *rtc = dev_get_drvdata(dev);
	unsigned long val;

	val = bin2bcd(alrm->time.tm_mday) << 0 | bin2bcd(alrm->time.tm_mon + 1) << 8 |
	      bin2bcd(alrm->time.tm_year - 100) << 16;
	rtc_reg_write(rtc, MA35_REG_RTC_CALM, val);

	val = bin2bcd(alrm->time.tm_sec) << 0 | bin2bcd(alrm->time.tm_min) << 8 |
	      bin2bcd(alrm->time.tm_hour) << 16;
	rtc_reg_write(rtc, MA35_REG_RTC_TALM, val);

	ma35d1_alarm_irq_enable(dev, alrm->enabled);

	return 0;
}

static const struct rtc_class_ops ma35d1_rtc_ops = {
	.read_time = ma35d1_rtc_read_time,
	.set_time = ma35d1_rtc_set_time,
	.read_alarm = ma35d1_rtc_read_alarm,
	.set_alarm = ma35d1_rtc_set_alarm,
	.alarm_irq_enable = ma35d1_alarm_irq_enable,
};

static int ma35d1_rtc_probe(struct platform_device *pdev)
{
	struct ma35_rtc *rtc;
	struct clk *clk;
	u32 regval;
	int ret;

	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->rtc_reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rtc->rtc_reg))
		return PTR_ERR(rtc->rtc_reg);

	clk = of_clk_get(pdev->dev.of_node, 0);
	if (IS_ERR(clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(clk), "failed to find rtc clock\n");

	ret = clk_prepare_enable(clk);
	if (ret)
		return ret;

	if (!(rtc_reg_read(rtc, MA35_REG_RTC_INIT) & RTC_INIT_ACTIVE)) {
		ret = ma35d1_rtc_init(rtc, RTC_INIT_TIMEOUT);
		if (ret)
			return dev_err_probe(&pdev->dev, ret, "rtc init failed\n");
	}

	rtc->irq_num = platform_get_irq(pdev, 0);

	ret = devm_request_irq(&pdev->dev, rtc->irq_num, ma35d1_rtc_interrupt,
			       IRQF_NO_SUSPEND, "ma35d1rtc", rtc);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to request rtc irq\n");

	platform_set_drvdata(pdev, rtc);

	device_init_wakeup(&pdev->dev, true);

	rtc->rtcdev = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc->rtcdev))
		return PTR_ERR(rtc->rtcdev);

	rtc->rtcdev->ops = &ma35d1_rtc_ops;
	rtc->rtcdev->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rtc->rtcdev->range_max = RTC_TIMESTAMP_END_2099;

	ret = devm_rtc_register_device(rtc->rtcdev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to register rtc device\n");

	regval = rtc_reg_read(rtc, MA35_REG_RTC_INTEN);
	regval |= RTC_INTEN_UIEN;
	rtc_reg_write(rtc, MA35_REG_RTC_INTEN, regval);

	return 0;
}

static int ma35d1_rtc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct ma35_rtc *rtc = platform_get_drvdata(pdev);
	u32 regval;

	if (device_may_wakeup(&pdev->dev))
		enable_irq_wake(rtc->irq_num);

	regval = rtc_reg_read(rtc, MA35_REG_RTC_INTEN);
	regval &= ~RTC_INTEN_UIEN;
	rtc_reg_write(rtc, MA35_REG_RTC_INTEN, regval);

	return 0;
}

static int ma35d1_rtc_resume(struct platform_device *pdev)
{
	struct ma35_rtc *rtc = platform_get_drvdata(pdev);
	u32 regval;

	if (device_may_wakeup(&pdev->dev))
		disable_irq_wake(rtc->irq_num);

	regval = rtc_reg_read(rtc, MA35_REG_RTC_INTEN);
	regval |= RTC_INTEN_UIEN;
	rtc_reg_write(rtc, MA35_REG_RTC_INTEN, regval);

	return 0;
}

static const struct of_device_id ma35d1_rtc_of_match[] = {
	{ .compatible = "nuvoton,ma35d1-rtc", },
	{},
};
MODULE_DEVICE_TABLE(of, ma35d1_rtc_of_match);

static struct platform_driver ma35d1_rtc_driver = {
	.suspend    = ma35d1_rtc_suspend,
	.resume     = ma35d1_rtc_resume,
	.probe      = ma35d1_rtc_probe,
	.driver		= {
		.name	= "rtc-ma35d1",
		.of_match_table = ma35d1_rtc_of_match,
	},
};

module_platform_driver(ma35d1_rtc_driver);

MODULE_AUTHOR("Ming-Jen Chen <mjchen@nuvoton.com>");
MODULE_DESCRIPTION("MA35D1 RTC driver");
MODULE_LICENSE("GPL");

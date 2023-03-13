// SPDX-License-Identifier: GPL-2.0+
//
// Copyright 2022 NXP.

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/regmap.h>
#include <linux/rtc.h>

#define BBNSM_CTRL	0x8
#define BBNSM_INT_EN	0x10
#define BBNSM_EVENTS	0x14
#define BBNSM_RTC_LS	0x40
#define BBNSM_RTC_MS	0x44
#define BBNSM_TA	0x50

#define RTC_EN		0x2
#define RTC_EN_MSK	0x3
#define TA_EN		(0x2 << 2)
#define TA_DIS		(0x1 << 2)
#define TA_EN_MSK	(0x3 << 2)
#define RTC_INT_EN	0x2
#define TA_INT_EN	(0x2 << 2)

#define BBNSM_EVENT_TA	(0x2 << 2)

#define CNTR_TO_SECS_SH	15

struct bbnsm_rtc {
	struct rtc_device *rtc;
	struct regmap *regmap;
	int irq;
	struct clk *clk;
};

static u32 bbnsm_read_counter(struct bbnsm_rtc *bbnsm)
{
	u32 rtc_msb, rtc_lsb;
	unsigned int timeout = 100;
	u32 time;
	u32 tmp = 0;

	do {
		time = tmp;
		/* read the msb */
		regmap_read(bbnsm->regmap, BBNSM_RTC_MS, &rtc_msb);
		/* read the lsb */
		regmap_read(bbnsm->regmap, BBNSM_RTC_LS, &rtc_lsb);
		/* convert to seconds */
		tmp = (rtc_msb << 17) | (rtc_lsb >> 15);
	} while (tmp != time && --timeout);

	return time;
}

static int bbnsm_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct bbnsm_rtc *bbnsm = dev_get_drvdata(dev);
	unsigned long time;
	u32 val;

	regmap_read(bbnsm->regmap, BBNSM_CTRL, &val);
	if ((val & RTC_EN_MSK) != RTC_EN)
		return -EINVAL;

	time = bbnsm_read_counter(bbnsm);
	rtc_time64_to_tm(time, tm);

	return 0;
}

static int bbnsm_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct bbnsm_rtc *bbnsm = dev_get_drvdata(dev);
	unsigned long time = rtc_tm_to_time64(tm);

	/* disable the RTC first */
	regmap_update_bits(bbnsm->regmap, BBNSM_CTRL, RTC_EN_MSK, 0);

	/* write the 32bit sec time to 47 bit timer counter, leaving 15 LSBs blank */
	regmap_write(bbnsm->regmap, BBNSM_RTC_LS, time << CNTR_TO_SECS_SH);
	regmap_write(bbnsm->regmap, BBNSM_RTC_MS, time >> (32 - CNTR_TO_SECS_SH));

	/* Enable the RTC again */
	regmap_update_bits(bbnsm->regmap, BBNSM_CTRL, RTC_EN_MSK, RTC_EN);

	return 0;
}

static int bbnsm_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct bbnsm_rtc *bbnsm = dev_get_drvdata(dev);
	u32 bbnsm_events, bbnsm_ta;

	regmap_read(bbnsm->regmap, BBNSM_TA, &bbnsm_ta);
	rtc_time64_to_tm(bbnsm_ta, &alrm->time);

	regmap_read(bbnsm->regmap, BBNSM_EVENTS, &bbnsm_events);
	alrm->pending = (bbnsm_events & BBNSM_EVENT_TA) ? 1 : 0;

	return 0;
}

static int bbnsm_rtc_alarm_irq_enable(struct device *dev, unsigned int enable)
{
	struct bbnsm_rtc *bbnsm = dev_get_drvdata(dev);

	/* enable the alarm event */
	regmap_update_bits(bbnsm->regmap, BBNSM_CTRL, TA_EN_MSK, enable ? TA_EN : TA_DIS);
	/* enable the alarm interrupt */
	regmap_update_bits(bbnsm->regmap, BBNSM_INT_EN, TA_EN_MSK, enable ? TA_EN : TA_DIS);

	return 0;
}

static int bbnsm_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct bbnsm_rtc *bbnsm = dev_get_drvdata(dev);
	unsigned long time = rtc_tm_to_time64(&alrm->time);

	/* disable the alarm */
	regmap_update_bits(bbnsm->regmap, BBNSM_CTRL, TA_EN, TA_EN);

	/* write the seconds to TA */
	regmap_write(bbnsm->regmap, BBNSM_TA, time);

	return bbnsm_rtc_alarm_irq_enable(dev, alrm->enabled);
}

static const struct rtc_class_ops bbnsm_rtc_ops = {
	.read_time = bbnsm_rtc_read_time,
	.set_time = bbnsm_rtc_set_time,
	.read_alarm = bbnsm_rtc_read_alarm,
	.set_alarm = bbnsm_rtc_set_alarm,
	.alarm_irq_enable = bbnsm_rtc_alarm_irq_enable,
};

static irqreturn_t bbnsm_rtc_irq_handler(int irq, void *dev_id)
{
	struct device *dev = dev_id;
	struct bbnsm_rtc  *bbnsm = dev_get_drvdata(dev);
	u32 val;

	regmap_read(bbnsm->regmap, BBNSM_EVENTS, &val);
	if (val & BBNSM_EVENT_TA) {
		bbnsm_rtc_alarm_irq_enable(dev, false);
		/* clear the alarm event */
		regmap_write_bits(bbnsm->regmap, BBNSM_EVENTS, TA_EN_MSK, BBNSM_EVENT_TA);
		rtc_update_irq(bbnsm->rtc, 1, RTC_AF | RTC_IRQF);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int bbnsm_rtc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct bbnsm_rtc *bbnsm;
	int ret;

	bbnsm = devm_kzalloc(&pdev->dev, sizeof(*bbnsm), GFP_KERNEL);
	if (!bbnsm)
		return -ENOMEM;

	bbnsm->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(bbnsm->rtc))
		return PTR_ERR(bbnsm->rtc);

	bbnsm->regmap = syscon_node_to_regmap(np->parent);
	if (IS_ERR(bbnsm->regmap)) {
		dev_dbg(&pdev->dev, "bbnsm get regmap failed\n");
		return PTR_ERR(bbnsm->regmap);
	}

	bbnsm->irq = platform_get_irq(pdev, 0);
	if (bbnsm->irq < 0)
		return bbnsm->irq;

	platform_set_drvdata(pdev, bbnsm);

	/* clear all the pending events */
	regmap_write(bbnsm->regmap, BBNSM_EVENTS, 0x7A);

	device_init_wakeup(&pdev->dev, true);
	dev_pm_set_wake_irq(&pdev->dev, bbnsm->irq);

	ret = devm_request_irq(&pdev->dev, bbnsm->irq, bbnsm_rtc_irq_handler,
			       IRQF_SHARED, "rtc alarm", &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq %d: %d\n",
			bbnsm->irq, ret);
		return ret;
	}

	bbnsm->rtc->ops = &bbnsm_rtc_ops;
	bbnsm->rtc->range_max = U32_MAX;

	return devm_rtc_register_device(bbnsm->rtc);
}

static const struct of_device_id bbnsm_dt_ids[] = {
	{ .compatible = "nxp,imx93-bbnsm-rtc" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, bbnsm_dt_ids);

static struct platform_driver bbnsm_rtc_driver = {
	.driver = {
		.name = "bbnsm_rtc",
		.of_match_table = bbnsm_dt_ids,
	},
	.probe = bbnsm_rtc_probe,
};
module_platform_driver(bbnsm_rtc_driver);

MODULE_AUTHOR("Jacky Bai <ping.bai@nxp.com>");
MODULE_DESCRIPTION("NXP BBNSM RTC Driver");
MODULE_LICENSE("GPL");

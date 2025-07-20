// SPDX-License-Identifier: GPL-2.0-only
/*
 * rtc-cv1800.c: RTC driver for Sophgo cv1800 RTC
 *
 * Author: Jingbao Qiu <qiujingbao.dlmu@gmail.com>
 */

#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/rtc.h>

#define SEC_PULSE_GEN          0x1004
#define ALARM_TIME             0x1008
#define ALARM_ENABLE           0x100C
#define SET_SEC_CNTR_VAL       0x1010
#define SET_SEC_CNTR_TRIG      0x1014
#define SEC_CNTR_VAL           0x1018

/*
 * When in VDDBKUP domain, this MACRO register
 * does not power down
 */
#define MACRO_RO_T             0x14A8
#define MACRO_RG_SET_T         0x1498

#define ALARM_ENABLE_MASK      BIT(0)
#define SEL_SEC_PULSE          BIT(31)

struct cv1800_rtc_priv {
	struct rtc_device *rtc_dev;
	struct regmap *rtc_map;
	struct clk *clk;
	int irq;
};

static bool cv1800_rtc_enabled(struct device *dev)
{
	struct cv1800_rtc_priv *info = dev_get_drvdata(dev);
	u32 reg;

	regmap_read(info->rtc_map, SEC_PULSE_GEN, &reg);

	return (reg & SEL_SEC_PULSE) == 0;
}

static void cv1800_rtc_enable(struct device *dev)
{
	struct cv1800_rtc_priv *info = dev_get_drvdata(dev);

	/* Sec pulse generated internally */
	regmap_update_bits(info->rtc_map, SEC_PULSE_GEN, SEL_SEC_PULSE, 0);
}

static int cv1800_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct cv1800_rtc_priv *info = dev_get_drvdata(dev);

	regmap_write(info->rtc_map, ALARM_ENABLE, enabled);

	return 0;
}

static int cv1800_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct cv1800_rtc_priv *info = dev_get_drvdata(dev);
	unsigned long alarm_time;

	alarm_time = rtc_tm_to_time64(&alrm->time);

	cv1800_rtc_alarm_irq_enable(dev, 0);

	regmap_write(info->rtc_map, ALARM_TIME, alarm_time);

	cv1800_rtc_alarm_irq_enable(dev, alrm->enabled);

	return 0;
}

static int cv1800_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct cv1800_rtc_priv *info = dev_get_drvdata(dev);
	u32 enabled;
	u32 time;

	if (!cv1800_rtc_enabled(dev)) {
		alarm->enabled = 0;
		return 0;
	}

	regmap_read(info->rtc_map, ALARM_ENABLE, &enabled);

	alarm->enabled = enabled & ALARM_ENABLE_MASK;

	regmap_read(info->rtc_map, ALARM_TIME, &time);

	rtc_time64_to_tm(time, &alarm->time);

	return 0;
}

static int cv1800_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct cv1800_rtc_priv *info = dev_get_drvdata(dev);
	u32 sec;

	if (!cv1800_rtc_enabled(dev))
		return -EINVAL;

	regmap_read(info->rtc_map, SEC_CNTR_VAL, &sec);

	rtc_time64_to_tm(sec, tm);

	return 0;
}

static int cv1800_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct cv1800_rtc_priv *info = dev_get_drvdata(dev);
	unsigned long sec;

	sec = rtc_tm_to_time64(tm);

	regmap_write(info->rtc_map, SET_SEC_CNTR_VAL, sec);
	regmap_write(info->rtc_map, SET_SEC_CNTR_TRIG, 1);

	regmap_write(info->rtc_map, MACRO_RG_SET_T, sec);

	cv1800_rtc_enable(dev);

	return 0;
}

static irqreturn_t cv1800_rtc_irq_handler(int irq, void *dev_id)
{
	struct cv1800_rtc_priv *info = dev_id;

	rtc_update_irq(info->rtc_dev, 1, RTC_IRQF | RTC_AF);

	regmap_write(info->rtc_map, ALARM_ENABLE, 0);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops cv1800_rtc_ops = {
	.read_time = cv1800_rtc_read_time,
	.set_time = cv1800_rtc_set_time,
	.read_alarm = cv1800_rtc_read_alarm,
	.set_alarm = cv1800_rtc_set_alarm,
	.alarm_irq_enable = cv1800_rtc_alarm_irq_enable,
};

static int cv1800_rtc_probe(struct platform_device *pdev)
{
	struct cv1800_rtc_priv *rtc;
	int ret;

	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->rtc_map = device_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(rtc->rtc_map))
		return dev_err_probe(&pdev->dev, PTR_ERR(rtc->rtc_map),
				     "cannot get parent regmap\n");

	rtc->irq = platform_get_irq(pdev, 0);
	if (rtc->irq < 0)
		return rtc->irq;

	rtc->clk = devm_clk_get_enabled(pdev->dev.parent, "rtc");
	if (IS_ERR(rtc->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(rtc->clk),
				     "rtc clk not found\n");

	platform_set_drvdata(pdev, rtc);

	device_init_wakeup(&pdev->dev, 1);

	rtc->rtc_dev = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc->rtc_dev))
		return PTR_ERR(rtc->rtc_dev);

	rtc->rtc_dev->ops = &cv1800_rtc_ops;
	rtc->rtc_dev->range_max = U32_MAX;

	ret = devm_request_irq(&pdev->dev, rtc->irq, cv1800_rtc_irq_handler,
			       IRQF_TRIGGER_HIGH, "rtc alarm", rtc);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "cannot register interrupt handler\n");

	return devm_rtc_register_device(rtc->rtc_dev);
}

static const struct platform_device_id cv1800_rtc_id[] = {
	{ .name = "cv1800b-rtc" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, cv1800_rtc_id);

static struct platform_driver cv1800_rtc_driver = {
	.driver = {
		.name = "sophgo-cv1800-rtc",
	},
	.probe = cv1800_rtc_probe,
	.id_table = cv1800_rtc_id,
};

module_platform_driver(cv1800_rtc_driver);
MODULE_AUTHOR("Jingbao Qiu");
MODULE_DESCRIPTION("Sophgo cv1800 RTC Driver");
MODULE_LICENSE("GPL");

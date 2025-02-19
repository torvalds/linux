// SPDX-License-Identifier: GPL-2.0

/*
 * The RTC driver for Sunplus	SP7021
 *
 * Copyright (C) 2019 Sunplus Technology Inc., All rights reseerved.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/rtc.h>

#define RTC_REG_NAME			"rtc"

#define RTC_CTRL			0x40
#define TIMER_FREEZE_MASK_BIT		BIT(5 + 16)
#define TIMER_FREEZE			BIT(5)
#define DIS_SYS_RST_RTC_MASK_BIT	BIT(4 + 16)
#define DIS_SYS_RST_RTC			BIT(4)
#define RTC32K_MODE_RESET_MASK_BIT	BIT(3 + 16)
#define RTC32K_MODE_RESET		BIT(3)
#define ALARM_EN_OVERDUE_MASK_BIT	BIT(2 + 16)
#define ALARM_EN_OVERDUE		BIT(2)
#define ALARM_EN_PMC_MASK_BIT		BIT(1 + 16)
#define ALARM_EN_PMC			BIT(1)
#define ALARM_EN_MASK_BIT		BIT(0 + 16)
#define ALARM_EN			BIT(0)
#define RTC_TIMER_OUT			0x44
#define RTC_DIVIDER			0x48
#define RTC_TIMER_SET			0x4c
#define RTC_ALARM_SET			0x50
#define RTC_USER_DATA			0x54
#define RTC_RESET_RECORD		0x58
#define RTC_BATT_CHARGE_CTRL		0x5c
#define BAT_CHARGE_RSEL_MASK_BIT	GENMASK(3 + 16, 2 + 16)
#define BAT_CHARGE_RSEL_MASK		GENMASK(3, 2)
#define BAT_CHARGE_RSEL_2K_OHM		FIELD_PREP(BAT_CHARGE_RSEL_MASK, 0)
#define BAT_CHARGE_RSEL_250_OHM		FIELD_PREP(BAT_CHARGE_RSEL_MASK, 1)
#define BAT_CHARGE_RSEL_50_OHM		FIELD_PREP(BAT_CHARGE_RSEL_MASK, 2)
#define BAT_CHARGE_RSEL_0_OHM		FIELD_PREP(BAT_CHARGE_RSEL_MASK, 3)
#define BAT_CHARGE_DSEL_MASK_BIT	BIT(1 + 16)
#define BAT_CHARGE_DSEL_MASK		GENMASK(1, 1)
#define BAT_CHARGE_DSEL_ON		FIELD_PREP(BAT_CHARGE_DSEL_MASK, 0)
#define BAT_CHARGE_DSEL_OFF		FIELD_PREP(BAT_CHARGE_DSEL_MASK, 1)
#define BAT_CHARGE_EN_MASK_BIT		BIT(0 + 16)
#define BAT_CHARGE_EN			BIT(0)
#define RTC_TRIM_CTRL			0x60

struct sunplus_rtc {
	struct rtc_device *rtc;
	struct resource *res;
	struct clk *rtcclk;
	struct reset_control *rstc;
	void __iomem *reg_base;
	int irq;
};

static void sp_get_seconds(struct device *dev, unsigned long *secs)
{
	struct sunplus_rtc *sp_rtc = dev_get_drvdata(dev);

	*secs = (unsigned long)readl(sp_rtc->reg_base + RTC_TIMER_OUT);
}

static void sp_set_seconds(struct device *dev, unsigned long secs)
{
	struct sunplus_rtc *sp_rtc = dev_get_drvdata(dev);

	writel((u32)secs, sp_rtc->reg_base + RTC_TIMER_SET);
}

static int sp_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long secs;

	sp_get_seconds(dev, &secs);
	rtc_time64_to_tm(secs, tm);

	return 0;
}

static int sp_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long secs;

	secs = rtc_tm_to_time64(tm);
	dev_dbg(dev, "%s, secs = %lu\n", __func__, secs);
	sp_set_seconds(dev, secs);

	return 0;
}

static int sp_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct sunplus_rtc *sp_rtc = dev_get_drvdata(dev);
	unsigned long alarm_time;

	alarm_time = rtc_tm_to_time64(&alrm->time);
	dev_dbg(dev, "%s, alarm_time: %u\n", __func__, (u32)(alarm_time));
	writel((u32)alarm_time, sp_rtc->reg_base + RTC_ALARM_SET);

	return 0;
}

static int sp_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct sunplus_rtc *sp_rtc = dev_get_drvdata(dev);
	unsigned int alarm_time;

	alarm_time = readl(sp_rtc->reg_base + RTC_ALARM_SET);
	dev_dbg(dev, "%s, alarm_time: %u\n", __func__, alarm_time);

	if (alarm_time == 0)
		alrm->enabled = 0;
	else
		alrm->enabled = 1;

	rtc_time64_to_tm((unsigned long)(alarm_time), &alrm->time);

	return 0;
}

static int sp_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct sunplus_rtc *sp_rtc = dev_get_drvdata(dev);

	if (enabled)
		writel((TIMER_FREEZE_MASK_BIT | DIS_SYS_RST_RTC_MASK_BIT |
			RTC32K_MODE_RESET_MASK_BIT | ALARM_EN_OVERDUE_MASK_BIT |
			ALARM_EN_PMC_MASK_BIT | ALARM_EN_MASK_BIT) |
			(DIS_SYS_RST_RTC | ALARM_EN_OVERDUE | ALARM_EN_PMC | ALARM_EN),
			sp_rtc->reg_base + RTC_CTRL);
	else
		writel((ALARM_EN_OVERDUE_MASK_BIT | ALARM_EN_PMC_MASK_BIT | ALARM_EN_MASK_BIT) |
			0x0, sp_rtc->reg_base + RTC_CTRL);

	return 0;
}

static const struct rtc_class_ops sp_rtc_ops = {
	.read_time =		sp_rtc_read_time,
	.set_time =		sp_rtc_set_time,
	.set_alarm =		sp_rtc_set_alarm,
	.read_alarm =		sp_rtc_read_alarm,
	.alarm_irq_enable =	sp_rtc_alarm_irq_enable,
};

static irqreturn_t sp_rtc_irq_handler(int irq, void *dev_id)
{
	struct platform_device *plat_dev = dev_id;
	struct sunplus_rtc *sp_rtc = dev_get_drvdata(&plat_dev->dev);

	rtc_update_irq(sp_rtc->rtc, 1, RTC_IRQF | RTC_AF);
	dev_dbg(&plat_dev->dev, "[RTC] ALARM INT\n");

	return IRQ_HANDLED;
}

/*
 * -------------------------------------------------------------------------------------
 * bat_charge_rsel   bat_charge_dsel   bat_charge_en     Remarks
 *         x              x                 0            Disable
 *         0              0                 1            0.86mA (2K Ohm with diode)
 *         1              0                 1            1.81mA (250 Ohm with diode)
 *         2              0                 1            2.07mA (50 Ohm with diode)
 *         3              0                 1            16.0mA (0 Ohm with diode)
 *         0              1                 1            1.36mA (2K Ohm without diode)
 *         1              1                 1            3.99mA (250 Ohm without diode)
 *         2              1                 1            4.41mA (50 Ohm without diode)
 *         3              1                 1            16.0mA (0 Ohm without diode)
 * -------------------------------------------------------------------------------------
 */
static void sp_rtc_set_trickle_charger(struct device dev)
{
	struct sunplus_rtc *sp_rtc = dev_get_drvdata(&dev);
	u32 ohms, rsel;
	u32 chargeable;

	if (of_property_read_u32(dev.of_node, "trickle-resistor-ohms", &ohms) ||
	    of_property_read_u32(dev.of_node, "aux-voltage-chargeable", &chargeable)) {
		dev_warn(&dev, "battery charger disabled\n");
		return;
	}

	switch (ohms) {
	case 2000:
		rsel = BAT_CHARGE_RSEL_2K_OHM;
		break;
	case 250:
		rsel = BAT_CHARGE_RSEL_250_OHM;
		break;
	case 50:
		rsel = BAT_CHARGE_RSEL_50_OHM;
		break;
	case 0:
		rsel = BAT_CHARGE_RSEL_0_OHM;
		break;
	default:
		dev_err(&dev, "invalid charger resistor value (%d)\n", ohms);
		return;
	}

	writel(BAT_CHARGE_RSEL_MASK_BIT | rsel, sp_rtc->reg_base + RTC_BATT_CHARGE_CTRL);

	switch (chargeable) {
	case 0:
		writel(BAT_CHARGE_DSEL_MASK_BIT | BAT_CHARGE_DSEL_OFF,
		       sp_rtc->reg_base + RTC_BATT_CHARGE_CTRL);
		break;
	case 1:
		writel(BAT_CHARGE_DSEL_MASK_BIT | BAT_CHARGE_DSEL_ON,
		       sp_rtc->reg_base + RTC_BATT_CHARGE_CTRL);
		break;
	default:
		dev_err(&dev, "invalid aux-voltage-chargeable value (%d)\n", chargeable);
		return;
	}

	writel(BAT_CHARGE_EN_MASK_BIT | BAT_CHARGE_EN, sp_rtc->reg_base + RTC_BATT_CHARGE_CTRL);
}

static int sp_rtc_probe(struct platform_device *plat_dev)
{
	struct sunplus_rtc *sp_rtc;
	int ret;

	sp_rtc = devm_kzalloc(&plat_dev->dev, sizeof(*sp_rtc), GFP_KERNEL);
	if (!sp_rtc)
		return -ENOMEM;

	sp_rtc->reg_base = devm_platform_ioremap_resource_byname(plat_dev, RTC_REG_NAME);
	if (IS_ERR(sp_rtc->reg_base))
		return dev_err_probe(&plat_dev->dev, PTR_ERR(sp_rtc->reg_base),
					    "%s devm_ioremap_resource fail\n", RTC_REG_NAME);
	dev_dbg(&plat_dev->dev, "res = %pR, reg_base = %p\n",
		sp_rtc->res, sp_rtc->reg_base);

	sp_rtc->irq = platform_get_irq(plat_dev, 0);
	if (sp_rtc->irq < 0)
		return sp_rtc->irq;

	ret = devm_request_irq(&plat_dev->dev, sp_rtc->irq, sp_rtc_irq_handler,
			       IRQF_TRIGGER_RISING, "rtc irq", plat_dev);
	if (ret)
		return dev_err_probe(&plat_dev->dev, ret, "devm_request_irq failed:\n");

	sp_rtc->rtcclk = devm_clk_get(&plat_dev->dev, NULL);
	if (IS_ERR(sp_rtc->rtcclk))
		return dev_err_probe(&plat_dev->dev, PTR_ERR(sp_rtc->rtcclk),
					    "devm_clk_get fail\n");

	sp_rtc->rstc = devm_reset_control_get_exclusive(&plat_dev->dev, NULL);
	if (IS_ERR(sp_rtc->rstc))
		return dev_err_probe(&plat_dev->dev, PTR_ERR(sp_rtc->rstc),
					    "failed to retrieve reset controller\n");

	ret = clk_prepare_enable(sp_rtc->rtcclk);
	if (ret)
		goto free_clk;

	ret = reset_control_deassert(sp_rtc->rstc);
	if (ret)
		goto free_reset_assert;

	device_init_wakeup(&plat_dev->dev, true);
	dev_set_drvdata(&plat_dev->dev, sp_rtc);

	sp_rtc->rtc = devm_rtc_allocate_device(&plat_dev->dev);
	if (IS_ERR(sp_rtc->rtc)) {
		ret = PTR_ERR(sp_rtc->rtc);
		goto free_reset_assert;
	}

	sp_rtc->rtc->range_max = U32_MAX;
	sp_rtc->rtc->range_min = 0;
	sp_rtc->rtc->ops = &sp_rtc_ops;

	ret = devm_rtc_register_device(sp_rtc->rtc);
	if (ret)
		goto free_reset_assert;

	/* Setup trickle charger */
	if (plat_dev->dev.of_node)
		sp_rtc_set_trickle_charger(plat_dev->dev);

	/* Keep RTC from system reset */
	writel(DIS_SYS_RST_RTC_MASK_BIT | DIS_SYS_RST_RTC, sp_rtc->reg_base + RTC_CTRL);

	return 0;

free_reset_assert:
	reset_control_assert(sp_rtc->rstc);
free_clk:
	clk_disable_unprepare(sp_rtc->rtcclk);

	return ret;
}

static void sp_rtc_remove(struct platform_device *plat_dev)
{
	struct sunplus_rtc *sp_rtc = dev_get_drvdata(&plat_dev->dev);

	device_init_wakeup(&plat_dev->dev, false);
	reset_control_assert(sp_rtc->rstc);
	clk_disable_unprepare(sp_rtc->rtcclk);
}

#ifdef CONFIG_PM_SLEEP
static int sp_rtc_suspend(struct device *dev)
{
	struct sunplus_rtc *sp_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(sp_rtc->irq);

	return 0;
}

static int sp_rtc_resume(struct device *dev)
{
	struct sunplus_rtc *sp_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(sp_rtc->irq);

	return 0;
}
#endif

static const struct of_device_id sp_rtc_of_match[] = {
	{ .compatible = "sunplus,sp7021-rtc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sp_rtc_of_match);

static SIMPLE_DEV_PM_OPS(sp_rtc_pm_ops, sp_rtc_suspend, sp_rtc_resume);

static struct platform_driver sp_rtc_driver = {
	.probe   = sp_rtc_probe,
	.remove = sp_rtc_remove,
	.driver  = {
		.name	= "sp7021-rtc",
		.of_match_table = sp_rtc_of_match,
		.pm	= &sp_rtc_pm_ops,
	},
};
module_platform_driver(sp_rtc_driver);

MODULE_AUTHOR("Vincent Shih <vincent.sunplus@gmail.com>");
MODULE_DESCRIPTION("Sunplus RTC driver");
MODULE_LICENSE("GPL v2");


// SPDX-License-Identifier: GPL-2.0
/*
 * JZ47xx SoCs TCU Operating System Timer driver
 *
 * Copyright (C) 2016 Maarten ter Huurne <maarten@treewalker.org>
 * Copyright (C) 2020 Paul Cercueil <paul@crapouillou.net>
 */

#include <linux/clk.h>
#include <linux/clocksource.h>
#include <linux/mfd/ingenic-tcu.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/sched_clock.h>

#define TCU_OST_TCSR_MASK	0xffc0
#define TCU_OST_TCSR_CNT_MD	BIT(15)

#define TCU_OST_CHANNEL		15

/*
 * The TCU_REG_OST_CNT{L,R} from <linux/mfd/ingenic-tcu.h> are only for the
 * regmap; these are for use with the __iomem pointer.
 */
#define OST_REG_CNTL		0x4
#define OST_REG_CNTH		0x8

struct ingenic_ost_soc_info {
	bool is64bit;
};

struct ingenic_ost {
	void __iomem *regs;
	struct clk *clk;

	struct clocksource cs;
};

static struct ingenic_ost *ingenic_ost;

static u64 notrace ingenic_ost_read_cntl(void)
{
	/* Read using __iomem pointer instead of regmap to avoid locking */
	return readl(ingenic_ost->regs + OST_REG_CNTL);
}

static u64 notrace ingenic_ost_read_cnth(void)
{
	/* Read using __iomem pointer instead of regmap to avoid locking */
	return readl(ingenic_ost->regs + OST_REG_CNTH);
}

static u64 notrace ingenic_ost_clocksource_readl(struct clocksource *cs)
{
	return ingenic_ost_read_cntl();
}

static u64 notrace ingenic_ost_clocksource_readh(struct clocksource *cs)
{
	return ingenic_ost_read_cnth();
}

static int __init ingenic_ost_probe(struct platform_device *pdev)
{
	const struct ingenic_ost_soc_info *soc_info;
	struct device *dev = &pdev->dev;
	struct ingenic_ost *ost;
	struct clocksource *cs;
	struct regmap *map;
	unsigned long rate;
	int err;

	soc_info = device_get_match_data(dev);
	if (!soc_info)
		return -EINVAL;

	ost = devm_kzalloc(dev, sizeof(*ost), GFP_KERNEL);
	if (!ost)
		return -ENOMEM;

	ingenic_ost = ost;

	ost->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ost->regs))
		return PTR_ERR(ost->regs);

	map = device_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(map)) {
		dev_err(dev, "regmap not found");
		return PTR_ERR(map);
	}

	ost->clk = devm_clk_get_enabled(dev, "ost");
	if (IS_ERR(ost->clk))
		return PTR_ERR(ost->clk);

	/* Clear counter high/low registers */
	if (soc_info->is64bit)
		regmap_write(map, TCU_REG_OST_CNTL, 0);
	regmap_write(map, TCU_REG_OST_CNTH, 0);

	/* Don't reset counter at compare value. */
	regmap_update_bits(map, TCU_REG_OST_TCSR,
			   TCU_OST_TCSR_MASK, TCU_OST_TCSR_CNT_MD);

	rate = clk_get_rate(ost->clk);

	/* Enable OST TCU channel */
	regmap_write(map, TCU_REG_TESR, BIT(TCU_OST_CHANNEL));

	cs = &ost->cs;
	cs->name	= "ingenic-ost";
	cs->rating	= 320;
	cs->flags	= CLOCK_SOURCE_IS_CONTINUOUS;
	cs->mask	= CLOCKSOURCE_MASK(32);

	if (soc_info->is64bit)
		cs->read = ingenic_ost_clocksource_readl;
	else
		cs->read = ingenic_ost_clocksource_readh;

	err = clocksource_register_hz(cs, rate);
	if (err) {
		dev_err(dev, "clocksource registration failed");
		return err;
	}

	if (soc_info->is64bit)
		sched_clock_register(ingenic_ost_read_cntl, 32, rate);
	else
		sched_clock_register(ingenic_ost_read_cnth, 32, rate);

	return 0;
}

static int ingenic_ost_suspend(struct device *dev)
{
	struct ingenic_ost *ost = dev_get_drvdata(dev);

	clk_disable(ost->clk);

	return 0;
}

static int ingenic_ost_resume(struct device *dev)
{
	struct ingenic_ost *ost = dev_get_drvdata(dev);

	return clk_enable(ost->clk);
}

static const struct dev_pm_ops ingenic_ost_pm_ops = {
	/* _noirq: We want the OST clock to be gated last / ungated first */
	.suspend_noirq = ingenic_ost_suspend,
	.resume_noirq  = ingenic_ost_resume,
};

static const struct ingenic_ost_soc_info jz4725b_ost_soc_info = {
	.is64bit = false,
};

static const struct ingenic_ost_soc_info jz4760b_ost_soc_info = {
	.is64bit = true,
};

static const struct of_device_id ingenic_ost_of_match[] = {
	{ .compatible = "ingenic,jz4725b-ost", .data = &jz4725b_ost_soc_info, },
	{ .compatible = "ingenic,jz4760b-ost", .data = &jz4760b_ost_soc_info, },
	{ .compatible = "ingenic,jz4770-ost", .data = &jz4760b_ost_soc_info, },
	{ }
};

static struct platform_driver ingenic_ost_driver = {
	.driver = {
		.name = "ingenic-ost",
		.pm = pm_sleep_ptr(&ingenic_ost_pm_ops),
		.of_match_table = ingenic_ost_of_match,
	},
};
builtin_platform_driver_probe(ingenic_ost_driver, ingenic_ost_probe);

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Clk driver for NXP LPC18xx/43xx Configuration Registers (CREG)
 *
 * Copyright (C) 2015 Joachim Eastwood <manabian@gmail.com>
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define LPC18XX_CREG_CREG0			0x004
#define  LPC18XX_CREG_CREG0_EN1KHZ		BIT(0)
#define  LPC18XX_CREG_CREG0_EN32KHZ		BIT(1)
#define  LPC18XX_CREG_CREG0_RESET32KHZ		BIT(2)
#define  LPC18XX_CREG_CREG0_PD32KHZ		BIT(3)

#define to_clk_creg(_hw) container_of(_hw, struct clk_creg_data, hw)

enum {
	CREG_CLK_1KHZ,
	CREG_CLK_32KHZ,
	CREG_CLK_MAX,
};

struct clk_creg_data {
	struct clk_hw hw;
	const char *name;
	struct regmap *reg;
	unsigned int en_mask;
	const struct clk_ops *ops;
};

#define CREG_CLK(_name, _emask, _ops)		\
{						\
	.name = _name,				\
	.en_mask = LPC18XX_CREG_CREG0_##_emask,	\
	.ops = &_ops,				\
}

static int clk_creg_32k_prepare(struct clk_hw *hw)
{
	struct clk_creg_data *creg = to_clk_creg(hw);
	int ret;

	ret = regmap_update_bits(creg->reg, LPC18XX_CREG_CREG0,
				 LPC18XX_CREG_CREG0_PD32KHZ |
				 LPC18XX_CREG_CREG0_RESET32KHZ, 0);

	/*
	 * Powering up the 32k oscillator takes a long while
	 * and sadly there aren't any status bit to poll.
	 */
	msleep(2500);

	return ret;
}

static void clk_creg_32k_unprepare(struct clk_hw *hw)
{
	struct clk_creg_data *creg = to_clk_creg(hw);

	regmap_update_bits(creg->reg, LPC18XX_CREG_CREG0,
			   LPC18XX_CREG_CREG0_PD32KHZ,
			   LPC18XX_CREG_CREG0_PD32KHZ);
}

static int clk_creg_32k_is_prepared(struct clk_hw *hw)
{
	struct clk_creg_data *creg = to_clk_creg(hw);
	u32 reg;

	regmap_read(creg->reg, LPC18XX_CREG_CREG0, &reg);

	return !(reg & LPC18XX_CREG_CREG0_PD32KHZ) &&
	       !(reg & LPC18XX_CREG_CREG0_RESET32KHZ);
}

static unsigned long clk_creg_1k_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	return parent_rate / 32;
}

static int clk_creg_enable(struct clk_hw *hw)
{
	struct clk_creg_data *creg = to_clk_creg(hw);

	return regmap_update_bits(creg->reg, LPC18XX_CREG_CREG0,
				  creg->en_mask, creg->en_mask);
}

static void clk_creg_disable(struct clk_hw *hw)
{
	struct clk_creg_data *creg = to_clk_creg(hw);

	regmap_update_bits(creg->reg, LPC18XX_CREG_CREG0,
			   creg->en_mask, 0);
}

static int clk_creg_is_enabled(struct clk_hw *hw)
{
	struct clk_creg_data *creg = to_clk_creg(hw);
	u32 reg;

	regmap_read(creg->reg, LPC18XX_CREG_CREG0, &reg);

	return !!(reg & creg->en_mask);
}

static const struct clk_ops clk_creg_32k = {
	.enable		= clk_creg_enable,
	.disable	= clk_creg_disable,
	.is_enabled	= clk_creg_is_enabled,
	.prepare	= clk_creg_32k_prepare,
	.unprepare	= clk_creg_32k_unprepare,
	.is_prepared	= clk_creg_32k_is_prepared,
};

static const struct clk_ops clk_creg_1k = {
	.enable		= clk_creg_enable,
	.disable	= clk_creg_disable,
	.is_enabled	= clk_creg_is_enabled,
	.recalc_rate	= clk_creg_1k_recalc_rate,
};

static struct clk_creg_data clk_creg_clocks[] = {
	[CREG_CLK_1KHZ]  = CREG_CLK("1khz_clk",  EN1KHZ,  clk_creg_1k),
	[CREG_CLK_32KHZ] = CREG_CLK("32khz_clk", EN32KHZ, clk_creg_32k),
};

static struct clk *clk_register_creg_clk(struct device *dev,
					 struct clk_creg_data *creg_clk,
					 const char **parent_name,
					 struct regmap *syscon)
{
	struct clk_init_data init;

	init.ops = creg_clk->ops;
	init.name = creg_clk->name;
	init.parent_names = parent_name;
	init.num_parents = 1;
	init.flags = 0;

	creg_clk->reg = syscon;
	creg_clk->hw.init = &init;

	if (dev)
		return devm_clk_register(dev, &creg_clk->hw);

	return clk_register(NULL, &creg_clk->hw);
}

static struct clk *clk_creg_early[CREG_CLK_MAX];
static struct clk_onecell_data clk_creg_early_data = {
	.clks = clk_creg_early,
	.clk_num = CREG_CLK_MAX,
};

static void __init lpc18xx_creg_clk_init(struct device_node *np)
{
	const char *clk_32khz_parent;
	struct regmap *syscon;

	syscon = syscon_node_to_regmap(np->parent);
	if (IS_ERR(syscon)) {
		pr_err("%s: syscon lookup failed\n", __func__);
		return;
	}

	clk_32khz_parent = of_clk_get_parent_name(np, 0);

	clk_creg_early[CREG_CLK_32KHZ] =
		clk_register_creg_clk(NULL, &clk_creg_clocks[CREG_CLK_32KHZ],
				      &clk_32khz_parent, syscon);
	clk_creg_early[CREG_CLK_1KHZ] = ERR_PTR(-EPROBE_DEFER);

	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_creg_early_data);
}
CLK_OF_DECLARE_DRIVER(lpc18xx_creg_clk, "nxp,lpc1850-creg-clk",
		      lpc18xx_creg_clk_init);

static struct clk *clk_creg[CREG_CLK_MAX];
static struct clk_onecell_data clk_creg_data = {
	.clks = clk_creg,
	.clk_num = CREG_CLK_MAX,
};

static int lpc18xx_creg_clk_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct regmap *syscon;

	syscon = syscon_node_to_regmap(np->parent);
	if (IS_ERR(syscon)) {
		dev_err(&pdev->dev, "syscon lookup failed\n");
		return PTR_ERR(syscon);
	}

	clk_creg[CREG_CLK_32KHZ] = clk_creg_early[CREG_CLK_32KHZ];
	clk_creg[CREG_CLK_1KHZ] =
		clk_register_creg_clk(NULL, &clk_creg_clocks[CREG_CLK_1KHZ],
				      &clk_creg_clocks[CREG_CLK_32KHZ].name,
				      syscon);

	return of_clk_add_provider(np, of_clk_src_onecell_get, &clk_creg_data);
}

static const struct of_device_id lpc18xx_creg_clk_of_match[] = {
	{ .compatible = "nxp,lpc1850-creg-clk" },
	{},
};

static struct platform_driver lpc18xx_creg_clk_driver = {
	.probe = lpc18xx_creg_clk_probe,
	.driver = {
		.name = "lpc18xx-creg-clk",
		.of_match_table = lpc18xx_creg_clk_of_match,
	},
};
builtin_platform_driver(lpc18xx_creg_clk_driver);

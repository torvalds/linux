// SPDX-License-Identifier: GPL-2.0+
/*
 * Marvell Armada 37xx SoC Time Base Generator clocks
 *
 * Copyright (C) 2016 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define NUM_TBG	    4

#define TBG_CTRL0		0x4
#define TBG_CTRL1		0x8
#define TBG_CTRL7		0x20
#define TBG_CTRL8		0x30

#define TBG_DIV_MASK		0x1FF

#define TBG_A_REFDIV		0
#define TBG_B_REFDIV		16

#define TBG_A_FBDIV		2
#define TBG_B_FBDIV		18

#define TBG_A_VCODIV_SE		0
#define TBG_B_VCODIV_SE		16

#define TBG_A_VCODIV_DIFF	1
#define TBG_B_VCODIV_DIFF	17

struct tbg_def {
	char *name;
	u32 refdiv_offset;
	u32 fbdiv_offset;
	u32 vcodiv_reg;
	u32 vcodiv_offset;
};

static const struct tbg_def tbg[NUM_TBG] = {
	{"TBG-A-P", TBG_A_REFDIV, TBG_A_FBDIV, TBG_CTRL8, TBG_A_VCODIV_DIFF},
	{"TBG-B-P", TBG_B_REFDIV, TBG_B_FBDIV, TBG_CTRL8, TBG_B_VCODIV_DIFF},
	{"TBG-A-S", TBG_A_REFDIV, TBG_A_FBDIV, TBG_CTRL1, TBG_A_VCODIV_SE},
	{"TBG-B-S", TBG_B_REFDIV, TBG_B_FBDIV, TBG_CTRL1, TBG_B_VCODIV_SE},
};

static unsigned int tbg_get_mult(void __iomem *reg, const struct tbg_def *ptbg)
{
	u32 val;

	val = readl(reg + TBG_CTRL0);

	return ((val >> ptbg->fbdiv_offset) & TBG_DIV_MASK) << 2;
}

static unsigned int tbg_get_div(void __iomem *reg, const struct tbg_def *ptbg)
{
	u32 val;
	unsigned int div;

	val = readl(reg + TBG_CTRL7);

	div = (val >> ptbg->refdiv_offset) & TBG_DIV_MASK;
	if (div == 0)
		div = 1;
	val = readl(reg + ptbg->vcodiv_reg);

	div *= 1 << ((val >>  ptbg->vcodiv_offset) & TBG_DIV_MASK);

	return div;
}


static int armada_3700_tbg_clock_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct clk_hw_onecell_data *hw_tbg_data;
	struct device *dev = &pdev->dev;
	const char *parent_name;
	struct resource *res;
	struct clk *parent;
	void __iomem *reg;
	int i;

	hw_tbg_data = devm_kzalloc(&pdev->dev,
				   struct_size(hw_tbg_data, hws, NUM_TBG),
				   GFP_KERNEL);
	if (!hw_tbg_data)
		return -ENOMEM;
	hw_tbg_data->num = NUM_TBG;
	platform_set_drvdata(pdev, hw_tbg_data);

	parent = clk_get(dev, NULL);
	if (IS_ERR(parent)) {
		dev_err(dev, "Could get the clock parent\n");
		return -EINVAL;
	}
	parent_name = __clk_get_name(parent);
	clk_put(parent);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg = devm_ioremap_resource(dev, res);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	for (i = 0; i < NUM_TBG; i++) {
		const char *name;
		unsigned int mult, div;

		name = tbg[i].name;
		mult = tbg_get_mult(reg, &tbg[i]);
		div = tbg_get_div(reg, &tbg[i]);
		hw_tbg_data->hws[i] = clk_hw_register_fixed_factor(NULL, name,
						parent_name, 0, mult, div);
		if (IS_ERR(hw_tbg_data->hws[i]))
			dev_err(dev, "Can't register TBG clock %s\n", name);
	}

	return of_clk_add_hw_provider(np, of_clk_hw_onecell_get, hw_tbg_data);
}

static int armada_3700_tbg_clock_remove(struct platform_device *pdev)
{
	int i;
	struct clk_hw_onecell_data *hw_tbg_data = platform_get_drvdata(pdev);

	of_clk_del_provider(pdev->dev.of_node);
	for (i = 0; i < hw_tbg_data->num; i++)
		clk_hw_unregister_fixed_factor(hw_tbg_data->hws[i]);

	return 0;
}

static const struct of_device_id armada_3700_tbg_clock_of_match[] = {
	{ .compatible = "marvell,armada-3700-tbg-clock", },
	{ }
};

static struct platform_driver armada_3700_tbg_clock_driver = {
	.probe = armada_3700_tbg_clock_probe,
	.remove = armada_3700_tbg_clock_remove,
	.driver		= {
		.name	= "marvell-armada-3700-tbg-clock",
		.of_match_table = armada_3700_tbg_clock_of_match,
	},
};

builtin_platform_driver(armada_3700_tbg_clock_driver);

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Daniel Golle <daniel@makrotopia.org>
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "clk-mtk.h"
#include "clk-gate.h"
#include <dt-bindings/clock/mediatek,mt7988-clk.h>

/* Register to control USXGMII XFI PLL analog */
#define XFI_PLL_ANA_GLB8		0x108
#define RG_XFI_PLL_ANA_SWWA		0x02283248

static const struct mtk_gate_regs xfipll_cg_regs = {
	.set_ofs = 0x8,
	.clr_ofs = 0x8,
	.sta_ofs = 0x8,
};

#define GATE_XFIPLL(_id, _name, _parent, _shift)		\
	{							\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.regs = &xfipll_cg_regs,			\
		.shift = _shift,				\
		.ops = &mtk_clk_gate_ops_no_setclr_inv,		\
	}

static const struct mtk_fixed_factor xfipll_divs[] = {
	FACTOR(CLK_XFIPLL_PLL, "xfipll_pll", "top_xtal", 125, 32),
};

static const struct mtk_gate xfipll_clks[] = {
	GATE_XFIPLL(CLK_XFIPLL_PLL_EN, "xfipll_pll_en", "xfipll_pll", 31),
};

static const struct mtk_clk_desc xfipll_desc = {
	.clks = xfipll_clks,
	.num_clks = ARRAY_SIZE(xfipll_clks),
	.factor_clks = xfipll_divs,
	.num_factor_clks = ARRAY_SIZE(xfipll_divs),
};

static int clk_mt7988_xfipll_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	void __iomem *base = of_iomap(node, 0);

	if (!base)
		return -ENOMEM;

	/* Apply software workaround for USXGMII PLL TCL issue */
	writel(RG_XFI_PLL_ANA_SWWA, base + XFI_PLL_ANA_GLB8);
	iounmap(base);

	return mtk_clk_simple_probe(pdev);
};

static const struct of_device_id of_match_clk_mt7988_xfipll[] = {
	{ .compatible = "mediatek,mt7988-xfi-pll", .data = &xfipll_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt7988_xfipll);

static struct platform_driver clk_mt7988_xfipll_drv = {
	.driver = {
		.name = "clk-mt7988-xfipll",
		.of_match_table = of_match_clk_mt7988_xfipll,
	},
	.probe = clk_mt7988_xfipll_probe,
	.remove_new = mtk_clk_simple_remove,
};
module_platform_driver(clk_mt7988_xfipll_drv);

MODULE_DESCRIPTION("MediaTek MT7988 XFI PLL clock driver");
MODULE_LICENSE("GPL");

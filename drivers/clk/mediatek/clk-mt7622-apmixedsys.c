// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 MediaTek Inc.
 * Copyright (c) 2023 Collabora, Ltd.
 *               AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <dt-bindings/clock/mt7622-clk.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"
#include "clk-pll.h"

#define MT7622_PLL_FMAX		(2500UL * MHZ)
#define CON0_MT7622_RST_BAR	BIT(27)

#define PLL_xtal(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,\
			_pd_reg, _pd_shift, _tuner_reg, _pcw_reg,	\
			_pcw_shift, _div_table, _parent_name) {		\
		.id = _id,						\
		.name = _name,						\
		.reg = _reg,						\
		.pwr_reg = _pwr_reg,					\
		.en_mask = _en_mask,					\
		.flags = _flags,					\
		.rst_bar_mask = CON0_MT7622_RST_BAR,			\
		.fmax = MT7622_PLL_FMAX,				\
		.pcwbits = _pcwbits,					\
		.pd_reg = _pd_reg,					\
		.pd_shift = _pd_shift,					\
		.tuner_reg = _tuner_reg,				\
		.pcw_reg = _pcw_reg,					\
		.pcw_shift = _pcw_shift,				\
		.div_table = _div_table,				\
		.parent_name = _parent_name,				\
	}

#define PLL(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,	\
			_pd_reg, _pd_shift, _tuner_reg, _pcw_reg,	\
			_pcw_shift)					\
	PLL_xtal(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,\
		 _pd_reg, _pd_shift, _tuner_reg, _pcw_reg, _pcw_shift,  \
		 NULL, "clkxtal")

static const struct mtk_gate_regs apmixed_cg_regs = {
	.set_ofs = 0x8,
	.clr_ofs = 0x8,
	.sta_ofs = 0x8,
};

#define GATE_APMIXED_AO(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &apmixed_cg_regs, _shift,	\
		 &mtk_clk_gate_ops_no_setclr_inv, CLK_IS_CRITICAL)

static const struct mtk_pll_data plls[] = {
	PLL(CLK_APMIXED_ARMPLL, "armpll", 0x0200, 0x020C, 0,
	    PLL_AO, 21, 0x0204, 24, 0, 0x0204, 0),
	PLL(CLK_APMIXED_MAINPLL, "mainpll", 0x0210, 0x021C, 0,
	    HAVE_RST_BAR, 21, 0x0214, 24, 0, 0x0214, 0),
	PLL(CLK_APMIXED_UNIV2PLL, "univ2pll", 0x0220, 0x022C, 0,
	    HAVE_RST_BAR, 7, 0x0224, 24, 0, 0x0224, 14),
	PLL(CLK_APMIXED_ETH1PLL, "eth1pll", 0x0300, 0x0310, 0,
	    0, 21, 0x0300, 1, 0, 0x0304, 0),
	PLL(CLK_APMIXED_ETH2PLL, "eth2pll", 0x0314, 0x0320, 0,
	    0, 21, 0x0314, 1, 0, 0x0318, 0),
	PLL(CLK_APMIXED_AUD1PLL, "aud1pll", 0x0324, 0x0330, 0,
	    0, 31, 0x0324, 1, 0, 0x0328, 0),
	PLL(CLK_APMIXED_AUD2PLL, "aud2pll", 0x0334, 0x0340, 0,
	    0, 31, 0x0334, 1, 0, 0x0338, 0),
	PLL(CLK_APMIXED_TRGPLL, "trgpll", 0x0344, 0x0354, 0,
	    0, 21, 0x0344, 1, 0, 0x0348, 0),
	PLL(CLK_APMIXED_SGMIPLL, "sgmipll", 0x0358, 0x0368, 0,
	    0, 21, 0x0358, 1, 0, 0x035C, 0),
};

static const struct mtk_gate apmixed_clks[] = {
	GATE_APMIXED_AO(CLK_APMIXED_MAIN_CORE_EN, "main_core_en", "mainpll", 5),
};

static int clk_mt7622_apmixed_probe(struct platform_device *pdev)
{
	void __iomem *base;
	struct clk_hw_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk_data = mtk_devm_alloc_clk_data(dev, CLK_APMIXED_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	ret = mtk_clk_register_plls(node, plls, ARRAY_SIZE(plls), clk_data);
	if (ret)
		return ret;

	ret = mtk_clk_register_gates(&pdev->dev, node, apmixed_clks,
				     ARRAY_SIZE(apmixed_clks), clk_data);
	if (ret)
		goto unregister_plls;

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (ret)
		goto unregister_gates;

	return 0;

unregister_gates:
	mtk_clk_unregister_gates(apmixed_clks, ARRAY_SIZE(apmixed_clks), clk_data);
unregister_plls:
	mtk_clk_unregister_plls(plls, ARRAY_SIZE(plls), clk_data);

	return ret;
}

static void clk_mt7622_apmixed_remove(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw_onecell_data *clk_data = platform_get_drvdata(pdev);

	of_clk_del_provider(node);
	mtk_clk_unregister_gates(apmixed_clks, ARRAY_SIZE(apmixed_clks), clk_data);
	mtk_clk_unregister_plls(plls, ARRAY_SIZE(plls), clk_data);
}

static const struct of_device_id of_match_clk_mt7622_apmixed[] = {
	{ .compatible = "mediatek,mt7622-apmixedsys" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt7622_apmixed);

static struct platform_driver clk_mt7622_apmixed_drv = {
	.probe = clk_mt7622_apmixed_probe,
	.remove = clk_mt7622_apmixed_remove,
	.driver = {
		.name = "clk-mt7622-apmixed",
		.of_match_table = of_match_clk_mt7622_apmixed,
	},
};
module_platform_driver(clk_mt7622_apmixed_drv)

MODULE_DESCRIPTION("MediaTek MT7622 apmixedsys clocks driver");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include "clk-mtk.h"
#include "clk-pll.h"

#include <dt-bindings/clock/mt8195-clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#define MT8195_PLL_FMAX		(3800UL * MHZ)
#define MT8195_PLL_FMIN		(1500UL * MHZ)
#define MT8195_INTEGER_BITS	(8)
#define MT8195_PCW_BITS		(22)
#define MT8195_POSDIV_SHIFT	(24)
#define MT8195_PLL_EN_BIT	(0)
#define MT8195_PCW_SHIFT	(0)

/*
 * The "en_reg" and "pcw_chg_reg" fields are standard offset register compared
 * with "reg" field, so set zero to imply it.
 * No tuner control in apu pll, so set "tuner_XXX" as zero to imply it.
 * No rst or post divider enable in apu pll, so set "rst_bar_mask" and "en_mask"
 * as zero to imply it.
 */
#define PLL(_id, _name, _reg, _pwr_reg, _pd_reg, _pcw_reg) {		\
		.id = _id,						\
		.name = _name,						\
		.reg = _reg,						\
		.pwr_reg = _pwr_reg,					\
		.en_mask = 0,						\
		.flags = 0,						\
		.rst_bar_mask = 0,					\
		.fmax = MT8195_PLL_FMAX,				\
		.fmin = MT8195_PLL_FMIN,				\
		.pcwbits = MT8195_PCW_BITS,				\
		.pcwibits = MT8195_INTEGER_BITS,			\
		.pd_reg = _pd_reg,					\
		.pd_shift = MT8195_POSDIV_SHIFT,			\
		.tuner_reg = 0,						\
		.tuner_en_reg = 0,					\
		.tuner_en_bit = 0,					\
		.pcw_reg = _pcw_reg,					\
		.pcw_shift = MT8195_PCW_SHIFT,				\
		.pcw_chg_reg = 0,					\
		.en_reg = 0,						\
		.pll_en_bit = MT8195_PLL_EN_BIT,			\
	}

static const struct mtk_pll_data apusys_plls[] = {
	PLL(CLK_APUSYS_PLL_APUPLL, "apusys_pll_apupll", 0x008, 0x014, 0x00c, 0x00c),
	PLL(CLK_APUSYS_PLL_NPUPLL, "apusys_pll_npupll", 0x018, 0x024, 0x01c, 0x01c),
	PLL(CLK_APUSYS_PLL_APUPLL1, "apusys_pll_apupll1", 0x028, 0x034, 0x02c, 0x02c),
	PLL(CLK_APUSYS_PLL_APUPLL2, "apusys_pll_apupll2", 0x038, 0x044, 0x03c, 0x03c),
};

static int clk_mt8195_apusys_pll_probe(struct platform_device *pdev)
{
	struct clk_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	int r;

	clk_data = mtk_alloc_clk_data(CLK_APUSYS_PLL_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	r = mtk_clk_register_plls(node, apusys_plls, ARRAY_SIZE(apusys_plls), clk_data);
	if (r)
		goto free_apusys_pll_data;

	r = of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
	if (r)
		goto unregister_plls;

	platform_set_drvdata(pdev, clk_data);

	return r;

unregister_plls:
	mtk_clk_unregister_plls(apusys_plls, ARRAY_SIZE(apusys_plls), clk_data);
free_apusys_pll_data:
	mtk_free_clk_data(clk_data);
	return r;
}

static int clk_mt8195_apusys_pll_remove(struct platform_device *pdev)
{
	struct clk_onecell_data *clk_data = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;

	of_clk_del_provider(node);
	mtk_clk_unregister_plls(apusys_plls, ARRAY_SIZE(apusys_plls), clk_data);
	mtk_free_clk_data(clk_data);

	return 0;
}

static const struct of_device_id of_match_clk_mt8195_apusys_pll[] = {
	{ .compatible = "mediatek,mt8195-apusys_pll", },
	{}
};

static struct platform_driver clk_mt8195_apusys_pll_drv = {
	.probe = clk_mt8195_apusys_pll_probe,
	.remove = clk_mt8195_apusys_pll_remove,
	.driver = {
		.name = "clk-mt8195-apusys_pll",
		.of_match_table = of_match_clk_mt8195_apusys_pll,
	},
};
builtin_platform_driver(clk_mt8195_apusys_pll_drv);

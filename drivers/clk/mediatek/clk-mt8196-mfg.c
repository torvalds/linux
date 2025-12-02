// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 MediaTek Inc.
 *                    Guangjie Song <guangjie.song@mediatek.com>
 * Copyright (c) 2025 Collabora Ltd.
 *                    Laura Nao <laura.nao@collabora.com>
 */
#include <dt-bindings/clock/mediatek,mt8196-clock.h>

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-pll.h"

#define MFGPLL_CON0	0x008
#define MFGPLL_CON1	0x00c
#define MFGPLL_CON2	0x010
#define MFGPLL_CON3	0x014
#define MFGPLL_SC0_CON0	0x008
#define MFGPLL_SC0_CON1	0x00c
#define MFGPLL_SC0_CON2	0x010
#define MFGPLL_SC0_CON3	0x014
#define MFGPLL_SC1_CON0	0x008
#define MFGPLL_SC1_CON1	0x00c
#define MFGPLL_SC1_CON2	0x010
#define MFGPLL_SC1_CON3	0x014

#define MT8196_PLL_FMAX		(3800UL * MHZ)
#define MT8196_PLL_FMIN		(1500UL * MHZ)
#define MT8196_INTEGER_BITS	8

#define PLL(_id, _name, _reg, _en_reg, _en_mask, _pll_en_bit,	\
	    _flags, _rst_bar_mask,				\
	    _pd_reg, _pd_shift, _tuner_reg,			\
	    _tuner_en_reg, _tuner_en_bit,			\
	    _pcw_reg, _pcw_shift, _pcwbits) {			\
		.id = _id,					\
		.name = _name,					\
		.reg = _reg,					\
		.en_reg = _en_reg,				\
		.en_mask = _en_mask,				\
		.pll_en_bit = _pll_en_bit,			\
		.flags = _flags,				\
		.rst_bar_mask = _rst_bar_mask,			\
		.fmax = MT8196_PLL_FMAX,			\
		.fmin = MT8196_PLL_FMIN,			\
		.pd_reg = _pd_reg,				\
		.pd_shift = _pd_shift,				\
		.tuner_reg = _tuner_reg,			\
		.tuner_en_reg = _tuner_en_reg,			\
		.tuner_en_bit = _tuner_en_bit,			\
		.pcw_reg = _pcw_reg,				\
		.pcw_shift = _pcw_shift,			\
		.pcwbits = _pcwbits,				\
		.pcwibits = MT8196_INTEGER_BITS,		\
	}

static const struct mtk_pll_data mfg_ao_plls[] = {
	PLL(CLK_MFG_AO_MFGPLL, "mfgpll", MFGPLL_CON0, MFGPLL_CON0, 0, 0, 0,
	    BIT(0), MFGPLL_CON1, 24, 0, 0, 0,
	    MFGPLL_CON1, 0, 22),
};

static const struct mtk_pll_data mfgsc0_ao_plls[] = {
	PLL(CLK_MFGSC0_AO_MFGPLL_SC0, "mfgpll-sc0", MFGPLL_SC0_CON0,
	    MFGPLL_SC0_CON0, 0, 0, 0, BIT(0), MFGPLL_SC0_CON1, 24, 0, 0, 0,
	    MFGPLL_SC0_CON1, 0, 22),
};

static const struct mtk_pll_data mfgsc1_ao_plls[] = {
	PLL(CLK_MFGSC1_AO_MFGPLL_SC1, "mfgpll-sc1", MFGPLL_SC1_CON0,
	    MFGPLL_SC1_CON0, 0, 0, 0, BIT(0), MFGPLL_SC1_CON1, 24, 0, 0, 0,
	    MFGPLL_SC1_CON1, 0, 22),
};

static const struct of_device_id of_match_clk_mt8196_mfg[] = {
	{ .compatible = "mediatek,mt8196-mfgpll-pll-ctrl",
	  .data = &mfg_ao_plls },
	{ .compatible = "mediatek,mt8196-mfgpll-sc0-pll-ctrl",
	  .data = &mfgsc0_ao_plls },
	{ .compatible = "mediatek,mt8196-mfgpll-sc1-pll-ctrl",
	  .data = &mfgsc1_ao_plls },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8196_mfg);

static int clk_mt8196_mfg_probe(struct platform_device *pdev)
{
	const struct mtk_pll_data *plls;
	struct clk_hw_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	const int num_plls = 1;
	int r;

	plls = of_device_get_match_data(&pdev->dev);
	if (!plls)
		return -EINVAL;

	clk_data = mtk_alloc_clk_data(num_plls);
	if (!clk_data)
		return -ENOMEM;

	r = mtk_clk_register_plls(node, plls, num_plls, clk_data);
	if (r)
		goto free_clk_data;

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		goto unregister_plls;

	platform_set_drvdata(pdev, clk_data);

	return r;

unregister_plls:
	mtk_clk_unregister_plls(plls, num_plls, clk_data);
free_clk_data:
	mtk_free_clk_data(clk_data);

	return r;
}

static void clk_mt8196_mfg_remove(struct platform_device *pdev)
{
	const struct mtk_pll_data *plls = of_device_get_match_data(&pdev->dev);
	struct clk_hw_onecell_data *clk_data = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;

	of_clk_del_provider(node);
	mtk_clk_unregister_plls(plls, 1, clk_data);
	mtk_free_clk_data(clk_data);
}

static struct platform_driver clk_mt8196_mfg_drv = {
	.probe = clk_mt8196_mfg_probe,
	.remove = clk_mt8196_mfg_remove,
	.driver = {
		.name = "clk-mt8196-mfg",
		.of_match_table = of_match_clk_mt8196_mfg,
	},
};
module_platform_driver(clk_mt8196_mfg_drv);

MODULE_DESCRIPTION("MediaTek MT8196 GPU mfg clocks driver");
MODULE_LICENSE("GPL");

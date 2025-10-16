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

#define ARMPLL_LL_CON0	0x008
#define ARMPLL_LL_CON1	0x00c
#define ARMPLL_LL_CON2	0x010
#define ARMPLL_LL_CON3	0x014
#define ARMPLL_BL_CON0	0x008
#define ARMPLL_BL_CON1	0x00c
#define ARMPLL_BL_CON2	0x010
#define ARMPLL_BL_CON3	0x014
#define ARMPLL_B_CON0	0x008
#define ARMPLL_B_CON1	0x00c
#define ARMPLL_B_CON2	0x010
#define ARMPLL_B_CON3	0x014
#define CCIPLL_CON0	0x008
#define CCIPLL_CON1	0x00c
#define CCIPLL_CON2	0x010
#define CCIPLL_CON3	0x014
#define PTPPLL_CON0	0x008
#define PTPPLL_CON1	0x00c
#define PTPPLL_CON2	0x010
#define PTPPLL_CON3	0x014

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

static const struct mtk_pll_data cpu_bl_plls[] = {
	PLL(CLK_CPBL_ARMPLL_BL, "armpll-bl", ARMPLL_BL_CON0, ARMPLL_BL_CON0, 0,
	    0, PLL_AO, BIT(0), ARMPLL_BL_CON1, 24, 0, 0, 0, ARMPLL_BL_CON1, 0, 22),
};

static const struct mtk_pll_data cpu_b_plls[] = {
	PLL(CLK_CPB_ARMPLL_B, "armpll-b", ARMPLL_B_CON0, ARMPLL_B_CON0, 0, 0,
	    PLL_AO, BIT(0), ARMPLL_B_CON1, 24, 0, 0, 0, ARMPLL_B_CON1, 0, 22),
};

static const struct mtk_pll_data cpu_ll_plls[] = {
	PLL(CLK_CPLL_ARMPLL_LL, "armpll-ll", ARMPLL_LL_CON0, ARMPLL_LL_CON0, 0,
	    0, PLL_AO, BIT(0), ARMPLL_LL_CON1, 24, 0, 0, 0, ARMPLL_LL_CON1, 0, 22),
};

static const struct mtk_pll_data cci_plls[] = {
	PLL(CLK_CCIPLL, "ccipll", CCIPLL_CON0, CCIPLL_CON0, 0, 0, PLL_AO,
	    BIT(0), CCIPLL_CON1, 24, 0, 0, 0, CCIPLL_CON1, 0, 22),
};

static const struct mtk_pll_data ptp_plls[] = {
	PLL(CLK_PTPPLL, "ptppll", PTPPLL_CON0, PTPPLL_CON0, 0, 0, PLL_AO,
	    BIT(0), PTPPLL_CON1, 24, 0, 0, 0, PTPPLL_CON1, 0, 22),
};

static const struct of_device_id of_match_clk_mt8196_mcu[] = {
	{ .compatible = "mediatek,mt8196-armpll-bl-pll-ctrl",
	  .data = &cpu_bl_plls },
	{ .compatible = "mediatek,mt8196-armpll-b-pll-ctrl",
	  .data = &cpu_b_plls },
	{ .compatible = "mediatek,mt8196-armpll-ll-pll-ctrl",
	  .data = &cpu_ll_plls },
	{ .compatible = "mediatek,mt8196-ccipll-pll-ctrl", .data = &cci_plls },
	{ .compatible = "mediatek,mt8196-ptppll-pll-ctrl", .data = &ptp_plls },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8196_mcu);

static int clk_mt8196_mcu_probe(struct platform_device *pdev)
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

static void clk_mt8196_mcu_remove(struct platform_device *pdev)
{
	const struct mtk_pll_data *plls = of_device_get_match_data(&pdev->dev);
	struct clk_hw_onecell_data *clk_data = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;

	of_clk_del_provider(node);
	mtk_clk_unregister_plls(plls, 1, clk_data);
	mtk_free_clk_data(clk_data);
}

static struct platform_driver clk_mt8196_mcu_drv = {
	.probe = clk_mt8196_mcu_probe,
	.remove = clk_mt8196_mcu_remove,
	.driver = {
		.name = "clk-mt8196-mcu",
		.of_match_table = of_match_clk_mt8196_mcu,
	},
};
module_platform_driver(clk_mt8196_mcu_drv);

MODULE_DESCRIPTION("MediaTek MT8196 mcusys clocks driver");
MODULE_LICENSE("GPL");

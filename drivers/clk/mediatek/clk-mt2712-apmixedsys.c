// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 MediaTek Inc.
 *                    Weiyi Lu <weiyi.lu@mediatek.com>
 * Copyright (c) 2023 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "clk-pll.h"
#include "clk-mtk.h"

#include <dt-bindings/clock/mt2712-clk.h>

#define MT2712_PLL_FMAX		(3000UL * MHZ)

#define CON0_MT2712_RST_BAR	BIT(24)

#define PLL_B(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,	\
			_pd_reg, _pd_shift, _tuner_reg, _tuner_en_reg,	\
			_tuner_en_bit, _pcw_reg, _pcw_shift,		\
			_div_table) {					\
		.id = _id,						\
		.name = _name,						\
		.reg = _reg,						\
		.pwr_reg = _pwr_reg,					\
		.en_mask = _en_mask,					\
		.flags = _flags,					\
		.rst_bar_mask = CON0_MT2712_RST_BAR,			\
		.fmax = MT2712_PLL_FMAX,				\
		.pcwbits = _pcwbits,					\
		.pd_reg = _pd_reg,					\
		.pd_shift = _pd_shift,					\
		.tuner_reg = _tuner_reg,				\
		.tuner_en_reg = _tuner_en_reg,				\
		.tuner_en_bit = _tuner_en_bit,				\
		.pcw_reg = _pcw_reg,					\
		.pcw_shift = _pcw_shift,				\
		.div_table = _div_table,				\
	}

#define PLL(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,	\
			_pd_reg, _pd_shift, _tuner_reg, _tuner_en_reg,	\
			_tuner_en_bit, _pcw_reg, _pcw_shift)		\
		PLL_B(_id, _name, _reg, _pwr_reg, _en_mask, _flags,	\
			_pcwbits, _pd_reg, _pd_shift, _tuner_reg,	\
			_tuner_en_reg, _tuner_en_bit, _pcw_reg,		\
			_pcw_shift, NULL)

static const struct mtk_pll_div_table armca35pll_div_table[] = {
	{ .div = 0, .freq = MT2712_PLL_FMAX },
	{ .div = 1, .freq = 1202500000 },
	{ .div = 2, .freq = 500500000 },
	{ .div = 3, .freq = 315250000 },
	{ .div = 4, .freq = 157625000 },
	{ /* sentinel */ }
};

static const struct mtk_pll_div_table armca72pll_div_table[] = {
	{ .div = 0, .freq = MT2712_PLL_FMAX },
	{ .div = 1, .freq = 994500000 },
	{ .div = 2, .freq = 520000000 },
	{ .div = 3, .freq = 315250000 },
	{ .div = 4, .freq = 157625000 },
	{ /* sentinel */ }
};

static const struct mtk_pll_div_table mmpll_div_table[] = {
	{ .div = 0, .freq = MT2712_PLL_FMAX },
	{ .div = 1, .freq = 1001000000 },
	{ .div = 2, .freq = 601250000 },
	{ .div = 3, .freq = 250250000 },
	{ .div = 4, .freq = 125125000 },
	{ /* sentinel */ }
};

static const struct mtk_pll_data plls[] = {
	PLL(CLK_APMIXED_MAINPLL, "mainpll", 0x0230, 0x023C, 0xf0000100,
	    HAVE_RST_BAR, 31, 0x0230, 4, 0, 0, 0, 0x0234, 0),
	PLL(CLK_APMIXED_UNIVPLL, "univpll", 0x0240, 0x024C, 0xfe000100,
	    HAVE_RST_BAR, 31, 0x0240, 4, 0, 0, 0, 0x0244, 0),
	PLL(CLK_APMIXED_VCODECPLL, "vcodecpll", 0x0320, 0x032C, 0xc0000100,
	    0, 31, 0x0320, 4, 0, 0, 0, 0x0324, 0),
	PLL(CLK_APMIXED_VENCPLL, "vencpll", 0x0280, 0x028C, 0x00000100,
	    0, 31, 0x0280, 4, 0, 0, 0, 0x0284, 0),
	PLL(CLK_APMIXED_APLL1, "apll1", 0x0330, 0x0340, 0x00000100,
	    0, 31, 0x0330, 4, 0x0338, 0x0014, 0, 0x0334, 0),
	PLL(CLK_APMIXED_APLL2, "apll2", 0x0350, 0x0360, 0x00000100,
	    0, 31, 0x0350, 4, 0x0358, 0x0014, 1, 0x0354, 0),
	PLL(CLK_APMIXED_LVDSPLL, "lvdspll", 0x0370, 0x037c, 0x00000100,
	    0, 31, 0x0370, 4, 0, 0, 0, 0x0374, 0),
	PLL(CLK_APMIXED_LVDSPLL2, "lvdspll2", 0x0390, 0x039C, 0x00000100,
	    0, 31, 0x0390, 4, 0, 0, 0, 0x0394, 0),
	PLL(CLK_APMIXED_MSDCPLL, "msdcpll", 0x0270, 0x027C, 0x00000100,
	    0, 31, 0x0270, 4, 0, 0, 0, 0x0274, 0),
	PLL(CLK_APMIXED_MSDCPLL2, "msdcpll2", 0x0410, 0x041C, 0x00000100,
	    0, 31, 0x0410, 4, 0, 0, 0, 0x0414, 0),
	PLL(CLK_APMIXED_TVDPLL, "tvdpll", 0x0290, 0x029C, 0xc0000100,
	    0, 31, 0x0290, 4, 0, 0, 0, 0x0294, 0),
	PLL_B(CLK_APMIXED_MMPLL, "mmpll", 0x0250, 0x0260, 0x00000100,
	      0, 31, 0x0250, 4, 0, 0, 0, 0x0254, 0, mmpll_div_table),
	PLL_B(CLK_APMIXED_ARMCA35PLL, "armca35pll", 0x0100, 0x0110, 0xf0000100,
	      HAVE_RST_BAR, 31, 0x0100, 4, 0, 0, 0, 0x0104, 0, armca35pll_div_table),
	PLL_B(CLK_APMIXED_ARMCA72PLL, "armca72pll", 0x0210, 0x0220, 0x00000100,
	      0, 31, 0x0210, 4, 0, 0, 0, 0x0214, 0, armca72pll_div_table),
	PLL(CLK_APMIXED_ETHERPLL, "etherpll", 0x0300, 0x030C, 0xc0000100,
	    0, 31, 0x0300, 4, 0, 0, 0, 0x0304, 0),
};

static int clk_mt2712_apmixed_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	int r;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_APMIXED_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	r = mtk_clk_register_plls(node, plls, ARRAY_SIZE(plls), clk_data);
	if (r)
		goto free_clk_data;

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r) {
		dev_err(&pdev->dev, "Cannot register clock provider: %d\n", r);
		goto unregister_plls;
	}

	return 0;

unregister_plls:
	mtk_clk_unregister_plls(plls, ARRAY_SIZE(plls), clk_data);
free_clk_data:
	mtk_free_clk_data(clk_data);
	return r;
}

static const struct of_device_id of_match_clk_mt2712_apmixed[] = {
	{ .compatible = "mediatek,mt2712-apmixedsys" },
	{ /* sentinel */ }
};

static struct platform_driver clk_mt2712_apmixed_drv = {
	.probe = clk_mt2712_apmixed_probe,
	.driver = {
		.name = "clk-mt2712-apmixed",
		.of_match_table = of_match_clk_mt2712_apmixed,
	},
};
builtin_platform_driver(clk_mt2712_apmixed_drv)

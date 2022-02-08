// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include "clk-gate.h"
#include "clk-mtk.h"
#include "clk-pll.h"

#include <dt-bindings/clock/mt8195-clk.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

static const struct mtk_gate_regs apmixed_cg_regs = {
	.set_ofs = 0x8,
	.clr_ofs = 0x8,
	.sta_ofs = 0x8,
};

#define GATE_APMIXED(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &apmixed_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate apmixed_clks[] = {
	GATE_APMIXED(CLK_APMIXED_PLL_SSUSB26M, "pll_ssusb26m", "clk26m", 1),
};

#define MT8195_PLL_FMAX		(3800UL * MHZ)
#define MT8195_PLL_FMIN		(1500UL * MHZ)
#define MT8195_INTEGER_BITS	8

#define PLL(_id, _name, _reg, _pwr_reg, _en_mask, _flags,		\
	    _rst_bar_mask, _pcwbits, _pd_reg, _pd_shift,		\
	    _tuner_reg, _tuner_en_reg, _tuner_en_bit,			\
	    _pcw_reg, _pcw_shift, _pcw_chg_reg,				\
	    _en_reg, _pll_en_bit) {					\
		.id = _id,						\
		.name = _name,						\
		.reg = _reg,						\
		.pwr_reg = _pwr_reg,					\
		.en_mask = _en_mask,					\
		.flags = _flags,					\
		.rst_bar_mask = _rst_bar_mask,				\
		.fmax = MT8195_PLL_FMAX,				\
		.fmin = MT8195_PLL_FMIN,				\
		.pcwbits = _pcwbits,					\
		.pcwibits = MT8195_INTEGER_BITS,			\
		.pd_reg = _pd_reg,					\
		.pd_shift = _pd_shift,					\
		.tuner_reg = _tuner_reg,				\
		.tuner_en_reg = _tuner_en_reg,				\
		.tuner_en_bit = _tuner_en_bit,				\
		.pcw_reg = _pcw_reg,					\
		.pcw_shift = _pcw_shift,				\
		.pcw_chg_reg = _pcw_chg_reg,				\
		.en_reg = _en_reg,					\
		.pll_en_bit = _pll_en_bit,				\
	}

static const struct mtk_pll_data plls[] = {
	PLL(CLK_APMIXED_NNAPLL, "nnapll", 0x0390, 0x03a0, 0,
	    0, 0, 22, 0x0398, 24, 0, 0, 0, 0x0398, 0, 0x0398, 0, 9),
	PLL(CLK_APMIXED_RESPLL, "respll", 0x0190, 0x0320, 0,
	    0, 0, 22, 0x0198, 24, 0, 0, 0, 0x0198, 0, 0x0198, 0, 9),
	PLL(CLK_APMIXED_ETHPLL, "ethpll", 0x0360, 0x0370, 0,
	    0, 0, 22, 0x0368, 24, 0, 0, 0, 0x0368, 0, 0x0368, 0, 9),
	PLL(CLK_APMIXED_MSDCPLL, "msdcpll", 0x0710, 0x0720, 0,
	    0, 0, 22, 0x0718, 24, 0, 0, 0, 0x0718, 0, 0x0718, 0, 9),
	PLL(CLK_APMIXED_TVDPLL1, "tvdpll1", 0x00a0, 0x00b0, 0,
	    0, 0, 22, 0x00a8, 24, 0, 0, 0, 0x00a8, 0, 0x00a8, 0, 9),
	PLL(CLK_APMIXED_TVDPLL2, "tvdpll2", 0x00c0, 0x00d0, 0,
	    0, 0, 22, 0x00c8, 24, 0, 0, 0, 0x00c8, 0, 0x00c8, 0, 9),
	PLL(CLK_APMIXED_MMPLL, "mmpll", 0x00e0, 0x00f0, 0xff000000,
	    HAVE_RST_BAR, BIT(23), 22, 0x00e8, 24, 0, 0, 0, 0x00e8, 0, 0x00e8, 0, 9),
	PLL(CLK_APMIXED_MAINPLL, "mainpll", 0x01d0, 0x01e0, 0xff000000,
	    HAVE_RST_BAR, BIT(23), 22, 0x01d8, 24, 0, 0, 0, 0x01d8, 0, 0x01d8, 0, 9),
	PLL(CLK_APMIXED_VDECPLL, "vdecpll", 0x0890, 0x08a0, 0,
	    0, 0, 22, 0x0898, 24, 0, 0, 0, 0x0898, 0, 0x0898, 0, 9),
	PLL(CLK_APMIXED_IMGPLL, "imgpll", 0x0100, 0x0110, 0,
	    0, 0, 22, 0x0108, 24, 0, 0, 0, 0x0108, 0, 0x0108, 0, 9),
	PLL(CLK_APMIXED_UNIVPLL, "univpll", 0x01f0, 0x0700, 0xff000000,
	    HAVE_RST_BAR, BIT(23), 22, 0x01f8, 24, 0, 0, 0, 0x01f8, 0, 0x01f8, 0, 9),
	PLL(CLK_APMIXED_HDMIPLL1, "hdmipll1", 0x08c0, 0x08d0, 0,
	    0, 0, 22, 0x08c8, 24, 0, 0, 0, 0x08c8, 0, 0x08c8, 0, 9),
	PLL(CLK_APMIXED_HDMIPLL2, "hdmipll2", 0x0870, 0x0880, 0,
	    0, 0, 22, 0x0878, 24, 0, 0, 0, 0x0878, 0, 0x0878, 0, 9),
	PLL(CLK_APMIXED_HDMIRX_APLL, "hdmirx_apll", 0x08e0, 0x0dd4, 0,
	    0, 0, 32, 0x08e8, 24, 0, 0, 0, 0x08ec, 0, 0x08e8, 0, 9),
	PLL(CLK_APMIXED_USB1PLL, "usb1pll", 0x01a0, 0x01b0, 0,
	    0, 0, 22, 0x01a8, 24, 0, 0, 0, 0x01a8, 0, 0x01a8, 0, 9),
	PLL(CLK_APMIXED_ADSPPLL, "adsppll", 0x07e0, 0x07f0, 0,
	    0, 0, 22, 0x07e8, 24, 0, 0, 0, 0x07e8, 0, 0x07e8, 0, 9),
	PLL(CLK_APMIXED_APLL1, "apll1", 0x07c0, 0x0dc0, 0,
	    0, 0, 32, 0x07c8, 24, 0x0470, 0x0000, 12, 0x07cc, 0, 0x07c8, 0, 9),
	PLL(CLK_APMIXED_APLL2, "apll2", 0x0780, 0x0dc4, 0,
	    0, 0, 32, 0x0788, 24, 0x0474, 0x0000, 13, 0x078c, 0, 0x0788, 0, 9),
	PLL(CLK_APMIXED_APLL3, "apll3", 0x0760, 0x0dc8, 0,
	    0, 0, 32, 0x0768, 24, 0x0478, 0x0000, 14, 0x076c, 0, 0x0768, 0, 9),
	PLL(CLK_APMIXED_APLL4, "apll4", 0x0740, 0x0dcc, 0,
	    0, 0, 32, 0x0748, 24, 0x047C, 0x0000, 15, 0x074c, 0, 0x0748, 0, 9),
	PLL(CLK_APMIXED_APLL5, "apll5", 0x07a0, 0x0dd0, 0x100000,
	    0, 0, 32, 0x07a8, 24, 0x0480, 0x0000, 16, 0x07ac, 0, 0x07a8, 0, 9),
	PLL(CLK_APMIXED_MFGPLL, "mfgpll", 0x0340, 0x0350, 0,
	    0, 0, 22, 0x0348, 24, 0, 0, 0, 0x0348, 0, 0x0348, 0, 9),
	PLL(CLK_APMIXED_DGIPLL, "dgipll", 0x0150, 0x0160, 0,
	    0, 0, 22, 0x0158, 24, 0, 0, 0, 0x0158, 0, 0x0158, 0, 9),
};

static const struct of_device_id of_match_clk_mt8195_apmixed[] = {
	{ .compatible = "mediatek,mt8195-apmixedsys", },
	{}
};

static int clk_mt8195_apmixed_probe(struct platform_device *pdev)
{
	struct clk_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	int r;

	clk_data = mtk_alloc_clk_data(CLK_APMIXED_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	r = mtk_clk_register_plls(node, plls, ARRAY_SIZE(plls), clk_data);
	if (r)
		goto free_apmixed_data;

	r = mtk_clk_register_gates(node, apmixed_clks, ARRAY_SIZE(apmixed_clks), clk_data);
	if (r)
		goto unregister_plls;

	r = of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
	if (r)
		goto unregister_gates;

	return r;

unregister_gates:
	mtk_clk_unregister_gates(apmixed_clks, ARRAY_SIZE(apmixed_clks), clk_data);
unregister_plls:
	mtk_clk_unregister_plls(plls, ARRAY_SIZE(plls), clk_data);
free_apmixed_data:
	mtk_free_clk_data(clk_data);
	return r;
}

static struct platform_driver clk_mt8195_apmixed_drv = {
	.probe = clk_mt8195_apmixed_probe,
	.driver = {
		.name = "clk-mt8195-apmixed",
		.of_match_table = of_match_clk_mt8195_apmixed,
	},
};
builtin_platform_driver(clk_mt8195_apmixed_drv);

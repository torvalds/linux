// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mt8186-clk.h>

#include "clk-fhctl.h"
#include "clk-mtk.h"
#include "clk-pll.h"
#include "clk-pllfh.h"

#define MT8186_PLL_FMAX		(3800UL * MHZ)
#define MT8186_PLL_FMIN		(1500UL * MHZ)
#define MT8186_INTEGER_BITS	(8)

#define PLL(_id, _name, _reg, _pwr_reg, _en_mask, _flags,		\
	    _rst_bar_mask, _pcwbits, _pd_reg, _pd_shift,		\
	    _tuner_reg, _tuner_en_reg, _tuner_en_bit,			\
	    _pcw_reg) {							\
		.id = _id,						\
		.name = _name,						\
		.reg = _reg,						\
		.pwr_reg = _pwr_reg,					\
		.en_mask = _en_mask,					\
		.flags = _flags,					\
		.rst_bar_mask = _rst_bar_mask,				\
		.fmax = MT8186_PLL_FMAX,				\
		.fmin = MT8186_PLL_FMIN,				\
		.pcwbits = _pcwbits,					\
		.pcwibits = MT8186_INTEGER_BITS,			\
		.pd_reg = _pd_reg,					\
		.pd_shift = _pd_shift,					\
		.tuner_reg = _tuner_reg,				\
		.tuner_en_reg = _tuner_en_reg,				\
		.tuner_en_bit = _tuner_en_bit,				\
		.pcw_reg = _pcw_reg,					\
		.pcw_shift = 0,						\
		.pcw_chg_reg = 0,					\
		.en_reg = 0,						\
		.pll_en_bit = 0,					\
	}

static const struct mtk_pll_data plls[] = {
	/*
	 * armpll_ll/armpll_bl/ccipll are main clock source of AP MCU,
	 * should not be closed in Linux world.
	 */
	PLL(CLK_APMIXED_ARMPLL_LL, "armpll_ll", 0x0204, 0x0210, 0,
	    PLL_AO, 0, 22, 0x0208, 24, 0, 0, 0, 0x0208),
	PLL(CLK_APMIXED_ARMPLL_BL, "armpll_bl", 0x0214, 0x0220, 0,
	    PLL_AO, 0, 22, 0x0218, 24, 0, 0, 0, 0x0218),
	PLL(CLK_APMIXED_CCIPLL, "ccipll", 0x0224, 0x0230, 0,
	    PLL_AO, 0, 22, 0x0228, 24, 0, 0, 0, 0x0228),
	PLL(CLK_APMIXED_MAINPLL, "mainpll", 0x0244, 0x0250, 0xff000000,
	    HAVE_RST_BAR, BIT(23), 22, 0x0248, 24, 0, 0, 0, 0x0248),
	PLL(CLK_APMIXED_UNIV2PLL, "univ2pll", 0x0324, 0x0330, 0xff000000,
	    HAVE_RST_BAR, BIT(23), 22, 0x0328, 24, 0, 0, 0, 0x0328),
	PLL(CLK_APMIXED_MSDCPLL, "msdcpll", 0x038C, 0x0398, 0,
	    0, 0, 22, 0x0390, 24, 0, 0, 0, 0x0390),
	PLL(CLK_APMIXED_MMPLL, "mmpll", 0x0254, 0x0260, 0,
	    0, 0, 22, 0x0258, 24, 0, 0, 0, 0x0258),
	PLL(CLK_APMIXED_NNAPLL, "nnapll", 0x035C, 0x0368, 0,
	    0, 0, 22, 0x0360, 24, 0, 0, 0, 0x0360),
	PLL(CLK_APMIXED_NNA2PLL, "nna2pll", 0x036C, 0x0378, 0,
	    0, 0, 22, 0x0370, 24, 0, 0, 0, 0x0370),
	PLL(CLK_APMIXED_ADSPPLL, "adsppll", 0x0304, 0x0310, 0,
	    0, 0, 22, 0x0308, 24, 0, 0, 0, 0x0308),
	PLL(CLK_APMIXED_MFGPLL, "mfgpll", 0x0314, 0x0320, 0,
	    0, 0, 22, 0x0318, 24, 0, 0, 0, 0x0318),
	PLL(CLK_APMIXED_TVDPLL, "tvdpll", 0x0264, 0x0270, 0,
	    0, 0, 22, 0x0268, 24, 0, 0, 0, 0x0268),
	PLL(CLK_APMIXED_APLL1, "apll1", 0x0334, 0x0344, 0,
	    0, 0, 32, 0x0338, 24, 0x0040, 0x000C, 0, 0x033C),
	PLL(CLK_APMIXED_APLL2, "apll2", 0x0348, 0x0358, 0,
	    0, 0, 32, 0x034C, 24, 0x0044, 0x000C, 5, 0x0350),
};

enum fh_pll_id {
	FH_ARMPLL_LL,
	FH_ARMPLL_BL,
	FH_CCIPLL,
	FH_MAINPLL,
	FH_MMPLL,
	FH_TVDPLL,
	FH_RESERVE6,
	FH_ADSPPLL,
	FH_MFGPLL,
	FH_NNAPLL,
	FH_NNA2PLL,
	FH_MSDCPLL,
	FH_RESERVE12,
	FH_NR_FH,
};

#define FH(_pllid, _fhid, _offset) {					\
		.data = {						\
			.pll_id = _pllid,				\
			.fh_id = _fhid,					\
			.fh_ver = FHCTL_PLLFH_V2,			\
			.fhx_offset = _offset,				\
			.dds_mask = GENMASK(21, 0),			\
			.slope0_value = 0x6003c97,			\
			.slope1_value = 0x6003c97,			\
			.sfstrx_en = BIT(2),				\
			.frddsx_en = BIT(1),				\
			.fhctlx_en = BIT(0),				\
			.tgl_org = BIT(31),				\
			.dvfs_tri = BIT(31),				\
			.pcwchg = BIT(31),				\
			.dt_val = 0x0,					\
			.df_val = 0x9,					\
			.updnlmt_shft = 16,				\
			.msk_frddsx_dys = GENMASK(23, 20),		\
			.msk_frddsx_dts = GENMASK(19, 16),		\
		},							\
	}

static struct mtk_pllfh_data pllfhs[] = {
	FH(CLK_APMIXED_ARMPLL_LL, FH_ARMPLL_LL, 0x003C),
	FH(CLK_APMIXED_ARMPLL_BL, FH_ARMPLL_BL, 0x0050),
	FH(CLK_APMIXED_CCIPLL, FH_CCIPLL, 0x0064),
	FH(CLK_APMIXED_MAINPLL, FH_MAINPLL, 0x0078),
	FH(CLK_APMIXED_MMPLL, FH_MMPLL, 0x008C),
	FH(CLK_APMIXED_TVDPLL, FH_TVDPLL, 0x00A0),
	FH(CLK_APMIXED_ADSPPLL, FH_ADSPPLL, 0x00C8),
	FH(CLK_APMIXED_MFGPLL, FH_MFGPLL, 0x00DC),
	FH(CLK_APMIXED_NNAPLL, FH_NNAPLL, 0x00F0),
	FH(CLK_APMIXED_NNA2PLL, FH_NNA2PLL, 0x0104),
	FH(CLK_APMIXED_MSDCPLL, FH_MSDCPLL, 0x0118),
};

static const struct of_device_id of_match_clk_mt8186_apmixed[] = {
	{ .compatible = "mediatek,mt8186-apmixedsys", },
	{}
};

static int clk_mt8186_apmixed_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	const u8 *fhctl_node = "mediatek,mt8186-fhctl";
	int r;

	clk_data = mtk_alloc_clk_data(CLK_APMIXED_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	fhctl_parse_dt(fhctl_node, pllfhs, ARRAY_SIZE(pllfhs));

	r = mtk_clk_register_pllfhs(node, plls, ARRAY_SIZE(plls),
				    pllfhs, ARRAY_SIZE(pllfhs), clk_data);
	if (r)
		goto free_apmixed_data;

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		goto unregister_plls;

	platform_set_drvdata(pdev, clk_data);

	return r;

unregister_plls:
	mtk_clk_unregister_pllfhs(plls, ARRAY_SIZE(plls), pllfhs,
				  ARRAY_SIZE(pllfhs), clk_data);
free_apmixed_data:
	mtk_free_clk_data(clk_data);
	return r;
}

static int clk_mt8186_apmixed_remove(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw_onecell_data *clk_data = platform_get_drvdata(pdev);

	of_clk_del_provider(node);
	mtk_clk_unregister_pllfhs(plls, ARRAY_SIZE(plls), pllfhs,
				  ARRAY_SIZE(pllfhs), clk_data);
	mtk_free_clk_data(clk_data);

	return 0;
}

static struct platform_driver clk_mt8186_apmixed_drv = {
	.probe = clk_mt8186_apmixed_probe,
	.remove = clk_mt8186_apmixed_remove,
	.driver = {
		.name = "clk-mt8186-apmixed",
		.of_match_table = of_match_clk_mt8186_apmixed,
	},
};
builtin_platform_driver(clk_mt8186_apmixed_drv);

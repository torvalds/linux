// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Copyright (c) 2022 Collabora Ltd.
 * Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <dt-bindings/clock/mt8173-clk.h>
#include <linux/of_address.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "clk-fhctl.h"
#include "clk-mtk.h"
#include "clk-pll.h"
#include "clk-pllfh.h"

#define REGOFF_REF2USB		0x8
#define REGOFF_HDMI_REF		0x40

#define MT8173_PLL_FMAX		(3000UL * MHZ)

#define CON0_MT8173_RST_BAR	BIT(24)

#define PLL_B(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,	\
			_pd_reg, _pd_shift, _tuner_reg, _pcw_reg,	\
			_pcw_shift, _div_table) {			\
		.id = _id,						\
		.name = _name,						\
		.reg = _reg,						\
		.pwr_reg = _pwr_reg,					\
		.en_mask = _en_mask,					\
		.flags = _flags,					\
		.rst_bar_mask = CON0_MT8173_RST_BAR,			\
		.fmax = MT8173_PLL_FMAX,				\
		.pcwbits = _pcwbits,					\
		.pd_reg = _pd_reg,					\
		.pd_shift = _pd_shift,					\
		.tuner_reg = _tuner_reg,				\
		.pcw_reg = _pcw_reg,					\
		.pcw_shift = _pcw_shift,				\
		.div_table = _div_table,				\
	}

#define PLL(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,	\
			_pd_reg, _pd_shift, _tuner_reg, _pcw_reg,	\
			_pcw_shift)					\
		PLL_B(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits, \
			_pd_reg, _pd_shift, _tuner_reg, _pcw_reg, _pcw_shift, \
			NULL)

static const struct mtk_pll_div_table mmpll_div_table[] = {
	{ .div = 0, .freq = MT8173_PLL_FMAX },
	{ .div = 1, .freq = 1000000000 },
	{ .div = 2, .freq = 702000000 },
	{ .div = 3, .freq = 253500000 },
	{ .div = 4, .freq = 126750000 },
	{ } /* sentinel */
};

static const struct mtk_pll_data plls[] = {
	PLL(CLK_APMIXED_ARMCA15PLL, "armca15pll", 0x200, 0x20c, 0, PLL_AO,
	    21, 0x204, 24, 0x0, 0x204, 0),
	PLL(CLK_APMIXED_ARMCA7PLL, "armca7pll", 0x210, 0x21c, 0, PLL_AO,
	    21, 0x214, 24, 0x0, 0x214, 0),
	PLL(CLK_APMIXED_MAINPLL, "mainpll", 0x220, 0x22c, 0xf0000100, HAVE_RST_BAR, 21,
	    0x220, 4, 0x0, 0x224, 0),
	PLL(CLK_APMIXED_UNIVPLL, "univpll", 0x230, 0x23c, 0xfe000000, HAVE_RST_BAR, 7,
	    0x230, 4, 0x0, 0x234, 14),
	PLL_B(CLK_APMIXED_MMPLL, "mmpll", 0x240, 0x24c, 0, 0, 21, 0x244, 24, 0x0,
	      0x244, 0, mmpll_div_table),
	PLL(CLK_APMIXED_MSDCPLL, "msdcpll", 0x250, 0x25c, 0, 0, 21, 0x250, 4, 0x0, 0x254, 0),
	PLL(CLK_APMIXED_VENCPLL, "vencpll", 0x260, 0x26c, 0, 0, 21, 0x260, 4, 0x0, 0x264, 0),
	PLL(CLK_APMIXED_TVDPLL, "tvdpll", 0x270, 0x27c, 0, 0, 21, 0x270, 4, 0x0, 0x274, 0),
	PLL(CLK_APMIXED_MPLL, "mpll", 0x280, 0x28c, 0, 0, 21, 0x280, 4, 0x0, 0x284, 0),
	PLL(CLK_APMIXED_VCODECPLL, "vcodecpll", 0x290, 0x29c, 0, 0, 21, 0x290, 4, 0x0, 0x294, 0),
	PLL(CLK_APMIXED_APLL1, "apll1", 0x2a0, 0x2b0, 0, 0, 31, 0x2a0, 4, 0x2a4, 0x2a4, 0),
	PLL(CLK_APMIXED_APLL2, "apll2", 0x2b4, 0x2c4, 0, 0, 31, 0x2b4, 4, 0x2b8, 0x2b8, 0),
	PLL(CLK_APMIXED_LVDSPLL, "lvdspll", 0x2d0, 0x2dc, 0, 0, 21, 0x2d0, 4, 0x0, 0x2d4, 0),
	PLL(CLK_APMIXED_MSDCPLL2, "msdcpll2", 0x2f0, 0x2fc, 0, 0, 21, 0x2f0, 4, 0x0, 0x2f4, 0),
};

enum fh_pll_id {
	FH_ARMCA7PLL,
	FH_ARMCA15PLL,
	FH_MAINPLL,
	FH_MPLL,
	FH_MSDCPLL,
	FH_MMPLL,
	FH_VENCPLL,
	FH_TVDPLL,
	FH_VCODECPLL,
	FH_LVDSPLL,
	FH_MSDC2PLL,
	FH_NR_FH,
};

#define FH(_pllid, _fhid, _offset) {					\
		.data = {						\
			.pll_id = _pllid,				\
			.fh_id = _fhid,					\
			.fh_ver = FHCTL_PLLFH_V1,			\
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
	FH(CLK_APMIXED_ARMCA7PLL, FH_ARMCA7PLL, 0x38),
	FH(CLK_APMIXED_ARMCA15PLL, FH_ARMCA15PLL, 0x4c),
	FH(CLK_APMIXED_MAINPLL, FH_MAINPLL, 0x60),
	FH(CLK_APMIXED_MPLL, FH_MPLL, 0x74),
	FH(CLK_APMIXED_MSDCPLL, FH_MSDCPLL, 0x88),
	FH(CLK_APMIXED_MMPLL, FH_MMPLL, 0x9c),
	FH(CLK_APMIXED_VENCPLL, FH_VENCPLL, 0xb0),
	FH(CLK_APMIXED_TVDPLL, FH_TVDPLL, 0xc4),
	FH(CLK_APMIXED_VCODECPLL, FH_VCODECPLL, 0xd8),
	FH(CLK_APMIXED_LVDSPLL, FH_LVDSPLL, 0xec),
	FH(CLK_APMIXED_MSDCPLL2, FH_MSDC2PLL, 0x100),
};

static const struct of_device_id of_match_clk_mt8173_apmixed[] = {
	{ .compatible = "mediatek,mt8173-apmixedsys" },
	{ /* sentinel */ }
};

static int clk_mt8173_apmixed_probe(struct platform_device *pdev)
{
	const u8 *fhctl_node = "mediatek,mt8173-fhctl";
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw_onecell_data *clk_data;
	void __iomem *base;
	struct clk_hw *hw;
	int r;

	base = of_iomap(node, 0);
	if (!base)
		return PTR_ERR(base);

	clk_data = mtk_alloc_clk_data(CLK_APMIXED_NR_CLK);
	if (IS_ERR_OR_NULL(clk_data))
		return -ENOMEM;

	fhctl_parse_dt(fhctl_node, pllfhs, ARRAY_SIZE(pllfhs));
	r = mtk_clk_register_pllfhs(node, plls, ARRAY_SIZE(plls),
				    pllfhs, ARRAY_SIZE(pllfhs), clk_data);
	if (r)
		goto free_clk_data;

	hw = mtk_clk_register_ref2usb_tx("ref2usb_tx", "clk26m", base + REGOFF_REF2USB);
	if (IS_ERR(hw)) {
		r = PTR_ERR(hw);
		dev_err(&pdev->dev, "Failed to register ref2usb_tx: %d\n", r);
		goto unregister_plls;
	}
	clk_data->hws[CLK_APMIXED_REF2USB_TX] = hw;

	hw = devm_clk_hw_register_divider(&pdev->dev, "hdmi_ref", "tvdpll_594m", 0,
					  base + REGOFF_HDMI_REF, 16, 3,
					  CLK_DIVIDER_POWER_OF_TWO, NULL);
	clk_data->hws[CLK_APMIXED_HDMI_REF] = hw;

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		goto unregister_ref2usb;

	return 0;

unregister_ref2usb:
	mtk_clk_unregister_ref2usb_tx(clk_data->hws[CLK_APMIXED_REF2USB_TX]);
unregister_plls:
	mtk_clk_unregister_pllfhs(plls, ARRAY_SIZE(plls), pllfhs,
				  ARRAY_SIZE(pllfhs), clk_data);
free_clk_data:
	mtk_free_clk_data(clk_data);
	return r;
}

static int clk_mt8173_apmixed_remove(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw_onecell_data *clk_data = platform_get_drvdata(pdev);

	of_clk_del_provider(node);
	mtk_clk_unregister_ref2usb_tx(clk_data->hws[CLK_APMIXED_REF2USB_TX]);
	mtk_clk_unregister_pllfhs(plls, ARRAY_SIZE(plls), pllfhs,
				  ARRAY_SIZE(pllfhs), clk_data);
	mtk_free_clk_data(clk_data);

	return 0;
}

static struct platform_driver clk_mt8173_apmixed_drv = {
	.probe = clk_mt8173_apmixed_probe,
	.remove = clk_mt8173_apmixed_remove,
	.driver = {
		.name = "clk-mt8173-apmixed",
		.of_match_table = of_match_clk_mt8173_apmixed,
	},
};
module_platform_driver(clk_mt8173_apmixed_drv);

MODULE_DESCRIPTION("MediaTek MT8173 apmixed clocks driver");
MODULE_LICENSE("GPL");

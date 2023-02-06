// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Collabora Ltd.
 * Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <dt-bindings/clock/mediatek,mt6795-clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "clk-fhctl.h"
#include "clk-mtk.h"
#include "clk-pll.h"
#include "clk-pllfh.h"

#define REG_REF2USB		0x8
#define REG_AP_PLL_CON7		0x1c
 #define MD1_MTCMOS_OFF		BIT(0)
 #define MD1_MEM_OFF		BIT(1)
 #define MD1_CLK_OFF		BIT(4)
 #define MD1_ISO_OFF		BIT(8)

#define MT6795_PLL_FMAX		(3000UL * MHZ)
#define MT6795_CON0_EN		BIT(0)
#define MT6795_CON0_RST_BAR	BIT(24)

#define PLL(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,	\
	    _pd_reg, _pd_shift, _tuner_reg, _pcw_reg, _pcw_shift) {	\
		.id = _id,						\
		.name = _name,						\
		.reg = _reg,						\
		.pwr_reg = _pwr_reg,					\
		.en_mask = MT6795_CON0_EN | _en_mask,			\
		.flags = _flags,					\
		.rst_bar_mask = MT6795_CON0_RST_BAR,			\
		.fmax = MT6795_PLL_FMAX,				\
		.pcwbits = _pcwbits,					\
		.pd_reg = _pd_reg,					\
		.pd_shift = _pd_shift,					\
		.tuner_reg = _tuner_reg,				\
		.pcw_reg = _pcw_reg,					\
		.pcw_shift = _pcw_shift,				\
		.div_table = NULL,					\
		.pll_en_bit = 0,					\
	}

static const struct mtk_pll_data plls[] = {
	PLL(CLK_APMIXED_ARMCA53PLL, "armca53pll", 0x200, 0x20c, 0, PLL_AO,
	    21, 0x204, 24, 0x0, 0x204, 0),
	PLL(CLK_APMIXED_MAINPLL, "mainpll", 0x220, 0x22c, 0xf0000101, HAVE_RST_BAR,
	    21, 0x220, 4, 0x0, 0x224, 0),
	PLL(CLK_APMIXED_UNIVPLL, "univpll", 0x230, 0x23c, 0xfe000101, HAVE_RST_BAR,
	    7, 0x230, 4, 0x0, 0x234, 14),
	PLL(CLK_APMIXED_MMPLL, "mmpll", 0x240, 0x24c, 0, 0, 21, 0x244, 24, 0x0, 0x244, 0),
	PLL(CLK_APMIXED_MSDCPLL, "msdcpll", 0x250, 0x25c, 0, 0, 21, 0x250, 4, 0x0, 0x254, 0),
	PLL(CLK_APMIXED_VENCPLL, "vencpll", 0x260, 0x26c, 0, 0, 21, 0x260, 4, 0x0, 0x264, 0),
	PLL(CLK_APMIXED_TVDPLL, "tvdpll", 0x270, 0x27c, 0, 0, 21, 0x270, 4, 0x0, 0x274, 0),
	PLL(CLK_APMIXED_MPLL, "mpll", 0x280, 0x28c, 0, 0, 21, 0x280, 4, 0x0, 0x284, 0),
	PLL(CLK_APMIXED_VCODECPLL, "vcodecpll", 0x290, 0x29c, 0, 0, 21, 0x290, 4, 0x0, 0x294, 0),
	PLL(CLK_APMIXED_APLL1, "apll1", 0x2a0, 0x2b0, 0, 0, 31, 0x2a0, 4, 0x2a8, 0x2a4, 0),
	PLL(CLK_APMIXED_APLL2, "apll2", 0x2b4, 0x2c4, 0, 0, 31, 0x2b4, 4, 0x2bc, 0x2b8, 0),
};

enum fh_pll_id {
	FH_CA53PLL_LL,
	FH_CA53PLL_BL,
	FH_MAINPLL,
	FH_MPLL,
	FH_MSDCPLL,
	FH_MMPLL,
	FH_VENCPLL,
	FH_TVDPLL,
	FH_VCODECPLL,
	FH_NR_FH,
};

#define _FH(_pllid, _fhid, _slope, _offset) {				\
		.data = {						\
			.pll_id = _pllid,				\
			.fh_id = _fhid,					\
			.fh_ver = FHCTL_PLLFH_V1,			\
			.fhx_offset = _offset,				\
			.dds_mask = GENMASK(21, 0),			\
			.slope0_value = _slope,				\
			.slope1_value = _slope,				\
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

#define FH(_pllid, _fhid, _offset)	_FH(_pllid, _fhid, 0x6003c97, _offset)
#define FH_M(_pllid, _fhid, _offset)	_FH(_pllid, _fhid, 0x6000140, _offset)

static struct mtk_pllfh_data pllfhs[] = {
	FH(CLK_APMIXED_ARMCA53PLL, FH_CA53PLL_BL, 0x38),
	FH(CLK_APMIXED_MAINPLL, FH_MAINPLL, 0x60),
	FH_M(CLK_APMIXED_MPLL, FH_MPLL, 0x74),
	FH(CLK_APMIXED_MSDCPLL, FH_MSDCPLL, 0x88),
	FH(CLK_APMIXED_MMPLL, FH_MMPLL, 0x9c),
	FH(CLK_APMIXED_VENCPLL, FH_VENCPLL, 0xb0),
	FH(CLK_APMIXED_TVDPLL, FH_TVDPLL, 0xc4),
	FH(CLK_APMIXED_VCODECPLL, FH_VCODECPLL, 0xd8),
};

static void clk_mt6795_apmixed_setup_md1(void __iomem *base)
{
	void __iomem *reg = base + REG_AP_PLL_CON7;

	/* Turn on MD1 internal clock */
	writel(readl(reg) & ~MD1_CLK_OFF, reg);

	/* Unlock MD1's MTCMOS power path */
	writel(readl(reg) & ~MD1_MTCMOS_OFF, reg);

	/* Turn on ISO */
	writel(readl(reg) & ~MD1_ISO_OFF, reg);

	/* Turn on memory */
	writel(readl(reg) & ~MD1_MEM_OFF, reg);
}

static const struct of_device_id of_match_clk_mt6795_apmixed[] = {
	{ .compatible = "mediatek,mt6795-apmixedsys" },
	{ /* sentinel */ }
};

static int clk_mt6795_apmixed_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	const u8 *fhctl_node = "mediatek,mt6795-fhctl";
	void __iomem *base;
	struct clk_hw *hw;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk_data = mtk_alloc_clk_data(CLK_APMIXED_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	fhctl_parse_dt(fhctl_node, pllfhs, ARRAY_SIZE(pllfhs));
	ret = mtk_clk_register_pllfhs(node, plls, ARRAY_SIZE(plls),
				      pllfhs, ARRAY_SIZE(pllfhs), clk_data);
	if (ret)
		goto free_clk_data;

	hw = mtk_clk_register_ref2usb_tx("ref2usb_tx", "clk26m", base + REG_REF2USB);
	if (IS_ERR(hw)) {
		ret = PTR_ERR(hw);
		dev_err(dev, "Failed to register ref2usb_tx: %d\n", ret);
		goto unregister_plls;
	}
	clk_data->hws[CLK_APMIXED_REF2USB_TX] = hw;

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (ret) {
		dev_err(dev, "Cannot register clock provider: %d\n", ret);
		goto unregister_ref2usb;
	}

	/* Setup MD1 to avoid random crashes */
	dev_dbg(dev, "Performing initial setup for MD1\n");
	clk_mt6795_apmixed_setup_md1(base);

	return 0;

unregister_ref2usb:
	mtk_clk_unregister_ref2usb_tx(clk_data->hws[CLK_APMIXED_REF2USB_TX]);
unregister_plls:
	mtk_clk_unregister_pllfhs(plls, ARRAY_SIZE(plls), pllfhs,
				  ARRAY_SIZE(pllfhs), clk_data);
free_clk_data:
	mtk_free_clk_data(clk_data);
	return ret;
}

static int clk_mt6795_apmixed_remove(struct platform_device *pdev)
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

static struct platform_driver clk_mt6795_apmixed_drv = {
	.probe = clk_mt6795_apmixed_probe,
	.remove = clk_mt6795_apmixed_remove,
	.driver = {
		.name = "clk-mt6795-apmixed",
		.of_match_table = of_match_clk_mt6795_apmixed,
	},
};
module_platform_driver(clk_mt6795_apmixed_drv);

MODULE_DESCRIPTION("MediaTek MT6795 apmixed clocks driver");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Copyright (c) 2023 Collabora Ltd.
 */

#include <dt-bindings/clock/mediatek,mt8365-clk.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "clk-pll.h"
#include "clk-mtk.h"

#define MT8365_PLL_FMAX		(3800UL * MHZ)
#define MT8365_PLL_FMIN		(1500UL * MHZ)
#define CON0_MT8365_RST_BAR	BIT(23)

#define PLL_B(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,	\
		_pd_reg, _pd_shift, _tuner_reg, _tuner_en_reg,		\
		_tuner_en_bit,	_pcw_reg, _pcw_shift, _div_table,	\
		_rst_bar_mask, _pcw_chg_reg) {				\
		.id = _id,						\
		.name = _name,						\
		.reg = _reg,						\
		.pwr_reg = _pwr_reg,					\
		.en_mask = _en_mask,					\
		.flags = _flags,					\
		.rst_bar_mask = _rst_bar_mask,				\
		.fmax = MT8365_PLL_FMAX,				\
		.fmin = MT8365_PLL_FMIN,				\
		.pcwbits = _pcwbits,					\
		.pcwibits = 8,						\
		.pd_reg = _pd_reg,					\
		.pd_shift = _pd_shift,					\
		.tuner_reg = _tuner_reg,				\
		.tuner_en_reg = _tuner_en_reg,				\
		.tuner_en_bit = _tuner_en_bit,				\
		.pcw_reg = _pcw_reg,					\
		.pcw_shift = _pcw_shift,				\
		.pcw_chg_reg = _pcw_chg_reg,				\
		.div_table = _div_table,				\
	}

#define PLL(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,	\
			_pd_reg, _pd_shift, _tuner_reg,			\
			_tuner_en_reg, _tuner_en_bit, _pcw_reg,		\
			_pcw_shift, _rst_bar_mask, _pcw_chg_reg)	\
		PLL_B(_id, _name, _reg, _pwr_reg, _en_mask, _flags,	\
			_pcwbits, _pd_reg, _pd_shift,			\
			_tuner_reg, _tuner_en_reg, _tuner_en_bit,	\
			_pcw_reg, _pcw_shift, NULL, _rst_bar_mask,	\
			_pcw_chg_reg)					\

static const struct mtk_pll_div_table armpll_div_table[] = {
	{ .div = 0, .freq = MT8365_PLL_FMAX },
	{ .div = 1, .freq = 1500 * MHZ },
	{ .div = 2, .freq = 750 * MHZ },
	{ .div = 3, .freq = 375 * MHZ },
	{ .div = 4, .freq = 182500000 },
	{ } /* sentinel */
};

static const struct mtk_pll_div_table mfgpll_div_table[] = {
	{ .div = 0, .freq = MT8365_PLL_FMAX },
	{ .div = 1, .freq = 1600 * MHZ },
	{ .div = 2, .freq = 800 * MHZ },
	{ .div = 3, .freq = 400 * MHZ },
	{ .div = 4, .freq = 200 * MHZ },
	{ } /* sentinel */
};

static const struct mtk_pll_div_table dsppll_div_table[] = {
	{ .div = 0, .freq = MT8365_PLL_FMAX },
	{ .div = 1, .freq = 1600 * MHZ },
	{ .div = 2, .freq = 600 * MHZ },
	{ .div = 3, .freq = 400 * MHZ },
	{ .div = 4, .freq = 200 * MHZ },
	{ } /* sentinel */
};

static const struct mtk_pll_data plls[] = {
	PLL_B(CLK_APMIXED_ARMPLL, "armpll", 0x030C, 0x0318, 0x00000001, PLL_AO,
	      22, 0x0310, 24, 0, 0, 0, 0x0310, 0, armpll_div_table, 0, 0),
	PLL(CLK_APMIXED_MAINPLL, "mainpll", 0x0228, 0x0234, 0xFF000001,
	    HAVE_RST_BAR, 22, 0x022C, 24, 0, 0, 0, 0x022C, 0, CON0_MT8365_RST_BAR, 0),
	PLL(CLK_APMIXED_UNIVPLL, "univpll2", 0x0208, 0x0214, 0xFF000001,
	    HAVE_RST_BAR, 22, 0x020C, 24, 0, 0, 0, 0x020C, 0, CON0_MT8365_RST_BAR, 0),
	PLL_B(CLK_APMIXED_MFGPLL, "mfgpll", 0x0218, 0x0224, 0x00000001, 0, 22,
	      0x021C, 24, 0, 0, 0, 0x021C, 0, mfgpll_div_table, 0, 0),
	PLL(CLK_APMIXED_MSDCPLL, "msdcpll", 0x0350, 0x035C, 0x00000001, 0, 22,
	    0x0354, 24, 0, 0, 0, 0x0354, 0, 0, 0),
	PLL(CLK_APMIXED_MMPLL, "mmpll", 0x0330, 0x033C, 0x00000001, 0, 22,
	    0x0334, 24, 0, 0, 0, 0x0334, 0, 0, 0),
	PLL(CLK_APMIXED_APLL1, "apll1", 0x031C, 0x032C, 0x00000001, 0, 32,
	    0x0320, 24, 0x0040, 0x000C, 0, 0x0324, 0, 0, 0x0320),
	PLL(CLK_APMIXED_APLL2, "apll2", 0x0360, 0x0370, 0x00000001, 0, 32,
	    0x0364, 24, 0x004C, 0x000C, 5, 0x0368, 0, 0, 0x0364),
	PLL(CLK_APMIXED_LVDSPLL, "lvdspll", 0x0374, 0x0380, 0x00000001, 0, 22,
	    0x0378, 24, 0, 0, 0, 0x0378, 0, 0, 0),
	PLL_B(CLK_APMIXED_DSPPLL, "dsppll", 0x0390, 0x039C, 0x00000001, 0, 22,
	      0x0394, 24, 0, 0, 0, 0x0394, 0, dsppll_div_table, 0, 0),
	PLL(CLK_APMIXED_APUPLL, "apupll", 0x03A0, 0x03AC, 0x00000001, 0, 22,
	    0x03A4, 24, 0, 0, 0, 0x03A4, 0, 0, 0),
};

static int clk_mt8365_apmixed_probe(struct platform_device *pdev)
{
	void __iomem *base;
	struct clk_hw_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct clk_hw *hw;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk_data = mtk_devm_alloc_clk_data(dev, CLK_APMIXED_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	hw = devm_clk_hw_register_gate(dev, "univ_en", "univpll2", 0,
				       base + 0x204, 0, 0, NULL);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	clk_data->hws[CLK_APMIXED_UNIV_EN] = hw;

	hw = devm_clk_hw_register_gate(dev, "usb20_en", "univ_en", 0,
				       base + 0x204, 1, 0, NULL);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	clk_data->hws[CLK_APMIXED_USB20_EN] = hw;

	ret = mtk_clk_register_plls(node, plls, ARRAY_SIZE(plls), clk_data);
	if (ret)
		return ret;

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (ret)
		goto unregister_plls;

	return 0;

unregister_plls:
	mtk_clk_unregister_plls(plls, ARRAY_SIZE(plls), clk_data);

	return ret;
}

static const struct of_device_id of_match_clk_mt8365_apmixed[] = {
	{ .compatible = "mediatek,mt8365-apmixedsys" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8365_apmixed);

static struct platform_driver clk_mt8365_apmixed_drv = {
	.probe = clk_mt8365_apmixed_probe,
	.driver = {
		.name = "clk-mt8365-apmixed",
		.of_match_table = of_match_clk_mt8365_apmixed,
	},
};
builtin_platform_driver(clk_mt8365_apmixed_drv)
MODULE_LICENSE("GPL");

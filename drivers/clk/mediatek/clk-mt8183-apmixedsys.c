// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 MediaTek Inc.
 *               Weiyi Lu <weiyi.lu@mediatek.com>
 * Copyright (c) 2023 Collabora, Ltd.
 *               AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <dt-bindings/clock/mt8183-clk.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"
#include "clk-pll.h"

static const struct mtk_gate_regs apmixed_cg_regs = {
	.set_ofs = 0x20,
	.clr_ofs = 0x20,
	.sta_ofs = 0x20,
};

#define GATE_APMIXED_FLAGS(_id, _name, _parent, _shift, _flags)		\
	GATE_MTK_FLAGS(_id, _name, _parent, &apmixed_cg_regs,		\
		       _shift, &mtk_clk_gate_ops_no_setclr_inv, _flags)

#define GATE_APMIXED(_id, _name, _parent, _shift)			\
	GATE_APMIXED_FLAGS(_id, _name, _parent, _shift,	0)

/*
 * CRITICAL CLOCK:
 * apmixed_appll26m is the toppest clock gate of all PLLs.
 */
static const struct mtk_gate apmixed_clks[] = {
	/* AUDIO0 */
	GATE_APMIXED(CLK_APMIXED_SSUSB_26M, "apmixed_ssusb26m", "f_f26m_ck", 4),
	GATE_APMIXED_FLAGS(CLK_APMIXED_APPLL_26M, "apmixed_appll26m",
			   "f_f26m_ck", 5, CLK_IS_CRITICAL),
	GATE_APMIXED(CLK_APMIXED_MIPIC0_26M, "apmixed_mipic026m", "f_f26m_ck", 6),
	GATE_APMIXED(CLK_APMIXED_MDPLLGP_26M, "apmixed_mdpll26m", "f_f26m_ck", 7),
	GATE_APMIXED(CLK_APMIXED_MMSYS_26M, "apmixed_mmsys26m", "f_f26m_ck", 8),
	GATE_APMIXED(CLK_APMIXED_UFS_26M, "apmixed_ufs26m", "f_f26m_ck", 9),
	GATE_APMIXED(CLK_APMIXED_MIPIC1_26M, "apmixed_mipic126m", "f_f26m_ck", 11),
	GATE_APMIXED(CLK_APMIXED_MEMPLL_26M, "apmixed_mempll26m", "f_f26m_ck", 13),
	GATE_APMIXED(CLK_APMIXED_CLKSQ_LVPLL_26M, "apmixed_lvpll26m", "f_f26m_ck", 14),
	GATE_APMIXED(CLK_APMIXED_MIPID0_26M, "apmixed_mipid026m", "f_f26m_ck", 16),
	GATE_APMIXED(CLK_APMIXED_MIPID1_26M, "apmixed_mipid126m", "f_f26m_ck", 17),
};

#define MT8183_PLL_FMAX		(3800UL * MHZ)
#define MT8183_PLL_FMIN		(1500UL * MHZ)

#define PLL_B(_id, _name, _reg, _pwr_reg, _en_mask, _flags,		\
			_rst_bar_mask, _pcwbits, _pcwibits, _pd_reg,	\
			_pd_shift, _tuner_reg,  _tuner_en_reg,		\
			_tuner_en_bit, _pcw_reg, _pcw_shift,		\
			_pcw_chg_reg, _div_table) {			\
		.id = _id,						\
		.name = _name,						\
		.reg = _reg,						\
		.pwr_reg = _pwr_reg,					\
		.en_mask = _en_mask,					\
		.flags = _flags,					\
		.rst_bar_mask = _rst_bar_mask,				\
		.fmax = MT8183_PLL_FMAX,				\
		.fmin = MT8183_PLL_FMIN,				\
		.pcwbits = _pcwbits,					\
		.pcwibits = _pcwibits,					\
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

#define PLL(_id, _name, _reg, _pwr_reg, _en_mask, _flags,		\
			_rst_bar_mask, _pcwbits, _pcwibits, _pd_reg,	\
			_pd_shift, _tuner_reg, _tuner_en_reg,		\
			_tuner_en_bit, _pcw_reg, _pcw_shift,		\
			_pcw_chg_reg)					\
		PLL_B(_id, _name, _reg, _pwr_reg, _en_mask, _flags,	\
			_rst_bar_mask, _pcwbits, _pcwibits, _pd_reg,	\
			_pd_shift, _tuner_reg, _tuner_en_reg,		\
			_tuner_en_bit, _pcw_reg, _pcw_shift,		\
			_pcw_chg_reg, NULL)

static const struct mtk_pll_div_table armpll_div_table[] = {
	{ .div = 0, .freq = MT8183_PLL_FMAX },
	{ .div = 1, .freq = 1500 * MHZ },
	{ .div = 2, .freq = 750 * MHZ },
	{ .div = 3, .freq = 375 * MHZ },
	{ .div = 4, .freq = 187500000 },
	{ /* sentinel */ }
};

static const struct mtk_pll_div_table mfgpll_div_table[] = {
	{ .div = 0, .freq = MT8183_PLL_FMAX },
	{ .div = 1, .freq = 1600 * MHZ },
	{ .div = 2, .freq = 800 * MHZ },
	{ .div = 3, .freq = 400 * MHZ },
	{ .div = 4, .freq = 200 * MHZ },
	{ /* sentinel */ }
};

static const struct mtk_pll_data plls[] = {
	PLL_B(CLK_APMIXED_ARMPLL_LL, "armpll_ll", 0x0200, 0x020C, 0,
	      HAVE_RST_BAR | PLL_AO, BIT(24), 22, 8, 0x0204, 24, 0x0, 0x0, 0,
	      0x0204, 0, 0, armpll_div_table),
	PLL_B(CLK_APMIXED_ARMPLL_L, "armpll_l", 0x0210, 0x021C, 0,
	      HAVE_RST_BAR | PLL_AO, BIT(24), 22, 8, 0x0214, 24, 0x0, 0x0, 0,
	      0x0214, 0, 0, armpll_div_table),
	PLL(CLK_APMIXED_CCIPLL, "ccipll", 0x0290, 0x029C, 0,
	    HAVE_RST_BAR | PLL_AO, BIT(24), 22, 8, 0x0294, 24, 0x0, 0x0, 0,
	    0x0294, 0, 0),
	PLL(CLK_APMIXED_MAINPLL, "mainpll", 0x0220, 0x022C, 0,
	    HAVE_RST_BAR, BIT(24), 22, 8, 0x0224, 24, 0x0, 0x0, 0,
	    0x0224, 0, 0),
	PLL(CLK_APMIXED_UNIV2PLL, "univ2pll", 0x0230, 0x023C, 0,
	    HAVE_RST_BAR, BIT(24), 22, 8, 0x0234, 24, 0x0, 0x0, 0,
	    0x0234, 0, 0),
	PLL_B(CLK_APMIXED_MFGPLL, "mfgpll", 0x0240, 0x024C, 0,
	      0, 0, 22, 8, 0x0244, 24, 0x0, 0x0, 0, 0x0244, 0, 0,
	      mfgpll_div_table),
	PLL(CLK_APMIXED_MSDCPLL, "msdcpll", 0x0250, 0x025C, 0,
	    0, 0, 22, 8, 0x0254, 24, 0x0, 0x0, 0, 0x0254, 0, 0),
	PLL(CLK_APMIXED_TVDPLL, "tvdpll", 0x0260, 0x026C, 0,
	    0, 0, 22, 8, 0x0264, 24, 0x0, 0x0, 0, 0x0264, 0, 0),
	PLL(CLK_APMIXED_MMPLL, "mmpll", 0x0270, 0x027C, 0,
	    HAVE_RST_BAR, BIT(23), 22, 8, 0x0274, 24, 0x0, 0x0, 0,
	    0x0274, 0, 0),
	PLL(CLK_APMIXED_APLL1, "apll1", 0x02A0, 0x02B0, 0,
	    0, 0, 32, 8, 0x02A0, 1, 0x02A8, 0x0014, 0, 0x02A4, 0, 0x02A0),
	PLL(CLK_APMIXED_APLL2, "apll2", 0x02b4, 0x02c4, 0,
	    0, 0, 32, 8, 0x02B4, 1, 0x02BC, 0x0014, 1, 0x02B8, 0, 0x02B4),
};

static int clk_mt8183_apmixed_probe(struct platform_device *pdev)
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

static const struct of_device_id of_match_clk_mt8183_apmixed[] = {
	{ .compatible = "mediatek,mt8183-apmixedsys" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8183_apmixed);

static struct platform_driver clk_mt8183_apmixed_drv = {
	.probe = clk_mt8183_apmixed_probe,
	.driver = {
		.name = "clk-mt8183-apmixed",
		.of_match_table = of_match_clk_mt8183_apmixed,
	},
};
builtin_platform_driver(clk_mt8183_apmixed_drv)
MODULE_LICENSE("GPL");

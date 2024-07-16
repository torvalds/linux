// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Jie Qiu <jie.qiu@mediatek.com>
 */

#include "phy-mtk-hdmi.h"
#include "phy-mtk-io.h"

#define HDMI_CON0		0x00
#define RG_HDMITX_PLL_EN		BIT(31)
#define RG_HDMITX_PLL_FBKDIV		GENMASK(30, 24)
#define RG_HDMITX_PLL_FBKSEL		GENMASK(23, 22)
#define RG_HDMITX_PLL_PREDIV		GENMASK(21, 20)
#define RG_HDMITX_PLL_POSDIV		GENMASK(19, 18)
#define RG_HDMITX_PLL_RST_DLY		GENMASK(17, 16)
#define RG_HDMITX_PLL_IR		GENMASK(15, 12)
#define RG_HDMITX_PLL_IC		GENMASK(11, 8)
#define RG_HDMITX_PLL_BP		GENMASK(7, 4)
#define RG_HDMITX_PLL_BR		GENMASK(3, 2)
#define RG_HDMITX_PLL_BC		GENMASK(1, 0)
#define HDMI_CON1		0x04
#define RG_HDMITX_PLL_DIVEN		GENMASK(31, 29)
#define RG_HDMITX_PLL_AUTOK_EN		BIT(28)
#define RG_HDMITX_PLL_AUTOK_KF		GENMASK(27, 26)
#define RG_HDMITX_PLL_AUTOK_KS		GENMASK(25, 24)
#define RG_HDMITX_PLL_AUTOK_LOAD	BIT(23)
#define RG_HDMITX_PLL_BAND		GENMASK(21, 16)
#define RG_HDMITX_PLL_REF_SEL		BIT(15)
#define RG_HDMITX_PLL_BIAS_EN		BIT(14)
#define RG_HDMITX_PLL_BIAS_LPF_EN	BIT(13)
#define RG_HDMITX_PLL_TXDIV_EN		BIT(12)
#define RG_HDMITX_PLL_TXDIV		GENMASK(11, 10)
#define RG_HDMITX_PLL_LVROD_EN		BIT(9)
#define RG_HDMITX_PLL_MONVC_EN		BIT(8)
#define RG_HDMITX_PLL_MONCK_EN		BIT(7)
#define RG_HDMITX_PLL_MONREF_EN		BIT(6)
#define RG_HDMITX_PLL_TST_EN		BIT(5)
#define RG_HDMITX_PLL_TST_CK_EN		BIT(4)
#define RG_HDMITX_PLL_TST_SEL		GENMASK(3, 0)
#define HDMI_CON2		0x08
#define RGS_HDMITX_PLL_AUTOK_BAND	GENMASK(14, 8)
#define RGS_HDMITX_PLL_AUTOK_FAIL	BIT(1)
#define RG_HDMITX_EN_TX_CKLDO		BIT(0)
#define HDMI_CON3		0x0c
#define RG_HDMITX_SER_EN		GENMASK(31, 28)
#define RG_HDMITX_PRD_EN		GENMASK(27, 24)
#define RG_HDMITX_PRD_IMP_EN		GENMASK(23, 20)
#define RG_HDMITX_DRV_EN		GENMASK(19, 16)
#define RG_HDMITX_DRV_IMP_EN		GENMASK(15, 12)
#define RG_HDMITX_MHLCK_FORCE		BIT(10)
#define RG_HDMITX_MHLCK_PPIX_EN		BIT(9)
#define RG_HDMITX_MHLCK_EN		BIT(8)
#define RG_HDMITX_SER_DIN_SEL		GENMASK(7, 4)
#define RG_HDMITX_SER_5T1_BIST_EN	BIT(3)
#define RG_HDMITX_SER_BIST_TOG		BIT(2)
#define RG_HDMITX_SER_DIN_TOG		BIT(1)
#define RG_HDMITX_SER_CLKDIG_INV	BIT(0)
#define HDMI_CON4		0x10
#define RG_HDMITX_PRD_IBIAS_CLK		GENMASK(27, 24)
#define RG_HDMITX_PRD_IBIAS_D2		GENMASK(19, 16)
#define RG_HDMITX_PRD_IBIAS_D1		GENMASK(11, 8)
#define RG_HDMITX_PRD_IBIAS_D0		GENMASK(3, 0)
#define HDMI_CON5		0x14
#define RG_HDMITX_DRV_IBIAS_CLK		GENMASK(29, 24)
#define RG_HDMITX_DRV_IBIAS_D2		GENMASK(21, 16)
#define RG_HDMITX_DRV_IBIAS_D1		GENMASK(13, 8)
#define RG_HDMITX_DRV_IBIAS_D0		GENMASK(5, 0)
#define HDMI_CON6		0x18
#define RG_HDMITX_DRV_IMP_CLK		GENMASK(29, 24)
#define RG_HDMITX_DRV_IMP_D2		GENMASK(21, 16)
#define RG_HDMITX_DRV_IMP_D1		GENMASK(13, 8)
#define RG_HDMITX_DRV_IMP_D0		GENMASK(5, 0)
#define HDMI_CON7		0x1c
#define RG_HDMITX_MHLCK_DRV_IBIAS	GENMASK(31, 27)
#define RG_HDMITX_SER_DIN		GENMASK(25, 16)
#define RG_HDMITX_CHLDC_TST		GENMASK(15, 12)
#define RG_HDMITX_CHLCK_TST		GENMASK(11, 8)
#define RG_HDMITX_RESERVE		GENMASK(7, 0)
#define HDMI_CON8		0x20
#define RGS_HDMITX_2T1_LEV		GENMASK(19, 16)
#define RGS_HDMITX_2T1_EDG		GENMASK(15, 12)
#define RGS_HDMITX_5T1_LEV		GENMASK(11, 8)
#define RGS_HDMITX_5T1_EDG		GENMASK(7, 4)
#define RGS_HDMITX_PLUG_TST		BIT(0)

static int mtk_hdmi_pll_prepare(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);
	void __iomem *base = hdmi_phy->regs;

	mtk_phy_set_bits(base + HDMI_CON1, RG_HDMITX_PLL_AUTOK_EN);
	mtk_phy_set_bits(base + HDMI_CON0, RG_HDMITX_PLL_POSDIV);
	mtk_phy_clear_bits(base + HDMI_CON3, RG_HDMITX_MHLCK_EN);
	mtk_phy_set_bits(base + HDMI_CON1, RG_HDMITX_PLL_BIAS_EN);
	usleep_range(100, 150);
	mtk_phy_set_bits(base + HDMI_CON0, RG_HDMITX_PLL_EN);
	usleep_range(100, 150);
	mtk_phy_set_bits(base + HDMI_CON1, RG_HDMITX_PLL_BIAS_LPF_EN);
	mtk_phy_set_bits(base + HDMI_CON1, RG_HDMITX_PLL_TXDIV_EN);

	return 0;
}

static void mtk_hdmi_pll_unprepare(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);
	void __iomem *base = hdmi_phy->regs;

	mtk_phy_clear_bits(base + HDMI_CON1, RG_HDMITX_PLL_TXDIV_EN);
	mtk_phy_clear_bits(base + HDMI_CON1, RG_HDMITX_PLL_BIAS_LPF_EN);
	usleep_range(100, 150);
	mtk_phy_clear_bits(base + HDMI_CON0, RG_HDMITX_PLL_EN);
	usleep_range(100, 150);
	mtk_phy_clear_bits(base + HDMI_CON1, RG_HDMITX_PLL_BIAS_EN);
	mtk_phy_clear_bits(base + HDMI_CON0, RG_HDMITX_PLL_POSDIV);
	mtk_phy_clear_bits(base + HDMI_CON1, RG_HDMITX_PLL_AUTOK_EN);
	usleep_range(100, 150);
}

static long mtk_hdmi_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long *parent_rate)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);

	hdmi_phy->pll_rate = rate;
	if (rate <= 74250000)
		*parent_rate = rate;
	else
		*parent_rate = rate / 2;

	return rate;
}

static int mtk_hdmi_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);
	void __iomem *base = hdmi_phy->regs;
	unsigned int pre_div;
	unsigned int div;
	unsigned int pre_ibias;
	unsigned int hdmi_ibias;
	unsigned int imp_en;

	dev_dbg(hdmi_phy->dev, "%s: %lu Hz, parent: %lu Hz\n", __func__,
		rate, parent_rate);

	if (rate <= 27000000) {
		pre_div = 0;
		div = 3;
	} else if (rate <= 74250000) {
		pre_div = 1;
		div = 2;
	} else {
		pre_div = 1;
		div = 1;
	}

	mtk_phy_update_field(base + HDMI_CON0, RG_HDMITX_PLL_PREDIV, pre_div);
	mtk_phy_set_bits(base + HDMI_CON0, RG_HDMITX_PLL_POSDIV);
	mtk_phy_update_bits(base + HDMI_CON0,
			    RG_HDMITX_PLL_IC | RG_HDMITX_PLL_IR,
			    FIELD_PREP(RG_HDMITX_PLL_IC, 0x1) |
			    FIELD_PREP(RG_HDMITX_PLL_IR, 0x1));
	mtk_phy_update_field(base + HDMI_CON1, RG_HDMITX_PLL_TXDIV, div);
	mtk_phy_update_bits(base + HDMI_CON0,
			    RG_HDMITX_PLL_FBKSEL | RG_HDMITX_PLL_FBKDIV,
			    FIELD_PREP(RG_HDMITX_PLL_FBKSEL, 0x1) |
			    FIELD_PREP(RG_HDMITX_PLL_FBKDIV, 19));
	mtk_phy_update_field(base + HDMI_CON1, RG_HDMITX_PLL_DIVEN, 0x2);
	mtk_phy_update_bits(base + HDMI_CON0,
			    RG_HDMITX_PLL_BP | RG_HDMITX_PLL_BC |
			    RG_HDMITX_PLL_BR,
			    FIELD_PREP(RG_HDMITX_PLL_BP, 0xc) |
			    FIELD_PREP(RG_HDMITX_PLL_BC, 0x2) |
			    FIELD_PREP(RG_HDMITX_PLL_BR, 0x1));
	if (rate < 165000000) {
		mtk_phy_clear_bits(base + HDMI_CON3, RG_HDMITX_PRD_IMP_EN);
		pre_ibias = 0x3;
		imp_en = 0x0;
		hdmi_ibias = hdmi_phy->ibias;
	} else {
		mtk_phy_set_bits(base + HDMI_CON3, RG_HDMITX_PRD_IMP_EN);
		pre_ibias = 0x6;
		imp_en = 0xf;
		hdmi_ibias = hdmi_phy->ibias_up;
	}
	mtk_phy_update_bits(base + HDMI_CON4,
			    RG_HDMITX_PRD_IBIAS_CLK | RG_HDMITX_PRD_IBIAS_D2 |
			    RG_HDMITX_PRD_IBIAS_D1 | RG_HDMITX_PRD_IBIAS_D0,
			    FIELD_PREP(RG_HDMITX_PRD_IBIAS_CLK, pre_ibias) |
			    FIELD_PREP(RG_HDMITX_PRD_IBIAS_D2, pre_ibias) |
			    FIELD_PREP(RG_HDMITX_PRD_IBIAS_D1, pre_ibias) |
			    FIELD_PREP(RG_HDMITX_PRD_IBIAS_D0, pre_ibias));
	mtk_phy_update_field(base + HDMI_CON3, RG_HDMITX_DRV_IMP_EN, imp_en);
	mtk_phy_update_bits(base + HDMI_CON6,
			    RG_HDMITX_DRV_IMP_CLK | RG_HDMITX_DRV_IMP_D2 |
			    RG_HDMITX_DRV_IMP_D1 | RG_HDMITX_DRV_IMP_D0,
			    FIELD_PREP(RG_HDMITX_DRV_IMP_CLK, hdmi_phy->drv_imp_clk) |
			    FIELD_PREP(RG_HDMITX_DRV_IMP_D2, hdmi_phy->drv_imp_d2) |
			    FIELD_PREP(RG_HDMITX_DRV_IMP_D1, hdmi_phy->drv_imp_d1) |
			    FIELD_PREP(RG_HDMITX_DRV_IMP_D0, hdmi_phy->drv_imp_d0));
	mtk_phy_update_bits(base + HDMI_CON5,
			    RG_HDMITX_DRV_IBIAS_CLK | RG_HDMITX_DRV_IBIAS_D2 |
			    RG_HDMITX_DRV_IBIAS_D1 | RG_HDMITX_DRV_IBIAS_D0,
			    FIELD_PREP(RG_HDMITX_DRV_IBIAS_CLK, hdmi_ibias) |
			    FIELD_PREP(RG_HDMITX_DRV_IBIAS_D2, hdmi_ibias) |
			    FIELD_PREP(RG_HDMITX_DRV_IBIAS_D1, hdmi_ibias) |
			    FIELD_PREP(RG_HDMITX_DRV_IBIAS_D0, hdmi_ibias));
	return 0;
}

static unsigned long mtk_hdmi_pll_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);

	return hdmi_phy->pll_rate;
}

static const struct clk_ops mtk_hdmi_phy_pll_ops = {
	.prepare = mtk_hdmi_pll_prepare,
	.unprepare = mtk_hdmi_pll_unprepare,
	.set_rate = mtk_hdmi_pll_set_rate,
	.round_rate = mtk_hdmi_pll_round_rate,
	.recalc_rate = mtk_hdmi_pll_recalc_rate,
};

static void mtk_hdmi_phy_enable_tmds(struct mtk_hdmi_phy *hdmi_phy)
{
	mtk_phy_set_bits(hdmi_phy->regs + HDMI_CON3,
			 RG_HDMITX_SER_EN | RG_HDMITX_PRD_EN |
			 RG_HDMITX_DRV_EN);
	usleep_range(100, 150);
}

static void mtk_hdmi_phy_disable_tmds(struct mtk_hdmi_phy *hdmi_phy)
{
	mtk_phy_clear_bits(hdmi_phy->regs + HDMI_CON3,
			   RG_HDMITX_DRV_EN | RG_HDMITX_PRD_EN |
			   RG_HDMITX_SER_EN);
}

struct mtk_hdmi_phy_conf mtk_hdmi_phy_8173_conf = {
	.flags = CLK_SET_RATE_PARENT | CLK_SET_RATE_GATE,
	.hdmi_phy_clk_ops = &mtk_hdmi_phy_pll_ops,
	.hdmi_phy_enable_tmds = mtk_hdmi_phy_enable_tmds,
	.hdmi_phy_disable_tmds = mtk_hdmi_phy_disable_tmds,
};

MODULE_AUTHOR("Jie Qiu <jie.qiu@mediatek.com>");
MODULE_DESCRIPTION("MediaTek MT8173 HDMI PHY Driver");
MODULE_LICENSE("GPL v2");

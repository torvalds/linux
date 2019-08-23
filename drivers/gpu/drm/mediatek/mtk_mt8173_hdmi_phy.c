// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Jie Qiu <jie.qiu@mediatek.com>
 */

#include "mtk_hdmi_phy.h"

#define HDMI_CON0		0x00
#define RG_HDMITX_PLL_EN		BIT(31)
#define RG_HDMITX_PLL_FBKDIV		(0x7f << 24)
#define PLL_FBKDIV_SHIFT		24
#define RG_HDMITX_PLL_FBKSEL		(0x3 << 22)
#define PLL_FBKSEL_SHIFT		22
#define RG_HDMITX_PLL_PREDIV		(0x3 << 20)
#define PREDIV_SHIFT			20
#define RG_HDMITX_PLL_POSDIV		(0x3 << 18)
#define POSDIV_SHIFT			18
#define RG_HDMITX_PLL_RST_DLY		(0x3 << 16)
#define RG_HDMITX_PLL_IR		(0xf << 12)
#define PLL_IR_SHIFT			12
#define RG_HDMITX_PLL_IC		(0xf << 8)
#define PLL_IC_SHIFT			8
#define RG_HDMITX_PLL_BP		(0xf << 4)
#define PLL_BP_SHIFT			4
#define RG_HDMITX_PLL_BR		(0x3 << 2)
#define PLL_BR_SHIFT			2
#define RG_HDMITX_PLL_BC		(0x3 << 0)
#define PLL_BC_SHIFT			0
#define HDMI_CON1		0x04
#define RG_HDMITX_PLL_DIVEN		(0x7 << 29)
#define PLL_DIVEN_SHIFT			29
#define RG_HDMITX_PLL_AUTOK_EN		BIT(28)
#define RG_HDMITX_PLL_AUTOK_KF		(0x3 << 26)
#define RG_HDMITX_PLL_AUTOK_KS		(0x3 << 24)
#define RG_HDMITX_PLL_AUTOK_LOAD	BIT(23)
#define RG_HDMITX_PLL_BAND		(0x3f << 16)
#define RG_HDMITX_PLL_REF_SEL		BIT(15)
#define RG_HDMITX_PLL_BIAS_EN		BIT(14)
#define RG_HDMITX_PLL_BIAS_LPF_EN	BIT(13)
#define RG_HDMITX_PLL_TXDIV_EN		BIT(12)
#define RG_HDMITX_PLL_TXDIV		(0x3 << 10)
#define PLL_TXDIV_SHIFT			10
#define RG_HDMITX_PLL_LVROD_EN		BIT(9)
#define RG_HDMITX_PLL_MONVC_EN		BIT(8)
#define RG_HDMITX_PLL_MONCK_EN		BIT(7)
#define RG_HDMITX_PLL_MONREF_EN		BIT(6)
#define RG_HDMITX_PLL_TST_EN		BIT(5)
#define RG_HDMITX_PLL_TST_CK_EN		BIT(4)
#define RG_HDMITX_PLL_TST_SEL		(0xf << 0)
#define HDMI_CON2		0x08
#define RGS_HDMITX_PLL_AUTOK_BAND	(0x7f << 8)
#define RGS_HDMITX_PLL_AUTOK_FAIL	BIT(1)
#define RG_HDMITX_EN_TX_CKLDO		BIT(0)
#define HDMI_CON3		0x0c
#define RG_HDMITX_SER_EN		(0xf << 28)
#define RG_HDMITX_PRD_EN		(0xf << 24)
#define RG_HDMITX_PRD_IMP_EN		(0xf << 20)
#define RG_HDMITX_DRV_EN		(0xf << 16)
#define RG_HDMITX_DRV_IMP_EN		(0xf << 12)
#define DRV_IMP_EN_SHIFT		12
#define RG_HDMITX_MHLCK_FORCE		BIT(10)
#define RG_HDMITX_MHLCK_PPIX_EN		BIT(9)
#define RG_HDMITX_MHLCK_EN		BIT(8)
#define RG_HDMITX_SER_DIN_SEL		(0xf << 4)
#define RG_HDMITX_SER_5T1_BIST_EN	BIT(3)
#define RG_HDMITX_SER_BIST_TOG		BIT(2)
#define RG_HDMITX_SER_DIN_TOG		BIT(1)
#define RG_HDMITX_SER_CLKDIG_INV	BIT(0)
#define HDMI_CON4		0x10
#define RG_HDMITX_PRD_IBIAS_CLK		(0xf << 24)
#define RG_HDMITX_PRD_IBIAS_D2		(0xf << 16)
#define RG_HDMITX_PRD_IBIAS_D1		(0xf << 8)
#define RG_HDMITX_PRD_IBIAS_D0		(0xf << 0)
#define PRD_IBIAS_CLK_SHIFT		24
#define PRD_IBIAS_D2_SHIFT		16
#define PRD_IBIAS_D1_SHIFT		8
#define PRD_IBIAS_D0_SHIFT		0
#define HDMI_CON5		0x14
#define RG_HDMITX_DRV_IBIAS_CLK		(0x3f << 24)
#define RG_HDMITX_DRV_IBIAS_D2		(0x3f << 16)
#define RG_HDMITX_DRV_IBIAS_D1		(0x3f << 8)
#define RG_HDMITX_DRV_IBIAS_D0		(0x3f << 0)
#define DRV_IBIAS_CLK_SHIFT		24
#define DRV_IBIAS_D2_SHIFT		16
#define DRV_IBIAS_D1_SHIFT		8
#define DRV_IBIAS_D0_SHIFT		0
#define HDMI_CON6		0x18
#define RG_HDMITX_DRV_IMP_CLK		(0x3f << 24)
#define RG_HDMITX_DRV_IMP_D2		(0x3f << 16)
#define RG_HDMITX_DRV_IMP_D1		(0x3f << 8)
#define RG_HDMITX_DRV_IMP_D0		(0x3f << 0)
#define DRV_IMP_CLK_SHIFT		24
#define DRV_IMP_D2_SHIFT		16
#define DRV_IMP_D1_SHIFT		8
#define DRV_IMP_D0_SHIFT		0
#define HDMI_CON7		0x1c
#define RG_HDMITX_MHLCK_DRV_IBIAS	(0x1f << 27)
#define RG_HDMITX_SER_DIN		(0x3ff << 16)
#define RG_HDMITX_CHLDC_TST		(0xf << 12)
#define RG_HDMITX_CHLCK_TST		(0xf << 8)
#define RG_HDMITX_RESERVE		(0xff << 0)
#define HDMI_CON8		0x20
#define RGS_HDMITX_2T1_LEV		(0xf << 16)
#define RGS_HDMITX_2T1_EDG		(0xf << 12)
#define RGS_HDMITX_5T1_LEV		(0xf << 8)
#define RGS_HDMITX_5T1_EDG		(0xf << 4)
#define RGS_HDMITX_PLUG_TST		BIT(0)

static const u8 PREDIV[3][4] = {
	{0x0, 0x0, 0x0, 0x0},	/* 27Mhz */
	{0x1, 0x1, 0x1, 0x1},	/* 74Mhz */
	{0x1, 0x1, 0x1, 0x1}	/* 148Mhz */
};

static const u8 TXDIV[3][4] = {
	{0x3, 0x3, 0x3, 0x2},	/* 27Mhz */
	{0x2, 0x1, 0x1, 0x1},	/* 74Mhz */
	{0x1, 0x0, 0x0, 0x0}	/* 148Mhz */
};

static const u8 FBKSEL[3][4] = {
	{0x1, 0x1, 0x1, 0x1},	/* 27Mhz */
	{0x1, 0x0, 0x1, 0x1},	/* 74Mhz */
	{0x1, 0x0, 0x1, 0x1}	/* 148Mhz */
};

static const u8 FBKDIV[3][4] = {
	{19, 24, 29, 19},	/* 27Mhz */
	{19, 24, 14, 19},	/* 74Mhz */
	{19, 24, 14, 19}	/* 148Mhz */
};

static const u8 DIVEN[3][4] = {
	{0x2, 0x1, 0x1, 0x2},	/* 27Mhz */
	{0x2, 0x2, 0x2, 0x2},	/* 74Mhz */
	{0x2, 0x2, 0x2, 0x2}	/* 148Mhz */
};

static const u8 HTPLLBP[3][4] = {
	{0xc, 0xc, 0x8, 0xc},	/* 27Mhz */
	{0xc, 0xf, 0xf, 0xc},	/* 74Mhz */
	{0xc, 0xf, 0xf, 0xc}	/* 148Mhz */
};

static const u8 HTPLLBC[3][4] = {
	{0x2, 0x3, 0x3, 0x2},	/* 27Mhz */
	{0x2, 0x3, 0x3, 0x2},	/* 74Mhz */
	{0x2, 0x3, 0x3, 0x2}	/* 148Mhz */
};

static const u8 HTPLLBR[3][4] = {
	{0x1, 0x1, 0x0, 0x1},	/* 27Mhz */
	{0x1, 0x2, 0x2, 0x1},	/* 74Mhz */
	{0x1, 0x2, 0x2, 0x1}	/* 148Mhz */
};

static int mtk_hdmi_pll_prepare(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);

	dev_dbg(hdmi_phy->dev, "%s\n", __func__);

	mtk_hdmi_phy_set_bits(hdmi_phy, HDMI_CON1, RG_HDMITX_PLL_AUTOK_EN);
	mtk_hdmi_phy_set_bits(hdmi_phy, HDMI_CON0, RG_HDMITX_PLL_POSDIV);
	mtk_hdmi_phy_clear_bits(hdmi_phy, HDMI_CON3, RG_HDMITX_MHLCK_EN);
	mtk_hdmi_phy_set_bits(hdmi_phy, HDMI_CON1, RG_HDMITX_PLL_BIAS_EN);
	usleep_range(100, 150);
	mtk_hdmi_phy_set_bits(hdmi_phy, HDMI_CON0, RG_HDMITX_PLL_EN);
	usleep_range(100, 150);
	mtk_hdmi_phy_set_bits(hdmi_phy, HDMI_CON1, RG_HDMITX_PLL_BIAS_LPF_EN);
	mtk_hdmi_phy_set_bits(hdmi_phy, HDMI_CON1, RG_HDMITX_PLL_TXDIV_EN);

	return 0;
}

static void mtk_hdmi_pll_unprepare(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);

	dev_dbg(hdmi_phy->dev, "%s\n", __func__);

	mtk_hdmi_phy_clear_bits(hdmi_phy, HDMI_CON1, RG_HDMITX_PLL_TXDIV_EN);
	mtk_hdmi_phy_clear_bits(hdmi_phy, HDMI_CON1, RG_HDMITX_PLL_BIAS_LPF_EN);
	usleep_range(100, 150);
	mtk_hdmi_phy_clear_bits(hdmi_phy, HDMI_CON0, RG_HDMITX_PLL_EN);
	usleep_range(100, 150);
	mtk_hdmi_phy_clear_bits(hdmi_phy, HDMI_CON1, RG_HDMITX_PLL_BIAS_EN);
	mtk_hdmi_phy_clear_bits(hdmi_phy, HDMI_CON0, RG_HDMITX_PLL_POSDIV);
	mtk_hdmi_phy_clear_bits(hdmi_phy, HDMI_CON1, RG_HDMITX_PLL_AUTOK_EN);
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

	mtk_hdmi_phy_mask(hdmi_phy, HDMI_CON0,
			  (pre_div << PREDIV_SHIFT), RG_HDMITX_PLL_PREDIV);
	mtk_hdmi_phy_set_bits(hdmi_phy, HDMI_CON0, RG_HDMITX_PLL_POSDIV);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_CON0,
			  (0x1 << PLL_IC_SHIFT) | (0x1 << PLL_IR_SHIFT),
			  RG_HDMITX_PLL_IC | RG_HDMITX_PLL_IR);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_CON1,
			  (div << PLL_TXDIV_SHIFT), RG_HDMITX_PLL_TXDIV);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_CON0,
			  (0x1 << PLL_FBKSEL_SHIFT) | (19 << PLL_FBKDIV_SHIFT),
			  RG_HDMITX_PLL_FBKSEL | RG_HDMITX_PLL_FBKDIV);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_CON1,
			  (0x2 << PLL_DIVEN_SHIFT), RG_HDMITX_PLL_DIVEN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_CON0,
			  (0xc << PLL_BP_SHIFT) | (0x2 << PLL_BC_SHIFT) |
			  (0x1 << PLL_BR_SHIFT),
			  RG_HDMITX_PLL_BP | RG_HDMITX_PLL_BC |
			  RG_HDMITX_PLL_BR);
	if (rate < 165000000) {
		mtk_hdmi_phy_clear_bits(hdmi_phy, HDMI_CON3,
					RG_HDMITX_PRD_IMP_EN);
		pre_ibias = 0x3;
		imp_en = 0x0;
		hdmi_ibias = hdmi_phy->ibias;
	} else {
		mtk_hdmi_phy_set_bits(hdmi_phy, HDMI_CON3,
				      RG_HDMITX_PRD_IMP_EN);
		pre_ibias = 0x6;
		imp_en = 0xf;
		hdmi_ibias = hdmi_phy->ibias_up;
	}
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_CON4,
			  (pre_ibias << PRD_IBIAS_CLK_SHIFT) |
			  (pre_ibias << PRD_IBIAS_D2_SHIFT) |
			  (pre_ibias << PRD_IBIAS_D1_SHIFT) |
			  (pre_ibias << PRD_IBIAS_D0_SHIFT),
			  RG_HDMITX_PRD_IBIAS_CLK |
			  RG_HDMITX_PRD_IBIAS_D2 |
			  RG_HDMITX_PRD_IBIAS_D1 |
			  RG_HDMITX_PRD_IBIAS_D0);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_CON3,
			  (imp_en << DRV_IMP_EN_SHIFT),
			  RG_HDMITX_DRV_IMP_EN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_CON6,
			  (hdmi_phy->drv_imp_clk << DRV_IMP_CLK_SHIFT) |
			  (hdmi_phy->drv_imp_d2 << DRV_IMP_D2_SHIFT) |
			  (hdmi_phy->drv_imp_d1 << DRV_IMP_D1_SHIFT) |
			  (hdmi_phy->drv_imp_d0 << DRV_IMP_D0_SHIFT),
			  RG_HDMITX_DRV_IMP_CLK | RG_HDMITX_DRV_IMP_D2 |
			  RG_HDMITX_DRV_IMP_D1 | RG_HDMITX_DRV_IMP_D0);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_CON5,
			  (hdmi_ibias << DRV_IBIAS_CLK_SHIFT) |
			  (hdmi_ibias << DRV_IBIAS_D2_SHIFT) |
			  (hdmi_ibias << DRV_IBIAS_D1_SHIFT) |
			  (hdmi_ibias << DRV_IBIAS_D0_SHIFT),
			  RG_HDMITX_DRV_IBIAS_CLK |
			  RG_HDMITX_DRV_IBIAS_D2 |
			  RG_HDMITX_DRV_IBIAS_D1 |
			  RG_HDMITX_DRV_IBIAS_D0);
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
	mtk_hdmi_phy_set_bits(hdmi_phy, HDMI_CON3,
			      RG_HDMITX_SER_EN | RG_HDMITX_PRD_EN |
			      RG_HDMITX_DRV_EN);
	usleep_range(100, 150);
}

static void mtk_hdmi_phy_disable_tmds(struct mtk_hdmi_phy *hdmi_phy)
{
	mtk_hdmi_phy_clear_bits(hdmi_phy, HDMI_CON3,
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

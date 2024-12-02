// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 MediaTek Inc.
 * Author: Chunhui Dai <chunhui.dai@mediatek.com>
 */

#include "phy-mtk-hdmi.h"
#include "phy-mtk-io.h"

#define HDMI_CON0	0x00
#define RG_HDMITX_DRV_IBIAS_MASK	GENMASK(5, 0)
#define RG_HDMITX_EN_SER_MASK		GENMASK(15, 12)
#define RG_HDMITX_EN_SLDO_MASK		GENMASK(19, 16)
#define RG_HDMITX_EN_PRED_MASK		GENMASK(23, 20)
#define RG_HDMITX_EN_IMP_MASK		GENMASK(27, 24)
#define RG_HDMITX_EN_DRV_MASK		GENMASK(31, 28)

#define HDMI_CON1	0x04
#define RG_HDMITX_PRED_IBIAS_MASK	GENMASK(21, 18)
#define RG_HDMITX_PRED_IMP		BIT(22)
#define RG_HDMITX_DRV_IMP_MASK		GENMASK(31, 26)

#define HDMI_CON2	0x08
#define RG_HDMITX_EN_TX_CKLDO		BIT(0)
#define RG_HDMITX_EN_TX_POSDIV		BIT(1)
#define RG_HDMITX_TX_POSDIV_MASK	GENMASK(4, 3)
#define RG_HDMITX_EN_MBIAS		BIT(6)
#define RG_HDMITX_MBIAS_LPF_EN		BIT(7)

#define HDMI_CON4	0x10
#define RG_HDMITX_RESERVE_MASK		GENMASK(31, 0)

#define HDMI_CON6	0x18
#define RG_HTPLL_BR_MASK		GENMASK(1, 0)
#define RG_HTPLL_BC_MASK		GENMASK(3, 2)
#define RG_HTPLL_BP_MASK		GENMASK(7, 4)
#define RG_HTPLL_IR_MASK		GENMASK(11, 8)
#define RG_HTPLL_IC_MASK		GENMASK(15, 12)
#define RG_HTPLL_POSDIV_MASK		GENMASK(17, 16)
#define RG_HTPLL_PREDIV_MASK		GENMASK(19, 18)
#define RG_HTPLL_FBKSEL_MASK		GENMASK(21, 20)
#define RG_HTPLL_RLH_EN			BIT(22)
#define RG_HTPLL_FBKDIV_MASK		GENMASK(30, 24)
#define RG_HTPLL_EN			BIT(31)

#define HDMI_CON7	0x1c
#define RG_HTPLL_AUTOK_EN		BIT(23)
#define RG_HTPLL_DIVEN_MASK		GENMASK(30, 28)

static int mtk_hdmi_pll_prepare(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);
	void __iomem *base = hdmi_phy->regs;

	mtk_phy_set_bits(base + HDMI_CON7, RG_HTPLL_AUTOK_EN);
	mtk_phy_clear_bits(base + HDMI_CON6, RG_HTPLL_RLH_EN);
	mtk_phy_set_bits(base + HDMI_CON6, RG_HTPLL_POSDIV_MASK);
	mtk_phy_set_bits(base + HDMI_CON2, RG_HDMITX_EN_MBIAS);
	usleep_range(80, 100);
	mtk_phy_set_bits(base + HDMI_CON6, RG_HTPLL_EN);
	mtk_phy_set_bits(base + HDMI_CON2, RG_HDMITX_EN_TX_CKLDO);
	mtk_phy_set_bits(base + HDMI_CON0, RG_HDMITX_EN_SLDO_MASK);
	usleep_range(80, 100);
	mtk_phy_set_bits(base + HDMI_CON2, RG_HDMITX_MBIAS_LPF_EN);
	mtk_phy_set_bits(base + HDMI_CON0, RG_HDMITX_EN_SER_MASK);
	mtk_phy_set_bits(base + HDMI_CON0, RG_HDMITX_EN_PRED_MASK);
	mtk_phy_set_bits(base + HDMI_CON0, RG_HDMITX_EN_DRV_MASK);
	usleep_range(80, 100);
	return 0;
}

static void mtk_hdmi_pll_unprepare(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);
	void __iomem *base = hdmi_phy->regs;

	mtk_phy_clear_bits(base + HDMI_CON0, RG_HDMITX_EN_DRV_MASK);
	mtk_phy_clear_bits(base + HDMI_CON0, RG_HDMITX_EN_PRED_MASK);
	mtk_phy_clear_bits(base + HDMI_CON0, RG_HDMITX_EN_SER_MASK);
	mtk_phy_clear_bits(base + HDMI_CON2, RG_HDMITX_MBIAS_LPF_EN);
	usleep_range(80, 100);
	mtk_phy_clear_bits(base + HDMI_CON0, RG_HDMITX_EN_SLDO_MASK);
	mtk_phy_clear_bits(base + HDMI_CON2, RG_HDMITX_EN_TX_CKLDO);
	mtk_phy_clear_bits(base + HDMI_CON6, RG_HTPLL_EN);
	usleep_range(80, 100);
	mtk_phy_clear_bits(base + HDMI_CON2, RG_HDMITX_EN_MBIAS);
	mtk_phy_clear_bits(base + HDMI_CON6, RG_HTPLL_POSDIV_MASK);
	mtk_phy_clear_bits(base + HDMI_CON6, RG_HTPLL_RLH_EN);
	mtk_phy_clear_bits(base + HDMI_CON7, RG_HTPLL_AUTOK_EN);
	usleep_range(80, 100);
}

static long mtk_hdmi_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long *parent_rate)
{
	return rate;
}

static int mtk_hdmi_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);
	void __iomem *base = hdmi_phy->regs;
	u32 pos_div;

	if (rate <= 64000000)
		pos_div = 3;
	else if (rate <= 128000000)
		pos_div = 2;
	else
		pos_div = 1;

	mtk_phy_set_bits(base + HDMI_CON6, RG_HTPLL_PREDIV_MASK);
	mtk_phy_set_bits(base + HDMI_CON6, RG_HTPLL_POSDIV_MASK);
	mtk_phy_set_bits(base + HDMI_CON2, RG_HDMITX_EN_TX_POSDIV);
	mtk_phy_update_field(base + HDMI_CON6, RG_HTPLL_IC_MASK, 0x1);
	mtk_phy_update_field(base + HDMI_CON6, RG_HTPLL_IR_MASK, 0x1);
	mtk_phy_update_field(base + HDMI_CON2, RG_HDMITX_TX_POSDIV_MASK, pos_div);
	mtk_phy_update_field(base + HDMI_CON6, RG_HTPLL_FBKSEL_MASK, 1);
	mtk_phy_update_field(base + HDMI_CON6, RG_HTPLL_FBKDIV_MASK, 19);
	mtk_phy_update_field(base + HDMI_CON7, RG_HTPLL_DIVEN_MASK, 0x2);
	mtk_phy_update_field(base + HDMI_CON6, RG_HTPLL_BP_MASK, 0xc);
	mtk_phy_update_field(base + HDMI_CON6, RG_HTPLL_BC_MASK, 0x2);
	mtk_phy_update_field(base + HDMI_CON6, RG_HTPLL_BR_MASK, 0x1);

	mtk_phy_clear_bits(base + HDMI_CON1, RG_HDMITX_PRED_IMP);
	mtk_phy_update_field(base + HDMI_CON1, RG_HDMITX_PRED_IBIAS_MASK, 0x3);
	mtk_phy_clear_bits(base + HDMI_CON0, RG_HDMITX_EN_IMP_MASK);
	mtk_phy_update_field(base + HDMI_CON1, RG_HDMITX_DRV_IMP_MASK, 0x28);
	mtk_phy_update_field(base + HDMI_CON4, RG_HDMITX_RESERVE_MASK, 0x28);
	mtk_phy_update_field(base + HDMI_CON0, RG_HDMITX_DRV_IBIAS_MASK, 0xa);
	return 0;
}

static unsigned long mtk_hdmi_pll_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);
	unsigned long out_rate, val;
	u32 tmp;

	tmp = readl(hdmi_phy->regs + HDMI_CON6);
	val = FIELD_GET(RG_HTPLL_PREDIV_MASK, tmp);
	switch (val) {
	case 0x00:
		out_rate = parent_rate;
		break;
	case 0x01:
		out_rate = parent_rate / 2;
		break;
	default:
		out_rate = parent_rate / 4;
		break;
	}

	val = FIELD_GET(RG_HTPLL_FBKDIV_MASK, tmp);
	out_rate *= (val + 1) * 2;

	tmp = readl(hdmi_phy->regs + HDMI_CON2);
	val = FIELD_GET(RG_HDMITX_TX_POSDIV_MASK, tmp);
	out_rate >>= val;

	if (tmp & RG_HDMITX_EN_TX_POSDIV)
		out_rate /= 5;

	return out_rate;
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
	void __iomem *base = hdmi_phy->regs;

	mtk_phy_set_bits(base + HDMI_CON7, RG_HTPLL_AUTOK_EN);
	mtk_phy_clear_bits(base + HDMI_CON6, RG_HTPLL_RLH_EN);
	mtk_phy_set_bits(base + HDMI_CON6, RG_HTPLL_POSDIV_MASK);
	mtk_phy_set_bits(base + HDMI_CON2, RG_HDMITX_EN_MBIAS);
	usleep_range(80, 100);
	mtk_phy_set_bits(base + HDMI_CON6, RG_HTPLL_EN);
	mtk_phy_set_bits(base + HDMI_CON2, RG_HDMITX_EN_TX_CKLDO);
	mtk_phy_set_bits(base + HDMI_CON0, RG_HDMITX_EN_SLDO_MASK);
	usleep_range(80, 100);
	mtk_phy_set_bits(base + HDMI_CON2, RG_HDMITX_MBIAS_LPF_EN);
	mtk_phy_set_bits(base + HDMI_CON0, RG_HDMITX_EN_SER_MASK);
	mtk_phy_set_bits(base + HDMI_CON0, RG_HDMITX_EN_PRED_MASK);
	mtk_phy_set_bits(base + HDMI_CON0, RG_HDMITX_EN_DRV_MASK);
	usleep_range(80, 100);
}

static void mtk_hdmi_phy_disable_tmds(struct mtk_hdmi_phy *hdmi_phy)
{
	void __iomem *base = hdmi_phy->regs;

	mtk_phy_clear_bits(base + HDMI_CON0, RG_HDMITX_EN_DRV_MASK);
	mtk_phy_clear_bits(base + HDMI_CON0, RG_HDMITX_EN_PRED_MASK);
	mtk_phy_clear_bits(base + HDMI_CON0, RG_HDMITX_EN_SER_MASK);
	mtk_phy_clear_bits(base + HDMI_CON2, RG_HDMITX_MBIAS_LPF_EN);
	usleep_range(80, 100);
	mtk_phy_clear_bits(base + HDMI_CON0, RG_HDMITX_EN_SLDO_MASK);
	mtk_phy_clear_bits(base + HDMI_CON2, RG_HDMITX_EN_TX_CKLDO);
	mtk_phy_clear_bits(base + HDMI_CON6, RG_HTPLL_EN);
	usleep_range(80, 100);
	mtk_phy_clear_bits(base + HDMI_CON2, RG_HDMITX_EN_MBIAS);
	mtk_phy_clear_bits(base + HDMI_CON6, RG_HTPLL_POSDIV_MASK);
	mtk_phy_clear_bits(base + HDMI_CON6, RG_HTPLL_RLH_EN);
	mtk_phy_clear_bits(base + HDMI_CON7, RG_HTPLL_AUTOK_EN);
	usleep_range(80, 100);
}

struct mtk_hdmi_phy_conf mtk_hdmi_phy_2701_conf = {
	.flags = CLK_SET_RATE_GATE,
	.pll_default_off = true,
	.hdmi_phy_clk_ops = &mtk_hdmi_phy_pll_ops,
	.hdmi_phy_enable_tmds = mtk_hdmi_phy_enable_tmds,
	.hdmi_phy_disable_tmds = mtk_hdmi_phy_disable_tmds,
};

MODULE_AUTHOR("Chunhui Dai <chunhui.dai@mediatek.com>");
MODULE_DESCRIPTION("MediaTek HDMI PHY Driver");
MODULE_LICENSE("GPL v2");

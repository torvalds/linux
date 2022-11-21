// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author: jitao.shi <jitao.shi@mediatek.com>
 */

#include "phy-mtk-mipi-dsi.h"

#define MIPITX_DSI_CON		0x00
#define RG_DSI_LDOCORE_EN		BIT(0)
#define RG_DSI_CKG_LDOOUT_EN		BIT(1)
#define RG_DSI_BCLK_SEL			(3 << 2)
#define RG_DSI_LD_IDX_SEL		(7 << 4)
#define RG_DSI_PHYCLK_SEL		(2 << 8)
#define RG_DSI_DSICLK_FREQ_SEL		BIT(10)
#define RG_DSI_LPTX_CLMP_EN		BIT(11)

#define MIPITX_DSI_CLOCK_LANE	0x04
#define MIPITX_DSI_DATA_LANE0	0x08
#define MIPITX_DSI_DATA_LANE1	0x0c
#define MIPITX_DSI_DATA_LANE2	0x10
#define MIPITX_DSI_DATA_LANE3	0x14
#define RG_DSI_LNTx_LDOOUT_EN		BIT(0)
#define RG_DSI_LNTx_CKLANE_EN		BIT(1)
#define RG_DSI_LNTx_LPTX_IPLUS1		BIT(2)
#define RG_DSI_LNTx_LPTX_IPLUS2		BIT(3)
#define RG_DSI_LNTx_LPTX_IMINUS		BIT(4)
#define RG_DSI_LNTx_LPCD_IPLUS		BIT(5)
#define RG_DSI_LNTx_LPCD_IMINUS		BIT(6)
#define RG_DSI_LNTx_RT_CODE		(0xf << 8)

#define MIPITX_DSI_TOP_CON	0x40
#define RG_DSI_LNT_INTR_EN		BIT(0)
#define RG_DSI_LNT_HS_BIAS_EN		BIT(1)
#define RG_DSI_LNT_IMP_CAL_EN		BIT(2)
#define RG_DSI_LNT_TESTMODE_EN		BIT(3)
#define RG_DSI_LNT_IMP_CAL_CODE		(0xf << 4)
#define RG_DSI_LNT_AIO_SEL		(7 << 8)
#define RG_DSI_PAD_TIE_LOW_EN		BIT(11)
#define RG_DSI_DEBUG_INPUT_EN		BIT(12)
#define RG_DSI_PRESERVE			(7 << 13)

#define MIPITX_DSI_BG_CON	0x44
#define RG_DSI_BG_CORE_EN		BIT(0)
#define RG_DSI_BG_CKEN			BIT(1)
#define RG_DSI_BG_DIV			(0x3 << 2)
#define RG_DSI_BG_FAST_CHARGE		BIT(4)
#define RG_DSI_VOUT_MSK			(0x3ffff << 5)
#define RG_DSI_V12_SEL			(7 << 5)
#define RG_DSI_V10_SEL			(7 << 8)
#define RG_DSI_V072_SEL			(7 << 11)
#define RG_DSI_V04_SEL			(7 << 14)
#define RG_DSI_V032_SEL			(7 << 17)
#define RG_DSI_V02_SEL			(7 << 20)
#define RG_DSI_BG_R1_TRIM		(0xf << 24)
#define RG_DSI_BG_R2_TRIM		(0xf << 28)

#define MIPITX_DSI_PLL_CON0	0x50
#define RG_DSI_MPPLL_PLL_EN		BIT(0)
#define RG_DSI_MPPLL_DIV_MSK		(0x1ff << 1)
#define RG_DSI_MPPLL_PREDIV		(3 << 1)
#define RG_DSI_MPPLL_TXDIV0		(3 << 3)
#define RG_DSI_MPPLL_TXDIV1		(3 << 5)
#define RG_DSI_MPPLL_POSDIV		(7 << 7)
#define RG_DSI_MPPLL_MONVC_EN		BIT(10)
#define RG_DSI_MPPLL_MONREF_EN		BIT(11)
#define RG_DSI_MPPLL_VOD_EN		BIT(12)

#define MIPITX_DSI_PLL_CON1	0x54
#define RG_DSI_MPPLL_SDM_FRA_EN		BIT(0)
#define RG_DSI_MPPLL_SDM_SSC_PH_INIT	BIT(1)
#define RG_DSI_MPPLL_SDM_SSC_EN		BIT(2)
#define RG_DSI_MPPLL_SDM_SSC_PRD	(0xffff << 16)

#define MIPITX_DSI_PLL_CON2	0x58

#define MIPITX_DSI_PLL_TOP	0x64
#define RG_DSI_MPPLL_PRESERVE		(0xff << 8)

#define MIPITX_DSI_PLL_PWR	0x68
#define RG_DSI_MPPLL_SDM_PWR_ON		BIT(0)
#define RG_DSI_MPPLL_SDM_ISO_EN		BIT(1)
#define RG_DSI_MPPLL_SDM_PWR_ACK	BIT(8)

#define MIPITX_DSI_SW_CTRL	0x80
#define SW_CTRL_EN			BIT(0)

#define MIPITX_DSI_SW_CTRL_CON0	0x84
#define SW_LNTC_LPTX_PRE_OE		BIT(0)
#define SW_LNTC_LPTX_OE			BIT(1)
#define SW_LNTC_LPTX_P			BIT(2)
#define SW_LNTC_LPTX_N			BIT(3)
#define SW_LNTC_HSTX_PRE_OE		BIT(4)
#define SW_LNTC_HSTX_OE			BIT(5)
#define SW_LNTC_HSTX_ZEROCLK		BIT(6)
#define SW_LNT0_LPTX_PRE_OE		BIT(7)
#define SW_LNT0_LPTX_OE			BIT(8)
#define SW_LNT0_LPTX_P			BIT(9)
#define SW_LNT0_LPTX_N			BIT(10)
#define SW_LNT0_HSTX_PRE_OE		BIT(11)
#define SW_LNT0_HSTX_OE			BIT(12)
#define SW_LNT0_LPRX_EN			BIT(13)
#define SW_LNT1_LPTX_PRE_OE		BIT(14)
#define SW_LNT1_LPTX_OE			BIT(15)
#define SW_LNT1_LPTX_P			BIT(16)
#define SW_LNT1_LPTX_N			BIT(17)
#define SW_LNT1_HSTX_PRE_OE		BIT(18)
#define SW_LNT1_HSTX_OE			BIT(19)
#define SW_LNT2_LPTX_PRE_OE		BIT(20)
#define SW_LNT2_LPTX_OE			BIT(21)
#define SW_LNT2_LPTX_P			BIT(22)
#define SW_LNT2_LPTX_N			BIT(23)
#define SW_LNT2_HSTX_PRE_OE		BIT(24)
#define SW_LNT2_HSTX_OE			BIT(25)

static int mtk_mipi_tx_pll_prepare(struct clk_hw *hw)
{
	struct mtk_mipi_tx *mipi_tx = mtk_mipi_tx_from_clk_hw(hw);
	u8 txdiv, txdiv0, txdiv1;
	u64 pcw;

	dev_dbg(mipi_tx->dev, "prepare: %u Hz\n", mipi_tx->data_rate);

	if (mipi_tx->data_rate >= 500000000) {
		txdiv = 1;
		txdiv0 = 0;
		txdiv1 = 0;
	} else if (mipi_tx->data_rate >= 250000000) {
		txdiv = 2;
		txdiv0 = 1;
		txdiv1 = 0;
	} else if (mipi_tx->data_rate >= 125000000) {
		txdiv = 4;
		txdiv0 = 2;
		txdiv1 = 0;
	} else if (mipi_tx->data_rate > 62000000) {
		txdiv = 8;
		txdiv0 = 2;
		txdiv1 = 1;
	} else if (mipi_tx->data_rate >= 50000000) {
		txdiv = 16;
		txdiv0 = 2;
		txdiv1 = 2;
	} else {
		return -EINVAL;
	}

	mtk_mipi_tx_update_bits(mipi_tx, MIPITX_DSI_BG_CON,
				RG_DSI_VOUT_MSK |
				RG_DSI_BG_CKEN | RG_DSI_BG_CORE_EN,
				(4 << 20) | (4 << 17) | (4 << 14) |
				(4 << 11) | (4 << 8) | (4 << 5) |
				RG_DSI_BG_CKEN | RG_DSI_BG_CORE_EN);

	usleep_range(30, 100);

	mtk_mipi_tx_update_bits(mipi_tx, MIPITX_DSI_TOP_CON,
				RG_DSI_LNT_IMP_CAL_CODE | RG_DSI_LNT_HS_BIAS_EN,
				(8 << 4) | RG_DSI_LNT_HS_BIAS_EN);

	mtk_mipi_tx_set_bits(mipi_tx, MIPITX_DSI_CON,
			     RG_DSI_CKG_LDOOUT_EN | RG_DSI_LDOCORE_EN);

	mtk_mipi_tx_update_bits(mipi_tx, MIPITX_DSI_PLL_PWR,
				RG_DSI_MPPLL_SDM_PWR_ON |
				RG_DSI_MPPLL_SDM_ISO_EN,
				RG_DSI_MPPLL_SDM_PWR_ON);

	mtk_mipi_tx_clear_bits(mipi_tx, MIPITX_DSI_PLL_CON0,
			       RG_DSI_MPPLL_PLL_EN);

	mtk_mipi_tx_update_bits(mipi_tx, MIPITX_DSI_PLL_CON0,
				RG_DSI_MPPLL_TXDIV0 | RG_DSI_MPPLL_TXDIV1 |
				RG_DSI_MPPLL_PREDIV,
				(txdiv0 << 3) | (txdiv1 << 5));

	/*
	 * PLL PCW config
	 * PCW bit 24~30 = integer part of pcw
	 * PCW bit 0~23 = fractional part of pcw
	 * pcw = data_Rate*4*txdiv/(Ref_clk*2);
	 * Post DIV =4, so need data_Rate*4
	 * Ref_clk is 26MHz
	 */
	pcw = div_u64(((u64)mipi_tx->data_rate * 2 * txdiv) << 24,
		      26000000);
	writel(pcw, mipi_tx->regs + MIPITX_DSI_PLL_CON2);

	mtk_mipi_tx_set_bits(mipi_tx, MIPITX_DSI_PLL_CON1,
			     RG_DSI_MPPLL_SDM_FRA_EN);

	mtk_mipi_tx_set_bits(mipi_tx, MIPITX_DSI_PLL_CON0, RG_DSI_MPPLL_PLL_EN);

	usleep_range(20, 100);

	mtk_mipi_tx_clear_bits(mipi_tx, MIPITX_DSI_PLL_CON1,
			       RG_DSI_MPPLL_SDM_SSC_EN);

	mtk_mipi_tx_update_bits(mipi_tx, MIPITX_DSI_PLL_TOP,
				RG_DSI_MPPLL_PRESERVE,
				mipi_tx->driver_data->mppll_preserve);

	return 0;
}

static void mtk_mipi_tx_pll_unprepare(struct clk_hw *hw)
{
	struct mtk_mipi_tx *mipi_tx = mtk_mipi_tx_from_clk_hw(hw);

	dev_dbg(mipi_tx->dev, "unprepare\n");

	mtk_mipi_tx_clear_bits(mipi_tx, MIPITX_DSI_PLL_CON0,
			       RG_DSI_MPPLL_PLL_EN);

	mtk_mipi_tx_update_bits(mipi_tx, MIPITX_DSI_PLL_TOP,
				RG_DSI_MPPLL_PRESERVE, 0);

	mtk_mipi_tx_update_bits(mipi_tx, MIPITX_DSI_PLL_PWR,
				RG_DSI_MPPLL_SDM_ISO_EN |
				RG_DSI_MPPLL_SDM_PWR_ON,
				RG_DSI_MPPLL_SDM_ISO_EN);

	mtk_mipi_tx_clear_bits(mipi_tx, MIPITX_DSI_TOP_CON,
			       RG_DSI_LNT_HS_BIAS_EN);

	mtk_mipi_tx_clear_bits(mipi_tx, MIPITX_DSI_CON,
			       RG_DSI_CKG_LDOOUT_EN | RG_DSI_LDOCORE_EN);

	mtk_mipi_tx_clear_bits(mipi_tx, MIPITX_DSI_BG_CON,
			       RG_DSI_BG_CKEN | RG_DSI_BG_CORE_EN);

	mtk_mipi_tx_clear_bits(mipi_tx, MIPITX_DSI_PLL_CON0,
			       RG_DSI_MPPLL_DIV_MSK);
}

static long mtk_mipi_tx_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long *prate)
{
	return clamp_val(rate, 50000000, 1250000000);
}

static const struct clk_ops mtk_mipi_tx_pll_ops = {
	.prepare = mtk_mipi_tx_pll_prepare,
	.unprepare = mtk_mipi_tx_pll_unprepare,
	.round_rate = mtk_mipi_tx_pll_round_rate,
	.set_rate = mtk_mipi_tx_pll_set_rate,
	.recalc_rate = mtk_mipi_tx_pll_recalc_rate,
};

static void mtk_mipi_tx_power_on_signal(struct phy *phy)
{
	struct mtk_mipi_tx *mipi_tx = phy_get_drvdata(phy);
	u32 reg;

	for (reg = MIPITX_DSI_CLOCK_LANE;
	     reg <= MIPITX_DSI_DATA_LANE3; reg += 4)
		mtk_mipi_tx_set_bits(mipi_tx, reg, RG_DSI_LNTx_LDOOUT_EN);

	mtk_mipi_tx_clear_bits(mipi_tx, MIPITX_DSI_TOP_CON,
			       RG_DSI_PAD_TIE_LOW_EN);
}

static void mtk_mipi_tx_power_off_signal(struct phy *phy)
{
	struct mtk_mipi_tx *mipi_tx = phy_get_drvdata(phy);
	u32 reg;

	mtk_mipi_tx_set_bits(mipi_tx, MIPITX_DSI_TOP_CON,
			     RG_DSI_PAD_TIE_LOW_EN);

	for (reg = MIPITX_DSI_CLOCK_LANE;
	     reg <= MIPITX_DSI_DATA_LANE3; reg += 4)
		mtk_mipi_tx_clear_bits(mipi_tx, reg, RG_DSI_LNTx_LDOOUT_EN);
}

const struct mtk_mipitx_data mt2701_mipitx_data = {
	.mppll_preserve = (3 << 8),
	.mipi_tx_clk_ops = &mtk_mipi_tx_pll_ops,
	.mipi_tx_enable_signal = mtk_mipi_tx_power_on_signal,
	.mipi_tx_disable_signal = mtk_mipi_tx_power_off_signal,
};

const struct mtk_mipitx_data mt8173_mipitx_data = {
	.mppll_preserve = (0 << 8),
	.mipi_tx_clk_ops = &mtk_mipi_tx_pll_ops,
	.mipi_tx_enable_signal = mtk_mipi_tx_power_on_signal,
	.mipi_tx_disable_signal = mtk_mipi_tx_power_off_signal,
};

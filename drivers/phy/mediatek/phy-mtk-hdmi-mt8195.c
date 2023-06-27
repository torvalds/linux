// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Copyright (c) 2022 BayLibre, SAS
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/units.h>
#include <linux/nvmem-consumer.h>

#include "phy-mtk-io.h"
#include "phy-mtk-hdmi.h"
#include "phy-mtk-hdmi-mt8195.h"

static void mtk_hdmi_ana_fifo_en(struct mtk_hdmi_phy *hdmi_phy)
{
	/* make data fifo writable for hdmi2.0 */
	mtk_phy_set_bits(hdmi_phy->regs + HDMI_ANA_CTL, REG_ANA_HDMI20_FIFO_EN);
}

static void
mtk_phy_tmds_clk_ratio(struct mtk_hdmi_phy *hdmi_phy, bool enable)
{
	void __iomem *regs = hdmi_phy->regs;

	mtk_hdmi_ana_fifo_en(hdmi_phy);

	/* HDMI 2.0 specification, 3.4Gbps <= TMDS Bit Rate <= 6G,
	 * clock bit ratio 1:40, under 3.4Gbps, clock bit ratio 1:10
	 */
	if (enable)
		mtk_phy_update_field(regs + HDMI20_CLK_CFG, REG_TXC_DIV, 3);
	else
		mtk_phy_clear_bits(regs + HDMI20_CLK_CFG, REG_TXC_DIV);
}

static void mtk_hdmi_pll_sel_src(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);
	void __iomem *regs = hdmi_phy->regs;

	mtk_phy_clear_bits(regs + HDMI_CTL_3, REG_HDMITX_REF_XTAL_SEL);
	mtk_phy_clear_bits(regs + HDMI_CTL_3, REG_HDMITX_REF_RESPLL_SEL);

	/* DA_HDMITX21_REF_CK for TXPLL input source */
	mtk_phy_clear_bits(regs + HDMI_1_CFG_10, RG_HDMITXPLL_REF_CK_SEL);
}

static void mtk_hdmi_pll_perf(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);
	void __iomem *regs = hdmi_phy->regs;

	mtk_phy_set_bits(regs + HDMI_1_PLL_CFG_0, RG_HDMITXPLL_BP2);
	mtk_phy_set_bits(regs + HDMI_1_PLL_CFG_2, RG_HDMITXPLL_BC);
	mtk_phy_update_field(regs + HDMI_1_PLL_CFG_2, RG_HDMITXPLL_IC, 0x1);
	mtk_phy_update_field(regs + HDMI_1_PLL_CFG_2, RG_HDMITXPLL_BR, 0x2);
	mtk_phy_update_field(regs + HDMI_1_PLL_CFG_2, RG_HDMITXPLL_IR, 0x2);
	mtk_phy_set_bits(regs + HDMI_1_PLL_CFG_2, RG_HDMITXPLL_BP);
	mtk_phy_clear_bits(regs + HDMI_1_PLL_CFG_0, RG_HDMITXPLL_IBAND_FIX_EN);
	mtk_phy_clear_bits(regs + HDMI_1_PLL_CFG_1, RG_HDMITXPLL_RESERVE_BIT14);
	mtk_phy_clear_bits(regs + HDMI_1_PLL_CFG_2, RG_HDMITXPLL_HIKVCO);
	mtk_phy_update_field(regs + HDMI_1_PLL_CFG_0, RG_HDMITXPLL_HREN, 0x1);
	mtk_phy_update_field(regs + HDMI_1_PLL_CFG_0, RG_HDMITXPLL_LVR_SEL, 0x1);
	mtk_phy_set_bits(regs + HDMI_1_PLL_CFG_1, RG_HDMITXPLL_RESERVE_BIT12_11);
	mtk_phy_set_bits(regs + HDMI_1_PLL_CFG_0, RG_HDMITXPLL_TCL_EN);
}

static int mtk_hdmi_pll_set_hw(struct clk_hw *hw, u8 prediv,
			       u8 fbkdiv_high,
			       u32 fbkdiv_low,
			       u8 fbkdiv_hs3, u8 posdiv1,
			       u8 posdiv2, u8 txprediv,
			       u8 txposdiv,
			       u8 digital_div)
{
	u8 txposdiv_value;
	u8 div3_ctrl_value;
	u8 posdiv_vallue;
	u8 div_ctrl_value;
	u8 reserve_3_2_value;
	u8 prediv_value;
	u8 reserve13_value;
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);
	void __iomem *regs = hdmi_phy->regs;

	mtk_hdmi_pll_sel_src(hw);

	mtk_hdmi_pll_perf(hw);

	mtk_phy_update_field(regs + HDMI_1_CFG_10, RG_HDMITX21_BIAS_PE_BG_VREF_SEL, 0x2);
	mtk_phy_clear_bits(regs + HDMI_1_CFG_10, RG_HDMITX21_VREF_SEL);
	mtk_phy_update_field(regs + HDMI_1_CFG_9, RG_HDMITX21_SLDO_VREF_SEL, 0x2);
	mtk_phy_clear_bits(regs + HDMI_1_CFG_10, RG_HDMITX21_BIAS_PE_VREF_SELB);
	mtk_phy_set_bits(regs + HDMI_1_CFG_3, RG_HDMITX21_SLDOLPF_EN);
	mtk_phy_update_field(regs + HDMI_1_CFG_6, RG_HDMITX21_INTR_CAL, 0x11);
	mtk_phy_set_bits(regs + HDMI_1_PLL_CFG_2, RG_HDMITXPLL_PWD);

	/* TXPOSDIV */
	txposdiv_value = ilog2(txposdiv);

	mtk_phy_update_field(regs + HDMI_1_CFG_6, RG_HDMITX21_TX_POSDIV, txposdiv_value);
	mtk_phy_set_bits(regs + HDMI_1_CFG_6, RG_HDMITX21_TX_POSDIV_EN);
	mtk_phy_clear_bits(regs + HDMI_1_CFG_6, RG_HDMITX21_FRL_EN);

	/* TXPREDIV */
	switch (txprediv) {
	case 2:
		div3_ctrl_value = 0x0;
		posdiv_vallue = 0x0;
		break;
	case 4:
		div3_ctrl_value = 0x0;
		posdiv_vallue = 0x1;
		break;
	case 6:
		div3_ctrl_value = 0x1;
		posdiv_vallue = 0x0;
		break;
	case 12:
		div3_ctrl_value = 0x1;
		posdiv_vallue = 0x1;
		break;
	default:
		return -EINVAL;
	}

	mtk_phy_update_field(regs + HDMI_1_PLL_CFG_4, RG_HDMITXPLL_POSDIV_DIV3_CTRL, div3_ctrl_value);
	mtk_phy_update_field(regs + HDMI_1_PLL_CFG_4, RG_HDMITXPLL_POSDIV, posdiv_vallue);

	/* POSDIV1 */
	switch (posdiv1) {
	case 5:
		div_ctrl_value = 0x0;
		break;
	case 10:
		div_ctrl_value = 0x1;
		break;
	case 12:
		div_ctrl_value = 0x2;
		break;
	case 15:
		div_ctrl_value = 0x3;
		break;
	default:
		return -EINVAL;
	}

	mtk_phy_update_field(regs + HDMI_1_PLL_CFG_4, RG_HDMITXPLL_DIV_CTRL, div_ctrl_value);

	/* DE add new setting */
	mtk_phy_clear_bits(regs + HDMI_1_PLL_CFG_1, RG_HDMITXPLL_RESERVE_BIT14);

	/* POSDIV2 */
	switch (posdiv2) {
	case 1:
		reserve_3_2_value = 0x0;
		break;
	case 2:
		reserve_3_2_value = 0x1;
		break;
	case 4:
		reserve_3_2_value = 0x2;
		break;
	case 6:
		reserve_3_2_value = 0x3;
		break;
	default:
		return -EINVAL;
	}

	mtk_phy_update_field(regs + HDMI_1_PLL_CFG_1, RG_HDMITXPLL_RESERVE_BIT3_2, reserve_3_2_value);

	/* DE add new setting */
	mtk_phy_update_field(regs + HDMI_1_PLL_CFG_1, RG_HDMITXPLL_RESERVE_BIT1_0, 0x2);

	/* PREDIV */
	prediv_value = ilog2(prediv);

	mtk_phy_update_field(regs + HDMI_1_PLL_CFG_4, RG_HDMITXPLL_PREDIV, prediv_value);

	/* FBKDIV_HS3 */
	reserve13_value = ilog2(fbkdiv_hs3);

	mtk_phy_update_field(regs + HDMI_1_PLL_CFG_1, RG_HDMITXPLL_RESERVE_BIT13, reserve13_value);

	/* FBDIV */
	mtk_phy_update_field(regs + HDMI_1_PLL_CFG_4, RG_HDMITXPLL_FBKDIV_HIGH, fbkdiv_high);
	mtk_phy_update_field(regs + HDMI_1_PLL_CFG_3, RG_HDMITXPLL_FBKDIV_LOW, fbkdiv_low);

	/* Digital DIVIDER */
	mtk_phy_clear_bits(regs + HDMI_CTL_3, REG_PIXEL_CLOCK_SEL);

	if (digital_div == 1) {
		mtk_phy_clear_bits(regs + HDMI_CTL_3, REG_HDMITX_PIXEL_CLOCK);
	} else {
		mtk_phy_set_bits(regs + HDMI_CTL_3, REG_HDMITX_PIXEL_CLOCK);
		mtk_phy_update_field(regs + HDMI_CTL_3, REG_HDMITXPLL_DIV, digital_div - 1);
	}

	return 0;
}

static int mtk_hdmi_pll_calc(struct mtk_hdmi_phy *hdmi_phy, struct clk_hw *hw,
			     unsigned long rate, unsigned long parent_rate)
{
	u8 digital_div, txprediv, txposdiv, fbkdiv_high, posdiv1, posdiv2;
	u64 tmds_clk, pixel_clk, da_hdmitx21_ref_ck, ns_hdmipll_ck, pcw;
	u8 txpredivs[4] = { 2, 4, 6, 12 };
	u32 fbkdiv_low;
	int i;

	pixel_clk = rate;
	tmds_clk = pixel_clk;

	if (tmds_clk < 25 * MEGA || tmds_clk > 594 * MEGA)
		return -EINVAL;

	if (tmds_clk >= 340 * MEGA)
		hdmi_phy->tmds_over_340M = true;
	else
		hdmi_phy->tmds_over_340M = false;

	/* in Hz */
	da_hdmitx21_ref_ck = 26 * MEGA;

	/*  TXPOSDIV stage treatment:
	 *	0M  <  TMDS clk  < 54M		  /8
	 *	54M <= TMDS clk  < 148.35M    /4
	 *	148.35M <=TMDS clk < 296.7M   /2
	 *	296.7 <=TMDS clk <= 594M	  /1
	 */
	if (tmds_clk < 54 * MEGA)
		txposdiv = 8;
	else if (tmds_clk >= 54 * MEGA && (tmds_clk * 100) < 14835 * MEGA)
		txposdiv = 4;
	else if ((tmds_clk * 100) >= 14835 * MEGA && (tmds_clk * 10) < 2967 * MEGA)
		txposdiv = 2;
	else if ((tmds_clk * 10) >= 2967 * MEGA && tmds_clk <= 594 * MEGA)
		txposdiv = 1;
	else
		return -EINVAL;

	/* calculate txprediv: can be 2, 4, 6, 12
	 * ICO clk = 5*TMDS_CLK*TXPOSDIV*TXPREDIV
	 * ICO clk constraint: 5G =< ICO clk <= 12G
	 */
	for (i = 0; i < ARRAY_SIZE(txpredivs); i++) {
		ns_hdmipll_ck = 5 * tmds_clk * txposdiv * txpredivs[i];
		if (ns_hdmipll_ck >= 5 * GIGA &&
		    ns_hdmipll_ck <= 1 * GIGA)
			break;
	}
	if (i == (ARRAY_SIZE(txpredivs) - 1) &&
	    (ns_hdmipll_ck < 5 * GIGA || ns_hdmipll_ck > 12 * GIGA)) {
		return -EINVAL;
	}
	if (i == ARRAY_SIZE(txpredivs))
		return -EINVAL;

	txprediv = txpredivs[i];

	/* PCW calculation: FBKDIV
	 * formula: pcw=(frequency_out*2^pcw_bit) / frequency_in / FBKDIV_HS3;
	 * RG_HDMITXPLL_FBKDIV[32:0]:
	 * [32,24] 9bit integer, [23,0]:24bit fraction
	 */
	pcw = div_u64(((u64)ns_hdmipll_ck) << PCW_DECIMAL_WIDTH,
		      da_hdmitx21_ref_ck * PLL_FBKDIV_HS3);

	if (pcw > GENMASK_ULL(32, 0))
		return -EINVAL;

	fbkdiv_high = FIELD_GET(GENMASK_ULL(63, 32), pcw);
	fbkdiv_low = FIELD_GET(GENMASK(31, 0), pcw);

	/* posdiv1:
	 * posdiv1 stage treatment according to color_depth:
	 * 24bit -> posdiv1 /10, 30bit -> posdiv1 /12.5,
	 * 36bit -> posdiv1 /15, 48bit -> posdiv1 /10
	 */
	posdiv1 = 10;
	posdiv2 = 1;

	/* Digital clk divider, max /32 */
	digital_div = div_u64(ns_hdmipll_ck, posdiv1 * posdiv2 * pixel_clk);
	if (!(digital_div <= 32 && digital_div >= 1))
		return -EINVAL;

	return mtk_hdmi_pll_set_hw(hw, PLL_PREDIV, fbkdiv_high, fbkdiv_low,
			    PLL_FBKDIV_HS3, posdiv1, posdiv2, txprediv,
			    txposdiv, digital_div);
}

static int mtk_hdmi_pll_drv_setting(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);
	void __iomem *regs = hdmi_phy->regs;
	u8 data_channel_bias, clk_channel_bias;
	u8 impedance, impedance_en;
	u32 tmds_clk;
	u32 pixel_clk = hdmi_phy->pll_rate;

	tmds_clk = pixel_clk;

	/* bias & impedance setting:
	 * 3G < data rate <= 6G: enable impedance 100ohm,
	 *      data channel bias 24mA, clock channel bias 20mA
	 * pixel clk >= HD,  74.175MHZ <= pixel clk <= 300MHZ:
	 *      enalbe impedance 100ohm
	 *      data channel 20mA, clock channel 16mA
	 * 27M =< pixel clk < 74.175: disable impedance
	 *      data channel & clock channel bias 10mA
	 */

	/* 3G < data rate <= 6G, 300M < tmds rate <= 594M */
	if (tmds_clk > 300 * MEGA && tmds_clk <= 594 * MEGA) {
		data_channel_bias = 0x3c; /* 24mA */
		clk_channel_bias = 0x34; /* 20mA */
		impedance_en = 0xf;
		impedance = 0x36; /* 100ohm */
	} else if (((u64)pixel_clk * 1000) >= 74175 * MEGA && pixel_clk <= 300 * MEGA) {
		data_channel_bias = 0x34; /* 20mA */
		clk_channel_bias = 0x2c; /* 16mA */
		impedance_en = 0xf;
		impedance = 0x36; /* 100ohm */
	} else if (pixel_clk >= 27 * MEGA && ((u64)pixel_clk * 1000) < 74175 * MEGA) {
		data_channel_bias = 0x14; /* 10mA */
		clk_channel_bias = 0x14; /* 10mA */
		impedance_en = 0x0;
		impedance = 0x0;
	} else {
		return -EINVAL;
	}

	/* bias */
	mtk_phy_update_field(regs + HDMI_1_CFG_1, RG_HDMITX21_DRV_IBIAS_D0, data_channel_bias);
	mtk_phy_update_field(regs + HDMI_1_CFG_1, RG_HDMITX21_DRV_IBIAS_D1, data_channel_bias);
	mtk_phy_update_field(regs + HDMI_1_CFG_1, RG_HDMITX21_DRV_IBIAS_D2, data_channel_bias);
	mtk_phy_update_field(regs + HDMI_1_CFG_0, RG_HDMITX21_DRV_IBIAS_CLK, clk_channel_bias);

	/* impedance */
	mtk_phy_update_field(regs + HDMI_1_CFG_0, RG_HDMITX21_DRV_IMP_EN, impedance_en);
	mtk_phy_update_field(regs + HDMI_1_CFG_2, RG_HDMITX21_DRV_IMP_D0_EN1, impedance);
	mtk_phy_update_field(regs + HDMI_1_CFG_2, RG_HDMITX21_DRV_IMP_D1_EN1, impedance);
	mtk_phy_update_field(regs + HDMI_1_CFG_2, RG_HDMITX21_DRV_IMP_D2_EN1, impedance);
	mtk_phy_update_field(regs + HDMI_1_CFG_2, RG_HDMITX21_DRV_IMP_CLK_EN1, impedance);

	return 0;
}

static int mtk_hdmi_pll_prepare(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);
	void __iomem *regs = hdmi_phy->regs;

	mtk_phy_set_bits(regs + HDMI_1_CFG_6, RG_HDMITX21_TX_POSDIV_EN);

	mtk_phy_set_bits(regs + HDMI_1_CFG_0, RG_HDMITX21_SER_EN);
	mtk_phy_set_bits(regs + HDMI_1_CFG_6, RG_HDMITX21_D0_DRV_OP_EN);
	mtk_phy_set_bits(regs + HDMI_1_CFG_6, RG_HDMITX21_D1_DRV_OP_EN);
	mtk_phy_set_bits(regs + HDMI_1_CFG_6, RG_HDMITX21_D2_DRV_OP_EN);
	mtk_phy_set_bits(regs + HDMI_1_CFG_6, RG_HDMITX21_CK_DRV_OP_EN);

	mtk_phy_clear_bits(regs + HDMI_1_CFG_6, RG_HDMITX21_FRL_D0_EN);
	mtk_phy_clear_bits(regs + HDMI_1_CFG_6, RG_HDMITX21_FRL_D1_EN);
	mtk_phy_clear_bits(regs + HDMI_1_CFG_6, RG_HDMITX21_FRL_D2_EN);
	mtk_phy_clear_bits(regs + HDMI_1_CFG_6, RG_HDMITX21_FRL_CK_EN);

	mtk_hdmi_pll_drv_setting(hw);

	mtk_phy_clear_bits(regs + HDMI_1_CFG_10, RG_HDMITX21_BG_PWD);
	mtk_phy_set_bits(regs + HDMI_1_CFG_6, RG_HDMITX21_BIAS_EN);
	mtk_phy_set_bits(regs + HDMI_1_CFG_3, RG_HDMITX21_CKLDO_EN);
	mtk_phy_set_bits(regs + HDMI_1_CFG_3, RG_HDMITX21_SLDO_EN);

	mtk_phy_set_bits(regs + HDMI_1_PLL_CFG_4, DA_HDMITXPLL_PWR_ON);
	usleep_range(5, 10);
	mtk_phy_clear_bits(regs + HDMI_1_PLL_CFG_4, DA_HDMITXPLL_ISO_EN);
	usleep_range(5, 10);
	mtk_phy_clear_bits(regs + HDMI_1_PLL_CFG_2, RG_HDMITXPLL_PWD);
	usleep_range(30, 50);
	return 0;
}

static void mtk_hdmi_pll_unprepare(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);
	void __iomem *regs = hdmi_phy->regs;

	mtk_phy_set_bits(regs + HDMI_1_CFG_10, RG_HDMITX21_BG_PWD);
	mtk_phy_clear_bits(regs + HDMI_1_CFG_6, RG_HDMITX21_BIAS_EN);
	mtk_phy_clear_bits(regs + HDMI_1_CFG_3, RG_HDMITX21_CKLDO_EN);
	mtk_phy_clear_bits(regs + HDMI_1_CFG_3, RG_HDMITX21_SLDO_EN);

	mtk_phy_set_bits(regs + HDMI_1_PLL_CFG_2, RG_HDMITXPLL_PWD);
	usleep_range(10, 20);
	mtk_phy_set_bits(regs + HDMI_1_PLL_CFG_4, DA_HDMITXPLL_ISO_EN);
	usleep_range(10, 20);
	mtk_phy_clear_bits(regs + HDMI_1_PLL_CFG_4, DA_HDMITXPLL_PWR_ON);
}

static int mtk_hdmi_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);

	dev_dbg(hdmi_phy->dev, "%s: %lu Hz, parent: %lu Hz\n", __func__, rate,
		parent_rate);

	return mtk_hdmi_pll_calc(hdmi_phy, hw, rate, parent_rate);
}

static long mtk_hdmi_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long *parent_rate)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);

	hdmi_phy->pll_rate = rate;
	return rate;
}

static unsigned long mtk_hdmi_pll_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);

	return hdmi_phy->pll_rate;
}

static const struct clk_ops mtk_hdmi_pll_ops = {
	.prepare = mtk_hdmi_pll_prepare,
	.unprepare = mtk_hdmi_pll_unprepare,
	.set_rate = mtk_hdmi_pll_set_rate,
	.round_rate = mtk_hdmi_pll_round_rate,
	.recalc_rate = mtk_hdmi_pll_recalc_rate,
};

static void vtx_signal_en(struct mtk_hdmi_phy *hdmi_phy, bool on)
{
	void __iomem *regs = hdmi_phy->regs;

	if (on)
		mtk_phy_set_bits(regs + HDMI_1_CFG_0, RG_HDMITX21_DRV_EN);
	else
		mtk_phy_clear_bits(regs + HDMI_1_CFG_0, RG_HDMITX21_DRV_EN);
}

static void mtk_hdmi_phy_enable_tmds(struct mtk_hdmi_phy *hdmi_phy)
{
	vtx_signal_en(hdmi_phy, true);
	usleep_range(100, 150);
}

static void mtk_hdmi_phy_disable_tmds(struct mtk_hdmi_phy *hdmi_phy)
{
	vtx_signal_en(hdmi_phy, false);
}

static int mtk_hdmi_phy_configure(struct phy *phy, union phy_configure_opts *opts)
{
	struct phy_configure_opts_dp *dp_opts = &opts->dp;
	struct mtk_hdmi_phy *hdmi_phy = phy_get_drvdata(phy);
	int ret;

	ret = clk_set_rate(hdmi_phy->pll, dp_opts->link_rate);

	if (ret)
		return ret;

	mtk_phy_tmds_clk_ratio(hdmi_phy, hdmi_phy->tmds_over_340M);

	return ret;
}

struct mtk_hdmi_phy_conf mtk_hdmi_phy_8195_conf = {
	.flags = CLK_SET_RATE_PARENT | CLK_SET_RATE_GATE,
	.hdmi_phy_clk_ops = &mtk_hdmi_pll_ops,
	.hdmi_phy_enable_tmds = mtk_hdmi_phy_enable_tmds,
	.hdmi_phy_disable_tmds = mtk_hdmi_phy_disable_tmds,
	.hdmi_phy_configure = mtk_hdmi_phy_configure,
};

MODULE_AUTHOR("Can Zeng <can.zeng@mediatek.com>");
MODULE_DESCRIPTION("MediaTek MT8195 HDMI PHY Driver");
MODULE_LICENSE("GPL v2");

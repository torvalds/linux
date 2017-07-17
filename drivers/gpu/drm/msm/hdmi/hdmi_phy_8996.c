/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>

#include "hdmi.h"

#define HDMI_VCO_MAX_FREQ			12000000000UL
#define HDMI_VCO_MIN_FREQ			8000000000UL

#define HDMI_PCLK_MAX_FREQ			600000000
#define HDMI_PCLK_MIN_FREQ			25000000

#define HDMI_HIGH_FREQ_BIT_CLK_THRESHOLD	3400000000UL
#define HDMI_DIG_FREQ_BIT_CLK_THRESHOLD		1500000000UL
#define HDMI_MID_FREQ_BIT_CLK_THRESHOLD		750000000UL
#define HDMI_CORECLK_DIV			5
#define HDMI_DEFAULT_REF_CLOCK			19200000
#define HDMI_PLL_CMP_CNT			1024

#define HDMI_PLL_POLL_MAX_READS			100
#define HDMI_PLL_POLL_TIMEOUT_US		150

#define HDMI_NUM_TX_CHANNEL			4

struct hdmi_pll_8996 {
	struct platform_device *pdev;
	struct clk_hw clk_hw;

	/* pll mmio base */
	void __iomem *mmio_qserdes_com;
	/* tx channel base */
	void __iomem *mmio_qserdes_tx[HDMI_NUM_TX_CHANNEL];
};

#define hw_clk_to_pll(x) container_of(x, struct hdmi_pll_8996, clk_hw)

struct hdmi_8996_phy_pll_reg_cfg {
	u32 tx_lx_lane_mode[HDMI_NUM_TX_CHANNEL];
	u32 tx_lx_tx_band[HDMI_NUM_TX_CHANNEL];
	u32 com_svs_mode_clk_sel;
	u32 com_hsclk_sel;
	u32 com_pll_cctrl_mode0;
	u32 com_pll_rctrl_mode0;
	u32 com_cp_ctrl_mode0;
	u32 com_dec_start_mode0;
	u32 com_div_frac_start1_mode0;
	u32 com_div_frac_start2_mode0;
	u32 com_div_frac_start3_mode0;
	u32 com_integloop_gain0_mode0;
	u32 com_integloop_gain1_mode0;
	u32 com_lock_cmp_en;
	u32 com_lock_cmp1_mode0;
	u32 com_lock_cmp2_mode0;
	u32 com_lock_cmp3_mode0;
	u32 com_core_clk_en;
	u32 com_coreclk_div;
	u32 com_vco_tune_ctrl;

	u32 tx_lx_tx_drv_lvl[HDMI_NUM_TX_CHANNEL];
	u32 tx_lx_tx_emp_post1_lvl[HDMI_NUM_TX_CHANNEL];
	u32 tx_lx_vmode_ctrl1[HDMI_NUM_TX_CHANNEL];
	u32 tx_lx_vmode_ctrl2[HDMI_NUM_TX_CHANNEL];
	u32 tx_lx_res_code_lane_tx[HDMI_NUM_TX_CHANNEL];
	u32 tx_lx_hp_pd_enables[HDMI_NUM_TX_CHANNEL];

	u32 phy_mode;
};

struct hdmi_8996_post_divider {
	u64 vco_freq;
	int hsclk_divsel;
	int vco_ratio;
	int tx_band_sel;
	int half_rate_mode;
};

static inline struct hdmi_phy *pll_get_phy(struct hdmi_pll_8996 *pll)
{
	return platform_get_drvdata(pll->pdev);
}

static inline void hdmi_pll_write(struct hdmi_pll_8996 *pll, int offset,
				  u32 data)
{
	msm_writel(data, pll->mmio_qserdes_com + offset);
}

static inline u32 hdmi_pll_read(struct hdmi_pll_8996 *pll, int offset)
{
	return msm_readl(pll->mmio_qserdes_com + offset);
}

static inline void hdmi_tx_chan_write(struct hdmi_pll_8996 *pll, int channel,
				      int offset, int data)
{
	 msm_writel(data, pll->mmio_qserdes_tx[channel] + offset);
}

static inline u32 pll_get_cpctrl(u64 frac_start, unsigned long ref_clk,
				 bool gen_ssc)
{
	if ((frac_start != 0) || gen_ssc)
		return (11000000 / (ref_clk / 20));

	return 0x23;
}

static inline u32 pll_get_rctrl(u64 frac_start, bool gen_ssc)
{
	if ((frac_start != 0) || gen_ssc)
		return 0x16;

	return 0x10;
}

static inline u32 pll_get_cctrl(u64 frac_start, bool gen_ssc)
{
	if ((frac_start != 0) || gen_ssc)
		return 0x28;

	return 0x1;
}

static inline u32 pll_get_integloop_gain(u64 frac_start, u64 bclk, u32 ref_clk,
					 bool gen_ssc)
{
	int digclk_divsel = bclk >= HDMI_DIG_FREQ_BIT_CLK_THRESHOLD ? 1 : 2;
	u64 base;

	if ((frac_start != 0) || gen_ssc)
		base = (64 * ref_clk) / HDMI_DEFAULT_REF_CLOCK;
	else
		base = (1022 * ref_clk) / 100;

	base <<= digclk_divsel;

	return (base <= 2046 ? base : 2046);
}

static inline u32 pll_get_pll_cmp(u64 fdata, unsigned long ref_clk)
{
	u64 dividend = HDMI_PLL_CMP_CNT * fdata;
	u32 divisor = ref_clk * 10;
	u32 rem;

	rem = do_div(dividend, divisor);
	if (rem > (divisor >> 1))
		dividend++;

	return dividend - 1;
}

static inline u64 pll_cmp_to_fdata(u32 pll_cmp, unsigned long ref_clk)
{
	u64 fdata = ((u64)pll_cmp) * ref_clk * 10;

	do_div(fdata, HDMI_PLL_CMP_CNT);

	return fdata;
}

static int pll_get_post_div(struct hdmi_8996_post_divider *pd, u64 bclk)
{
	int ratio[] = { 2, 3, 4, 5, 6, 9, 10, 12, 14, 15, 20, 21, 25, 28, 35 };
	int hs_divsel[] = { 0, 4, 8, 12, 1, 5, 2, 9, 3, 13, 10, 7, 14, 11, 15 };
	int tx_band_sel[] = { 0, 1, 2, 3 };
	u64 vco_freq[60];
	u64 vco, vco_optimal;
	int half_rate_mode = 0;
	int vco_optimal_index, vco_freq_index;
	int i, j;

retry:
	vco_optimal = HDMI_VCO_MAX_FREQ;
	vco_optimal_index = -1;
	vco_freq_index = 0;
	for (i = 0; i < 15; i++) {
		for (j = 0; j < 4; j++) {
			u32 ratio_mult = ratio[i] << tx_band_sel[j];

			vco = bclk >> half_rate_mode;
			vco *= ratio_mult;
			vco_freq[vco_freq_index++] = vco;
		}
	}

	for (i = 0; i < 60; i++) {
		u64 vco_tmp = vco_freq[i];

		if ((vco_tmp >= HDMI_VCO_MIN_FREQ) &&
		    (vco_tmp <= vco_optimal)) {
			vco_optimal = vco_tmp;
			vco_optimal_index = i;
		}
	}

	if (vco_optimal_index == -1) {
		if (!half_rate_mode) {
			half_rate_mode = 1;
			goto retry;
		}
	} else {
		pd->vco_freq = vco_optimal;
		pd->tx_band_sel = tx_band_sel[vco_optimal_index % 4];
		pd->vco_ratio = ratio[vco_optimal_index / 4];
		pd->hsclk_divsel = hs_divsel[vco_optimal_index / 4];

		return 0;
	}

	return -EINVAL;
}

static int pll_calculate(unsigned long pix_clk, unsigned long ref_clk,
			 struct hdmi_8996_phy_pll_reg_cfg *cfg)
{
	struct hdmi_8996_post_divider pd;
	u64 bclk;
	u64 tmds_clk;
	u64 dec_start;
	u64 frac_start;
	u64 fdata;
	u32 pll_divisor;
	u32 rem;
	u32 cpctrl;
	u32 rctrl;
	u32 cctrl;
	u32 integloop_gain;
	u32 pll_cmp;
	int i, ret;

	/* bit clk = 10 * pix_clk */
	bclk = ((u64)pix_clk) * 10;

	if (bclk > HDMI_HIGH_FREQ_BIT_CLK_THRESHOLD)
		tmds_clk = pix_clk >> 2;
	else
		tmds_clk = pix_clk;

	ret = pll_get_post_div(&pd, bclk);
	if (ret)
		return ret;

	dec_start = pd.vco_freq;
	pll_divisor = 4 * ref_clk;
	do_div(dec_start, pll_divisor);

	frac_start = pd.vco_freq * (1 << 20);

	rem = do_div(frac_start, pll_divisor);
	frac_start -= dec_start * (1 << 20);
	if (rem > (pll_divisor >> 1))
		frac_start++;

	cpctrl = pll_get_cpctrl(frac_start, ref_clk, false);
	rctrl = pll_get_rctrl(frac_start, false);
	cctrl = pll_get_cctrl(frac_start, false);
	integloop_gain = pll_get_integloop_gain(frac_start, bclk,
						ref_clk, false);

	fdata = pd.vco_freq;
	do_div(fdata, pd.vco_ratio);

	pll_cmp = pll_get_pll_cmp(fdata, ref_clk);

	DBG("VCO freq: %llu", pd.vco_freq);
	DBG("fdata: %llu", fdata);
	DBG("pix_clk: %lu", pix_clk);
	DBG("tmds clk: %llu", tmds_clk);
	DBG("HSCLK_SEL: %d", pd.hsclk_divsel);
	DBG("DEC_START: %llu", dec_start);
	DBG("DIV_FRAC_START: %llu", frac_start);
	DBG("PLL_CPCTRL: %u", cpctrl);
	DBG("PLL_RCTRL: %u", rctrl);
	DBG("PLL_CCTRL: %u", cctrl);
	DBG("INTEGLOOP_GAIN: %u", integloop_gain);
	DBG("TX_BAND: %d", pd.tx_band_sel);
	DBG("PLL_CMP: %u", pll_cmp);

	/* Convert these values to register specific values */
	if (bclk > HDMI_DIG_FREQ_BIT_CLK_THRESHOLD)
		cfg->com_svs_mode_clk_sel = 1;
	else
		cfg->com_svs_mode_clk_sel = 2;

	cfg->com_hsclk_sel = (0x20 | pd.hsclk_divsel);
	cfg->com_pll_cctrl_mode0 = cctrl;
	cfg->com_pll_rctrl_mode0 = rctrl;
	cfg->com_cp_ctrl_mode0 = cpctrl;
	cfg->com_dec_start_mode0 = dec_start;
	cfg->com_div_frac_start1_mode0 = (frac_start & 0xff);
	cfg->com_div_frac_start2_mode0 = ((frac_start & 0xff00) >> 8);
	cfg->com_div_frac_start3_mode0 = ((frac_start & 0xf0000) >> 16);
	cfg->com_integloop_gain0_mode0 = (integloop_gain & 0xff);
	cfg->com_integloop_gain1_mode0 = ((integloop_gain & 0xf00) >> 8);
	cfg->com_lock_cmp1_mode0 = (pll_cmp & 0xff);
	cfg->com_lock_cmp2_mode0 = ((pll_cmp & 0xff00) >> 8);
	cfg->com_lock_cmp3_mode0 = ((pll_cmp & 0x30000) >> 16);
	cfg->com_lock_cmp_en = 0x0;
	cfg->com_core_clk_en = 0x2c;
	cfg->com_coreclk_div = HDMI_CORECLK_DIV;
	cfg->phy_mode = (bclk > HDMI_HIGH_FREQ_BIT_CLK_THRESHOLD) ? 0x10 : 0x0;
	cfg->com_vco_tune_ctrl = 0x0;

	cfg->tx_lx_lane_mode[0] =
		cfg->tx_lx_lane_mode[2] = 0x43;

	cfg->tx_lx_hp_pd_enables[0] =
		cfg->tx_lx_hp_pd_enables[1] =
		cfg->tx_lx_hp_pd_enables[2] = 0x0c;
	cfg->tx_lx_hp_pd_enables[3] = 0x3;

	for (i = 0; i < HDMI_NUM_TX_CHANNEL; i++)
		cfg->tx_lx_tx_band[i] = pd.tx_band_sel + 4;

	if (bclk > HDMI_HIGH_FREQ_BIT_CLK_THRESHOLD) {
		cfg->tx_lx_tx_drv_lvl[0] =
			cfg->tx_lx_tx_drv_lvl[1] =
			cfg->tx_lx_tx_drv_lvl[2] = 0x25;
		cfg->tx_lx_tx_drv_lvl[3] = 0x22;

		cfg->tx_lx_tx_emp_post1_lvl[0] =
			cfg->tx_lx_tx_emp_post1_lvl[1] =
			cfg->tx_lx_tx_emp_post1_lvl[2] = 0x23;
		cfg->tx_lx_tx_emp_post1_lvl[3] = 0x27;

		cfg->tx_lx_vmode_ctrl1[0] =
			cfg->tx_lx_vmode_ctrl1[1] =
			cfg->tx_lx_vmode_ctrl1[2] =
			cfg->tx_lx_vmode_ctrl1[3] = 0x00;

		cfg->tx_lx_vmode_ctrl2[0] =
			cfg->tx_lx_vmode_ctrl2[1] =
			cfg->tx_lx_vmode_ctrl2[2] = 0x0D;

		cfg->tx_lx_vmode_ctrl2[3] = 0x00;
	} else if (bclk > HDMI_MID_FREQ_BIT_CLK_THRESHOLD) {
		for (i = 0; i < HDMI_NUM_TX_CHANNEL; i++) {
			cfg->tx_lx_tx_drv_lvl[i] = 0x25;
			cfg->tx_lx_tx_emp_post1_lvl[i] = 0x23;
			cfg->tx_lx_vmode_ctrl1[i] = 0x00;
		}

		cfg->tx_lx_vmode_ctrl2[0] =
			cfg->tx_lx_vmode_ctrl2[1] =
			cfg->tx_lx_vmode_ctrl2[2] = 0x0D;
		cfg->tx_lx_vmode_ctrl2[3] = 0x00;
	} else {
		for (i = 0; i < HDMI_NUM_TX_CHANNEL; i++) {
			cfg->tx_lx_tx_drv_lvl[i] = 0x20;
			cfg->tx_lx_tx_emp_post1_lvl[i] = 0x20;
			cfg->tx_lx_vmode_ctrl1[i] = 0x00;
			cfg->tx_lx_vmode_ctrl2[i] = 0x0E;
		}
	}

	DBG("com_svs_mode_clk_sel = 0x%x", cfg->com_svs_mode_clk_sel);
	DBG("com_hsclk_sel = 0x%x", cfg->com_hsclk_sel);
	DBG("com_lock_cmp_en = 0x%x", cfg->com_lock_cmp_en);
	DBG("com_pll_cctrl_mode0 = 0x%x", cfg->com_pll_cctrl_mode0);
	DBG("com_pll_rctrl_mode0 = 0x%x", cfg->com_pll_rctrl_mode0);
	DBG("com_cp_ctrl_mode0 = 0x%x", cfg->com_cp_ctrl_mode0);
	DBG("com_dec_start_mode0 = 0x%x", cfg->com_dec_start_mode0);
	DBG("com_div_frac_start1_mode0 = 0x%x", cfg->com_div_frac_start1_mode0);
	DBG("com_div_frac_start2_mode0 = 0x%x", cfg->com_div_frac_start2_mode0);
	DBG("com_div_frac_start3_mode0 = 0x%x", cfg->com_div_frac_start3_mode0);
	DBG("com_integloop_gain0_mode0 = 0x%x", cfg->com_integloop_gain0_mode0);
	DBG("com_integloop_gain1_mode0 = 0x%x", cfg->com_integloop_gain1_mode0);
	DBG("com_lock_cmp1_mode0 = 0x%x", cfg->com_lock_cmp1_mode0);
	DBG("com_lock_cmp2_mode0 = 0x%x", cfg->com_lock_cmp2_mode0);
	DBG("com_lock_cmp3_mode0 = 0x%x", cfg->com_lock_cmp3_mode0);
	DBG("com_core_clk_en = 0x%x", cfg->com_core_clk_en);
	DBG("com_coreclk_div = 0x%x", cfg->com_coreclk_div);
	DBG("phy_mode = 0x%x", cfg->phy_mode);

	DBG("tx_l0_lane_mode = 0x%x", cfg->tx_lx_lane_mode[0]);
	DBG("tx_l2_lane_mode = 0x%x", cfg->tx_lx_lane_mode[2]);

	for (i = 0; i < HDMI_NUM_TX_CHANNEL; i++) {
		DBG("tx_l%d_tx_band = 0x%x", i, cfg->tx_lx_tx_band[i]);
		DBG("tx_l%d_tx_drv_lvl = 0x%x", i, cfg->tx_lx_tx_drv_lvl[i]);
		DBG("tx_l%d_tx_emp_post1_lvl = 0x%x", i,
		    cfg->tx_lx_tx_emp_post1_lvl[i]);
		DBG("tx_l%d_vmode_ctrl1 = 0x%x", i, cfg->tx_lx_vmode_ctrl1[i]);
		DBG("tx_l%d_vmode_ctrl2 = 0x%x", i, cfg->tx_lx_vmode_ctrl2[i]);
	}

	return 0;
}

static int hdmi_8996_pll_set_clk_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long parent_rate)
{
	struct hdmi_pll_8996 *pll = hw_clk_to_pll(hw);
	struct hdmi_phy *phy = pll_get_phy(pll);
	struct hdmi_8996_phy_pll_reg_cfg cfg;
	int i, ret;

	memset(&cfg, 0x00, sizeof(cfg));

	ret = pll_calculate(rate, parent_rate, &cfg);
	if (ret) {
		DRM_ERROR("PLL calculation failed\n");
		return ret;
	}

	/* Initially shut down PHY */
	DBG("Disabling PHY");
	hdmi_phy_write(phy, REG_HDMI_8996_PHY_PD_CTL, 0x0);
	udelay(500);

	/* Power up sequence */
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_BG_CTRL, 0x04);

	hdmi_phy_write(phy, REG_HDMI_8996_PHY_PD_CTL, 0x1);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_RESETSM_CNTRL, 0x20);
	hdmi_phy_write(phy, REG_HDMI_8996_PHY_TX0_TX1_LANE_CTL, 0x0F);
	hdmi_phy_write(phy, REG_HDMI_8996_PHY_TX2_TX3_LANE_CTL, 0x0F);

	for (i = 0; i < HDMI_NUM_TX_CHANNEL; i++) {
		hdmi_tx_chan_write(pll, i,
				   REG_HDMI_PHY_QSERDES_TX_LX_CLKBUF_ENABLE,
				   0x03);
		hdmi_tx_chan_write(pll, i,
				   REG_HDMI_PHY_QSERDES_TX_LX_TX_BAND,
				   cfg.tx_lx_tx_band[i]);
		hdmi_tx_chan_write(pll, i,
				   REG_HDMI_PHY_QSERDES_TX_LX_RESET_TSYNC_EN,
				   0x03);
	}

	hdmi_tx_chan_write(pll, 0, REG_HDMI_PHY_QSERDES_TX_LX_LANE_MODE,
			   cfg.tx_lx_lane_mode[0]);
	hdmi_tx_chan_write(pll, 2, REG_HDMI_PHY_QSERDES_TX_LX_LANE_MODE,
			   cfg.tx_lx_lane_mode[2]);

	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_SYSCLK_BUF_ENABLE, 0x1E);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_BIAS_EN_CLKBUFLR_EN, 0x07);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_SYSCLK_EN_SEL, 0x37);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_SYS_CLK_CTRL, 0x02);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_CLK_ENABLE1, 0x0E);

	/* Bypass VCO calibration */
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_SVS_MODE_CLK_SEL,
		       cfg.com_svs_mode_clk_sel);

	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_BG_TRIM, 0x0F);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_PLL_IVCO, 0x0F);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_VCO_TUNE_CTRL,
		       cfg.com_vco_tune_ctrl);

	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_BG_CTRL, 0x06);

	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_CLK_SELECT, 0x30);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_HSCLK_SEL,
		       cfg.com_hsclk_sel);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_LOCK_CMP_EN,
		       cfg.com_lock_cmp_en);

	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_PLL_CCTRL_MODE0,
		       cfg.com_pll_cctrl_mode0);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_PLL_RCTRL_MODE0,
		       cfg.com_pll_rctrl_mode0);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_CP_CTRL_MODE0,
		       cfg.com_cp_ctrl_mode0);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_DEC_START_MODE0,
		       cfg.com_dec_start_mode0);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_DIV_FRAC_START1_MODE0,
		       cfg.com_div_frac_start1_mode0);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_DIV_FRAC_START2_MODE0,
		       cfg.com_div_frac_start2_mode0);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_DIV_FRAC_START3_MODE0,
		       cfg.com_div_frac_start3_mode0);

	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_INTEGLOOP_GAIN0_MODE0,
		       cfg.com_integloop_gain0_mode0);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_INTEGLOOP_GAIN1_MODE0,
		       cfg.com_integloop_gain1_mode0);

	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_LOCK_CMP1_MODE0,
		       cfg.com_lock_cmp1_mode0);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_LOCK_CMP2_MODE0,
		       cfg.com_lock_cmp2_mode0);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_LOCK_CMP3_MODE0,
		       cfg.com_lock_cmp3_mode0);

	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_VCO_TUNE_MAP, 0x00);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_CORE_CLK_EN,
		       cfg.com_core_clk_en);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_CORECLK_DIV,
		       cfg.com_coreclk_div);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_CMN_CONFIG, 0x02);

	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_RESCODE_DIV_NUM, 0x15);

	/* TX lanes setup (TX 0/1/2/3) */
	for (i = 0; i < HDMI_NUM_TX_CHANNEL; i++) {
		hdmi_tx_chan_write(pll, i,
				   REG_HDMI_PHY_QSERDES_TX_LX_TX_DRV_LVL,
				   cfg.tx_lx_tx_drv_lvl[i]);
		hdmi_tx_chan_write(pll, i,
				   REG_HDMI_PHY_QSERDES_TX_LX_TX_EMP_POST1_LVL,
				   cfg.tx_lx_tx_emp_post1_lvl[i]);
		hdmi_tx_chan_write(pll, i,
				   REG_HDMI_PHY_QSERDES_TX_LX_VMODE_CTRL1,
				   cfg.tx_lx_vmode_ctrl1[i]);
		hdmi_tx_chan_write(pll, i,
				   REG_HDMI_PHY_QSERDES_TX_LX_VMODE_CTRL2,
				   cfg.tx_lx_vmode_ctrl2[i]);
		hdmi_tx_chan_write(pll, i,
				   REG_HDMI_PHY_QSERDES_TX_LX_TX_DRV_LVL_OFFSET,
				   0x00);
		hdmi_tx_chan_write(pll, i,
			REG_HDMI_PHY_QSERDES_TX_LX_RES_CODE_LANE_OFFSET,
			0x00);
		hdmi_tx_chan_write(pll, i,
			REG_HDMI_PHY_QSERDES_TX_LX_TRAN_DRVR_EMP_EN,
			0x03);
		hdmi_tx_chan_write(pll, i,
			REG_HDMI_PHY_QSERDES_TX_LX_PARRATE_REC_DETECT_IDLE_EN,
			0x40);
		hdmi_tx_chan_write(pll, i,
				   REG_HDMI_PHY_QSERDES_TX_LX_HP_PD_ENABLES,
				   cfg.tx_lx_hp_pd_enables[i]);
	}

	hdmi_phy_write(phy, REG_HDMI_8996_PHY_MODE, cfg.phy_mode);
	hdmi_phy_write(phy, REG_HDMI_8996_PHY_PD_CTL, 0x1F);

	/*
	 * Ensure that vco configuration gets flushed to hardware before
	 * enabling the PLL
	 */
	wmb();

	return 0;
}

static int hdmi_8996_phy_ready_status(struct hdmi_phy *phy)
{
	u32 nb_tries = HDMI_PLL_POLL_MAX_READS;
	unsigned long timeout = HDMI_PLL_POLL_TIMEOUT_US;
	u32 status;
	int phy_ready = 0;

	DBG("Waiting for PHY ready");

	while (nb_tries--) {
		status = hdmi_phy_read(phy, REG_HDMI_8996_PHY_STATUS);
		phy_ready = status & BIT(0);

		if (phy_ready)
			break;

		udelay(timeout);
	}

	DBG("PHY is %sready", phy_ready ? "" : "*not* ");

	return phy_ready;
}

static int hdmi_8996_pll_lock_status(struct hdmi_pll_8996 *pll)
{
	u32 status;
	int nb_tries = HDMI_PLL_POLL_MAX_READS;
	unsigned long timeout = HDMI_PLL_POLL_TIMEOUT_US;
	int pll_locked = 0;

	DBG("Waiting for PLL lock");

	while (nb_tries--) {
		status = hdmi_pll_read(pll,
				       REG_HDMI_PHY_QSERDES_COM_C_READY_STATUS);
		pll_locked = status & BIT(0);

		if (pll_locked)
			break;

		udelay(timeout);
	}

	DBG("HDMI PLL is %slocked", pll_locked ? "" : "*not* ");

	return pll_locked;
}

static int hdmi_8996_pll_prepare(struct clk_hw *hw)
{
	struct hdmi_pll_8996 *pll = hw_clk_to_pll(hw);
	struct hdmi_phy *phy = pll_get_phy(pll);
	int i, ret = 0;

	hdmi_phy_write(phy, REG_HDMI_8996_PHY_CFG, 0x1);
	udelay(100);

	hdmi_phy_write(phy, REG_HDMI_8996_PHY_CFG, 0x19);
	udelay(100);

	ret = hdmi_8996_pll_lock_status(pll);
	if (!ret)
		return ret;

	for (i = 0; i < HDMI_NUM_TX_CHANNEL; i++)
		hdmi_tx_chan_write(pll, i,
			REG_HDMI_PHY_QSERDES_TX_LX_HIGHZ_TRANSCEIVEREN_BIAS_DRVR_EN,
			0x6F);

	/* Disable SSC */
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_SSC_PER1, 0x0);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_SSC_PER2, 0x0);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_SSC_STEP_SIZE1, 0x0);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_SSC_STEP_SIZE2, 0x0);
	hdmi_pll_write(pll, REG_HDMI_PHY_QSERDES_COM_SSC_EN_CENTER, 0x2);

	ret = hdmi_8996_phy_ready_status(phy);
	if (!ret)
		return ret;

	/* Restart the retiming buffer */
	hdmi_phy_write(phy, REG_HDMI_8996_PHY_CFG, 0x18);
	udelay(1);
	hdmi_phy_write(phy, REG_HDMI_8996_PHY_CFG, 0x19);

	return 0;
}

static long hdmi_8996_pll_round_rate(struct clk_hw *hw,
				     unsigned long rate,
				     unsigned long *parent_rate)
{
	if (rate < HDMI_PCLK_MIN_FREQ)
		return HDMI_PCLK_MIN_FREQ;
	else if (rate > HDMI_PCLK_MAX_FREQ)
		return HDMI_PCLK_MAX_FREQ;
	else
		return rate;
}

static unsigned long hdmi_8996_pll_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct hdmi_pll_8996 *pll = hw_clk_to_pll(hw);
	u64 fdata;
	u32 cmp1, cmp2, cmp3, pll_cmp;

	cmp1 = hdmi_pll_read(pll, REG_HDMI_PHY_QSERDES_COM_LOCK_CMP1_MODE0);
	cmp2 = hdmi_pll_read(pll, REG_HDMI_PHY_QSERDES_COM_LOCK_CMP2_MODE0);
	cmp3 = hdmi_pll_read(pll, REG_HDMI_PHY_QSERDES_COM_LOCK_CMP3_MODE0);

	pll_cmp = cmp1 | (cmp2 << 8) | (cmp3 << 16);

	fdata = pll_cmp_to_fdata(pll_cmp + 1, parent_rate);

	do_div(fdata, 10);

	return fdata;
}

static void hdmi_8996_pll_unprepare(struct clk_hw *hw)
{
	struct hdmi_pll_8996 *pll = hw_clk_to_pll(hw);
	struct hdmi_phy *phy = pll_get_phy(pll);

	hdmi_phy_write(phy, REG_HDMI_8996_PHY_CFG, 0x6);
	usleep_range(100, 150);
}

static int hdmi_8996_pll_is_enabled(struct clk_hw *hw)
{
	struct hdmi_pll_8996 *pll = hw_clk_to_pll(hw);
	u32 status;
	int pll_locked;

	status = hdmi_pll_read(pll, REG_HDMI_PHY_QSERDES_COM_C_READY_STATUS);
	pll_locked = status & BIT(0);

	return pll_locked;
}

static struct clk_ops hdmi_8996_pll_ops = {
	.set_rate = hdmi_8996_pll_set_clk_rate,
	.round_rate = hdmi_8996_pll_round_rate,
	.recalc_rate = hdmi_8996_pll_recalc_rate,
	.prepare = hdmi_8996_pll_prepare,
	.unprepare = hdmi_8996_pll_unprepare,
	.is_enabled = hdmi_8996_pll_is_enabled,
};

static const char * const hdmi_pll_parents[] = {
	"xo",
};

static struct clk_init_data pll_init = {
	.name = "hdmipll",
	.ops = &hdmi_8996_pll_ops,
	.parent_names = hdmi_pll_parents,
	.num_parents = ARRAY_SIZE(hdmi_pll_parents),
	.flags = CLK_IGNORE_UNUSED,
};

int msm_hdmi_pll_8996_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hdmi_pll_8996 *pll;
	struct clk *clk;
	int i;

	pll = devm_kzalloc(dev, sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return -ENOMEM;

	pll->pdev = pdev;

	pll->mmio_qserdes_com = msm_ioremap(pdev, "hdmi_pll", "HDMI_PLL");
	if (IS_ERR(pll->mmio_qserdes_com)) {
		dev_err(dev, "failed to map pll base\n");
		return -ENOMEM;
	}

	for (i = 0; i < HDMI_NUM_TX_CHANNEL; i++) {
		char name[32], label[32];

		snprintf(name, sizeof(name), "hdmi_tx_l%d", i);
		snprintf(label, sizeof(label), "HDMI_TX_L%d", i);

		pll->mmio_qserdes_tx[i] = msm_ioremap(pdev, name, label);
		if (IS_ERR(pll->mmio_qserdes_tx[i])) {
			dev_err(dev, "failed to map pll base\n");
			return -ENOMEM;
		}
	}
	pll->clk_hw.init = &pll_init;

	clk = devm_clk_register(dev, &pll->clk_hw);
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to register pll clock\n");
		return -EINVAL;
	}

	return 0;
}

static const char * const hdmi_phy_8996_reg_names[] = {
	"vddio",
	"vcca",
};

static const char * const hdmi_phy_8996_clk_names[] = {
	"mmagic_iface_clk",
	"iface_clk",
	"ref_clk",
};

const struct hdmi_phy_cfg msm_hdmi_phy_8996_cfg = {
	.type = MSM_HDMI_PHY_8996,
	.reg_names = hdmi_phy_8996_reg_names,
	.num_regs = ARRAY_SIZE(hdmi_phy_8996_reg_names),
	.clk_names = hdmi_phy_8996_clk_names,
	.num_clks = ARRAY_SIZE(hdmi_phy_8996_clk_names),
};

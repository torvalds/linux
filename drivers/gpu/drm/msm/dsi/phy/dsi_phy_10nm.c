/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2018, The Linux Foundation
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/iopoll.h>

#include "dsi_phy.h"
#include "dsi.xml.h"
#include "dsi_phy_10nm.xml.h"

/*
 * DSI PLL 10nm - clock diagram (eg: DSI0):
 *
 *           dsi0_pll_out_div_clk  dsi0_pll_bit_clk
 *                              |                |
 *                              |                |
 *                 +---------+  |  +----------+  |  +----+
 *  dsi0vco_clk ---| out_div |--o--| divl_3_0 |--o--| /8 |-- dsi0_phy_pll_out_byteclk
 *                 +---------+  |  +----------+  |  +----+
 *                              |                |
 *                              |                |         dsi0_pll_by_2_bit_clk
 *                              |                |          |
 *                              |                |  +----+  |  |\  dsi0_pclk_mux
 *                              |                |--| /2 |--o--| \   |
 *                              |                |  +----+     |  \  |  +---------+
 *                              |                --------------|  |--o--| div_7_4 |-- dsi0_phy_pll_out_dsiclk
 *                              |------------------------------|  /     +---------+
 *                              |          +-----+             | /
 *                              -----------| /4? |--o----------|/
 *                                         +-----+  |           |
 *                                                  |           |dsiclk_sel
 *                                                  |
 *                                                  dsi0_pll_post_out_div_clk
 */

#define VCO_REF_CLK_RATE		19200000
#define FRAC_BITS 18

/* v3.0.0 10nm implementation that requires the old timings settings */
#define DSI_PHY_10NM_QUIRK_OLD_TIMINGS	BIT(0)

struct dsi_pll_config {
	bool enable_ssc;
	bool ssc_center;
	u32 ssc_freq;
	u32 ssc_offset;
	u32 ssc_adj_per;

	/* out */
	u32 pll_prop_gain_rate;
	u32 decimal_div_start;
	u32 frac_div_start;
	u32 pll_clock_inverters;
	u32 ssc_stepsize;
	u32 ssc_div_per;
};

struct pll_10nm_cached_state {
	unsigned long vco_rate;
	u8 bit_clk_div;
	u8 pix_clk_div;
	u8 pll_out_div;
	u8 pll_mux;
};

struct dsi_pll_10nm {
	struct clk_hw clk_hw;

	struct msm_dsi_phy *phy;

	u64 vco_current_rate;

	/* protects REG_DSI_10nm_PHY_CMN_CLK_CFG0 register */
	spinlock_t postdiv_lock;

	struct pll_10nm_cached_state cached_state;

	struct dsi_pll_10nm *slave;
};

#define to_pll_10nm(x)	container_of(x, struct dsi_pll_10nm, clk_hw)

/**
 * struct dsi_phy_10nm_tuning_cfg - Holds 10nm PHY tuning config parameters.
 * @rescode_offset_top: Offset for pull-up legs rescode.
 * @rescode_offset_bot: Offset for pull-down legs rescode.
 * @vreg_ctrl: vreg ctrl to drive LDO level
 */
struct dsi_phy_10nm_tuning_cfg {
	u8 rescode_offset_top[DSI_LANE_MAX];
	u8 rescode_offset_bot[DSI_LANE_MAX];
	u8 vreg_ctrl;
};

/*
 * Global list of private DSI PLL struct pointers. We need this for bonded DSI
 * mode, where the master PLL's clk_ops needs access the slave's private data
 */
static struct dsi_pll_10nm *pll_10nm_list[DSI_MAX];

static void dsi_pll_setup_config(struct dsi_pll_config *config)
{
	config->ssc_freq = 31500;
	config->ssc_offset = 5000;
	config->ssc_adj_per = 2;

	config->enable_ssc = false;
	config->ssc_center = false;
}

static void dsi_pll_calc_dec_frac(struct dsi_pll_10nm *pll, struct dsi_pll_config *config)
{
	u64 fref = VCO_REF_CLK_RATE;
	u64 pll_freq;
	u64 divider;
	u64 dec, dec_multiple;
	u32 frac;
	u64 multiplier;

	pll_freq = pll->vco_current_rate;

	divider = fref * 2;

	multiplier = 1 << FRAC_BITS;
	dec_multiple = div_u64(pll_freq * multiplier, divider);
	dec = div_u64_rem(dec_multiple, multiplier, &frac);

	if (pll_freq <= 1900000000UL)
		config->pll_prop_gain_rate = 8;
	else if (pll_freq <= 3000000000UL)
		config->pll_prop_gain_rate = 10;
	else
		config->pll_prop_gain_rate = 12;
	if (pll_freq < 1100000000UL)
		config->pll_clock_inverters = 8;
	else
		config->pll_clock_inverters = 0;

	config->decimal_div_start = dec;
	config->frac_div_start = frac;
}

#define SSC_CENTER		BIT(0)
#define SSC_EN			BIT(1)

static void dsi_pll_calc_ssc(struct dsi_pll_10nm *pll, struct dsi_pll_config *config)
{
	u32 ssc_per;
	u32 ssc_mod;
	u64 ssc_step_size;
	u64 frac;

	if (!config->enable_ssc) {
		DBG("SSC not enabled\n");
		return;
	}

	ssc_per = DIV_ROUND_CLOSEST(VCO_REF_CLK_RATE, config->ssc_freq) / 2 - 1;
	ssc_mod = (ssc_per + 1) % (config->ssc_adj_per + 1);
	ssc_per -= ssc_mod;

	frac = config->frac_div_start;
	ssc_step_size = config->decimal_div_start;
	ssc_step_size *= (1 << FRAC_BITS);
	ssc_step_size += frac;
	ssc_step_size *= config->ssc_offset;
	ssc_step_size *= (config->ssc_adj_per + 1);
	ssc_step_size = div_u64(ssc_step_size, (ssc_per + 1));
	ssc_step_size = DIV_ROUND_CLOSEST_ULL(ssc_step_size, 1000000);

	config->ssc_div_per = ssc_per;
	config->ssc_stepsize = ssc_step_size;

	pr_debug("SCC: Dec:%d, frac:%llu, frac_bits:%d\n",
		 config->decimal_div_start, frac, FRAC_BITS);
	pr_debug("SSC: div_per:0x%X, stepsize:0x%X, adjper:0x%X\n",
		 ssc_per, (u32)ssc_step_size, config->ssc_adj_per);
}

static void dsi_pll_ssc_commit(struct dsi_pll_10nm *pll, struct dsi_pll_config *config)
{
	void __iomem *base = pll->phy->pll_base;

	if (config->enable_ssc) {
		pr_debug("SSC is enabled\n");

		dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_SSC_STEPSIZE_LOW_1,
			      config->ssc_stepsize & 0xff);
		dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_SSC_STEPSIZE_HIGH_1,
			      config->ssc_stepsize >> 8);
		dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_SSC_DIV_PER_LOW_1,
			      config->ssc_div_per & 0xff);
		dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_SSC_DIV_PER_HIGH_1,
			      config->ssc_div_per >> 8);
		dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_SSC_DIV_ADJPER_LOW_1,
			      config->ssc_adj_per & 0xff);
		dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_SSC_DIV_ADJPER_HIGH_1,
			      config->ssc_adj_per >> 8);
		dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_SSC_CONTROL,
			      SSC_EN | (config->ssc_center ? SSC_CENTER : 0));
	}
}

static void dsi_pll_config_hzindep_reg(struct dsi_pll_10nm *pll)
{
	void __iomem *base = pll->phy->pll_base;

	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_ANALOG_CONTROLS_ONE, 0x80);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_ANALOG_CONTROLS_TWO, 0x03);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_ANALOG_CONTROLS_THREE, 0x00);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_DSM_DIVIDER, 0x00);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_FEEDBACK_DIVIDER, 0x4e);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_CALIBRATION_SETTINGS, 0x40);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_BAND_SEL_CAL_SETTINGS_THREE,
		      0xba);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_FREQ_DETECT_SETTINGS_ONE,
		      0x0c);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_OUTDIV, 0x00);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_CORE_OVERRIDE, 0x00);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_PLL_DIGITAL_TIMERS_TWO,
		      0x08);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_PLL_PROP_GAIN_RATE_1, 0x08);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_PLL_BAND_SET_RATE_1, 0xc0);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_PLL_INT_GAIN_IFILT_BAND_1,
		      0xfa);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_PLL_FL_INT_GAIN_PFILT_BAND_1,
		      0x4c);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_PLL_LOCK_OVERRIDE, 0x80);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_PFILT, 0x29);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_IFILT, 0x3f);
}

static void dsi_pll_commit(struct dsi_pll_10nm *pll, struct dsi_pll_config *config)
{
	void __iomem *base = pll->phy->pll_base;

	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_CORE_INPUT_OVERRIDE, 0x12);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_DECIMAL_DIV_START_1,
		      config->decimal_div_start);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_FRAC_DIV_START_LOW_1,
		      config->frac_div_start & 0xff);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_FRAC_DIV_START_MID_1,
		      (config->frac_div_start & 0xff00) >> 8);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_FRAC_DIV_START_HIGH_1,
		      (config->frac_div_start & 0x30000) >> 16);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_PLL_LOCKDET_RATE_1, 64);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_PLL_LOCK_DELAY, 0x06);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_CMODE, 0x10);
	dsi_phy_write(base + REG_DSI_10nm_PHY_PLL_CLOCK_INVERTERS,
		      config->pll_clock_inverters);
}

static int dsi_pll_10nm_vco_set_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	struct dsi_pll_10nm *pll_10nm = to_pll_10nm(hw);
	struct dsi_pll_config config;

	DBG("DSI PLL%d rate=%lu, parent's=%lu", pll_10nm->phy->id, rate,
	    parent_rate);

	pll_10nm->vco_current_rate = rate;

	dsi_pll_setup_config(&config);

	dsi_pll_calc_dec_frac(pll_10nm, &config);

	dsi_pll_calc_ssc(pll_10nm, &config);

	dsi_pll_commit(pll_10nm, &config);

	dsi_pll_config_hzindep_reg(pll_10nm);

	dsi_pll_ssc_commit(pll_10nm, &config);

	/* flush, ensure all register writes are done*/
	wmb();

	return 0;
}

static int dsi_pll_10nm_lock_status(struct dsi_pll_10nm *pll)
{
	struct device *dev = &pll->phy->pdev->dev;
	int rc;
	u32 status = 0;
	u32 const delay_us = 100;
	u32 const timeout_us = 5000;

	rc = readl_poll_timeout_atomic(pll->phy->pll_base +
				       REG_DSI_10nm_PHY_PLL_COMMON_STATUS_ONE,
				       status,
				       ((status & BIT(0)) > 0),
				       delay_us,
				       timeout_us);
	if (rc)
		DRM_DEV_ERROR(dev, "DSI PLL(%d) lock failed, status=0x%08x\n",
			      pll->phy->id, status);

	return rc;
}

static void dsi_pll_disable_pll_bias(struct dsi_pll_10nm *pll)
{
	u32 data = dsi_phy_read(pll->phy->base + REG_DSI_10nm_PHY_CMN_CTRL_0);

	dsi_phy_write(pll->phy->pll_base + REG_DSI_10nm_PHY_PLL_SYSTEM_MUXES, 0);
	dsi_phy_write(pll->phy->base + REG_DSI_10nm_PHY_CMN_CTRL_0,
		      data & ~BIT(5));
	ndelay(250);
}

static void dsi_pll_enable_pll_bias(struct dsi_pll_10nm *pll)
{
	u32 data = dsi_phy_read(pll->phy->base + REG_DSI_10nm_PHY_CMN_CTRL_0);

	dsi_phy_write(pll->phy->base + REG_DSI_10nm_PHY_CMN_CTRL_0,
		      data | BIT(5));
	dsi_phy_write(pll->phy->pll_base + REG_DSI_10nm_PHY_PLL_SYSTEM_MUXES, 0xc0);
	ndelay(250);
}

static void dsi_pll_disable_global_clk(struct dsi_pll_10nm *pll)
{
	u32 data;

	data = dsi_phy_read(pll->phy->base + REG_DSI_10nm_PHY_CMN_CLK_CFG1);
	dsi_phy_write(pll->phy->base + REG_DSI_10nm_PHY_CMN_CLK_CFG1,
		      data & ~BIT(5));
}

static void dsi_pll_enable_global_clk(struct dsi_pll_10nm *pll)
{
	u32 data;

	data = dsi_phy_read(pll->phy->base + REG_DSI_10nm_PHY_CMN_CLK_CFG1);
	dsi_phy_write(pll->phy->base + REG_DSI_10nm_PHY_CMN_CLK_CFG1,
		      data | BIT(5));
}

static int dsi_pll_10nm_vco_prepare(struct clk_hw *hw)
{
	struct dsi_pll_10nm *pll_10nm = to_pll_10nm(hw);
	struct device *dev = &pll_10nm->phy->pdev->dev;
	int rc;

	dsi_pll_enable_pll_bias(pll_10nm);
	if (pll_10nm->slave)
		dsi_pll_enable_pll_bias(pll_10nm->slave);

	rc = dsi_pll_10nm_vco_set_rate(hw,pll_10nm->vco_current_rate, 0);
	if (rc) {
		DRM_DEV_ERROR(dev, "vco_set_rate failed, rc=%d\n", rc);
		return rc;
	}

	/* Start PLL */
	dsi_phy_write(pll_10nm->phy->base + REG_DSI_10nm_PHY_CMN_PLL_CNTRL,
		      0x01);

	/*
	 * ensure all PLL configurations are written prior to checking
	 * for PLL lock.
	 */
	wmb();

	/* Check for PLL lock */
	rc = dsi_pll_10nm_lock_status(pll_10nm);
	if (rc) {
		DRM_DEV_ERROR(dev, "PLL(%d) lock failed\n", pll_10nm->phy->id);
		goto error;
	}

	pll_10nm->phy->pll_on = true;

	dsi_pll_enable_global_clk(pll_10nm);
	if (pll_10nm->slave)
		dsi_pll_enable_global_clk(pll_10nm->slave);

	dsi_phy_write(pll_10nm->phy->base + REG_DSI_10nm_PHY_CMN_RBUF_CTRL,
		      0x01);
	if (pll_10nm->slave)
		dsi_phy_write(pll_10nm->slave->phy->base +
			      REG_DSI_10nm_PHY_CMN_RBUF_CTRL, 0x01);

error:
	return rc;
}

static void dsi_pll_disable_sub(struct dsi_pll_10nm *pll)
{
	dsi_phy_write(pll->phy->base + REG_DSI_10nm_PHY_CMN_RBUF_CTRL, 0);
	dsi_pll_disable_pll_bias(pll);
}

static void dsi_pll_10nm_vco_unprepare(struct clk_hw *hw)
{
	struct dsi_pll_10nm *pll_10nm = to_pll_10nm(hw);

	/*
	 * To avoid any stray glitches while abruptly powering down the PLL
	 * make sure to gate the clock using the clock enable bit before
	 * powering down the PLL
	 */
	dsi_pll_disable_global_clk(pll_10nm);
	dsi_phy_write(pll_10nm->phy->base + REG_DSI_10nm_PHY_CMN_PLL_CNTRL, 0);
	dsi_pll_disable_sub(pll_10nm);
	if (pll_10nm->slave) {
		dsi_pll_disable_global_clk(pll_10nm->slave);
		dsi_pll_disable_sub(pll_10nm->slave);
	}
	/* flush, ensure all register writes are done */
	wmb();
	pll_10nm->phy->pll_on = false;
}

static unsigned long dsi_pll_10nm_vco_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct dsi_pll_10nm *pll_10nm = to_pll_10nm(hw);
	void __iomem *base = pll_10nm->phy->pll_base;
	u64 ref_clk = VCO_REF_CLK_RATE;
	u64 vco_rate = 0x0;
	u64 multiplier;
	u32 frac;
	u32 dec;
	u64 pll_freq, tmp64;

	dec = dsi_phy_read(base + REG_DSI_10nm_PHY_PLL_DECIMAL_DIV_START_1);
	dec &= 0xff;

	frac = dsi_phy_read(base + REG_DSI_10nm_PHY_PLL_FRAC_DIV_START_LOW_1);
	frac |= ((dsi_phy_read(base + REG_DSI_10nm_PHY_PLL_FRAC_DIV_START_MID_1) &
		  0xff) << 8);
	frac |= ((dsi_phy_read(base + REG_DSI_10nm_PHY_PLL_FRAC_DIV_START_HIGH_1) &
		  0x3) << 16);

	/*
	 * TODO:
	 *	1. Assumes prescaler is disabled
	 */
	multiplier = 1 << FRAC_BITS;
	pll_freq = dec * (ref_clk * 2);
	tmp64 = (ref_clk * 2 * frac);
	pll_freq += div_u64(tmp64, multiplier);

	vco_rate = pll_freq;
	pll_10nm->vco_current_rate = vco_rate;

	DBG("DSI PLL%d returning vco rate = %lu, dec = %x, frac = %x",
	    pll_10nm->phy->id, (unsigned long)vco_rate, dec, frac);

	return (unsigned long)vco_rate;
}

static long dsi_pll_10nm_clk_round_rate(struct clk_hw *hw,
		unsigned long rate, unsigned long *parent_rate)
{
	struct dsi_pll_10nm *pll_10nm = to_pll_10nm(hw);

	if      (rate < pll_10nm->phy->cfg->min_pll_rate)
		return  pll_10nm->phy->cfg->min_pll_rate;
	else if (rate > pll_10nm->phy->cfg->max_pll_rate)
		return  pll_10nm->phy->cfg->max_pll_rate;
	else
		return rate;
}

static const struct clk_ops clk_ops_dsi_pll_10nm_vco = {
	.round_rate = dsi_pll_10nm_clk_round_rate,
	.set_rate = dsi_pll_10nm_vco_set_rate,
	.recalc_rate = dsi_pll_10nm_vco_recalc_rate,
	.prepare = dsi_pll_10nm_vco_prepare,
	.unprepare = dsi_pll_10nm_vco_unprepare,
};

/*
 * PLL Callbacks
 */

static void dsi_10nm_pll_save_state(struct msm_dsi_phy *phy)
{
	struct dsi_pll_10nm *pll_10nm = to_pll_10nm(phy->vco_hw);
	struct pll_10nm_cached_state *cached = &pll_10nm->cached_state;
	void __iomem *phy_base = pll_10nm->phy->base;
	u32 cmn_clk_cfg0, cmn_clk_cfg1;

	cached->pll_out_div = dsi_phy_read(pll_10nm->phy->pll_base +
			REG_DSI_10nm_PHY_PLL_PLL_OUTDIV_RATE);
	cached->pll_out_div &= 0x3;

	cmn_clk_cfg0 = dsi_phy_read(phy_base + REG_DSI_10nm_PHY_CMN_CLK_CFG0);
	cached->bit_clk_div = cmn_clk_cfg0 & 0xf;
	cached->pix_clk_div = (cmn_clk_cfg0 & 0xf0) >> 4;

	cmn_clk_cfg1 = dsi_phy_read(phy_base + REG_DSI_10nm_PHY_CMN_CLK_CFG1);
	cached->pll_mux = cmn_clk_cfg1 & 0x3;

	DBG("DSI PLL%d outdiv %x bit_clk_div %x pix_clk_div %x pll_mux %x",
	    pll_10nm->phy->id, cached->pll_out_div, cached->bit_clk_div,
	    cached->pix_clk_div, cached->pll_mux);
}

static int dsi_10nm_pll_restore_state(struct msm_dsi_phy *phy)
{
	struct dsi_pll_10nm *pll_10nm = to_pll_10nm(phy->vco_hw);
	struct pll_10nm_cached_state *cached = &pll_10nm->cached_state;
	void __iomem *phy_base = pll_10nm->phy->base;
	u32 val;
	int ret;

	val = dsi_phy_read(pll_10nm->phy->pll_base + REG_DSI_10nm_PHY_PLL_PLL_OUTDIV_RATE);
	val &= ~0x3;
	val |= cached->pll_out_div;
	dsi_phy_write(pll_10nm->phy->pll_base + REG_DSI_10nm_PHY_PLL_PLL_OUTDIV_RATE, val);

	dsi_phy_write(phy_base + REG_DSI_10nm_PHY_CMN_CLK_CFG0,
		      cached->bit_clk_div | (cached->pix_clk_div << 4));

	val = dsi_phy_read(phy_base + REG_DSI_10nm_PHY_CMN_CLK_CFG1);
	val &= ~0x3;
	val |= cached->pll_mux;
	dsi_phy_write(phy_base + REG_DSI_10nm_PHY_CMN_CLK_CFG1, val);

	ret = dsi_pll_10nm_vco_set_rate(phy->vco_hw,
			pll_10nm->vco_current_rate,
			VCO_REF_CLK_RATE);
	if (ret) {
		DRM_DEV_ERROR(&pll_10nm->phy->pdev->dev,
			"restore vco rate failed. ret=%d\n", ret);
		return ret;
	}

	DBG("DSI PLL%d", pll_10nm->phy->id);

	return 0;
}

static int dsi_10nm_set_usecase(struct msm_dsi_phy *phy)
{
	struct dsi_pll_10nm *pll_10nm = to_pll_10nm(phy->vco_hw);
	void __iomem *base = phy->base;
	u32 data = 0x0;	/* internal PLL */

	DBG("DSI PLL%d", pll_10nm->phy->id);

	switch (phy->usecase) {
	case MSM_DSI_PHY_STANDALONE:
		break;
	case MSM_DSI_PHY_MASTER:
		pll_10nm->slave = pll_10nm_list[(pll_10nm->phy->id + 1) % DSI_MAX];
		break;
	case MSM_DSI_PHY_SLAVE:
		data = 0x1; /* external PLL */
		break;
	default:
		return -EINVAL;
	}

	/* set PLL src */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_CLK_CFG1, (data << 2));

	return 0;
}

/*
 * The post dividers and mux clocks are created using the standard divider and
 * mux API. Unlike the 14nm PHY, the slave PLL doesn't need its dividers/mux
 * state to follow the master PLL's divider/mux state. Therefore, we don't
 * require special clock ops that also configure the slave PLL registers
 */
static int pll_10nm_register(struct dsi_pll_10nm *pll_10nm, struct clk_hw **provided_clocks)
{
	char clk_name[32];
	struct clk_init_data vco_init = {
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "ref",
		},
		.num_parents = 1,
		.name = clk_name,
		.flags = CLK_IGNORE_UNUSED,
		.ops = &clk_ops_dsi_pll_10nm_vco,
	};
	struct device *dev = &pll_10nm->phy->pdev->dev;
	struct clk_hw *hw, *pll_out_div, *pll_bit, *pll_by_2_bit;
	struct clk_hw *pll_post_out_div, *pclk_mux;
	int ret;

	DBG("DSI%d", pll_10nm->phy->id);

	snprintf(clk_name, sizeof(clk_name), "dsi%dvco_clk", pll_10nm->phy->id);
	pll_10nm->clk_hw.init = &vco_init;

	ret = devm_clk_hw_register(dev, &pll_10nm->clk_hw);
	if (ret)
		return ret;

	snprintf(clk_name, sizeof(clk_name), "dsi%d_pll_out_div_clk", pll_10nm->phy->id);

	pll_out_div = devm_clk_hw_register_divider_parent_hw(dev, clk_name,
			&pll_10nm->clk_hw, CLK_SET_RATE_PARENT,
			pll_10nm->phy->pll_base +
				REG_DSI_10nm_PHY_PLL_PLL_OUTDIV_RATE,
			0, 2, CLK_DIVIDER_POWER_OF_TWO, NULL);
	if (IS_ERR(pll_out_div)) {
		ret = PTR_ERR(pll_out_div);
		goto fail;
	}

	snprintf(clk_name, sizeof(clk_name), "dsi%d_pll_bit_clk", pll_10nm->phy->id);

	/* BIT CLK: DIV_CTRL_3_0 */
	pll_bit = devm_clk_hw_register_divider_parent_hw(dev, clk_name,
			pll_out_div, CLK_SET_RATE_PARENT,
			pll_10nm->phy->base + REG_DSI_10nm_PHY_CMN_CLK_CFG0,
			0, 4, CLK_DIVIDER_ONE_BASED, &pll_10nm->postdiv_lock);
	if (IS_ERR(pll_bit)) {
		ret = PTR_ERR(pll_bit);
		goto fail;
	}

	snprintf(clk_name, sizeof(clk_name), "dsi%d_phy_pll_out_byteclk", pll_10nm->phy->id);

	/* DSI Byte clock = VCO_CLK / OUT_DIV / BIT_DIV / 8 */
	hw = devm_clk_hw_register_fixed_factor_parent_hw(dev, clk_name,
			pll_bit, CLK_SET_RATE_PARENT, 1, 8);
	if (IS_ERR(hw)) {
		ret = PTR_ERR(hw);
		goto fail;
	}

	provided_clocks[DSI_BYTE_PLL_CLK] = hw;

	snprintf(clk_name, sizeof(clk_name), "dsi%d_pll_by_2_bit_clk", pll_10nm->phy->id);

	pll_by_2_bit = devm_clk_hw_register_fixed_factor_parent_hw(dev,
			clk_name, pll_bit, 0, 1, 2);
	if (IS_ERR(pll_by_2_bit)) {
		ret = PTR_ERR(pll_by_2_bit);
		goto fail;
	}

	snprintf(clk_name, sizeof(clk_name), "dsi%d_pll_post_out_div_clk", pll_10nm->phy->id);

	pll_post_out_div = devm_clk_hw_register_fixed_factor_parent_hw(dev,
			clk_name, pll_out_div, 0, 1, 4);
	if (IS_ERR(pll_post_out_div)) {
		ret = PTR_ERR(pll_post_out_div);
		goto fail;
	}

	snprintf(clk_name, sizeof(clk_name), "dsi%d_pclk_mux", pll_10nm->phy->id);

	pclk_mux = devm_clk_hw_register_mux_parent_hws(dev, clk_name,
			((const struct clk_hw *[]){
				pll_bit,
				pll_by_2_bit,
				pll_out_div,
				pll_post_out_div,
			}), 4, 0, pll_10nm->phy->base +
				REG_DSI_10nm_PHY_CMN_CLK_CFG1, 0, 2, 0, NULL);
	if (IS_ERR(pclk_mux)) {
		ret = PTR_ERR(pclk_mux);
		goto fail;
	}

	snprintf(clk_name, sizeof(clk_name), "dsi%d_phy_pll_out_dsiclk", pll_10nm->phy->id);

	/* PIX CLK DIV : DIV_CTRL_7_4*/
	hw = devm_clk_hw_register_divider_parent_hw(dev, clk_name, pclk_mux,
			0, pll_10nm->phy->base + REG_DSI_10nm_PHY_CMN_CLK_CFG0,
			4, 4, CLK_DIVIDER_ONE_BASED, &pll_10nm->postdiv_lock);
	if (IS_ERR(hw)) {
		ret = PTR_ERR(hw);
		goto fail;
	}

	provided_clocks[DSI_PIXEL_PLL_CLK] = hw;

	return 0;

fail:

	return ret;
}

static int dsi_pll_10nm_init(struct msm_dsi_phy *phy)
{
	struct platform_device *pdev = phy->pdev;
	struct dsi_pll_10nm *pll_10nm;
	int ret;

	pll_10nm = devm_kzalloc(&pdev->dev, sizeof(*pll_10nm), GFP_KERNEL);
	if (!pll_10nm)
		return -ENOMEM;

	DBG("DSI PLL%d", phy->id);

	pll_10nm_list[phy->id] = pll_10nm;

	spin_lock_init(&pll_10nm->postdiv_lock);

	pll_10nm->phy = phy;

	ret = pll_10nm_register(pll_10nm, phy->provided_clocks->hws);
	if (ret) {
		DRM_DEV_ERROR(&pdev->dev, "failed to register PLL: %d\n", ret);
		return ret;
	}

	phy->vco_hw = &pll_10nm->clk_hw;

	/* TODO: Remove this when we have proper display handover support */
	msm_dsi_phy_pll_save_state(phy);

	return 0;
}

static int dsi_phy_hw_v3_0_is_pll_on(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->base;
	u32 data = 0;

	data = dsi_phy_read(base + REG_DSI_10nm_PHY_CMN_PLL_CNTRL);
	mb(); /* make sure read happened */

	return (data & BIT(0));
}

static void dsi_phy_hw_v3_0_config_lpcdrx(struct msm_dsi_phy *phy, bool enable)
{
	void __iomem *lane_base = phy->lane_base;
	int phy_lane_0 = 0;	/* TODO: Support all lane swap configs */

	/*
	 * LPRX and CDRX need to enabled only for physical data lane
	 * corresponding to the logical data lane 0
	 */
	if (enable)
		dsi_phy_write(lane_base +
			      REG_DSI_10nm_PHY_LN_LPRX_CTRL(phy_lane_0), 0x3);
	else
		dsi_phy_write(lane_base +
			      REG_DSI_10nm_PHY_LN_LPRX_CTRL(phy_lane_0), 0);
}

static void dsi_phy_hw_v3_0_lane_settings(struct msm_dsi_phy *phy)
{
	int i;
	u8 tx_dctrl[] = { 0x00, 0x00, 0x00, 0x04, 0x01 };
	void __iomem *lane_base = phy->lane_base;
	struct dsi_phy_10nm_tuning_cfg *tuning_cfg = phy->tuning_cfg;

	if (phy->cfg->quirks & DSI_PHY_10NM_QUIRK_OLD_TIMINGS)
		tx_dctrl[3] = 0x02;

	/* Strength ctrl settings */
	for (i = 0; i < 5; i++) {
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_LPTX_STR_CTRL(i),
			      0x55);
		/*
		 * Disable LPRX and CDRX for all lanes. And later on, it will
		 * be only enabled for the physical data lane corresponding
		 * to the logical data lane 0
		 */
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_LPRX_CTRL(i), 0);
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_PIN_SWAP(i), 0x0);
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_HSTX_STR_CTRL(i),
			      0x88);
	}

	dsi_phy_hw_v3_0_config_lpcdrx(phy, true);

	/* other settings */
	for (i = 0; i < 5; i++) {
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_CFG0(i), 0x0);
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_CFG1(i), 0x0);
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_CFG2(i), 0x0);
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_CFG3(i),
			      i == 4 ? 0x80 : 0x0);

		/* platform specific dsi phy drive strength adjustment */
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_OFFSET_TOP_CTRL(i),
				tuning_cfg->rescode_offset_top[i]);
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_OFFSET_BOT_CTRL(i),
				tuning_cfg->rescode_offset_bot[i]);

		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_TX_DCTRL(i),
			      tx_dctrl[i]);
	}

	if (!(phy->cfg->quirks & DSI_PHY_10NM_QUIRK_OLD_TIMINGS)) {
		/* Toggle BIT 0 to release freeze I/0 */
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_TX_DCTRL(3), 0x05);
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_TX_DCTRL(3), 0x04);
	}
}

static int dsi_10nm_phy_enable(struct msm_dsi_phy *phy,
			       struct msm_dsi_phy_clk_request *clk_req)
{
	int ret;
	u32 status;
	u32 const delay_us = 5;
	u32 const timeout_us = 1000;
	struct msm_dsi_dphy_timing *timing = &phy->timing;
	void __iomem *base = phy->base;
	struct dsi_phy_10nm_tuning_cfg *tuning_cfg = phy->tuning_cfg;
	u32 data;

	DBG("");

	if (msm_dsi_dphy_timing_calc_v3(timing, clk_req)) {
		DRM_DEV_ERROR(&phy->pdev->dev,
			"%s: D-PHY timing calculation failed\n", __func__);
		return -EINVAL;
	}

	if (dsi_phy_hw_v3_0_is_pll_on(phy))
		pr_warn("PLL turned on before configuring PHY\n");

	/* wait for REFGEN READY */
	ret = readl_poll_timeout_atomic(base + REG_DSI_10nm_PHY_CMN_PHY_STATUS,
					status, (status & BIT(0)),
					delay_us, timeout_us);
	if (ret) {
		pr_err("Ref gen not ready. Aborting\n");
		return -EINVAL;
	}

	/* de-assert digital and pll power down */
	data = BIT(6) | BIT(5);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_CTRL_0, data);

	/* Assert PLL core reset */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_PLL_CNTRL, 0x00);

	/* turn off resync FIFO */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_RBUF_CTRL, 0x00);

	/* Select MS1 byte-clk */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_GLBL_CTRL, 0x10);

	/* Enable LDO with platform specific drive level/amplitude adjustment */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_VREG_CTRL,
		      tuning_cfg->vreg_ctrl);

	/* Configure PHY lane swap (TODO: we need to calculate this) */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_LANE_CFG0, 0x21);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_LANE_CFG1, 0x84);

	/* DSI PHY timings */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_0,
		      timing->hs_halfbyte_en);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_1,
		      timing->clk_zero);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_2,
		      timing->clk_prepare);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_3,
		      timing->clk_trail);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_4,
		      timing->hs_exit);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_5,
		      timing->hs_zero);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_6,
		      timing->hs_prepare);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_7,
		      timing->hs_trail);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_8,
		      timing->hs_rqst);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_9,
		      timing->ta_go | (timing->ta_sure << 3));
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_10,
		      timing->ta_get);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_11,
		      0x00);

	/* Remove power down from all blocks */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_CTRL_0, 0x7f);

	/* power up lanes */
	data = dsi_phy_read(base + REG_DSI_10nm_PHY_CMN_CTRL_0);

	/* TODO: only power up lanes that are used */
	data |= 0x1F;
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_CTRL_0, data);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_LANE_CTRL0, 0x1F);

	/* Select full-rate mode */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_CTRL_2, 0x40);

	ret = dsi_10nm_set_usecase(phy);
	if (ret) {
		DRM_DEV_ERROR(&phy->pdev->dev, "%s: set pll usecase failed, %d\n",
			__func__, ret);
		return ret;
	}

	/* DSI lane settings */
	dsi_phy_hw_v3_0_lane_settings(phy);

	DBG("DSI%d PHY enabled", phy->id);

	return 0;
}

static void dsi_10nm_phy_disable(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->base;
	u32 data;

	DBG("");

	if (dsi_phy_hw_v3_0_is_pll_on(phy))
		pr_warn("Turning OFF PHY while PLL is on\n");

	dsi_phy_hw_v3_0_config_lpcdrx(phy, false);
	data = dsi_phy_read(base + REG_DSI_10nm_PHY_CMN_CTRL_0);

	/* disable all lanes */
	data &= ~0x1F;
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_CTRL_0, data);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_LANE_CTRL0, 0);

	/* Turn off all PHY blocks */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_CTRL_0, 0x00);
	/* make sure phy is turned off */
	wmb();

	DBG("DSI%d PHY disabled", phy->id);
}

static int dsi_10nm_phy_parse_dt(struct msm_dsi_phy *phy)
{
	struct device *dev = &phy->pdev->dev;
	struct dsi_phy_10nm_tuning_cfg *tuning_cfg;
	s8 offset_top[DSI_LANE_MAX] = { 0 }; /* No offset */
	s8 offset_bot[DSI_LANE_MAX] = { 0 }; /* No offset */
	u32 ldo_level = 400; /* 400mV */
	u8 level;
	int ret, i;

	tuning_cfg = devm_kzalloc(dev, sizeof(*tuning_cfg), GFP_KERNEL);
	if (!tuning_cfg)
		return -ENOMEM;

	/* Drive strength adjustment parameters */
	ret = of_property_read_u8_array(dev->of_node, "qcom,phy-rescode-offset-top",
					offset_top, DSI_LANE_MAX);
	if (ret && ret != -EINVAL) {
		DRM_DEV_ERROR(dev, "failed to parse qcom,phy-rescode-offset-top, %d\n", ret);
		return ret;
	}

	for (i = 0; i < DSI_LANE_MAX; i++) {
		if (offset_top[i] < -32 || offset_top[i] > 31) {
			DRM_DEV_ERROR(dev,
				"qcom,phy-rescode-offset-top value %d is not in range [-32..31]\n",
				offset_top[i]);
			return -EINVAL;
		}
		tuning_cfg->rescode_offset_top[i] = 0x3f & offset_top[i];
	}

	ret = of_property_read_u8_array(dev->of_node, "qcom,phy-rescode-offset-bot",
					offset_bot, DSI_LANE_MAX);
	if (ret && ret != -EINVAL) {
		DRM_DEV_ERROR(dev, "failed to parse qcom,phy-rescode-offset-bot, %d\n", ret);
		return ret;
	}

	for (i = 0; i < DSI_LANE_MAX; i++) {
		if (offset_bot[i] < -32 || offset_bot[i] > 31) {
			DRM_DEV_ERROR(dev,
				"qcom,phy-rescode-offset-bot value %d is not in range [-32..31]\n",
				offset_bot[i]);
			return -EINVAL;
		}
		tuning_cfg->rescode_offset_bot[i] = 0x3f & offset_bot[i];
	}

	/* Drive level/amplitude adjustment parameters */
	ret = of_property_read_u32(dev->of_node, "qcom,phy-drive-ldo-level", &ldo_level);
	if (ret && ret != -EINVAL) {
		DRM_DEV_ERROR(dev, "failed to parse qcom,phy-drive-ldo-level, %d\n", ret);
		return ret;
	}

	switch (ldo_level) {
	case 375:
		level = 0;
		break;
	case 400:
		level = 1;
		break;
	case 425:
		level = 2;
		break;
	case 450:
		level = 3;
		break;
	case 475:
		level = 4;
		break;
	case 500:
		level = 5;
		break;
	default:
		DRM_DEV_ERROR(dev, "qcom,phy-drive-ldo-level %d is not supported\n", ldo_level);
		return -EINVAL;
	}
	tuning_cfg->vreg_ctrl = 0x58 | (0x7 & level);

	phy->tuning_cfg = tuning_cfg;

	return 0;
}

static const struct regulator_bulk_data dsi_phy_10nm_regulators[] = {
	{ .supply = "vdds", .init_load_uA = 36000 },
};

const struct msm_dsi_phy_cfg dsi_phy_10nm_cfgs = {
	.has_phy_lane = true,
	.regulator_data = dsi_phy_10nm_regulators,
	.num_regulators = ARRAY_SIZE(dsi_phy_10nm_regulators),
	.ops = {
		.enable = dsi_10nm_phy_enable,
		.disable = dsi_10nm_phy_disable,
		.pll_init = dsi_pll_10nm_init,
		.save_pll_state = dsi_10nm_pll_save_state,
		.restore_pll_state = dsi_10nm_pll_restore_state,
		.parse_dt_properties = dsi_10nm_phy_parse_dt,
	},
	.min_pll_rate = 1000000000UL,
	.max_pll_rate = 3500000000UL,
	.io_start = { 0xae94400, 0xae96400 },
	.num_dsi_phy = 2,
};

const struct msm_dsi_phy_cfg dsi_phy_10nm_8998_cfgs = {
	.has_phy_lane = true,
	.regulator_data = dsi_phy_10nm_regulators,
	.num_regulators = ARRAY_SIZE(dsi_phy_10nm_regulators),
	.ops = {
		.enable = dsi_10nm_phy_enable,
		.disable = dsi_10nm_phy_disable,
		.pll_init = dsi_pll_10nm_init,
		.save_pll_state = dsi_10nm_pll_save_state,
		.restore_pll_state = dsi_10nm_pll_restore_state,
		.parse_dt_properties = dsi_10nm_phy_parse_dt,
	},
	.min_pll_rate = 1000000000UL,
	.max_pll_rate = 3500000000UL,
	.io_start = { 0xc994400, 0xc996400 },
	.num_dsi_phy = 2,
	.quirks = DSI_PHY_10NM_QUIRK_OLD_TIMINGS,
};

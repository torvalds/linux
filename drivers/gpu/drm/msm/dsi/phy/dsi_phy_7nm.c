/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2018, The Linux Foundation
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/iopoll.h>

#include "dsi_phy.h"
#include "dsi.xml.h"
#include "dsi_phy_7nm.xml.h"

/*
 * DSI PLL 7nm - clock diagram (eg: DSI0): TODO: updated CPHY diagram
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

/* Hardware is pre V4.1 */
#define DSI_PHY_7NM_QUIRK_PRE_V4_1	BIT(0)
/* Hardware is V4.1 */
#define DSI_PHY_7NM_QUIRK_V4_1		BIT(1)
/* Hardware is V4.2 */
#define DSI_PHY_7NM_QUIRK_V4_2		BIT(2)
/* Hardware is V4.3 */
#define DSI_PHY_7NM_QUIRK_V4_3		BIT(3)
/* Hardware is V5.2 */
#define DSI_PHY_7NM_QUIRK_V5_2		BIT(4)

struct dsi_pll_config {
	bool enable_ssc;
	bool ssc_center;
	u32 ssc_freq;
	u32 ssc_offset;
	u32 ssc_adj_per;

	/* out */
	u32 decimal_div_start;
	u32 frac_div_start;
	u32 pll_clock_inverters;
	u32 ssc_stepsize;
	u32 ssc_div_per;
};

struct pll_7nm_cached_state {
	unsigned long vco_rate;
	u8 bit_clk_div;
	u8 pix_clk_div;
	u8 pll_out_div;
	u8 pll_mux;
};

struct dsi_pll_7nm {
	struct clk_hw clk_hw;

	struct msm_dsi_phy *phy;

	u64 vco_current_rate;

	/* protects REG_DSI_7nm_PHY_CMN_CLK_CFG0 register */
	spinlock_t postdiv_lock;

	struct pll_7nm_cached_state cached_state;

	struct dsi_pll_7nm *slave;
};

#define to_pll_7nm(x)	container_of(x, struct dsi_pll_7nm, clk_hw)

/*
 * Global list of private DSI PLL struct pointers. We need this for bonded DSI
 * mode, where the master PLL's clk_ops needs access the slave's private data
 */
static struct dsi_pll_7nm *pll_7nm_list[DSI_MAX];

static void dsi_pll_setup_config(struct dsi_pll_config *config)
{
	config->ssc_freq = 31500;
	config->ssc_offset = 4800;
	config->ssc_adj_per = 2;

	/* TODO: ssc enable */
	config->enable_ssc = false;
	config->ssc_center = 0;
}

static void dsi_pll_calc_dec_frac(struct dsi_pll_7nm *pll, struct dsi_pll_config *config)
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

	if (pll->phy->cfg->quirks & DSI_PHY_7NM_QUIRK_PRE_V4_1)
		config->pll_clock_inverters = 0x28;
	else if ((pll->phy->cfg->quirks & DSI_PHY_7NM_QUIRK_V5_2)) {
		if (pll_freq <= 1300000000ULL)
			config->pll_clock_inverters = 0xa0;
		else if (pll_freq <= 2500000000ULL)
			config->pll_clock_inverters = 0x20;
		else if (pll_freq <= 4000000000ULL)
			config->pll_clock_inverters = 0x00;
		else
			config->pll_clock_inverters = 0x40;
	} else if (pll->phy->cfg->quirks & DSI_PHY_7NM_QUIRK_V4_1) {
		if (pll_freq <= 1000000000ULL)
			config->pll_clock_inverters = 0xa0;
		else if (pll_freq <= 2500000000ULL)
			config->pll_clock_inverters = 0x20;
		else if (pll_freq <= 3020000000ULL)
			config->pll_clock_inverters = 0x00;
		else
			config->pll_clock_inverters = 0x40;
	} else {
		/* 4.2, 4.3 */
		if (pll_freq <= 1000000000ULL)
			config->pll_clock_inverters = 0xa0;
		else if (pll_freq <= 2500000000ULL)
			config->pll_clock_inverters = 0x20;
		else if (pll_freq <= 3500000000ULL)
			config->pll_clock_inverters = 0x00;
		else
			config->pll_clock_inverters = 0x40;
	}

	config->decimal_div_start = dec;
	config->frac_div_start = frac;
}

#define SSC_CENTER		BIT(0)
#define SSC_EN			BIT(1)

static void dsi_pll_calc_ssc(struct dsi_pll_7nm *pll, struct dsi_pll_config *config)
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

static void dsi_pll_ssc_commit(struct dsi_pll_7nm *pll, struct dsi_pll_config *config)
{
	void __iomem *base = pll->phy->pll_base;

	if (config->enable_ssc) {
		pr_debug("SSC is enabled\n");

		writel(config->ssc_stepsize & 0xff,
		       base + REG_DSI_7nm_PHY_PLL_SSC_STEPSIZE_LOW_1);
		writel(config->ssc_stepsize >> 8,
		       base + REG_DSI_7nm_PHY_PLL_SSC_STEPSIZE_HIGH_1);
		writel(config->ssc_div_per & 0xff,
		       base + REG_DSI_7nm_PHY_PLL_SSC_DIV_PER_LOW_1);
		writel(config->ssc_div_per >> 8,
		       base + REG_DSI_7nm_PHY_PLL_SSC_DIV_PER_HIGH_1);
		writel(config->ssc_adj_per & 0xff,
		       base + REG_DSI_7nm_PHY_PLL_SSC_ADJPER_LOW_1);
		writel(config->ssc_adj_per >> 8,
		       base + REG_DSI_7nm_PHY_PLL_SSC_ADJPER_HIGH_1);
		writel(SSC_EN | (config->ssc_center ? SSC_CENTER : 0),
		       base + REG_DSI_7nm_PHY_PLL_SSC_CONTROL);
	}
}

static void dsi_pll_config_hzindep_reg(struct dsi_pll_7nm *pll)
{
	void __iomem *base = pll->phy->pll_base;
	u8 analog_controls_five_1 = 0x01, vco_config_1 = 0x00;

	if (!(pll->phy->cfg->quirks & DSI_PHY_7NM_QUIRK_PRE_V4_1))
		if (pll->vco_current_rate >= 3100000000ULL)
			analog_controls_five_1 = 0x03;

	if (pll->phy->cfg->quirks & DSI_PHY_7NM_QUIRK_V4_1) {
		if (pll->vco_current_rate < 1520000000ULL)
			vco_config_1 = 0x08;
		else if (pll->vco_current_rate < 2990000000ULL)
			vco_config_1 = 0x01;
	}

	if ((pll->phy->cfg->quirks & DSI_PHY_7NM_QUIRK_V4_2) ||
	    (pll->phy->cfg->quirks & DSI_PHY_7NM_QUIRK_V4_3)) {
		if (pll->vco_current_rate < 1520000000ULL)
			vco_config_1 = 0x08;
		else if (pll->vco_current_rate >= 2990000000ULL)
			vco_config_1 = 0x01;
	}

	if ((pll->phy->cfg->quirks & DSI_PHY_7NM_QUIRK_V5_2)) {
		if (pll->vco_current_rate < 1557000000ULL)
			vco_config_1 = 0x08;
		else
			vco_config_1 = 0x01;
	}

	writel(analog_controls_five_1, base + REG_DSI_7nm_PHY_PLL_ANALOG_CONTROLS_FIVE_1);
	writel(vco_config_1, base + REG_DSI_7nm_PHY_PLL_VCO_CONFIG_1);
	writel(0x01, base + REG_DSI_7nm_PHY_PLL_ANALOG_CONTROLS_FIVE);
	writel(0x03, base + REG_DSI_7nm_PHY_PLL_ANALOG_CONTROLS_TWO);
	writel(0x00, base + REG_DSI_7nm_PHY_PLL_ANALOG_CONTROLS_THREE);
	writel(0x00, base + REG_DSI_7nm_PHY_PLL_DSM_DIVIDER);
	writel(0x4e, base + REG_DSI_7nm_PHY_PLL_FEEDBACK_DIVIDER);
	writel(0x40, base + REG_DSI_7nm_PHY_PLL_CALIBRATION_SETTINGS);
	writel(0xba, base + REG_DSI_7nm_PHY_PLL_BAND_SEL_CAL_SETTINGS_THREE);
	writel(0x0c, base + REG_DSI_7nm_PHY_PLL_FREQ_DETECT_SETTINGS_ONE);
	writel(0x00, base + REG_DSI_7nm_PHY_PLL_OUTDIV);
	writel(0x00, base + REG_DSI_7nm_PHY_PLL_CORE_OVERRIDE);
	writel(0x08, base + REG_DSI_7nm_PHY_PLL_PLL_DIGITAL_TIMERS_TWO);
	writel(0x0a, base + REG_DSI_7nm_PHY_PLL_PLL_PROP_GAIN_RATE_1);
	writel(0xc0, base + REG_DSI_7nm_PHY_PLL_PLL_BAND_SEL_RATE_1);
	writel(0x84, base + REG_DSI_7nm_PHY_PLL_PLL_INT_GAIN_IFILT_BAND_1);
	writel(0x82, base + REG_DSI_7nm_PHY_PLL_PLL_INT_GAIN_IFILT_BAND_1);
	writel(0x4c, base + REG_DSI_7nm_PHY_PLL_PLL_FL_INT_GAIN_PFILT_BAND_1);
	writel(0x80, base + REG_DSI_7nm_PHY_PLL_PLL_LOCK_OVERRIDE);
	writel(0x29, base + REG_DSI_7nm_PHY_PLL_PFILT);
	writel(0x2f, base + REG_DSI_7nm_PHY_PLL_PFILT);
	writel(0x2a, base + REG_DSI_7nm_PHY_PLL_IFILT);
	writel(!(pll->phy->cfg->quirks & DSI_PHY_7NM_QUIRK_PRE_V4_1) ? 0x3f : 0x22,
	       base + REG_DSI_7nm_PHY_PLL_IFILT);

	if (!(pll->phy->cfg->quirks & DSI_PHY_7NM_QUIRK_PRE_V4_1)) {
		writel(0x22, base + REG_DSI_7nm_PHY_PLL_PERF_OPTIMIZE);
		if (pll->slave)
			writel(0x22, pll->slave->phy->pll_base + REG_DSI_7nm_PHY_PLL_PERF_OPTIMIZE);
	}
}

static void dsi_pll_commit(struct dsi_pll_7nm *pll, struct dsi_pll_config *config)
{
	void __iomem *base = pll->phy->pll_base;

	writel(0x12, base + REG_DSI_7nm_PHY_PLL_CORE_INPUT_OVERRIDE);
	writel(config->decimal_div_start,
	       base + REG_DSI_7nm_PHY_PLL_DECIMAL_DIV_START_1);
	writel(config->frac_div_start & 0xff,
	       base + REG_DSI_7nm_PHY_PLL_FRAC_DIV_START_LOW_1);
	writel((config->frac_div_start & 0xff00) >> 8,
	       base + REG_DSI_7nm_PHY_PLL_FRAC_DIV_START_MID_1);
	writel((config->frac_div_start & 0x30000) >> 16,
	       base + REG_DSI_7nm_PHY_PLL_FRAC_DIV_START_HIGH_1);
	writel(0x40, base + REG_DSI_7nm_PHY_PLL_PLL_LOCKDET_RATE_1);
	writel(0x06, base + REG_DSI_7nm_PHY_PLL_PLL_LOCK_DELAY);
	writel(pll->phy->cphy_mode ? 0x00 : 0x10,
	       base + REG_DSI_7nm_PHY_PLL_CMODE_1);
	writel(config->pll_clock_inverters,
	       base + REG_DSI_7nm_PHY_PLL_CLOCK_INVERTERS);
}

static int dsi_pll_7nm_vco_set_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	struct dsi_pll_7nm *pll_7nm = to_pll_7nm(hw);
	struct dsi_pll_config config;

	DBG("DSI PLL%d rate=%lu, parent's=%lu", pll_7nm->phy->id, rate,
	    parent_rate);

	pll_7nm->vco_current_rate = rate;

	dsi_pll_setup_config(&config);

	dsi_pll_calc_dec_frac(pll_7nm, &config);

	dsi_pll_calc_ssc(pll_7nm, &config);

	dsi_pll_commit(pll_7nm, &config);

	dsi_pll_config_hzindep_reg(pll_7nm);

	dsi_pll_ssc_commit(pll_7nm, &config);

	/* flush, ensure all register writes are done*/
	wmb();

	return 0;
}

static int dsi_pll_7nm_lock_status(struct dsi_pll_7nm *pll)
{
	int rc;
	u32 status = 0;
	u32 const delay_us = 100;
	u32 const timeout_us = 5000;

	rc = readl_poll_timeout_atomic(pll->phy->pll_base +
				       REG_DSI_7nm_PHY_PLL_COMMON_STATUS_ONE,
				       status,
				       ((status & BIT(0)) > 0),
				       delay_us,
				       timeout_us);
	if (rc)
		pr_err("DSI PLL(%d) lock failed, status=0x%08x\n",
		       pll->phy->id, status);

	return rc;
}

static void dsi_pll_disable_pll_bias(struct dsi_pll_7nm *pll)
{
	u32 data = readl(pll->phy->base + REG_DSI_7nm_PHY_CMN_CTRL_0);

	writel(0, pll->phy->pll_base + REG_DSI_7nm_PHY_PLL_SYSTEM_MUXES);
	writel(data & ~BIT(5), pll->phy->base + REG_DSI_7nm_PHY_CMN_CTRL_0);
	ndelay(250);
}

static void dsi_pll_enable_pll_bias(struct dsi_pll_7nm *pll)
{
	u32 data = readl(pll->phy->base + REG_DSI_7nm_PHY_CMN_CTRL_0);

	writel(data | BIT(5), pll->phy->base + REG_DSI_7nm_PHY_CMN_CTRL_0);
	writel(0xc0, pll->phy->pll_base + REG_DSI_7nm_PHY_PLL_SYSTEM_MUXES);
	ndelay(250);
}

static void dsi_pll_disable_global_clk(struct dsi_pll_7nm *pll)
{
	u32 data;

	data = readl(pll->phy->base + REG_DSI_7nm_PHY_CMN_CLK_CFG1);
	writel(data & ~BIT(5), pll->phy->base + REG_DSI_7nm_PHY_CMN_CLK_CFG1);
}

static void dsi_pll_enable_global_clk(struct dsi_pll_7nm *pll)
{
	u32 data;

	writel(0x04, pll->phy->base + REG_DSI_7nm_PHY_CMN_CTRL_3);

	data = readl(pll->phy->base + REG_DSI_7nm_PHY_CMN_CLK_CFG1);
	writel(data | BIT(5) | BIT(4), pll->phy->base + REG_DSI_7nm_PHY_CMN_CLK_CFG1);
}

static void dsi_pll_phy_dig_reset(struct dsi_pll_7nm *pll)
{
	/*
	 * Reset the PHY digital domain. This would be needed when
	 * coming out of a CX or analog rail power collapse while
	 * ensuring that the pads maintain LP00 or LP11 state
	 */
	writel(BIT(0), pll->phy->base + REG_DSI_7nm_PHY_CMN_GLBL_DIGTOP_SPARE4);
	wmb(); /* Ensure that the reset is deasserted */
	writel(0, pll->phy->base + REG_DSI_7nm_PHY_CMN_GLBL_DIGTOP_SPARE4);
	wmb(); /* Ensure that the reset is deasserted */
}

static int dsi_pll_7nm_vco_prepare(struct clk_hw *hw)
{
	struct dsi_pll_7nm *pll_7nm = to_pll_7nm(hw);
	int rc;

	dsi_pll_enable_pll_bias(pll_7nm);
	if (pll_7nm->slave)
		dsi_pll_enable_pll_bias(pll_7nm->slave);

	/* Start PLL */
	writel(BIT(0), pll_7nm->phy->base + REG_DSI_7nm_PHY_CMN_PLL_CNTRL);

	/*
	 * ensure all PLL configurations are written prior to checking
	 * for PLL lock.
	 */
	wmb();

	/* Check for PLL lock */
	rc = dsi_pll_7nm_lock_status(pll_7nm);
	if (rc) {
		pr_err("PLL(%d) lock failed\n", pll_7nm->phy->id);
		goto error;
	}

	pll_7nm->phy->pll_on = true;

	/*
	 * assert power on reset for PHY digital in case the PLL is
	 * enabled after CX of analog domain power collapse. This needs
	 * to be done before enabling the global clk.
	 */
	dsi_pll_phy_dig_reset(pll_7nm);
	if (pll_7nm->slave)
		dsi_pll_phy_dig_reset(pll_7nm->slave);

	dsi_pll_enable_global_clk(pll_7nm);
	if (pll_7nm->slave)
		dsi_pll_enable_global_clk(pll_7nm->slave);

error:
	return rc;
}

static void dsi_pll_disable_sub(struct dsi_pll_7nm *pll)
{
	writel(0, pll->phy->base + REG_DSI_7nm_PHY_CMN_RBUF_CTRL);
	dsi_pll_disable_pll_bias(pll);
}

static void dsi_pll_7nm_vco_unprepare(struct clk_hw *hw)
{
	struct dsi_pll_7nm *pll_7nm = to_pll_7nm(hw);

	/*
	 * To avoid any stray glitches while abruptly powering down the PLL
	 * make sure to gate the clock using the clock enable bit before
	 * powering down the PLL
	 */
	dsi_pll_disable_global_clk(pll_7nm);
	writel(0, pll_7nm->phy->base + REG_DSI_7nm_PHY_CMN_PLL_CNTRL);
	dsi_pll_disable_sub(pll_7nm);
	if (pll_7nm->slave) {
		dsi_pll_disable_global_clk(pll_7nm->slave);
		dsi_pll_disable_sub(pll_7nm->slave);
	}
	/* flush, ensure all register writes are done */
	wmb();
	pll_7nm->phy->pll_on = false;
}

static unsigned long dsi_pll_7nm_vco_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct dsi_pll_7nm *pll_7nm = to_pll_7nm(hw);
	void __iomem *base = pll_7nm->phy->pll_base;
	u64 ref_clk = VCO_REF_CLK_RATE;
	u64 vco_rate = 0x0;
	u64 multiplier;
	u32 frac;
	u32 dec;
	u64 pll_freq, tmp64;

	dec = readl(base + REG_DSI_7nm_PHY_PLL_DECIMAL_DIV_START_1);
	dec &= 0xff;

	frac = readl(base + REG_DSI_7nm_PHY_PLL_FRAC_DIV_START_LOW_1);
	frac |= ((readl(base + REG_DSI_7nm_PHY_PLL_FRAC_DIV_START_MID_1) &
		  0xff) << 8);
	frac |= ((readl(base + REG_DSI_7nm_PHY_PLL_FRAC_DIV_START_HIGH_1) &
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
	pll_7nm->vco_current_rate = vco_rate;

	DBG("DSI PLL%d returning vco rate = %lu, dec = %x, frac = %x",
	    pll_7nm->phy->id, (unsigned long)vco_rate, dec, frac);

	return (unsigned long)vco_rate;
}

static long dsi_pll_7nm_clk_round_rate(struct clk_hw *hw,
		unsigned long rate, unsigned long *parent_rate)
{
	struct dsi_pll_7nm *pll_7nm = to_pll_7nm(hw);

	if      (rate < pll_7nm->phy->cfg->min_pll_rate)
		return  pll_7nm->phy->cfg->min_pll_rate;
	else if (rate > pll_7nm->phy->cfg->max_pll_rate)
		return  pll_7nm->phy->cfg->max_pll_rate;
	else
		return rate;
}

static const struct clk_ops clk_ops_dsi_pll_7nm_vco = {
	.round_rate = dsi_pll_7nm_clk_round_rate,
	.set_rate = dsi_pll_7nm_vco_set_rate,
	.recalc_rate = dsi_pll_7nm_vco_recalc_rate,
	.prepare = dsi_pll_7nm_vco_prepare,
	.unprepare = dsi_pll_7nm_vco_unprepare,
};

/*
 * PLL Callbacks
 */

static void dsi_7nm_pll_save_state(struct msm_dsi_phy *phy)
{
	struct dsi_pll_7nm *pll_7nm = to_pll_7nm(phy->vco_hw);
	struct pll_7nm_cached_state *cached = &pll_7nm->cached_state;
	void __iomem *phy_base = pll_7nm->phy->base;
	u32 cmn_clk_cfg0, cmn_clk_cfg1;

	cached->pll_out_div = readl(pll_7nm->phy->pll_base +
			REG_DSI_7nm_PHY_PLL_PLL_OUTDIV_RATE);
	cached->pll_out_div &= 0x3;

	cmn_clk_cfg0 = readl(phy_base + REG_DSI_7nm_PHY_CMN_CLK_CFG0);
	cached->bit_clk_div = cmn_clk_cfg0 & 0xf;
	cached->pix_clk_div = (cmn_clk_cfg0 & 0xf0) >> 4;

	cmn_clk_cfg1 = readl(phy_base + REG_DSI_7nm_PHY_CMN_CLK_CFG1);
	cached->pll_mux = cmn_clk_cfg1 & 0x3;

	DBG("DSI PLL%d outdiv %x bit_clk_div %x pix_clk_div %x pll_mux %x",
	    pll_7nm->phy->id, cached->pll_out_div, cached->bit_clk_div,
	    cached->pix_clk_div, cached->pll_mux);
}

static int dsi_7nm_pll_restore_state(struct msm_dsi_phy *phy)
{
	struct dsi_pll_7nm *pll_7nm = to_pll_7nm(phy->vco_hw);
	struct pll_7nm_cached_state *cached = &pll_7nm->cached_state;
	void __iomem *phy_base = pll_7nm->phy->base;
	u32 val;
	int ret;

	val = readl(pll_7nm->phy->pll_base + REG_DSI_7nm_PHY_PLL_PLL_OUTDIV_RATE);
	val &= ~0x3;
	val |= cached->pll_out_div;
	writel(val, pll_7nm->phy->pll_base + REG_DSI_7nm_PHY_PLL_PLL_OUTDIV_RATE);

	writel(cached->bit_clk_div | (cached->pix_clk_div << 4),
	       phy_base + REG_DSI_7nm_PHY_CMN_CLK_CFG0);

	val = readl(phy_base + REG_DSI_7nm_PHY_CMN_CLK_CFG1);
	val &= ~0x3;
	val |= cached->pll_mux;
	writel(val, phy_base + REG_DSI_7nm_PHY_CMN_CLK_CFG1);

	ret = dsi_pll_7nm_vco_set_rate(phy->vco_hw,
			pll_7nm->vco_current_rate,
			VCO_REF_CLK_RATE);
	if (ret) {
		DRM_DEV_ERROR(&pll_7nm->phy->pdev->dev,
			"restore vco rate failed. ret=%d\n", ret);
		return ret;
	}

	DBG("DSI PLL%d", pll_7nm->phy->id);

	return 0;
}

static int dsi_7nm_set_usecase(struct msm_dsi_phy *phy)
{
	struct dsi_pll_7nm *pll_7nm = to_pll_7nm(phy->vco_hw);
	void __iomem *base = phy->base;
	u32 data = 0x0;	/* internal PLL */

	DBG("DSI PLL%d", pll_7nm->phy->id);

	switch (phy->usecase) {
	case MSM_DSI_PHY_STANDALONE:
		break;
	case MSM_DSI_PHY_MASTER:
		pll_7nm->slave = pll_7nm_list[(pll_7nm->phy->id + 1) % DSI_MAX];
		break;
	case MSM_DSI_PHY_SLAVE:
		data = 0x1; /* external PLL */
		break;
	default:
		return -EINVAL;
	}

	/* set PLL src */
	writel(data << 2, base + REG_DSI_7nm_PHY_CMN_CLK_CFG1);

	return 0;
}

/*
 * The post dividers and mux clocks are created using the standard divider and
 * mux API. Unlike the 14nm PHY, the slave PLL doesn't need its dividers/mux
 * state to follow the master PLL's divider/mux state. Therefore, we don't
 * require special clock ops that also configure the slave PLL registers
 */
static int pll_7nm_register(struct dsi_pll_7nm *pll_7nm, struct clk_hw **provided_clocks)
{
	char clk_name[32];
	struct clk_init_data vco_init = {
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "ref",
		},
		.num_parents = 1,
		.name = clk_name,
		.flags = CLK_IGNORE_UNUSED,
		.ops = &clk_ops_dsi_pll_7nm_vco,
	};
	struct device *dev = &pll_7nm->phy->pdev->dev;
	struct clk_hw *hw, *pll_out_div, *pll_bit, *pll_by_2_bit;
	struct clk_hw *pll_post_out_div, *phy_pll_out_dsi_parent;
	int ret;

	DBG("DSI%d", pll_7nm->phy->id);

	snprintf(clk_name, sizeof(clk_name), "dsi%dvco_clk", pll_7nm->phy->id);
	pll_7nm->clk_hw.init = &vco_init;

	ret = devm_clk_hw_register(dev, &pll_7nm->clk_hw);
	if (ret)
		return ret;

	snprintf(clk_name, sizeof(clk_name), "dsi%d_pll_out_div_clk", pll_7nm->phy->id);

	pll_out_div = devm_clk_hw_register_divider_parent_hw(dev, clk_name,
			&pll_7nm->clk_hw, CLK_SET_RATE_PARENT,
			pll_7nm->phy->pll_base +
				REG_DSI_7nm_PHY_PLL_PLL_OUTDIV_RATE,
			0, 2, CLK_DIVIDER_POWER_OF_TWO, NULL);
	if (IS_ERR(pll_out_div)) {
		ret = PTR_ERR(pll_out_div);
		goto fail;
	}

	snprintf(clk_name, sizeof(clk_name), "dsi%d_pll_bit_clk", pll_7nm->phy->id);

	/* BIT CLK: DIV_CTRL_3_0 */
	pll_bit = devm_clk_hw_register_divider_parent_hw(dev, clk_name,
			pll_out_div, CLK_SET_RATE_PARENT,
			pll_7nm->phy->base + REG_DSI_7nm_PHY_CMN_CLK_CFG0,
			0, 4, CLK_DIVIDER_ONE_BASED, &pll_7nm->postdiv_lock);
	if (IS_ERR(pll_bit)) {
		ret = PTR_ERR(pll_bit);
		goto fail;
	}

	snprintf(clk_name, sizeof(clk_name), "dsi%d_phy_pll_out_byteclk", pll_7nm->phy->id);

	/* DSI Byte clock = VCO_CLK / OUT_DIV / BIT_DIV / 8 */
	hw = devm_clk_hw_register_fixed_factor_parent_hw(dev, clk_name,
			pll_bit, CLK_SET_RATE_PARENT, 1,
			pll_7nm->phy->cphy_mode ? 7 : 8);
	if (IS_ERR(hw)) {
		ret = PTR_ERR(hw);
		goto fail;
	}

	provided_clocks[DSI_BYTE_PLL_CLK] = hw;

	snprintf(clk_name, sizeof(clk_name), "dsi%d_pll_by_2_bit_clk", pll_7nm->phy->id);

	pll_by_2_bit = devm_clk_hw_register_fixed_factor_parent_hw(dev,
			clk_name, pll_bit, 0, 1, 2);
	if (IS_ERR(pll_by_2_bit)) {
		ret = PTR_ERR(pll_by_2_bit);
		goto fail;
	}

	snprintf(clk_name, sizeof(clk_name), "dsi%d_pll_post_out_div_clk", pll_7nm->phy->id);

	if (pll_7nm->phy->cphy_mode)
		pll_post_out_div = devm_clk_hw_register_fixed_factor_parent_hw(
				dev, clk_name, pll_out_div, 0, 2, 7);
	else
		pll_post_out_div = devm_clk_hw_register_fixed_factor_parent_hw(
				dev, clk_name, pll_out_div, 0, 1, 4);
	if (IS_ERR(pll_post_out_div)) {
		ret = PTR_ERR(pll_post_out_div);
		goto fail;
	}

	/* in CPHY mode, pclk_mux will always have post_out_div as parent
	 * don't register a pclk_mux clock and just use post_out_div instead
	 */
	if (pll_7nm->phy->cphy_mode) {
		u32 data;

		data = readl(pll_7nm->phy->base + REG_DSI_7nm_PHY_CMN_CLK_CFG1);
		writel(data | 3, pll_7nm->phy->base + REG_DSI_7nm_PHY_CMN_CLK_CFG1);

		phy_pll_out_dsi_parent = pll_post_out_div;
	} else {
		snprintf(clk_name, sizeof(clk_name), "dsi%d_pclk_mux", pll_7nm->phy->id);

		hw = devm_clk_hw_register_mux_parent_hws(dev, clk_name,
				((const struct clk_hw *[]){
					pll_bit,
					pll_by_2_bit,
				}), 2, 0, pll_7nm->phy->base +
					REG_DSI_7nm_PHY_CMN_CLK_CFG1,
				0, 1, 0, NULL);
		if (IS_ERR(hw)) {
			ret = PTR_ERR(hw);
			goto fail;
		}

		phy_pll_out_dsi_parent = hw;
	}

	snprintf(clk_name, sizeof(clk_name), "dsi%d_phy_pll_out_dsiclk", pll_7nm->phy->id);

	/* PIX CLK DIV : DIV_CTRL_7_4*/
	hw = devm_clk_hw_register_divider_parent_hw(dev, clk_name,
			phy_pll_out_dsi_parent, 0,
			pll_7nm->phy->base + REG_DSI_7nm_PHY_CMN_CLK_CFG0,
			4, 4, CLK_DIVIDER_ONE_BASED, &pll_7nm->postdiv_lock);
	if (IS_ERR(hw)) {
		ret = PTR_ERR(hw);
		goto fail;
	}

	provided_clocks[DSI_PIXEL_PLL_CLK] = hw;

	return 0;

fail:

	return ret;
}

static int dsi_pll_7nm_init(struct msm_dsi_phy *phy)
{
	struct platform_device *pdev = phy->pdev;
	struct dsi_pll_7nm *pll_7nm;
	int ret;

	pll_7nm = devm_kzalloc(&pdev->dev, sizeof(*pll_7nm), GFP_KERNEL);
	if (!pll_7nm)
		return -ENOMEM;

	DBG("DSI PLL%d", phy->id);

	pll_7nm_list[phy->id] = pll_7nm;

	spin_lock_init(&pll_7nm->postdiv_lock);

	pll_7nm->phy = phy;

	ret = pll_7nm_register(pll_7nm, phy->provided_clocks->hws);
	if (ret) {
		DRM_DEV_ERROR(&pdev->dev, "failed to register PLL: %d\n", ret);
		return ret;
	}

	phy->vco_hw = &pll_7nm->clk_hw;

	/* TODO: Remove this when we have proper display handover support */
	msm_dsi_phy_pll_save_state(phy);

	return 0;
}

static int dsi_phy_hw_v4_0_is_pll_on(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->base;
	u32 data = 0;

	data = readl(base + REG_DSI_7nm_PHY_CMN_PLL_CNTRL);
	mb(); /* make sure read happened */

	return (data & BIT(0));
}

static void dsi_phy_hw_v4_0_config_lpcdrx(struct msm_dsi_phy *phy, bool enable)
{
	void __iomem *lane_base = phy->lane_base;
	int phy_lane_0 = 0;	/* TODO: Support all lane swap configs */

	/*
	 * LPRX and CDRX need to enabled only for physical data lane
	 * corresponding to the logical data lane 0
	 */
	if (enable)
		writel(0x3, lane_base + REG_DSI_7nm_PHY_LN_LPRX_CTRL(phy_lane_0));
	else
		writel(0, lane_base + REG_DSI_7nm_PHY_LN_LPRX_CTRL(phy_lane_0));
}

static void dsi_phy_hw_v4_0_lane_settings(struct msm_dsi_phy *phy)
{
	int i;
	const u8 tx_dctrl_0[] = { 0x00, 0x00, 0x00, 0x04, 0x01 };
	const u8 tx_dctrl_1[] = { 0x40, 0x40, 0x40, 0x46, 0x41 };
	const u8 *tx_dctrl = tx_dctrl_0;
	void __iomem *lane_base = phy->lane_base;

	if (!(phy->cfg->quirks & DSI_PHY_7NM_QUIRK_PRE_V4_1))
		tx_dctrl = tx_dctrl_1;

	/* Strength ctrl settings */
	for (i = 0; i < 5; i++) {
		/*
		 * Disable LPRX and CDRX for all lanes. And later on, it will
		 * be only enabled for the physical data lane corresponding
		 * to the logical data lane 0
		 */
		writel(0, lane_base + REG_DSI_7nm_PHY_LN_LPRX_CTRL(i));
		writel(0x0, lane_base + REG_DSI_7nm_PHY_LN_PIN_SWAP(i));
	}

	dsi_phy_hw_v4_0_config_lpcdrx(phy, true);

	/* other settings */
	for (i = 0; i < 5; i++) {
		writel(0x0, lane_base + REG_DSI_7nm_PHY_LN_CFG0(i));
		writel(0x0, lane_base + REG_DSI_7nm_PHY_LN_CFG1(i));
		writel(i == 4 ? 0x8a : 0xa, lane_base + REG_DSI_7nm_PHY_LN_CFG2(i));
		writel(tx_dctrl[i], lane_base + REG_DSI_7nm_PHY_LN_TX_DCTRL(i));
	}
}

static int dsi_7nm_phy_enable(struct msm_dsi_phy *phy,
			      struct msm_dsi_phy_clk_request *clk_req)
{
	int ret;
	u32 status;
	u32 const delay_us = 5;
	u32 const timeout_us = 1000;
	struct msm_dsi_dphy_timing *timing = &phy->timing;
	void __iomem *base = phy->base;
	bool less_than_1500_mhz;
	u32 vreg_ctrl_0, vreg_ctrl_1, lane_ctrl0;
	u32 glbl_pemph_ctrl_0;
	u32 glbl_str_swi_cal_sel_ctrl, glbl_hstx_str_ctrl_0;
	u32 glbl_rescode_top_ctrl, glbl_rescode_bot_ctrl;
	u32 data;

	DBG("");

	if (phy->cphy_mode)
		ret = msm_dsi_cphy_timing_calc_v4(timing, clk_req);
	else
		ret = msm_dsi_dphy_timing_calc_v4(timing, clk_req);
	if (ret) {
		DRM_DEV_ERROR(&phy->pdev->dev,
			      "%s: PHY timing calculation failed\n", __func__);
		return -EINVAL;
	}

	if (dsi_phy_hw_v4_0_is_pll_on(phy))
		pr_warn("PLL turned on before configuring PHY\n");

	/* Request for REFGEN READY */
	if ((phy->cfg->quirks & DSI_PHY_7NM_QUIRK_V4_3) ||
	    (phy->cfg->quirks & DSI_PHY_7NM_QUIRK_V5_2)) {
		writel(0x1, phy->base + REG_DSI_7nm_PHY_CMN_GLBL_DIGTOP_SPARE10);
		udelay(500);
	}

	/* wait for REFGEN READY */
	ret = readl_poll_timeout_atomic(base + REG_DSI_7nm_PHY_CMN_PHY_STATUS,
					status, (status & BIT(0)),
					delay_us, timeout_us);
	if (ret) {
		pr_err("Ref gen not ready. Aborting\n");
		return -EINVAL;
	}

	/* TODO: CPHY enable path (this is for DPHY only) */

	/* Alter PHY configurations if data rate less than 1.5GHZ*/
	less_than_1500_mhz = (clk_req->bitclk_rate <= 1500000000);

	glbl_str_swi_cal_sel_ctrl = 0x00;
	if (phy->cphy_mode) {
		vreg_ctrl_0 = 0x51;
		vreg_ctrl_1 = 0x55;
		glbl_hstx_str_ctrl_0 = 0x00;
		glbl_pemph_ctrl_0 = 0x11;
		lane_ctrl0 = 0x17;
	} else {
		vreg_ctrl_0 = less_than_1500_mhz ? 0x53 : 0x52;
		vreg_ctrl_1 = 0x5c;
		glbl_hstx_str_ctrl_0 = 0x88;
		glbl_pemph_ctrl_0 = 0x00;
		lane_ctrl0 = 0x1f;
	}

	if ((phy->cfg->quirks & DSI_PHY_7NM_QUIRK_V5_2)) {
		if (phy->cphy_mode) {
			vreg_ctrl_0 = 0x45;
			vreg_ctrl_1 = 0x41;
			glbl_rescode_top_ctrl = 0x00;
			glbl_rescode_bot_ctrl = 0x00;
		} else {
			vreg_ctrl_0 = 0x44;
			vreg_ctrl_1 = 0x19;
			glbl_rescode_top_ctrl = less_than_1500_mhz ? 0x3c :  0x03;
			glbl_rescode_bot_ctrl = less_than_1500_mhz ? 0x38 :  0x3c;
		}
	} else if ((phy->cfg->quirks & DSI_PHY_7NM_QUIRK_V4_3)) {
		if (phy->cphy_mode) {
			glbl_rescode_top_ctrl = less_than_1500_mhz ? 0x3d :  0x01;
			glbl_rescode_bot_ctrl = less_than_1500_mhz ? 0x38 :  0x3b;
		} else {
			glbl_rescode_top_ctrl = less_than_1500_mhz ? 0x3d :  0x01;
			glbl_rescode_bot_ctrl = less_than_1500_mhz ? 0x38 :  0x39;
		}
	} else if (phy->cfg->quirks & DSI_PHY_7NM_QUIRK_V4_2) {
		if (phy->cphy_mode) {
			glbl_rescode_top_ctrl = less_than_1500_mhz ? 0x3d :  0x01;
			glbl_rescode_bot_ctrl = less_than_1500_mhz ? 0x38 :  0x3b;
		} else {
			glbl_rescode_top_ctrl = less_than_1500_mhz ? 0x3c :  0x00;
			glbl_rescode_bot_ctrl = less_than_1500_mhz ? 0x38 :  0x39;
		}
	} else if (phy->cfg->quirks & DSI_PHY_7NM_QUIRK_V4_1) {
		if (phy->cphy_mode) {
			glbl_hstx_str_ctrl_0 = 0x88;
			glbl_rescode_top_ctrl = 0x00;
			glbl_rescode_bot_ctrl = 0x3c;
		} else {
			glbl_rescode_top_ctrl = less_than_1500_mhz ? 0x3d :  0x00;
			glbl_rescode_bot_ctrl = less_than_1500_mhz ? 0x39 :  0x3c;
		}
	} else {
		if (phy->cphy_mode) {
			glbl_str_swi_cal_sel_ctrl = 0x03;
			glbl_hstx_str_ctrl_0 = 0x66;
		} else {
			vreg_ctrl_0 = less_than_1500_mhz ? 0x5B : 0x59;
			glbl_str_swi_cal_sel_ctrl = less_than_1500_mhz ? 0x03 : 0x00;
			glbl_hstx_str_ctrl_0 = less_than_1500_mhz ? 0x66 : 0x88;
		}
		glbl_rescode_top_ctrl = 0x03;
		glbl_rescode_bot_ctrl = 0x3c;
	}

	/* de-assert digital and pll power down */
	data = BIT(6) | BIT(5);
	writel(data, base + REG_DSI_7nm_PHY_CMN_CTRL_0);

	/* Assert PLL core reset */
	writel(0x00, base + REG_DSI_7nm_PHY_CMN_PLL_CNTRL);

	/* turn off resync FIFO */
	writel(0x00, base + REG_DSI_7nm_PHY_CMN_RBUF_CTRL);

	/* program CMN_CTRL_4 for minor_ver 2 chipsets*/
	if ((phy->cfg->quirks & DSI_PHY_7NM_QUIRK_V5_2) ||
	    (readl(base + REG_DSI_7nm_PHY_CMN_REVISION_ID0) & (0xf0)) == 0x20)
		writel(0x04, base + REG_DSI_7nm_PHY_CMN_CTRL_4);

	/* Configure PHY lane swap (TODO: we need to calculate this) */
	writel(0x21, base + REG_DSI_7nm_PHY_CMN_LANE_CFG0);
	writel(0x84, base + REG_DSI_7nm_PHY_CMN_LANE_CFG1);

	if (phy->cphy_mode)
		writel(BIT(6), base + REG_DSI_7nm_PHY_CMN_GLBL_CTRL);

	/* Enable LDO */
	writel(vreg_ctrl_0, base + REG_DSI_7nm_PHY_CMN_VREG_CTRL_0);
	writel(vreg_ctrl_1, base + REG_DSI_7nm_PHY_CMN_VREG_CTRL_1);

	writel(0x00, base + REG_DSI_7nm_PHY_CMN_CTRL_3);
	writel(glbl_str_swi_cal_sel_ctrl,
	       base + REG_DSI_7nm_PHY_CMN_GLBL_STR_SWI_CAL_SEL_CTRL);
	writel(glbl_hstx_str_ctrl_0,
	       base + REG_DSI_7nm_PHY_CMN_GLBL_HSTX_STR_CTRL_0);
	writel(glbl_pemph_ctrl_0,
	       base + REG_DSI_7nm_PHY_CMN_GLBL_PEMPH_CTRL_0);
	if (phy->cphy_mode)
		writel(0x01, base + REG_DSI_7nm_PHY_CMN_GLBL_PEMPH_CTRL_1);
	writel(glbl_rescode_top_ctrl,
	       base + REG_DSI_7nm_PHY_CMN_GLBL_RESCODE_OFFSET_TOP_CTRL);
	writel(glbl_rescode_bot_ctrl,
	       base + REG_DSI_7nm_PHY_CMN_GLBL_RESCODE_OFFSET_BOT_CTRL);
	writel(0x55, base + REG_DSI_7nm_PHY_CMN_GLBL_LPTX_STR_CTRL);

	/* Remove power down from all blocks */
	writel(0x7f, base + REG_DSI_7nm_PHY_CMN_CTRL_0);

	writel(lane_ctrl0, base + REG_DSI_7nm_PHY_CMN_LANE_CTRL0);

	/* Select full-rate mode */
	if (!phy->cphy_mode)
		writel(0x40, base + REG_DSI_7nm_PHY_CMN_CTRL_2);

	ret = dsi_7nm_set_usecase(phy);
	if (ret) {
		DRM_DEV_ERROR(&phy->pdev->dev, "%s: set pll usecase failed, %d\n",
			__func__, ret);
		return ret;
	}

	/* DSI PHY timings */
	if (phy->cphy_mode) {
		writel(0x00, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_0);
		writel(timing->hs_exit, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_4);
		writel(timing->shared_timings.clk_pre,
		       base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_5);
		writel(timing->clk_prepare, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_6);
		writel(timing->shared_timings.clk_post,
		       base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_7);
		writel(timing->hs_rqst, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_8);
		writel(0x02, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_9);
		writel(0x04, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_10);
		writel(0x00, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_11);
	} else {
		writel(0x00, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_0);
		writel(timing->clk_zero, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_1);
		writel(timing->clk_prepare, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_2);
		writel(timing->clk_trail, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_3);
		writel(timing->hs_exit, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_4);
		writel(timing->hs_zero, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_5);
		writel(timing->hs_prepare, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_6);
		writel(timing->hs_trail, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_7);
		writel(timing->hs_rqst, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_8);
		writel(0x02, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_9);
		writel(0x04, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_10);
		writel(0x00, base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_11);
		writel(timing->shared_timings.clk_pre,
		       base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_12);
		writel(timing->shared_timings.clk_post,
		       base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_13);
	}

	/* DSI lane settings */
	dsi_phy_hw_v4_0_lane_settings(phy);

	DBG("DSI%d PHY enabled", phy->id);

	return 0;
}

static bool dsi_7nm_set_continuous_clock(struct msm_dsi_phy *phy, bool enable)
{
	void __iomem *base = phy->base;
	u32 data;

	data = readl(base + REG_DSI_7nm_PHY_CMN_LANE_CTRL1);
	if (enable)
		data |= BIT(5) | BIT(6);
	else
		data &= ~(BIT(5) | BIT(6));
	writel(data, base + REG_DSI_7nm_PHY_CMN_LANE_CTRL1);

	return enable;
}

static void dsi_7nm_phy_disable(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->base;
	u32 data;

	DBG("");

	if (dsi_phy_hw_v4_0_is_pll_on(phy))
		pr_warn("Turning OFF PHY while PLL is on\n");

	dsi_phy_hw_v4_0_config_lpcdrx(phy, false);

	/* Turn off REFGEN Vote */
	if ((phy->cfg->quirks & DSI_PHY_7NM_QUIRK_V4_3) ||
	    (phy->cfg->quirks & DSI_PHY_7NM_QUIRK_V5_2)) {
		writel(0x0, base + REG_DSI_7nm_PHY_CMN_GLBL_DIGTOP_SPARE10);
		wmb();
		/* Delay to ensure HW removes vote before PHY shut down */
		udelay(2);
	}

	data = readl(base + REG_DSI_7nm_PHY_CMN_CTRL_0);

	/* disable all lanes */
	data &= ~0x1F;
	writel(data, base + REG_DSI_7nm_PHY_CMN_CTRL_0);
	writel(0, base + REG_DSI_7nm_PHY_CMN_LANE_CTRL0);

	/* Turn off all PHY blocks */
	writel(0x00, base + REG_DSI_7nm_PHY_CMN_CTRL_0);
	/* make sure phy is turned off */
	wmb();

	DBG("DSI%d PHY disabled", phy->id);
}

static const struct regulator_bulk_data dsi_phy_7nm_36mA_regulators[] = {
	{ .supply = "vdds", .init_load_uA = 36000 },
};

static const struct regulator_bulk_data dsi_phy_7nm_37750uA_regulators[] = {
	{ .supply = "vdds", .init_load_uA = 37550 },
};

static const struct regulator_bulk_data dsi_phy_7nm_98000uA_regulators[] = {
	{ .supply = "vdds", .init_load_uA = 98000 },
};

static const struct regulator_bulk_data dsi_phy_7nm_97800uA_regulators[] = {
	{ .supply = "vdds", .init_load_uA = 97800 },
};

static const struct regulator_bulk_data dsi_phy_7nm_98400uA_regulators[] = {
	{ .supply = "vdds", .init_load_uA = 98400 },
};

const struct msm_dsi_phy_cfg dsi_phy_7nm_cfgs = {
	.has_phy_lane = true,
	.regulator_data = dsi_phy_7nm_36mA_regulators,
	.num_regulators = ARRAY_SIZE(dsi_phy_7nm_36mA_regulators),
	.ops = {
		.enable = dsi_7nm_phy_enable,
		.disable = dsi_7nm_phy_disable,
		.pll_init = dsi_pll_7nm_init,
		.save_pll_state = dsi_7nm_pll_save_state,
		.restore_pll_state = dsi_7nm_pll_restore_state,
		.set_continuous_clock = dsi_7nm_set_continuous_clock,
	},
	.min_pll_rate = 600000000UL,
#ifdef CONFIG_64BIT
	.max_pll_rate = 5000000000UL,
#else
	.max_pll_rate = ULONG_MAX,
#endif
	.io_start = { 0xae94400, 0xae96400 },
	.num_dsi_phy = 2,
	.quirks = DSI_PHY_7NM_QUIRK_V4_1,
};

const struct msm_dsi_phy_cfg dsi_phy_7nm_6375_cfgs = {
	.has_phy_lane = true,
	.ops = {
		.enable = dsi_7nm_phy_enable,
		.disable = dsi_7nm_phy_disable,
		.pll_init = dsi_pll_7nm_init,
		.save_pll_state = dsi_7nm_pll_save_state,
		.restore_pll_state = dsi_7nm_pll_restore_state,
	},
	.min_pll_rate = 600000000UL,
#ifdef CONFIG_64BIT
	.max_pll_rate = 5000000000ULL,
#else
	.max_pll_rate = ULONG_MAX,
#endif
	.io_start = { 0x5e94400 },
	.num_dsi_phy = 1,
	.quirks = DSI_PHY_7NM_QUIRK_V4_1,
};

const struct msm_dsi_phy_cfg dsi_phy_7nm_8150_cfgs = {
	.has_phy_lane = true,
	.regulator_data = dsi_phy_7nm_36mA_regulators,
	.num_regulators = ARRAY_SIZE(dsi_phy_7nm_36mA_regulators),
	.ops = {
		.enable = dsi_7nm_phy_enable,
		.disable = dsi_7nm_phy_disable,
		.pll_init = dsi_pll_7nm_init,
		.save_pll_state = dsi_7nm_pll_save_state,
		.restore_pll_state = dsi_7nm_pll_restore_state,
		.set_continuous_clock = dsi_7nm_set_continuous_clock,
	},
	.min_pll_rate = 1000000000UL,
	.max_pll_rate = 3500000000UL,
	.io_start = { 0xae94400, 0xae96400 },
	.num_dsi_phy = 2,
	.quirks = DSI_PHY_7NM_QUIRK_PRE_V4_1,
};

const struct msm_dsi_phy_cfg dsi_phy_7nm_7280_cfgs = {
	.has_phy_lane = true,
	.regulator_data = dsi_phy_7nm_37750uA_regulators,
	.num_regulators = ARRAY_SIZE(dsi_phy_7nm_37750uA_regulators),
	.ops = {
		.enable = dsi_7nm_phy_enable,
		.disable = dsi_7nm_phy_disable,
		.pll_init = dsi_pll_7nm_init,
		.save_pll_state = dsi_7nm_pll_save_state,
		.restore_pll_state = dsi_7nm_pll_restore_state,
	},
	.min_pll_rate = 600000000UL,
#ifdef CONFIG_64BIT
	.max_pll_rate = 5000000000ULL,
#else
	.max_pll_rate = ULONG_MAX,
#endif
	.io_start = { 0xae94400 },
	.num_dsi_phy = 1,
	.quirks = DSI_PHY_7NM_QUIRK_V4_1,
};

const struct msm_dsi_phy_cfg dsi_phy_5nm_8350_cfgs = {
	.has_phy_lane = true,
	.regulator_data = dsi_phy_7nm_37750uA_regulators,
	.num_regulators = ARRAY_SIZE(dsi_phy_7nm_37750uA_regulators),
	.ops = {
		.enable = dsi_7nm_phy_enable,
		.disable = dsi_7nm_phy_disable,
		.pll_init = dsi_pll_7nm_init,
		.save_pll_state = dsi_7nm_pll_save_state,
		.restore_pll_state = dsi_7nm_pll_restore_state,
		.set_continuous_clock = dsi_7nm_set_continuous_clock,
	},
	.min_pll_rate = 600000000UL,
#ifdef CONFIG_64BIT
	.max_pll_rate = 5000000000UL,
#else
	.max_pll_rate = ULONG_MAX,
#endif
	.io_start = { 0xae94400, 0xae96400 },
	.num_dsi_phy = 2,
	.quirks = DSI_PHY_7NM_QUIRK_V4_2,
};

const struct msm_dsi_phy_cfg dsi_phy_5nm_8450_cfgs = {
	.has_phy_lane = true,
	.regulator_data = dsi_phy_7nm_97800uA_regulators,
	.num_regulators = ARRAY_SIZE(dsi_phy_7nm_97800uA_regulators),
	.ops = {
		.enable = dsi_7nm_phy_enable,
		.disable = dsi_7nm_phy_disable,
		.pll_init = dsi_pll_7nm_init,
		.save_pll_state = dsi_7nm_pll_save_state,
		.restore_pll_state = dsi_7nm_pll_restore_state,
		.set_continuous_clock = dsi_7nm_set_continuous_clock,
	},
	.min_pll_rate = 600000000UL,
#ifdef CONFIG_64BIT
	.max_pll_rate = 5000000000UL,
#else
	.max_pll_rate = ULONG_MAX,
#endif
	.io_start = { 0xae94400, 0xae96400 },
	.num_dsi_phy = 2,
	.quirks = DSI_PHY_7NM_QUIRK_V4_3,
};

const struct msm_dsi_phy_cfg dsi_phy_4nm_8550_cfgs = {
	.has_phy_lane = true,
	.regulator_data = dsi_phy_7nm_98400uA_regulators,
	.num_regulators = ARRAY_SIZE(dsi_phy_7nm_98400uA_regulators),
	.ops = {
		.enable = dsi_7nm_phy_enable,
		.disable = dsi_7nm_phy_disable,
		.pll_init = dsi_pll_7nm_init,
		.save_pll_state = dsi_7nm_pll_save_state,
		.restore_pll_state = dsi_7nm_pll_restore_state,
		.set_continuous_clock = dsi_7nm_set_continuous_clock,
	},
	.min_pll_rate = 600000000UL,
#ifdef CONFIG_64BIT
	.max_pll_rate = 5000000000UL,
#else
	.max_pll_rate = ULONG_MAX,
#endif
	.io_start = { 0xae95000, 0xae97000 },
	.num_dsi_phy = 2,
	.quirks = DSI_PHY_7NM_QUIRK_V5_2,
};

const struct msm_dsi_phy_cfg dsi_phy_4nm_8650_cfgs = {
	.has_phy_lane = true,
	.regulator_data = dsi_phy_7nm_98000uA_regulators,
	.num_regulators = ARRAY_SIZE(dsi_phy_7nm_98000uA_regulators),
	.ops = {
		.enable = dsi_7nm_phy_enable,
		.disable = dsi_7nm_phy_disable,
		.pll_init = dsi_pll_7nm_init,
		.save_pll_state = dsi_7nm_pll_save_state,
		.restore_pll_state = dsi_7nm_pll_restore_state,
		.set_continuous_clock = dsi_7nm_set_continuous_clock,
	},
	.min_pll_rate = 600000000UL,
#ifdef CONFIG_64BIT
	.max_pll_rate = 5000000000UL,
#else
	.max_pll_rate = ULONG_MAX,
#endif
	.io_start = { 0xae95000, 0xae97000 },
	.num_dsi_phy = 2,
	.quirks = DSI_PHY_7NM_QUIRK_V5_2,
};

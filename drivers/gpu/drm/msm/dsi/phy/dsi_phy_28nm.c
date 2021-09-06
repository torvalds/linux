// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>

#include "dsi_phy.h"
#include "dsi.xml.h"

/*
 * DSI PLL 28nm - clock diagram (eg: DSI0):
 *
 *         dsi0analog_postdiv_clk
 *                             |         dsi0indirect_path_div2_clk
 *                             |          |
 *                   +------+  |  +----+  |  |\   dsi0byte_mux
 *  dsi0vco_clk --o--| DIV1 |--o--| /2 |--o--| \   |
 *                |  +------+     +----+     | m|  |  +----+
 *                |                          | u|--o--| /4 |-- dsi0pllbyte
 *                |                          | x|     +----+
 *                o--------------------------| /
 *                |                          |/
 *                |          +------+
 *                o----------| DIV3 |------------------------- dsi0pll
 *                           +------+
 */

#define POLL_MAX_READS			10
#define POLL_TIMEOUT_US		50

#define VCO_REF_CLK_RATE		19200000
#define VCO_MIN_RATE			350000000
#define VCO_MAX_RATE			750000000

/* v2.0.0 28nm LP implementation */
#define DSI_PHY_28NM_QUIRK_PHY_LP	BIT(0)

#define LPFR_LUT_SIZE			10
struct lpfr_cfg {
	unsigned long vco_rate;
	u32 resistance;
};

/* Loop filter resistance: */
static const struct lpfr_cfg lpfr_lut[LPFR_LUT_SIZE] = {
	{ 479500000,  8 },
	{ 480000000, 11 },
	{ 575500000,  8 },
	{ 576000000, 12 },
	{ 610500000,  8 },
	{ 659500000,  9 },
	{ 671500000, 10 },
	{ 672000000, 14 },
	{ 708500000, 10 },
	{ 750000000, 11 },
};

struct pll_28nm_cached_state {
	unsigned long vco_rate;
	u8 postdiv3;
	u8 postdiv1;
	u8 byte_mux;
};

struct dsi_pll_28nm {
	struct clk_hw clk_hw;

	struct msm_dsi_phy *phy;

	struct pll_28nm_cached_state cached_state;
};

#define to_pll_28nm(x)	container_of(x, struct dsi_pll_28nm, clk_hw)

static bool pll_28nm_poll_for_ready(struct dsi_pll_28nm *pll_28nm,
				u32 nb_tries, u32 timeout_us)
{
	bool pll_locked = false;
	u32 val;

	while (nb_tries--) {
		val = dsi_phy_read(pll_28nm->phy->pll_base + REG_DSI_28nm_PHY_PLL_STATUS);
		pll_locked = !!(val & DSI_28nm_PHY_PLL_STATUS_PLL_RDY);

		if (pll_locked)
			break;

		udelay(timeout_us);
	}
	DBG("DSI PLL is %slocked", pll_locked ? "" : "*not* ");

	return pll_locked;
}

static void pll_28nm_software_reset(struct dsi_pll_28nm *pll_28nm)
{
	void __iomem *base = pll_28nm->phy->pll_base;

	/*
	 * Add HW recommended delays after toggling the software
	 * reset bit off and back on.
	 */
	dsi_phy_write_udelay(base + REG_DSI_28nm_PHY_PLL_TEST_CFG,
			DSI_28nm_PHY_PLL_TEST_CFG_PLL_SW_RESET, 1);
	dsi_phy_write_udelay(base + REG_DSI_28nm_PHY_PLL_TEST_CFG, 0x00, 1);
}

/*
 * Clock Callbacks
 */
static int dsi_pll_28nm_clk_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(hw);
	struct device *dev = &pll_28nm->phy->pdev->dev;
	void __iomem *base = pll_28nm->phy->pll_base;
	unsigned long div_fbx1000, gen_vco_clk;
	u32 refclk_cfg, frac_n_mode, frac_n_value;
	u32 sdm_cfg0, sdm_cfg1, sdm_cfg2, sdm_cfg3;
	u32 cal_cfg10, cal_cfg11;
	u32 rem;
	int i;

	VERB("rate=%lu, parent's=%lu", rate, parent_rate);

	/* Force postdiv2 to be div-4 */
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_POSTDIV2_CFG, 3);

	/* Configure the Loop filter resistance */
	for (i = 0; i < LPFR_LUT_SIZE; i++)
		if (rate <= lpfr_lut[i].vco_rate)
			break;
	if (i == LPFR_LUT_SIZE) {
		DRM_DEV_ERROR(dev, "unable to get loop filter resistance. vco=%lu\n",
				rate);
		return -EINVAL;
	}
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_LPFR_CFG, lpfr_lut[i].resistance);

	/* Loop filter capacitance values : c1 and c2 */
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_LPFC1_CFG, 0x70);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_LPFC2_CFG, 0x15);

	rem = rate % VCO_REF_CLK_RATE;
	if (rem) {
		refclk_cfg = DSI_28nm_PHY_PLL_REFCLK_CFG_DBLR;
		frac_n_mode = 1;
		div_fbx1000 = rate / (VCO_REF_CLK_RATE / 500);
		gen_vco_clk = div_fbx1000 * (VCO_REF_CLK_RATE / 500);
	} else {
		refclk_cfg = 0x0;
		frac_n_mode = 0;
		div_fbx1000 = rate / (VCO_REF_CLK_RATE / 1000);
		gen_vco_clk = div_fbx1000 * (VCO_REF_CLK_RATE / 1000);
	}

	DBG("refclk_cfg = %d", refclk_cfg);

	rem = div_fbx1000 % 1000;
	frac_n_value = (rem << 16) / 1000;

	DBG("div_fb = %lu", div_fbx1000);
	DBG("frac_n_value = %d", frac_n_value);

	DBG("Generated VCO Clock: %lu", gen_vco_clk);
	rem = 0;
	sdm_cfg1 = dsi_phy_read(base + REG_DSI_28nm_PHY_PLL_SDM_CFG1);
	sdm_cfg1 &= ~DSI_28nm_PHY_PLL_SDM_CFG1_DC_OFFSET__MASK;
	if (frac_n_mode) {
		sdm_cfg0 = 0x0;
		sdm_cfg0 |= DSI_28nm_PHY_PLL_SDM_CFG0_BYP_DIV(0);
		sdm_cfg1 |= DSI_28nm_PHY_PLL_SDM_CFG1_DC_OFFSET(
				(u32)(((div_fbx1000 / 1000) & 0x3f) - 1));
		sdm_cfg3 = frac_n_value >> 8;
		sdm_cfg2 = frac_n_value & 0xff;
	} else {
		sdm_cfg0 = DSI_28nm_PHY_PLL_SDM_CFG0_BYP;
		sdm_cfg0 |= DSI_28nm_PHY_PLL_SDM_CFG0_BYP_DIV(
				(u32)(((div_fbx1000 / 1000) & 0x3f) - 1));
		sdm_cfg1 |= DSI_28nm_PHY_PLL_SDM_CFG1_DC_OFFSET(0);
		sdm_cfg2 = 0;
		sdm_cfg3 = 0;
	}

	DBG("sdm_cfg0=%d", sdm_cfg0);
	DBG("sdm_cfg1=%d", sdm_cfg1);
	DBG("sdm_cfg2=%d", sdm_cfg2);
	DBG("sdm_cfg3=%d", sdm_cfg3);

	cal_cfg11 = (u32)(gen_vco_clk / (256 * 1000000));
	cal_cfg10 = (u32)((gen_vco_clk % (256 * 1000000)) / 1000000);
	DBG("cal_cfg10=%d, cal_cfg11=%d", cal_cfg10, cal_cfg11);

	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_CHGPUMP_CFG, 0x02);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_CAL_CFG3,    0x2b);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_CAL_CFG4,    0x06);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_LKDET_CFG2,  0x0d);

	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_SDM_CFG1, sdm_cfg1);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_SDM_CFG2,
		DSI_28nm_PHY_PLL_SDM_CFG2_FREQ_SEED_7_0(sdm_cfg2));
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_SDM_CFG3,
		DSI_28nm_PHY_PLL_SDM_CFG3_FREQ_SEED_15_8(sdm_cfg3));
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_SDM_CFG4, 0x00);

	/* Add hardware recommended delay for correct PLL configuration */
	if (pll_28nm->phy->cfg->quirks & DSI_PHY_28NM_QUIRK_PHY_LP)
		udelay(1000);
	else
		udelay(1);

	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_REFCLK_CFG, refclk_cfg);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_PWRGEN_CFG, 0x00);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_VCOLPF_CFG, 0x31);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_SDM_CFG0,   sdm_cfg0);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_CAL_CFG0,   0x12);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_CAL_CFG6,   0x30);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_CAL_CFG7,   0x00);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_CAL_CFG8,   0x60);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_CAL_CFG9,   0x00);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_CAL_CFG10,  cal_cfg10 & 0xff);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_CAL_CFG11,  cal_cfg11 & 0xff);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_EFUSE_CFG,  0x20);

	return 0;
}

static int dsi_pll_28nm_clk_is_enabled(struct clk_hw *hw)
{
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(hw);

	return pll_28nm_poll_for_ready(pll_28nm, POLL_MAX_READS,
					POLL_TIMEOUT_US);
}

static unsigned long dsi_pll_28nm_clk_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(hw);
	void __iomem *base = pll_28nm->phy->pll_base;
	u32 sdm0, doubler, sdm_byp_div;
	u32 sdm_dc_off, sdm_freq_seed, sdm2, sdm3;
	u32 ref_clk = VCO_REF_CLK_RATE;
	unsigned long vco_rate;

	VERB("parent_rate=%lu", parent_rate);

	/* Check to see if the ref clk doubler is enabled */
	doubler = dsi_phy_read(base + REG_DSI_28nm_PHY_PLL_REFCLK_CFG) &
			DSI_28nm_PHY_PLL_REFCLK_CFG_DBLR;
	ref_clk += (doubler * VCO_REF_CLK_RATE);

	/* see if it is integer mode or sdm mode */
	sdm0 = dsi_phy_read(base + REG_DSI_28nm_PHY_PLL_SDM_CFG0);
	if (sdm0 & DSI_28nm_PHY_PLL_SDM_CFG0_BYP) {
		/* integer mode */
		sdm_byp_div = FIELD(
				dsi_phy_read(base + REG_DSI_28nm_PHY_PLL_SDM_CFG0),
				DSI_28nm_PHY_PLL_SDM_CFG0_BYP_DIV) + 1;
		vco_rate = ref_clk * sdm_byp_div;
	} else {
		/* sdm mode */
		sdm_dc_off = FIELD(
				dsi_phy_read(base + REG_DSI_28nm_PHY_PLL_SDM_CFG1),
				DSI_28nm_PHY_PLL_SDM_CFG1_DC_OFFSET);
		DBG("sdm_dc_off = %d", sdm_dc_off);
		sdm2 = FIELD(dsi_phy_read(base + REG_DSI_28nm_PHY_PLL_SDM_CFG2),
				DSI_28nm_PHY_PLL_SDM_CFG2_FREQ_SEED_7_0);
		sdm3 = FIELD(dsi_phy_read(base + REG_DSI_28nm_PHY_PLL_SDM_CFG3),
				DSI_28nm_PHY_PLL_SDM_CFG3_FREQ_SEED_15_8);
		sdm_freq_seed = (sdm3 << 8) | sdm2;
		DBG("sdm_freq_seed = %d", sdm_freq_seed);

		vco_rate = (ref_clk * (sdm_dc_off + 1)) +
			mult_frac(ref_clk, sdm_freq_seed, BIT(16));
		DBG("vco rate = %lu", vco_rate);
	}

	DBG("returning vco rate = %lu", vco_rate);

	return vco_rate;
}

static int _dsi_pll_28nm_vco_prepare_hpm(struct dsi_pll_28nm *pll_28nm)
{
	struct device *dev = &pll_28nm->phy->pdev->dev;
	void __iomem *base = pll_28nm->phy->pll_base;
	u32 max_reads = 5, timeout_us = 100;
	bool locked;
	u32 val;
	int i;

	DBG("id=%d", pll_28nm->phy->id);

	pll_28nm_software_reset(pll_28nm);

	/*
	 * PLL power up sequence.
	 * Add necessary delays recommended by hardware.
	 */
	val = DSI_28nm_PHY_PLL_GLB_CFG_PLL_PWRDN_B;
	dsi_phy_write_udelay(base + REG_DSI_28nm_PHY_PLL_GLB_CFG, val, 1);

	val |= DSI_28nm_PHY_PLL_GLB_CFG_PLL_PWRGEN_PWRDN_B;
	dsi_phy_write_udelay(base + REG_DSI_28nm_PHY_PLL_GLB_CFG, val, 200);

	val |= DSI_28nm_PHY_PLL_GLB_CFG_PLL_LDO_PWRDN_B;
	dsi_phy_write_udelay(base + REG_DSI_28nm_PHY_PLL_GLB_CFG, val, 500);

	val |= DSI_28nm_PHY_PLL_GLB_CFG_PLL_ENABLE;
	dsi_phy_write_udelay(base + REG_DSI_28nm_PHY_PLL_GLB_CFG, val, 600);

	for (i = 0; i < 2; i++) {
		/* DSI Uniphy lock detect setting */
		dsi_phy_write_udelay(base + REG_DSI_28nm_PHY_PLL_LKDET_CFG2,
				0x0c, 100);
		dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_LKDET_CFG2, 0x0d);

		/* poll for PLL ready status */
		locked = pll_28nm_poll_for_ready(pll_28nm,
						max_reads, timeout_us);
		if (locked)
			break;

		pll_28nm_software_reset(pll_28nm);

		/*
		 * PLL power up sequence.
		 * Add necessary delays recommended by hardware.
		 */
		val = DSI_28nm_PHY_PLL_GLB_CFG_PLL_PWRDN_B;
		dsi_phy_write_udelay(base + REG_DSI_28nm_PHY_PLL_GLB_CFG, val, 1);

		val |= DSI_28nm_PHY_PLL_GLB_CFG_PLL_PWRGEN_PWRDN_B;
		dsi_phy_write_udelay(base + REG_DSI_28nm_PHY_PLL_GLB_CFG, val, 200);

		val |= DSI_28nm_PHY_PLL_GLB_CFG_PLL_LDO_PWRDN_B;
		dsi_phy_write_udelay(base + REG_DSI_28nm_PHY_PLL_GLB_CFG, val, 250);

		val &= ~DSI_28nm_PHY_PLL_GLB_CFG_PLL_LDO_PWRDN_B;
		dsi_phy_write_udelay(base + REG_DSI_28nm_PHY_PLL_GLB_CFG, val, 200);

		val |= DSI_28nm_PHY_PLL_GLB_CFG_PLL_LDO_PWRDN_B;
		dsi_phy_write_udelay(base + REG_DSI_28nm_PHY_PLL_GLB_CFG, val, 500);

		val |= DSI_28nm_PHY_PLL_GLB_CFG_PLL_ENABLE;
		dsi_phy_write_udelay(base + REG_DSI_28nm_PHY_PLL_GLB_CFG, val, 600);
	}

	if (unlikely(!locked))
		DRM_DEV_ERROR(dev, "DSI PLL lock failed\n");
	else
		DBG("DSI PLL Lock success");

	return locked ? 0 : -EINVAL;
}

static int dsi_pll_28nm_vco_prepare_hpm(struct clk_hw *hw)
{
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(hw);
	int i, ret;

	if (unlikely(pll_28nm->phy->pll_on))
		return 0;

	for (i = 0; i < 3; i++) {
		ret = _dsi_pll_28nm_vco_prepare_hpm(pll_28nm);
		if (!ret) {
			pll_28nm->phy->pll_on = true;
			return 0;
		}
	}

	return ret;
}

static int dsi_pll_28nm_vco_prepare_lp(struct clk_hw *hw)
{
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(hw);
	struct device *dev = &pll_28nm->phy->pdev->dev;
	void __iomem *base = pll_28nm->phy->pll_base;
	bool locked;
	u32 max_reads = 10, timeout_us = 50;
	u32 val;

	DBG("id=%d", pll_28nm->phy->id);

	if (unlikely(pll_28nm->phy->pll_on))
		return 0;

	pll_28nm_software_reset(pll_28nm);

	/*
	 * PLL power up sequence.
	 * Add necessary delays recommended by hardware.
	 */
	dsi_phy_write_ndelay(base + REG_DSI_28nm_PHY_PLL_CAL_CFG1, 0x34, 500);

	val = DSI_28nm_PHY_PLL_GLB_CFG_PLL_PWRDN_B;
	dsi_phy_write_ndelay(base + REG_DSI_28nm_PHY_PLL_GLB_CFG, val, 500);

	val |= DSI_28nm_PHY_PLL_GLB_CFG_PLL_PWRGEN_PWRDN_B;
	dsi_phy_write_ndelay(base + REG_DSI_28nm_PHY_PLL_GLB_CFG, val, 500);

	val |= DSI_28nm_PHY_PLL_GLB_CFG_PLL_LDO_PWRDN_B |
		DSI_28nm_PHY_PLL_GLB_CFG_PLL_ENABLE;
	dsi_phy_write_ndelay(base + REG_DSI_28nm_PHY_PLL_GLB_CFG, val, 500);

	/* DSI PLL toggle lock detect setting */
	dsi_phy_write_ndelay(base + REG_DSI_28nm_PHY_PLL_LKDET_CFG2, 0x04, 500);
	dsi_phy_write_udelay(base + REG_DSI_28nm_PHY_PLL_LKDET_CFG2, 0x05, 512);

	locked = pll_28nm_poll_for_ready(pll_28nm, max_reads, timeout_us);

	if (unlikely(!locked)) {
		DRM_DEV_ERROR(dev, "DSI PLL lock failed\n");
		return -EINVAL;
	}

	DBG("DSI PLL lock success");
	pll_28nm->phy->pll_on = true;

	return 0;
}

static void dsi_pll_28nm_vco_unprepare(struct clk_hw *hw)
{
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(hw);

	DBG("id=%d", pll_28nm->phy->id);

	if (unlikely(!pll_28nm->phy->pll_on))
		return;

	dsi_phy_write(pll_28nm->phy->pll_base + REG_DSI_28nm_PHY_PLL_GLB_CFG, 0x00);

	pll_28nm->phy->pll_on = false;
}

static long dsi_pll_28nm_clk_round_rate(struct clk_hw *hw,
		unsigned long rate, unsigned long *parent_rate)
{
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(hw);

	if      (rate < pll_28nm->phy->cfg->min_pll_rate)
		return  pll_28nm->phy->cfg->min_pll_rate;
	else if (rate > pll_28nm->phy->cfg->max_pll_rate)
		return  pll_28nm->phy->cfg->max_pll_rate;
	else
		return rate;
}

static const struct clk_ops clk_ops_dsi_pll_28nm_vco_hpm = {
	.round_rate = dsi_pll_28nm_clk_round_rate,
	.set_rate = dsi_pll_28nm_clk_set_rate,
	.recalc_rate = dsi_pll_28nm_clk_recalc_rate,
	.prepare = dsi_pll_28nm_vco_prepare_hpm,
	.unprepare = dsi_pll_28nm_vco_unprepare,
	.is_enabled = dsi_pll_28nm_clk_is_enabled,
};

static const struct clk_ops clk_ops_dsi_pll_28nm_vco_lp = {
	.round_rate = dsi_pll_28nm_clk_round_rate,
	.set_rate = dsi_pll_28nm_clk_set_rate,
	.recalc_rate = dsi_pll_28nm_clk_recalc_rate,
	.prepare = dsi_pll_28nm_vco_prepare_lp,
	.unprepare = dsi_pll_28nm_vco_unprepare,
	.is_enabled = dsi_pll_28nm_clk_is_enabled,
};

/*
 * PLL Callbacks
 */

static void dsi_28nm_pll_save_state(struct msm_dsi_phy *phy)
{
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(phy->vco_hw);
	struct pll_28nm_cached_state *cached_state = &pll_28nm->cached_state;
	void __iomem *base = pll_28nm->phy->pll_base;

	cached_state->postdiv3 =
			dsi_phy_read(base + REG_DSI_28nm_PHY_PLL_POSTDIV3_CFG);
	cached_state->postdiv1 =
			dsi_phy_read(base + REG_DSI_28nm_PHY_PLL_POSTDIV1_CFG);
	cached_state->byte_mux = dsi_phy_read(base + REG_DSI_28nm_PHY_PLL_VREG_CFG);
	if (dsi_pll_28nm_clk_is_enabled(phy->vco_hw))
		cached_state->vco_rate = clk_hw_get_rate(phy->vco_hw);
	else
		cached_state->vco_rate = 0;
}

static int dsi_28nm_pll_restore_state(struct msm_dsi_phy *phy)
{
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(phy->vco_hw);
	struct pll_28nm_cached_state *cached_state = &pll_28nm->cached_state;
	void __iomem *base = pll_28nm->phy->pll_base;
	int ret;

	ret = dsi_pll_28nm_clk_set_rate(phy->vco_hw,
					cached_state->vco_rate, 0);
	if (ret) {
		DRM_DEV_ERROR(&pll_28nm->phy->pdev->dev,
			"restore vco rate failed. ret=%d\n", ret);
		return ret;
	}

	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_POSTDIV3_CFG,
			cached_state->postdiv3);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_POSTDIV1_CFG,
			cached_state->postdiv1);
	dsi_phy_write(base + REG_DSI_28nm_PHY_PLL_VREG_CFG,
			cached_state->byte_mux);

	return 0;
}

static int pll_28nm_register(struct dsi_pll_28nm *pll_28nm, struct clk_hw **provided_clocks)
{
	char clk_name[32], parent1[32], parent2[32], vco_name[32];
	struct clk_init_data vco_init = {
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.name = vco_name,
		.flags = CLK_IGNORE_UNUSED,
	};
	struct device *dev = &pll_28nm->phy->pdev->dev;
	struct clk_hw *hw;
	int ret;

	DBG("%d", pll_28nm->phy->id);

	if (pll_28nm->phy->cfg->quirks & DSI_PHY_28NM_QUIRK_PHY_LP)
		vco_init.ops = &clk_ops_dsi_pll_28nm_vco_lp;
	else
		vco_init.ops = &clk_ops_dsi_pll_28nm_vco_hpm;

	snprintf(vco_name, 32, "dsi%dvco_clk", pll_28nm->phy->id);
	pll_28nm->clk_hw.init = &vco_init;
	ret = devm_clk_hw_register(dev, &pll_28nm->clk_hw);
	if (ret)
		return ret;

	snprintf(clk_name, 32, "dsi%danalog_postdiv_clk", pll_28nm->phy->id);
	snprintf(parent1, 32, "dsi%dvco_clk", pll_28nm->phy->id);
	hw = devm_clk_hw_register_divider(dev, clk_name,
			parent1, CLK_SET_RATE_PARENT,
			pll_28nm->phy->pll_base +
			REG_DSI_28nm_PHY_PLL_POSTDIV1_CFG,
			0, 4, 0, NULL);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	snprintf(clk_name, 32, "dsi%dindirect_path_div2_clk", pll_28nm->phy->id);
	snprintf(parent1, 32, "dsi%danalog_postdiv_clk", pll_28nm->phy->id);
	hw = devm_clk_hw_register_fixed_factor(dev, clk_name,
			parent1, CLK_SET_RATE_PARENT,
			1, 2);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	snprintf(clk_name, 32, "dsi%dpll", pll_28nm->phy->id);
	snprintf(parent1, 32, "dsi%dvco_clk", pll_28nm->phy->id);
	hw = devm_clk_hw_register_divider(dev, clk_name,
				parent1, 0, pll_28nm->phy->pll_base +
				REG_DSI_28nm_PHY_PLL_POSTDIV3_CFG,
				0, 8, 0, NULL);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	provided_clocks[DSI_PIXEL_PLL_CLK] = hw;

	snprintf(clk_name, 32, "dsi%dbyte_mux", pll_28nm->phy->id);
	snprintf(parent1, 32, "dsi%dvco_clk", pll_28nm->phy->id);
	snprintf(parent2, 32, "dsi%dindirect_path_div2_clk", pll_28nm->phy->id);
	hw = devm_clk_hw_register_mux(dev, clk_name,
			((const char *[]){
				parent1, parent2
			}), 2, CLK_SET_RATE_PARENT, pll_28nm->phy->pll_base +
			REG_DSI_28nm_PHY_PLL_VREG_CFG, 1, 1, 0, NULL);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	snprintf(clk_name, 32, "dsi%dpllbyte", pll_28nm->phy->id);
	snprintf(parent1, 32, "dsi%dbyte_mux", pll_28nm->phy->id);
	hw = devm_clk_hw_register_fixed_factor(dev, clk_name,
				parent1, CLK_SET_RATE_PARENT, 1, 4);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	provided_clocks[DSI_BYTE_PLL_CLK] = hw;

	return 0;
}

static int dsi_pll_28nm_init(struct msm_dsi_phy *phy)
{
	struct platform_device *pdev = phy->pdev;
	struct dsi_pll_28nm *pll_28nm;
	int ret;

	if (!pdev)
		return -ENODEV;

	pll_28nm = devm_kzalloc(&pdev->dev, sizeof(*pll_28nm), GFP_KERNEL);
	if (!pll_28nm)
		return -ENOMEM;

	pll_28nm->phy = phy;

	ret = pll_28nm_register(pll_28nm, phy->provided_clocks->hws);
	if (ret) {
		DRM_DEV_ERROR(&pdev->dev, "failed to register PLL: %d\n", ret);
		return ret;
	}

	phy->vco_hw = &pll_28nm->clk_hw;

	return 0;
}

static void dsi_28nm_dphy_set_timing(struct msm_dsi_phy *phy,
		struct msm_dsi_dphy_timing *timing)
{
	void __iomem *base = phy->base;

	dsi_phy_write(base + REG_DSI_28nm_PHY_TIMING_CTRL_0,
		DSI_28nm_PHY_TIMING_CTRL_0_CLK_ZERO(timing->clk_zero));
	dsi_phy_write(base + REG_DSI_28nm_PHY_TIMING_CTRL_1,
		DSI_28nm_PHY_TIMING_CTRL_1_CLK_TRAIL(timing->clk_trail));
	dsi_phy_write(base + REG_DSI_28nm_PHY_TIMING_CTRL_2,
		DSI_28nm_PHY_TIMING_CTRL_2_CLK_PREPARE(timing->clk_prepare));
	if (timing->clk_zero & BIT(8))
		dsi_phy_write(base + REG_DSI_28nm_PHY_TIMING_CTRL_3,
			DSI_28nm_PHY_TIMING_CTRL_3_CLK_ZERO_8);
	dsi_phy_write(base + REG_DSI_28nm_PHY_TIMING_CTRL_4,
		DSI_28nm_PHY_TIMING_CTRL_4_HS_EXIT(timing->hs_exit));
	dsi_phy_write(base + REG_DSI_28nm_PHY_TIMING_CTRL_5,
		DSI_28nm_PHY_TIMING_CTRL_5_HS_ZERO(timing->hs_zero));
	dsi_phy_write(base + REG_DSI_28nm_PHY_TIMING_CTRL_6,
		DSI_28nm_PHY_TIMING_CTRL_6_HS_PREPARE(timing->hs_prepare));
	dsi_phy_write(base + REG_DSI_28nm_PHY_TIMING_CTRL_7,
		DSI_28nm_PHY_TIMING_CTRL_7_HS_TRAIL(timing->hs_trail));
	dsi_phy_write(base + REG_DSI_28nm_PHY_TIMING_CTRL_8,
		DSI_28nm_PHY_TIMING_CTRL_8_HS_RQST(timing->hs_rqst));
	dsi_phy_write(base + REG_DSI_28nm_PHY_TIMING_CTRL_9,
		DSI_28nm_PHY_TIMING_CTRL_9_TA_GO(timing->ta_go) |
		DSI_28nm_PHY_TIMING_CTRL_9_TA_SURE(timing->ta_sure));
	dsi_phy_write(base + REG_DSI_28nm_PHY_TIMING_CTRL_10,
		DSI_28nm_PHY_TIMING_CTRL_10_TA_GET(timing->ta_get));
	dsi_phy_write(base + REG_DSI_28nm_PHY_TIMING_CTRL_11,
		DSI_28nm_PHY_TIMING_CTRL_11_TRIG3_CMD(0));
}

static void dsi_28nm_phy_regulator_enable_dcdc(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->reg_base;

	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_0, 0x0);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CAL_PWR_CFG, 1);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_5, 0);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_3, 0);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_2, 0x3);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_1, 0x9);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_0, 0x7);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_4, 0x20);
	dsi_phy_write(phy->base + REG_DSI_28nm_PHY_LDO_CNTRL, 0x00);
}

static void dsi_28nm_phy_regulator_enable_ldo(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->reg_base;

	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_0, 0x0);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CAL_PWR_CFG, 0);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_5, 0x7);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_3, 0);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_2, 0x1);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_1, 0x1);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_4, 0x20);

	if (phy->cfg->quirks & DSI_PHY_28NM_QUIRK_PHY_LP)
		dsi_phy_write(phy->base + REG_DSI_28nm_PHY_LDO_CNTRL, 0x05);
	else
		dsi_phy_write(phy->base + REG_DSI_28nm_PHY_LDO_CNTRL, 0x0d);
}

static void dsi_28nm_phy_regulator_ctrl(struct msm_dsi_phy *phy, bool enable)
{
	if (!enable) {
		dsi_phy_write(phy->reg_base +
			      REG_DSI_28nm_PHY_REGULATOR_CAL_PWR_CFG, 0);
		return;
	}

	if (phy->regulator_ldo_mode)
		dsi_28nm_phy_regulator_enable_ldo(phy);
	else
		dsi_28nm_phy_regulator_enable_dcdc(phy);
}

static int dsi_28nm_phy_enable(struct msm_dsi_phy *phy,
				struct msm_dsi_phy_clk_request *clk_req)
{
	struct msm_dsi_dphy_timing *timing = &phy->timing;
	int i;
	void __iomem *base = phy->base;
	u32 val;

	DBG("");

	if (msm_dsi_dphy_timing_calc(timing, clk_req)) {
		DRM_DEV_ERROR(&phy->pdev->dev,
			"%s: D-PHY timing calculation failed\n", __func__);
		return -EINVAL;
	}

	dsi_phy_write(base + REG_DSI_28nm_PHY_STRENGTH_0, 0xff);

	dsi_28nm_phy_regulator_ctrl(phy, true);

	dsi_28nm_dphy_set_timing(phy, timing);

	dsi_phy_write(base + REG_DSI_28nm_PHY_CTRL_1, 0x00);
	dsi_phy_write(base + REG_DSI_28nm_PHY_CTRL_0, 0x5f);

	dsi_phy_write(base + REG_DSI_28nm_PHY_STRENGTH_1, 0x6);

	for (i = 0; i < 4; i++) {
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_CFG_0(i), 0);
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_CFG_1(i), 0);
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_CFG_2(i), 0);
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_CFG_3(i), 0);
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_CFG_4(i), 0);
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_TEST_DATAPATH(i), 0);
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_DEBUG_SEL(i), 0);
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_TEST_STR_0(i), 0x1);
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_TEST_STR_1(i), 0x97);
	}

	dsi_phy_write(base + REG_DSI_28nm_PHY_LNCK_CFG_4, 0);
	dsi_phy_write(base + REG_DSI_28nm_PHY_LNCK_CFG_1, 0xc0);
	dsi_phy_write(base + REG_DSI_28nm_PHY_LNCK_TEST_STR0, 0x1);
	dsi_phy_write(base + REG_DSI_28nm_PHY_LNCK_TEST_STR1, 0xbb);

	dsi_phy_write(base + REG_DSI_28nm_PHY_CTRL_0, 0x5f);

	val = dsi_phy_read(base + REG_DSI_28nm_PHY_GLBL_TEST_CTRL);
	if (phy->id == DSI_1 && phy->usecase == MSM_DSI_PHY_SLAVE)
		val &= ~DSI_28nm_PHY_GLBL_TEST_CTRL_BITCLK_HS_SEL;
	else
		val |= DSI_28nm_PHY_GLBL_TEST_CTRL_BITCLK_HS_SEL;
	dsi_phy_write(base + REG_DSI_28nm_PHY_GLBL_TEST_CTRL, val);

	return 0;
}

static void dsi_28nm_phy_disable(struct msm_dsi_phy *phy)
{
	dsi_phy_write(phy->base + REG_DSI_28nm_PHY_CTRL_0, 0);
	dsi_28nm_phy_regulator_ctrl(phy, false);

	/*
	 * Wait for the registers writes to complete in order to
	 * ensure that the phy is completely disabled
	 */
	wmb();
}

const struct msm_dsi_phy_cfg dsi_phy_28nm_hpm_cfgs = {
	.has_phy_regulator = true,
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vddio", 100000, 100},
		},
	},
	.ops = {
		.enable = dsi_28nm_phy_enable,
		.disable = dsi_28nm_phy_disable,
		.pll_init = dsi_pll_28nm_init,
		.save_pll_state = dsi_28nm_pll_save_state,
		.restore_pll_state = dsi_28nm_pll_restore_state,
	},
	.min_pll_rate = VCO_MIN_RATE,
	.max_pll_rate = VCO_MAX_RATE,
	.io_start = { 0xfd922b00, 0xfd923100 },
	.num_dsi_phy = 2,
};

const struct msm_dsi_phy_cfg dsi_phy_28nm_hpm_famb_cfgs = {
	.has_phy_regulator = true,
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vddio", 100000, 100},
		},
	},
	.ops = {
		.enable = dsi_28nm_phy_enable,
		.disable = dsi_28nm_phy_disable,
		.pll_init = dsi_pll_28nm_init,
		.save_pll_state = dsi_28nm_pll_save_state,
		.restore_pll_state = dsi_28nm_pll_restore_state,
	},
	.min_pll_rate = VCO_MIN_RATE,
	.max_pll_rate = VCO_MAX_RATE,
	.io_start = { 0x1a94400, 0x1a96400 },
	.num_dsi_phy = 2,
};

const struct msm_dsi_phy_cfg dsi_phy_28nm_lp_cfgs = {
	.has_phy_regulator = true,
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vddio", 100000, 100},	/* 1.8 V */
		},
	},
	.ops = {
		.enable = dsi_28nm_phy_enable,
		.disable = dsi_28nm_phy_disable,
		.pll_init = dsi_pll_28nm_init,
		.save_pll_state = dsi_28nm_pll_save_state,
		.restore_pll_state = dsi_28nm_pll_restore_state,
	},
	.min_pll_rate = VCO_MIN_RATE,
	.max_pll_rate = VCO_MAX_RATE,
	.io_start = { 0x1a98500 },
	.num_dsi_phy = 1,
	.quirks = DSI_PHY_28NM_QUIRK_PHY_LP,
};


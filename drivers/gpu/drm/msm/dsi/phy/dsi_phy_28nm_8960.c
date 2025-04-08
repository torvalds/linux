// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 */

#include <dt-bindings/clock/qcom,dsi-phy-28nm.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>

#include "dsi_phy.h"
#include "dsi.xml.h"
#include "dsi_phy_28nm_8960.xml.h"

/*
 * DSI PLL 28nm (8960/A family) - clock diagram (eg: DSI1):
 *
 *
 *                        +------+
 *  dsi1vco_clk ----o-----| DIV1 |---dsi1pllbit (not exposed as clock)
 *  F * byte_clk    |     +------+
 *                  | bit clock divider (F / 8)
 *                  |
 *                  |     +------+
 *                  o-----| DIV2 |---dsi0pllbyte---o---> To byte RCG
 *                  |     +------+                 | (sets parent rate)
 *                  | byte clock divider (F)       |
 *                  |                              |
 *                  |                              o---> To esc RCG
 *                  |                                (doesn't set parent rate)
 *                  |
 *                  |     +------+
 *                  o-----| DIV3 |----dsi0pll------o---> To dsi RCG
 *                        +------+                 | (sets parent rate)
 *                  dsi clock divider (F * magic)  |
 *                                                 |
 *                                                 o---> To pixel rcg
 *                                                  (doesn't set parent rate)
 */

#define POLL_MAX_READS		8000
#define POLL_TIMEOUT_US		1

#define VCO_REF_CLK_RATE	27000000
#define VCO_MIN_RATE		600000000
#define VCO_MAX_RATE		1200000000

#define VCO_PREF_DIV_RATIO	27

struct pll_28nm_cached_state {
	unsigned long vco_rate;
	u8 postdiv3;
	u8 postdiv2;
	u8 postdiv1;
};

struct clk_bytediv {
	struct clk_hw hw;
	void __iomem *reg;
};

struct dsi_pll_28nm {
	struct clk_hw clk_hw;

	struct msm_dsi_phy *phy;

	struct pll_28nm_cached_state cached_state;
};

#define to_pll_28nm(x)	container_of(x, struct dsi_pll_28nm, clk_hw)

static bool pll_28nm_poll_for_ready(struct dsi_pll_28nm *pll_28nm,
				    int nb_tries, int timeout_us)
{
	bool pll_locked = false;
	u32 val;

	while (nb_tries--) {
		val = readl(pll_28nm->phy->pll_base + REG_DSI_28nm_8960_PHY_PLL_RDY);
		pll_locked = !!(val & DSI_28nm_8960_PHY_PLL_RDY_PLL_RDY);

		if (pll_locked)
			break;

		udelay(timeout_us);
	}
	DBG("DSI PLL is %slocked", pll_locked ? "" : "*not* ");

	return pll_locked;
}

/*
 * Clock Callbacks
 */
static int dsi_pll_28nm_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(hw);
	void __iomem *base = pll_28nm->phy->pll_base;
	u32 val, temp, fb_divider;

	DBG("rate=%lu, parent's=%lu", rate, parent_rate);

	temp = rate / 10;
	val = VCO_REF_CLK_RATE / 10;
	fb_divider = (temp * VCO_PREF_DIV_RATIO) / val;
	fb_divider = fb_divider / 2 - 1;
	writel(fb_divider & 0xff, base + REG_DSI_28nm_8960_PHY_PLL_CTRL_1);

	val = readl(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_2);

	val |= (fb_divider >> 8) & 0x07;

	writel(val, base + REG_DSI_28nm_8960_PHY_PLL_CTRL_2);

	val = readl(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_3);

	val |= (VCO_PREF_DIV_RATIO - 1) & 0x3f;

	writel(val, base + REG_DSI_28nm_8960_PHY_PLL_CTRL_3);

	writel(0xf, base + REG_DSI_28nm_8960_PHY_PLL_CTRL_6);

	val = readl(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_8);
	val |= 0x7 << 4;
	writel(val, base + REG_DSI_28nm_8960_PHY_PLL_CTRL_8);

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
	unsigned long vco_rate;
	u32 status, fb_divider, temp, ref_divider;

	VERB("parent_rate=%lu", parent_rate);

	status = readl(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_0);

	if (status & DSI_28nm_8960_PHY_PLL_CTRL_0_ENABLE) {
		fb_divider = readl(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_1);
		fb_divider &= 0xff;
		temp = readl(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_2) & 0x07;
		fb_divider = (temp << 8) | fb_divider;
		fb_divider += 1;

		ref_divider = readl(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_3);
		ref_divider &= 0x3f;
		ref_divider += 1;

		/* multiply by 2 */
		vco_rate = (parent_rate / ref_divider) * fb_divider * 2;
	} else {
		vco_rate = 0;
	}

	DBG("returning vco rate = %lu", vco_rate);

	return vco_rate;
}

static int dsi_pll_28nm_vco_prepare(struct clk_hw *hw)
{
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(hw);
	struct device *dev = &pll_28nm->phy->pdev->dev;
	void __iomem *base = pll_28nm->phy->pll_base;
	bool locked;
	unsigned int bit_div, byte_div;
	int max_reads = 1000, timeout_us = 100;
	u32 val;

	DBG("id=%d", pll_28nm->phy->id);

	if (unlikely(pll_28nm->phy->pll_on))
		return 0;

	/*
	 * before enabling the PLL, configure the bit clock divider since we
	 * don't expose it as a clock to the outside world
	 * 1: read back the byte clock divider that should already be set
	 * 2: divide by 8 to get bit clock divider
	 * 3: write it to POSTDIV1
	 */
	val = readl(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_9);
	byte_div = val + 1;
	bit_div = byte_div / 8;

	val = readl(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_8);
	val &= ~0xf;
	val |= (bit_div - 1);
	writel(val, base + REG_DSI_28nm_8960_PHY_PLL_CTRL_8);

	/* enable the PLL */
	writel(DSI_28nm_8960_PHY_PLL_CTRL_0_ENABLE,
	       base + REG_DSI_28nm_8960_PHY_PLL_CTRL_0);

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

	writel(0x00, pll_28nm->phy->pll_base + REG_DSI_28nm_8960_PHY_PLL_CTRL_0);

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

static const struct clk_ops clk_ops_dsi_pll_28nm_vco = {
	.round_rate = dsi_pll_28nm_clk_round_rate,
	.set_rate = dsi_pll_28nm_clk_set_rate,
	.recalc_rate = dsi_pll_28nm_clk_recalc_rate,
	.prepare = dsi_pll_28nm_vco_prepare,
	.unprepare = dsi_pll_28nm_vco_unprepare,
	.is_enabled = dsi_pll_28nm_clk_is_enabled,
};

/*
 * Custom byte clock divier clk_ops
 *
 * This clock is the entry point to configuring the PLL. The user (dsi host)
 * will set this clock's rate to the desired byte clock rate. The VCO lock
 * frequency is a multiple of the byte clock rate. The multiplication factor
 * (shown as F in the diagram above) is a function of the byte clock rate.
 *
 * This custom divider clock ensures that its parent (VCO) is set to the
 * desired rate, and that the byte clock postdivider (POSTDIV2) is configured
 * accordingly
 */
#define to_clk_bytediv(_hw) container_of(_hw, struct clk_bytediv, hw)

static unsigned long clk_bytediv_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_bytediv *bytediv = to_clk_bytediv(hw);
	unsigned int div;

	div = readl(bytediv->reg) & 0xff;

	return parent_rate / (div + 1);
}

/* find multiplication factor(wrt byte clock) at which the VCO should be set */
static unsigned int get_vco_mul_factor(unsigned long byte_clk_rate)
{
	unsigned long bit_mhz;

	/* convert to bit clock in Mhz */
	bit_mhz = (byte_clk_rate * 8) / 1000000;

	if (bit_mhz < 125)
		return 64;
	else if (bit_mhz < 250)
		return 32;
	else if (bit_mhz < 600)
		return 16;
	else
		return 8;
}

static long clk_bytediv_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *prate)
{
	unsigned long best_parent;
	unsigned int factor;

	factor = get_vco_mul_factor(rate);

	best_parent = rate * factor;
	*prate = clk_hw_round_rate(clk_hw_get_parent(hw), best_parent);

	return *prate / factor;
}

static int clk_bytediv_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_bytediv *bytediv = to_clk_bytediv(hw);
	u32 val;
	unsigned int factor;

	factor = get_vco_mul_factor(rate);

	val = readl(bytediv->reg);
	val |= (factor - 1) & 0xff;
	writel(val, bytediv->reg);

	return 0;
}

/* Our special byte clock divider ops */
static const struct clk_ops clk_bytediv_ops = {
	.round_rate = clk_bytediv_round_rate,
	.set_rate = clk_bytediv_set_rate,
	.recalc_rate = clk_bytediv_recalc_rate,
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
			readl(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_10);
	cached_state->postdiv2 =
			readl(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_9);
	cached_state->postdiv1 =
			readl(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_8);

	cached_state->vco_rate = clk_hw_get_rate(phy->vco_hw);
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

	writel(cached_state->postdiv3, base + REG_DSI_28nm_8960_PHY_PLL_CTRL_10);
	writel(cached_state->postdiv2, base + REG_DSI_28nm_8960_PHY_PLL_CTRL_9);
	writel(cached_state->postdiv1, base + REG_DSI_28nm_8960_PHY_PLL_CTRL_8);

	return 0;
}

static int pll_28nm_register(struct dsi_pll_28nm *pll_28nm, struct clk_hw **provided_clocks)
{
	char clk_name[32];
	struct clk_init_data vco_init = {
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "ref",
		},
		.num_parents = 1,
		.flags = CLK_IGNORE_UNUSED,
		.ops = &clk_ops_dsi_pll_28nm_vco,
	};
	struct device *dev = &pll_28nm->phy->pdev->dev;
	struct clk_hw *hw;
	struct clk_bytediv *bytediv;
	struct clk_init_data bytediv_init = { };
	int ret;

	DBG("%d", pll_28nm->phy->id);

	bytediv = devm_kzalloc(dev, sizeof(*bytediv), GFP_KERNEL);
	if (!bytediv)
		return -ENOMEM;

	snprintf(clk_name, sizeof(clk_name), "dsi%dvco_clk", pll_28nm->phy->id);
	vco_init.name = clk_name;

	pll_28nm->clk_hw.init = &vco_init;

	ret = devm_clk_hw_register(dev, &pll_28nm->clk_hw);
	if (ret)
		return ret;

	/* prepare and register bytediv */
	bytediv->hw.init = &bytediv_init;
	bytediv->reg = pll_28nm->phy->pll_base + REG_DSI_28nm_8960_PHY_PLL_CTRL_9;

	snprintf(clk_name, sizeof(clk_name), "dsi%dpllbyte", pll_28nm->phy->id + 1);

	bytediv_init.name = clk_name;
	bytediv_init.ops = &clk_bytediv_ops;
	bytediv_init.flags = CLK_SET_RATE_PARENT;
	bytediv_init.parent_hws = (const struct clk_hw*[]){
		&pll_28nm->clk_hw,
	};
	bytediv_init.num_parents = 1;

	/* DIV2 */
	ret = devm_clk_hw_register(dev, &bytediv->hw);
	if (ret)
		return ret;
	provided_clocks[DSI_BYTE_PLL_CLK] = &bytediv->hw;

	snprintf(clk_name, sizeof(clk_name), "dsi%dpll", pll_28nm->phy->id + 1);
	/* DIV3 */
	hw = devm_clk_hw_register_divider_parent_hw(dev, clk_name,
			&pll_28nm->clk_hw, 0, pll_28nm->phy->pll_base +
				REG_DSI_28nm_8960_PHY_PLL_CTRL_10,
			0, 8, 0, NULL);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	provided_clocks[DSI_PIXEL_PLL_CLK] = hw;

	return 0;
}

static int dsi_pll_28nm_8960_init(struct msm_dsi_phy *phy)
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

	writel(DSI_28nm_8960_PHY_TIMING_CTRL_0_CLK_ZERO(timing->clk_zero),
	       base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_0);
	writel(DSI_28nm_8960_PHY_TIMING_CTRL_1_CLK_TRAIL(timing->clk_trail),
	       base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_1);
	writel(DSI_28nm_8960_PHY_TIMING_CTRL_2_CLK_PREPARE(timing->clk_prepare),
	       base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_2);
	writel(0, base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_3);
	writel(DSI_28nm_8960_PHY_TIMING_CTRL_4_HS_EXIT(timing->hs_exit),
	       base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_4);
	writel(DSI_28nm_8960_PHY_TIMING_CTRL_5_HS_ZERO(timing->hs_zero),
	       base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_5);
	writel(DSI_28nm_8960_PHY_TIMING_CTRL_6_HS_PREPARE(timing->hs_prepare),
	       base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_6);
	writel(DSI_28nm_8960_PHY_TIMING_CTRL_7_HS_TRAIL(timing->hs_trail),
	       base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_7);
	writel(DSI_28nm_8960_PHY_TIMING_CTRL_8_HS_RQST(timing->hs_rqst),
	       base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_8);
	writel(DSI_28nm_8960_PHY_TIMING_CTRL_9_TA_GO(timing->ta_go) |
	       DSI_28nm_8960_PHY_TIMING_CTRL_9_TA_SURE(timing->ta_sure),
	       base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_9);
	writel(DSI_28nm_8960_PHY_TIMING_CTRL_10_TA_GET(timing->ta_get),
	       base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_10);
	writel(DSI_28nm_8960_PHY_TIMING_CTRL_11_TRIG3_CMD(0),
	       base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_11);
}

static void dsi_28nm_phy_regulator_init(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->reg_base;

	writel(0x3, base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_0);
	writel(1, base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_1);
	writel(1, base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_2);
	writel(0, base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_3);
	writel(0x100, base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_4);
}

static void dsi_28nm_phy_regulator_ctrl(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->reg_base;

	writel(0x3, base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_0);
	writel(0xa, base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_1);
	writel(0x4, base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_2);
	writel(0x0, base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_3);
	writel(0x20, base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_4);
}

static void dsi_28nm_phy_calibration(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->reg_base;
	u32 status;
	int i = 5000;

	writel(0x3, base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CAL_PWR_CFG);

	writel(0x0, base + REG_DSI_28nm_8960_PHY_MISC_CAL_SW_CFG_2);
	writel(0x5a, base + REG_DSI_28nm_8960_PHY_MISC_CAL_HW_CFG_1);
	writel(0x10, base + REG_DSI_28nm_8960_PHY_MISC_CAL_HW_CFG_3);
	writel(0x1, base + REG_DSI_28nm_8960_PHY_MISC_CAL_HW_CFG_4);
	writel(0x1, base + REG_DSI_28nm_8960_PHY_MISC_CAL_HW_CFG_0);

	writel(0x1, base + REG_DSI_28nm_8960_PHY_MISC_CAL_HW_TRIGGER);
	usleep_range(5000, 6000);
	writel(0x0, base + REG_DSI_28nm_8960_PHY_MISC_CAL_HW_TRIGGER);

	do {
		status = readl(base +
				REG_DSI_28nm_8960_PHY_MISC_CAL_STATUS);

		if (!(status & DSI_28nm_8960_PHY_MISC_CAL_STATUS_CAL_BUSY))
			break;

		udelay(1);
	} while (--i > 0);
}

static void dsi_28nm_phy_lane_config(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->base;
	int i;

	for (i = 0; i < 4; i++) {
		writel(0x80, base + REG_DSI_28nm_8960_PHY_LN_CFG_0(i));
		writel(0x45, base + REG_DSI_28nm_8960_PHY_LN_CFG_1(i));
		writel(0x00, base + REG_DSI_28nm_8960_PHY_LN_CFG_2(i));
		writel(0x00, base + REG_DSI_28nm_8960_PHY_LN_TEST_DATAPATH(i));
		writel(0x01, base + REG_DSI_28nm_8960_PHY_LN_TEST_STR_0(i));
		writel(0x66, base + REG_DSI_28nm_8960_PHY_LN_TEST_STR_1(i));
	}

	writel(0x40, base + REG_DSI_28nm_8960_PHY_LNCK_CFG_0);
	writel(0x67, base + REG_DSI_28nm_8960_PHY_LNCK_CFG_1);
	writel(0x0, base + REG_DSI_28nm_8960_PHY_LNCK_CFG_2);
	writel(0x0, base + REG_DSI_28nm_8960_PHY_LNCK_TEST_DATAPATH);
	writel(0x1, base + REG_DSI_28nm_8960_PHY_LNCK_TEST_STR0);
	writel(0x88, base + REG_DSI_28nm_8960_PHY_LNCK_TEST_STR1);
}

static int dsi_28nm_phy_enable(struct msm_dsi_phy *phy,
				struct msm_dsi_phy_clk_request *clk_req)
{
	struct msm_dsi_dphy_timing *timing = &phy->timing;
	void __iomem *base = phy->base;

	DBG("");

	if (msm_dsi_dphy_timing_calc(timing, clk_req)) {
		DRM_DEV_ERROR(&phy->pdev->dev,
			      "%s: D-PHY timing calculation failed\n",
			      __func__);
		return -EINVAL;
	}

	dsi_28nm_phy_regulator_init(phy);

	writel(0x04, base + REG_DSI_28nm_8960_PHY_LDO_CTRL);

	/* strength control */
	writel(0xff, base + REG_DSI_28nm_8960_PHY_STRENGTH_0);
	writel(0x00, base + REG_DSI_28nm_8960_PHY_STRENGTH_1);
	writel(0x06, base + REG_DSI_28nm_8960_PHY_STRENGTH_2);

	/* phy ctrl */
	writel(0x5f, base + REG_DSI_28nm_8960_PHY_CTRL_0);
	writel(0x00, base + REG_DSI_28nm_8960_PHY_CTRL_1);
	writel(0x00, base + REG_DSI_28nm_8960_PHY_CTRL_2);
	writel(0x10, base + REG_DSI_28nm_8960_PHY_CTRL_3);

	dsi_28nm_phy_regulator_ctrl(phy);

	dsi_28nm_phy_calibration(phy);

	dsi_28nm_phy_lane_config(phy);

	writel(0x0f, base + REG_DSI_28nm_8960_PHY_BIST_CTRL_4);
	writel(0x03, base + REG_DSI_28nm_8960_PHY_BIST_CTRL_1);
	writel(0x03, base + REG_DSI_28nm_8960_PHY_BIST_CTRL_0);
	writel(0x0, base + REG_DSI_28nm_8960_PHY_BIST_CTRL_4);

	dsi_28nm_dphy_set_timing(phy, timing);

	return 0;
}

static void dsi_28nm_phy_disable(struct msm_dsi_phy *phy)
{
	writel(0x0, phy->base + REG_DSI_28nm_8960_PHY_CTRL_0);

	/*
	 * Wait for the registers writes to complete in order to
	 * ensure that the phy is completely disabled
	 */
	wmb();
}

static const struct regulator_bulk_data dsi_phy_28nm_8960_regulators[] = {
	{ .supply = "vddio", .init_load_uA = 100000 },	/* 1.8 V */
};

const struct msm_dsi_phy_cfg dsi_phy_28nm_8960_cfgs = {
	.has_phy_regulator = true,
	.regulator_data = dsi_phy_28nm_8960_regulators,
	.num_regulators = ARRAY_SIZE(dsi_phy_28nm_8960_regulators),
	.ops = {
		.enable = dsi_28nm_phy_enable,
		.disable = dsi_28nm_phy_disable,
		.pll_init = dsi_pll_28nm_8960_init,
		.save_pll_state = dsi_28nm_pll_save_state,
		.restore_pll_state = dsi_28nm_pll_restore_state,
	},
	.min_pll_rate = VCO_MIN_RATE,
	.max_pll_rate = VCO_MAX_RATE,
	.io_start = { 0x4700300, 0x5800300 },
	.num_dsi_phy = 2,
};

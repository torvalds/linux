/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 */

#ifndef __DP_PLL_10NM_H
#define __DP_PLL_10NM_H

#include "dp_pll.h"
#include "dp_reg.h"

#define DP_VCO_HSCLK_RATE_1620MHZDIV1000	1620000UL
#define DP_VCO_HSCLK_RATE_2700MHZDIV1000	2700000UL
#define DP_VCO_HSCLK_RATE_5400MHZDIV1000	5400000UL
#define DP_VCO_HSCLK_RATE_8100MHZDIV1000	8100000UL

#define NUM_DP_CLOCKS_MAX			6

#define DP_PHY_PLL_POLL_SLEEP_US		500
#define DP_PHY_PLL_POLL_TIMEOUT_US		10000

#define DP_VCO_RATE_8100MHZDIV1000		8100000UL
#define DP_VCO_RATE_9720MHZDIV1000		9720000UL
#define DP_VCO_RATE_10800MHZDIV1000		10800000UL

struct dp_pll_vco_clk {
	struct clk_hw hw;
	unsigned long	rate;		/* current vco rate */
	u64		min_rate;	/* min vco rate */
	u64		max_rate;	/* max vco rate */
	void		*priv;
};

struct dp_pll_db {
	struct msm_dp_pll *base;

	int id;
	struct platform_device *pdev;

	/* private clocks: */
	bool fixed_factor_clk[NUM_DP_CLOCKS_MAX];
	struct clk_hw *hws[NUM_DP_CLOCKS_MAX];
	u32 num_hws;

	/* lane and orientation settings */
	u8 lane_cnt;
	u8 orientation;

	/* COM PHY settings */
	u32 hsclk_sel;
	u32 dec_start_mode0;
	u32 div_frac_start1_mode0;
	u32 div_frac_start2_mode0;
	u32 div_frac_start3_mode0;
	u32 integloop_gain0_mode0;
	u32 integloop_gain1_mode0;
	u32 vco_tune_map;
	u32 lock_cmp1_mode0;
	u32 lock_cmp2_mode0;
	u32 lock_cmp3_mode0;
	u32 lock_cmp_en;

	/* PHY vco divider */
	u32 phy_vco_div;
	/*
	 * Certain pll's needs to update the same vco rate after resume in
	 * suspend/resume scenario. Cached the vco rate for such plls.
	 */
	unsigned long	vco_cached_rate;
	u32		cached_cfg0;
	u32		cached_cfg1;
	u32		cached_outdiv;

	uint32_t index;
};

static inline struct dp_pll_vco_clk *to_dp_vco_hw(struct clk_hw *hw)
{
	return container_of(hw, struct dp_pll_vco_clk, hw);
}

#define to_msm_dp_pll(vco) ((struct msm_dp_pll *)vco->priv)

#define to_dp_pll_db(x)	((struct dp_pll_db *)x->priv)

int msm_dp_pll_10nm_init(struct msm_dp_pll *dp_pll, int id);
void msm_dp_pll_10nm_deinit(struct msm_dp_pll *dp_pll);

#endif /* __DP_PLL_10NM_H */

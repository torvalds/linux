/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#ifndef __DSI_PHY_H__
#define __DSI_PHY_H__

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>

#include "dsi.h"

#define dsi_phy_read(offset) msm_readl((offset))
#define dsi_phy_write(offset, data) msm_writel((data), (offset))
#define dsi_phy_write_udelay(offset, data, delay_us) { msm_writel((data), (offset)); udelay(delay_us); }
#define dsi_phy_write_ndelay(offset, data, delay_ns) { msm_writel((data), (offset)); ndelay(delay_ns); }

struct msm_dsi_phy_ops {
	int (*pll_init)(struct msm_dsi_phy *phy);
	int (*enable)(struct msm_dsi_phy *phy,
			struct msm_dsi_phy_clk_request *clk_req);
	void (*disable)(struct msm_dsi_phy *phy);
	void (*save_pll_state)(struct msm_dsi_phy *phy);
	int (*restore_pll_state)(struct msm_dsi_phy *phy);
	bool (*set_continuous_clock)(struct msm_dsi_phy *phy, bool enable);
};

struct msm_dsi_phy_cfg {
	struct dsi_reg_config reg_cfg;
	struct msm_dsi_phy_ops ops;

	unsigned long	min_pll_rate;
	unsigned long	max_pll_rate;

	const resource_size_t io_start[DSI_MAX];
	const int num_dsi_phy;
	const int quirks;
	bool has_phy_regulator;
	bool has_phy_lane;
};

extern const struct msm_dsi_phy_cfg dsi_phy_28nm_hpm_cfgs;
extern const struct msm_dsi_phy_cfg dsi_phy_28nm_hpm_famb_cfgs;
extern const struct msm_dsi_phy_cfg dsi_phy_28nm_lp_cfgs;
extern const struct msm_dsi_phy_cfg dsi_phy_20nm_cfgs;
extern const struct msm_dsi_phy_cfg dsi_phy_28nm_8960_cfgs;
extern const struct msm_dsi_phy_cfg dsi_phy_14nm_cfgs;
extern const struct msm_dsi_phy_cfg dsi_phy_14nm_660_cfgs;
extern const struct msm_dsi_phy_cfg dsi_phy_14nm_8953_cfgs;
extern const struct msm_dsi_phy_cfg dsi_phy_10nm_cfgs;
extern const struct msm_dsi_phy_cfg dsi_phy_10nm_8998_cfgs;
extern const struct msm_dsi_phy_cfg dsi_phy_7nm_cfgs;
extern const struct msm_dsi_phy_cfg dsi_phy_7nm_8150_cfgs;
extern const struct msm_dsi_phy_cfg dsi_phy_7nm_7280_cfgs;

struct msm_dsi_dphy_timing {
	u32 clk_zero;
	u32 clk_trail;
	u32 clk_prepare;
	u32 hs_exit;
	u32 hs_zero;
	u32 hs_prepare;
	u32 hs_trail;
	u32 hs_rqst;
	u32 ta_go;
	u32 ta_sure;
	u32 ta_get;

	struct msm_dsi_phy_shared_timings shared_timings;

	/* For PHY v2 only */
	u32 hs_rqst_ckln;
	u32 hs_prep_dly;
	u32 hs_prep_dly_ckln;
	u8 hs_halfbyte_en;
	u8 hs_halfbyte_en_ckln;
};

#define DSI_BYTE_PLL_CLK		0
#define DSI_PIXEL_PLL_CLK		1
#define NUM_PROVIDED_CLKS		2

struct msm_dsi_phy {
	struct platform_device *pdev;
	void __iomem *base;
	void __iomem *pll_base;
	void __iomem *reg_base;
	void __iomem *lane_base;
	phys_addr_t base_size;
	phys_addr_t pll_size;
	phys_addr_t reg_size;
	phys_addr_t lane_size;
	int id;

	struct clk *ahb_clk;
	struct regulator_bulk_data supplies[DSI_DEV_REGULATOR_MAX];

	struct msm_dsi_dphy_timing timing;
	const struct msm_dsi_phy_cfg *cfg;

	enum msm_dsi_phy_usecase usecase;
	bool regulator_ldo_mode;
	bool cphy_mode;

	struct clk_hw *vco_hw;
	bool pll_on;

	struct clk_hw_onecell_data *provided_clocks;

	bool state_saved;
};

/*
 * PHY internal functions
 */
int msm_dsi_dphy_timing_calc(struct msm_dsi_dphy_timing *timing,
			     struct msm_dsi_phy_clk_request *clk_req);
int msm_dsi_dphy_timing_calc_v2(struct msm_dsi_dphy_timing *timing,
				struct msm_dsi_phy_clk_request *clk_req);
int msm_dsi_dphy_timing_calc_v3(struct msm_dsi_dphy_timing *timing,
				struct msm_dsi_phy_clk_request *clk_req);
int msm_dsi_dphy_timing_calc_v4(struct msm_dsi_dphy_timing *timing,
				struct msm_dsi_phy_clk_request *clk_req);
int msm_dsi_cphy_timing_calc_v4(struct msm_dsi_dphy_timing *timing,
				struct msm_dsi_phy_clk_request *clk_req);

#endif /* __DSI_PHY_H__ */

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include "dsi_phy.h"
#include "dsi.xml.h"

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

	if (phy->cfg->type == MSM_DSI_PHY_28NM_LP)
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

static int dsi_28nm_phy_enable(struct msm_dsi_phy *phy, int src_pll_id,
				struct msm_dsi_phy_clk_request *clk_req)
{
	struct msm_dsi_dphy_timing *timing = &phy->timing;
	int i;
	void __iomem *base = phy->base;

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

	msm_dsi_phy_set_src_pll(phy, src_pll_id,
				REG_DSI_28nm_PHY_GLBL_TEST_CTRL,
				DSI_28nm_PHY_GLBL_TEST_CTRL_BITCLK_HS_SEL);

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
	.type = MSM_DSI_PHY_28NM_HPM,
	.src_pll_truthtable = { {true, true}, {false, true} },
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vddio", 100000, 100},
		},
	},
	.ops = {
		.enable = dsi_28nm_phy_enable,
		.disable = dsi_28nm_phy_disable,
		.init = msm_dsi_phy_init_common,
	},
	.io_start = { 0xfd922b00, 0xfd923100 },
	.num_dsi_phy = 2,
};

const struct msm_dsi_phy_cfg dsi_phy_28nm_lp_cfgs = {
	.type = MSM_DSI_PHY_28NM_LP,
	.src_pll_truthtable = { {true, true}, {true, true} },
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vddio", 100000, 100},	/* 1.8 V */
		},
	},
	.ops = {
		.enable = dsi_28nm_phy_enable,
		.disable = dsi_28nm_phy_disable,
		.init = msm_dsi_phy_init_common,
	},
	.io_start = { 0x1a98500 },
	.num_dsi_phy = 1,
};


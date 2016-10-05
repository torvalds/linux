/*
 * Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
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

#include "dsi_phy.h"
#include "dsi.xml.h"

static void dsi_28nm_dphy_set_timing(struct msm_dsi_phy *phy,
		struct msm_dsi_dphy_timing *timing)
{
	void __iomem *base = phy->base;

	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_0,
		DSI_28nm_8960_PHY_TIMING_CTRL_0_CLK_ZERO(timing->clk_zero));
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_1,
		DSI_28nm_8960_PHY_TIMING_CTRL_1_CLK_TRAIL(timing->clk_trail));
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_2,
		DSI_28nm_8960_PHY_TIMING_CTRL_2_CLK_PREPARE(timing->clk_prepare));
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_3, 0x0);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_4,
		DSI_28nm_8960_PHY_TIMING_CTRL_4_HS_EXIT(timing->hs_exit));
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_5,
		DSI_28nm_8960_PHY_TIMING_CTRL_5_HS_ZERO(timing->hs_zero));
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_6,
		DSI_28nm_8960_PHY_TIMING_CTRL_6_HS_PREPARE(timing->hs_prepare));
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_7,
		DSI_28nm_8960_PHY_TIMING_CTRL_7_HS_TRAIL(timing->hs_trail));
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_8,
		DSI_28nm_8960_PHY_TIMING_CTRL_8_HS_RQST(timing->hs_rqst));
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_9,
		DSI_28nm_8960_PHY_TIMING_CTRL_9_TA_GO(timing->ta_go) |
		DSI_28nm_8960_PHY_TIMING_CTRL_9_TA_SURE(timing->ta_sure));
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_10,
		DSI_28nm_8960_PHY_TIMING_CTRL_10_TA_GET(timing->ta_get));
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_TIMING_CTRL_11,
		DSI_28nm_8960_PHY_TIMING_CTRL_11_TRIG3_CMD(0));
}

static void dsi_28nm_phy_regulator_init(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->reg_base;

	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_0, 0x3);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_1, 1);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_2, 1);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_3, 0);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_4,
		0x100);
}

static void dsi_28nm_phy_regulator_ctrl(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->reg_base;

	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_0, 0x3);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_1, 0xa);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_2, 0x4);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_3, 0x0);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CTRL_4, 0x20);
}

static void dsi_28nm_phy_calibration(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->reg_base;
	u32 status;
	int i = 5000;

	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_REGULATOR_CAL_PWR_CFG,
			0x3);

	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_CAL_SW_CFG_2, 0x0);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_CAL_HW_CFG_1, 0x5a);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_CAL_HW_CFG_3, 0x10);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_CAL_HW_CFG_4, 0x1);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_CAL_HW_CFG_0, 0x1);

	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_CAL_HW_TRIGGER, 0x1);
	usleep_range(5000, 6000);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_MISC_CAL_HW_TRIGGER, 0x0);

	do {
		status = dsi_phy_read(base +
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
		dsi_phy_write(base + REG_DSI_28nm_8960_PHY_LN_CFG_0(i), 0x80);
		dsi_phy_write(base + REG_DSI_28nm_8960_PHY_LN_CFG_1(i), 0x45);
		dsi_phy_write(base + REG_DSI_28nm_8960_PHY_LN_CFG_2(i), 0x00);
		dsi_phy_write(base + REG_DSI_28nm_8960_PHY_LN_TEST_DATAPATH(i),
			0x00);
		dsi_phy_write(base + REG_DSI_28nm_8960_PHY_LN_TEST_STR_0(i),
			0x01);
		dsi_phy_write(base + REG_DSI_28nm_8960_PHY_LN_TEST_STR_1(i),
			0x66);
	}

	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_LNCK_CFG_0, 0x40);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_LNCK_CFG_1, 0x67);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_LNCK_CFG_2, 0x0);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_LNCK_TEST_DATAPATH, 0x0);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_LNCK_TEST_STR0, 0x1);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_LNCK_TEST_STR1, 0x88);
}

static int dsi_28nm_phy_enable(struct msm_dsi_phy *phy, int src_pll_id,
		const unsigned long bit_rate, const unsigned long esc_rate)
{
	struct msm_dsi_dphy_timing *timing = &phy->timing;
	void __iomem *base = phy->base;

	DBG("");

	if (msm_dsi_dphy_timing_calc(timing, bit_rate, esc_rate)) {
		dev_err(&phy->pdev->dev,
			"%s: D-PHY timing calculation failed\n", __func__);
		return -EINVAL;
	}

	dsi_28nm_phy_regulator_init(phy);

	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_LDO_CTRL, 0x04);

	/* strength control */
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_STRENGTH_0, 0xff);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_STRENGTH_1, 0x00);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_STRENGTH_2, 0x06);

	/* phy ctrl */
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_CTRL_0, 0x5f);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_CTRL_1, 0x00);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_CTRL_2, 0x00);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_CTRL_3, 0x10);

	dsi_28nm_phy_regulator_ctrl(phy);

	dsi_28nm_phy_calibration(phy);

	dsi_28nm_phy_lane_config(phy);

	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_BIST_CTRL_4, 0x0f);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_BIST_CTRL_1, 0x03);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_BIST_CTRL_0, 0x03);
	dsi_phy_write(base + REG_DSI_28nm_8960_PHY_BIST_CTRL_4, 0x0);

	dsi_28nm_dphy_set_timing(phy, timing);

	return 0;
}

static void dsi_28nm_phy_disable(struct msm_dsi_phy *phy)
{
	dsi_phy_write(phy->base + REG_DSI_28nm_8960_PHY_CTRL_0, 0x0);

	/*
	 * Wait for the registers writes to complete in order to
	 * ensure that the phy is completely disabled
	 */
	wmb();
}

const struct msm_dsi_phy_cfg dsi_phy_28nm_8960_cfgs = {
	.type = MSM_DSI_PHY_28NM_8960,
	.src_pll_truthtable = { {true, true}, {false, true} },
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vddio", 100000, 100},	/* 1.8 V */
		},
	},
	.ops = {
		.enable = dsi_28nm_phy_enable,
		.disable = dsi_28nm_phy_disable,
	},
	.io_start = { 0x4700300, 0x5800300 },
	.num_dsi_phy = 2,
};

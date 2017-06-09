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

#include "dsi_phy.h"
#include "dsi.xml.h"

#define PHY_14NM_CKLN_IDX	4

static void dsi_14nm_dphy_set_timing(struct msm_dsi_phy *phy,
				     struct msm_dsi_dphy_timing *timing,
				     int lane_idx)
{
	void __iomem *base = phy->lane_base;
	bool clk_ln = (lane_idx == PHY_14NM_CKLN_IDX);
	u32 zero = clk_ln ? timing->clk_zero : timing->hs_zero;
	u32 prepare = clk_ln ? timing->clk_prepare : timing->hs_prepare;
	u32 trail = clk_ln ? timing->clk_trail : timing->hs_trail;
	u32 rqst = clk_ln ? timing->hs_rqst_ckln : timing->hs_rqst;
	u32 prep_dly = clk_ln ? timing->hs_prep_dly_ckln : timing->hs_prep_dly;
	u32 halfbyte_en = clk_ln ? timing->hs_halfbyte_en_ckln :
				   timing->hs_halfbyte_en;

	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_TIMING_CTRL_4(lane_idx),
		      DSI_14nm_PHY_LN_TIMING_CTRL_4_HS_EXIT(timing->hs_exit));
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_TIMING_CTRL_5(lane_idx),
		      DSI_14nm_PHY_LN_TIMING_CTRL_5_HS_ZERO(zero));
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_TIMING_CTRL_6(lane_idx),
		      DSI_14nm_PHY_LN_TIMING_CTRL_6_HS_PREPARE(prepare));
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_TIMING_CTRL_7(lane_idx),
		      DSI_14nm_PHY_LN_TIMING_CTRL_7_HS_TRAIL(trail));
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_TIMING_CTRL_8(lane_idx),
		      DSI_14nm_PHY_LN_TIMING_CTRL_8_HS_RQST(rqst));
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_CFG0(lane_idx),
		      DSI_14nm_PHY_LN_CFG0_PREPARE_DLY(prep_dly));
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_CFG1(lane_idx),
		      halfbyte_en ? DSI_14nm_PHY_LN_CFG1_HALFBYTECLK_EN : 0);
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_TIMING_CTRL_9(lane_idx),
		      DSI_14nm_PHY_LN_TIMING_CTRL_9_TA_GO(timing->ta_go) |
		      DSI_14nm_PHY_LN_TIMING_CTRL_9_TA_SURE(timing->ta_sure));
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_TIMING_CTRL_10(lane_idx),
		      DSI_14nm_PHY_LN_TIMING_CTRL_10_TA_GET(timing->ta_get));
	dsi_phy_write(base + REG_DSI_14nm_PHY_LN_TIMING_CTRL_11(lane_idx),
		      DSI_14nm_PHY_LN_TIMING_CTRL_11_TRIG3_CMD(0xa0));
}

static int dsi_14nm_phy_enable(struct msm_dsi_phy *phy, int src_pll_id,
			       struct msm_dsi_phy_clk_request *clk_req)
{
	struct msm_dsi_dphy_timing *timing = &phy->timing;
	u32 data;
	int i;
	int ret;
	void __iomem *base = phy->base;
	void __iomem *lane_base = phy->lane_base;

	if (msm_dsi_dphy_timing_calc_v2(timing, clk_req)) {
		dev_err(&phy->pdev->dev,
			"%s: D-PHY timing calculation failed\n", __func__);
		return -EINVAL;
	}

	data = 0x1c;
	if (phy->usecase != MSM_DSI_PHY_STANDALONE)
		data |= DSI_14nm_PHY_CMN_LDO_CNTRL_VREG_CTRL(32);
	dsi_phy_write(base + REG_DSI_14nm_PHY_CMN_LDO_CNTRL, data);

	dsi_phy_write(base + REG_DSI_14nm_PHY_CMN_GLBL_TEST_CTRL, 0x1);

	/* 4 data lanes + 1 clk lane configuration */
	for (i = 0; i < 5; i++) {
		dsi_phy_write(lane_base + REG_DSI_14nm_PHY_LN_VREG_CNTRL(i),
			      0x1d);

		dsi_phy_write(lane_base +
			      REG_DSI_14nm_PHY_LN_STRENGTH_CTRL_0(i), 0xff);
		dsi_phy_write(lane_base +
			      REG_DSI_14nm_PHY_LN_STRENGTH_CTRL_1(i),
			      (i == PHY_14NM_CKLN_IDX) ? 0x00 : 0x06);

		dsi_phy_write(lane_base + REG_DSI_14nm_PHY_LN_CFG3(i),
			      (i == PHY_14NM_CKLN_IDX) ? 0x8f : 0x0f);
		dsi_phy_write(lane_base + REG_DSI_14nm_PHY_LN_CFG2(i), 0x10);
		dsi_phy_write(lane_base + REG_DSI_14nm_PHY_LN_TEST_DATAPATH(i),
			      0);
		dsi_phy_write(lane_base + REG_DSI_14nm_PHY_LN_TEST_STR(i),
			      0x88);

		dsi_14nm_dphy_set_timing(phy, timing, i);
	}

	/* Make sure PLL is not start */
	dsi_phy_write(base + REG_DSI_14nm_PHY_CMN_PLL_CNTRL, 0x00);

	wmb(); /* make sure everything is written before reset and enable */

	/* reset digital block */
	dsi_phy_write(base + REG_DSI_14nm_PHY_CMN_CTRL_1, 0x80);
	wmb(); /* ensure reset is asserted */
	udelay(100);
	dsi_phy_write(base + REG_DSI_14nm_PHY_CMN_CTRL_1, 0x00);

	msm_dsi_phy_set_src_pll(phy, src_pll_id,
				REG_DSI_14nm_PHY_CMN_GLBL_TEST_CTRL,
				DSI_14nm_PHY_CMN_GLBL_TEST_CTRL_BITCLK_HS_SEL);

	ret = msm_dsi_pll_set_usecase(phy->pll, phy->usecase);
	if (ret) {
		dev_err(&phy->pdev->dev, "%s: set pll usecase failed, %d\n",
			__func__, ret);
		return ret;
	}

	/* Remove power down from PLL and all lanes */
	dsi_phy_write(base + REG_DSI_14nm_PHY_CMN_CTRL_0, 0xff);

	return 0;
}

static void dsi_14nm_phy_disable(struct msm_dsi_phy *phy)
{
	dsi_phy_write(phy->base + REG_DSI_14nm_PHY_CMN_GLBL_TEST_CTRL, 0);
	dsi_phy_write(phy->base + REG_DSI_14nm_PHY_CMN_CTRL_0, 0);

	/* ensure that the phy is completely disabled */
	wmb();
}

static int dsi_14nm_phy_init(struct msm_dsi_phy *phy)
{
	struct platform_device *pdev = phy->pdev;

	phy->lane_base = msm_ioremap(pdev, "dsi_phy_lane",
				"DSI_PHY_LANE");
	if (IS_ERR(phy->lane_base)) {
		dev_err(&pdev->dev, "%s: failed to map phy lane base\n",
			__func__);
		return -ENOMEM;
	}

	return 0;
}

const struct msm_dsi_phy_cfg dsi_phy_14nm_cfgs = {
	.type = MSM_DSI_PHY_14NM,
	.src_pll_truthtable = { {false, false}, {true, false} },
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vcca", 17000, 32},
		},
	},
	.ops = {
		.enable = dsi_14nm_phy_enable,
		.disable = dsi_14nm_phy_disable,
		.init = dsi_14nm_phy_init,
	},
	.io_start = { 0x994400, 0x996400 },
	.num_dsi_phy = 2,
};

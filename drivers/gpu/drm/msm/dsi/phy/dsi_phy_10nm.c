/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2018, The Linux Foundation
 */

#include <linux/iopoll.h>

#include "dsi_phy.h"
#include "dsi.xml.h"

static int dsi_10nm_phy_enable(struct msm_dsi_phy *phy, int src_pll_id,
			       struct msm_dsi_phy_clk_request *clk_req)
{
	return 0;
}

static void dsi_10nm_phy_disable(struct msm_dsi_phy *phy)
{
}

static int dsi_10nm_phy_init(struct msm_dsi_phy *phy)
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

const struct msm_dsi_phy_cfg dsi_phy_10nm_cfgs = {
	.type = MSM_DSI_PHY_10NM,
	.src_pll_truthtable = { {false, false}, {true, false} },
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vdds", 36000, 32},
		},
	},
	.ops = {
		.enable = dsi_10nm_phy_enable,
		.disable = dsi_10nm_phy_disable,
		.init = dsi_10nm_phy_init,
	},
	.io_start = { 0xae94400, 0xae96400 },
	.num_dsi_phy = 2,
};

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 */

#include "edp.h"
#include "edp.xml.h"

#define EDP_MAX_LANE	4

struct edp_phy {
	void __iomem *base;
};

bool msm_edp_phy_ready(struct edp_phy *phy)
{
	u32 status;
	int cnt = 100;

	while (--cnt) {
		status = edp_read(phy->base +
				REG_EDP_PHY_GLB_PHY_STATUS);
		if (status & 0x01)
			break;
		usleep_range(500, 1000);
	}

	if (cnt == 0) {
		pr_err("%s: PHY NOT ready\n", __func__);
		return false;
	} else {
		return true;
	}
}

void msm_edp_phy_ctrl(struct edp_phy *phy, int enable)
{
	DBG("enable=%d", enable);
	if (enable) {
		/* Reset */
		edp_write(phy->base + REG_EDP_PHY_CTRL,
			EDP_PHY_CTRL_SW_RESET | EDP_PHY_CTRL_SW_RESET_PLL);
		/* Make sure fully reset */
		wmb();
		usleep_range(500, 1000);
		edp_write(phy->base + REG_EDP_PHY_CTRL, 0x000);
		edp_write(phy->base + REG_EDP_PHY_GLB_PD_CTL, 0x3f);
		edp_write(phy->base + REG_EDP_PHY_GLB_CFG, 0x1);
	} else {
		edp_write(phy->base + REG_EDP_PHY_GLB_PD_CTL, 0xc0);
	}
}

/* voltage mode and pre emphasis cfg */
void msm_edp_phy_vm_pe_init(struct edp_phy *phy)
{
	edp_write(phy->base + REG_EDP_PHY_GLB_VM_CFG0, 0x3);
	edp_write(phy->base + REG_EDP_PHY_GLB_VM_CFG1, 0x64);
	edp_write(phy->base + REG_EDP_PHY_GLB_MISC9, 0x6c);
}

void msm_edp_phy_vm_pe_cfg(struct edp_phy *phy, u32 v0, u32 v1)
{
	edp_write(phy->base + REG_EDP_PHY_GLB_VM_CFG0, v0);
	edp_write(phy->base + REG_EDP_PHY_GLB_VM_CFG1, v1);
}

void msm_edp_phy_lane_power_ctrl(struct edp_phy *phy, bool up, u32 max_lane)
{
	u32 i;
	u32 data;

	if (up)
		data = 0;	/* power up */
	else
		data = 0x7;	/* power down */

	for (i = 0; i < max_lane; i++)
		edp_write(phy->base + REG_EDP_PHY_LN_PD_CTL(i) , data);

	/* power down unused lane */
	data = 0x7;	/* power down */
	for (i = max_lane; i < EDP_MAX_LANE; i++)
		edp_write(phy->base + REG_EDP_PHY_LN_PD_CTL(i) , data);
}

void *msm_edp_phy_init(struct device *dev, void __iomem *regbase)
{
	struct edp_phy *phy = NULL;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return NULL;

	phy->base = regbase;
	return phy;
}


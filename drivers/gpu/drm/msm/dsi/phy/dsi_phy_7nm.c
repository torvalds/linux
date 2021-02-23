/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2018, The Linux Foundation
 */

#include <linux/iopoll.h>

#include "dsi_phy.h"
#include "dsi.xml.h"

static int dsi_phy_hw_v4_0_is_pll_on(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->base;
	u32 data = 0;

	data = dsi_phy_read(base + REG_DSI_7nm_PHY_CMN_PLL_CNTRL);
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
		dsi_phy_write(lane_base +
			      REG_DSI_7nm_PHY_LN_LPRX_CTRL(phy_lane_0), 0x3);
	else
		dsi_phy_write(lane_base +
			      REG_DSI_7nm_PHY_LN_LPRX_CTRL(phy_lane_0), 0);
}

static void dsi_phy_hw_v4_0_lane_settings(struct msm_dsi_phy *phy)
{
	int i;
	const u8 tx_dctrl_0[] = { 0x00, 0x00, 0x00, 0x04, 0x01 };
	const u8 tx_dctrl_1[] = { 0x40, 0x40, 0x40, 0x46, 0x41 };
	const u8 *tx_dctrl = tx_dctrl_0;
	void __iomem *lane_base = phy->lane_base;

	if (phy->cfg->type == MSM_DSI_PHY_7NM_V4_1)
		tx_dctrl = tx_dctrl_1;

	/* Strength ctrl settings */
	for (i = 0; i < 5; i++) {
		/*
		 * Disable LPRX and CDRX for all lanes. And later on, it will
		 * be only enabled for the physical data lane corresponding
		 * to the logical data lane 0
		 */
		dsi_phy_write(lane_base + REG_DSI_7nm_PHY_LN_LPRX_CTRL(i), 0);
		dsi_phy_write(lane_base + REG_DSI_7nm_PHY_LN_PIN_SWAP(i), 0x0);
	}

	dsi_phy_hw_v4_0_config_lpcdrx(phy, true);

	/* other settings */
	for (i = 0; i < 5; i++) {
		dsi_phy_write(lane_base + REG_DSI_7nm_PHY_LN_CFG0(i), 0x0);
		dsi_phy_write(lane_base + REG_DSI_7nm_PHY_LN_CFG1(i), 0x0);
		dsi_phy_write(lane_base + REG_DSI_7nm_PHY_LN_CFG2(i), i == 4 ? 0x8a : 0xa);
		dsi_phy_write(lane_base + REG_DSI_7nm_PHY_LN_TX_DCTRL(i), tx_dctrl[i]);
	}
}

static int dsi_7nm_phy_enable(struct msm_dsi_phy *phy, int src_pll_id,
			      struct msm_dsi_phy_clk_request *clk_req)
{
	int ret;
	u32 status;
	u32 const delay_us = 5;
	u32 const timeout_us = 1000;
	struct msm_dsi_dphy_timing *timing = &phy->timing;
	void __iomem *base = phy->base;
	bool less_than_1500_mhz;
	u32 vreg_ctrl_0, glbl_str_swi_cal_sel_ctrl, glbl_hstx_str_ctrl_0;
	u32 glbl_rescode_top_ctrl, glbl_rescode_bot_ctrl;
	u32 data;

	DBG("");

	if (msm_dsi_dphy_timing_calc_v4(timing, clk_req)) {
		DRM_DEV_ERROR(&phy->pdev->dev,
			"%s: D-PHY timing calculation failed\n", __func__);
		return -EINVAL;
	}

	if (dsi_phy_hw_v4_0_is_pll_on(phy))
		pr_warn("PLL turned on before configuring PHY\n");

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

	if (phy->cfg->type == MSM_DSI_PHY_7NM_V4_1) {
		vreg_ctrl_0 = less_than_1500_mhz ? 0x53 : 0x52;
		glbl_rescode_top_ctrl = less_than_1500_mhz ? 0x3d :  0x00;
		glbl_rescode_bot_ctrl = less_than_1500_mhz ? 0x39 :  0x3c;
		glbl_str_swi_cal_sel_ctrl = 0x00;
		glbl_hstx_str_ctrl_0 = 0x88;
	} else {
		vreg_ctrl_0 = less_than_1500_mhz ? 0x5B : 0x59;
		glbl_str_swi_cal_sel_ctrl = less_than_1500_mhz ? 0x03 : 0x00;
		glbl_hstx_str_ctrl_0 = less_than_1500_mhz ? 0x66 : 0x88;
		glbl_rescode_top_ctrl = 0x03;
		glbl_rescode_bot_ctrl = 0x3c;
	}

	/* de-assert digital and pll power down */
	data = BIT(6) | BIT(5);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_CTRL_0, data);

	/* Assert PLL core reset */
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_PLL_CNTRL, 0x00);

	/* turn off resync FIFO */
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_RBUF_CTRL, 0x00);

	/* program CMN_CTRL_4 for minor_ver 2 chipsets*/
	data = dsi_phy_read(base + REG_DSI_7nm_PHY_CMN_REVISION_ID0);
	data = data & (0xf0);
	if (data == 0x20)
		dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_CTRL_4, 0x04);

	/* Configure PHY lane swap (TODO: we need to calculate this) */
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_LANE_CFG0, 0x21);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_LANE_CFG1, 0x84);

	/* Enable LDO */
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_VREG_CTRL_0, vreg_ctrl_0);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_VREG_CTRL_1, 0x5c);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_CTRL_3, 0x00);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_GLBL_STR_SWI_CAL_SEL_CTRL,
		      glbl_str_swi_cal_sel_ctrl);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_GLBL_HSTX_STR_CTRL_0,
		      glbl_hstx_str_ctrl_0);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_GLBL_PEMPH_CTRL_0, 0x00);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_GLBL_RESCODE_OFFSET_TOP_CTRL,
		      glbl_rescode_top_ctrl);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_GLBL_RESCODE_OFFSET_BOT_CTRL,
		      glbl_rescode_bot_ctrl);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_GLBL_LPTX_STR_CTRL, 0x55);

	/* Remove power down from all blocks */
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_CTRL_0, 0x7f);

	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_LANE_CTRL0, 0x1f);

	/* Select full-rate mode */
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_CTRL_2, 0x40);

	ret = msm_dsi_pll_set_usecase(phy->pll, phy->usecase);
	if (ret) {
		DRM_DEV_ERROR(&phy->pdev->dev, "%s: set pll usecase failed, %d\n",
			__func__, ret);
		return ret;
	}

	/* DSI PHY timings */
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_0, 0x00);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_1, timing->clk_zero);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_2, timing->clk_prepare);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_3, timing->clk_trail);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_4, timing->hs_exit);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_5, timing->hs_zero);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_6, timing->hs_prepare);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_7, timing->hs_trail);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_8, timing->hs_rqst);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_9, 0x02);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_10, 0x04);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_11, 0x00);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_12,
		      timing->shared_timings.clk_pre);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_TIMING_CTRL_13,
		      timing->shared_timings.clk_post);

	/* DSI lane settings */
	dsi_phy_hw_v4_0_lane_settings(phy);

	DBG("DSI%d PHY enabled", phy->id);

	return 0;
}

static void dsi_7nm_phy_disable(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->base;
	u32 data;

	DBG("");

	if (dsi_phy_hw_v4_0_is_pll_on(phy))
		pr_warn("Turning OFF PHY while PLL is on\n");

	dsi_phy_hw_v4_0_config_lpcdrx(phy, false);
	data = dsi_phy_read(base + REG_DSI_7nm_PHY_CMN_CTRL_0);

	/* disable all lanes */
	data &= ~0x1F;
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_CTRL_0, data);
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_LANE_CTRL0, 0);

	/* Turn off all PHY blocks */
	dsi_phy_write(base + REG_DSI_7nm_PHY_CMN_CTRL_0, 0x00);
	/* make sure phy is turned off */
	wmb();

	DBG("DSI%d PHY disabled", phy->id);
}

static int dsi_7nm_phy_init(struct msm_dsi_phy *phy)
{
	struct platform_device *pdev = phy->pdev;

	phy->lane_base = msm_ioremap(pdev, "dsi_phy_lane",
				     "DSI_PHY_LANE");
	if (IS_ERR(phy->lane_base)) {
		DRM_DEV_ERROR(&pdev->dev, "%s: failed to map phy lane base\n",
			__func__);
		return -ENOMEM;
	}

	return 0;
}

const struct msm_dsi_phy_cfg dsi_phy_7nm_cfgs = {
	.type = MSM_DSI_PHY_7NM_V4_1,
	.src_pll_truthtable = { {false, false}, {true, false} },
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vdds", 36000, 32},
		},
	},
	.ops = {
		.enable = dsi_7nm_phy_enable,
		.disable = dsi_7nm_phy_disable,
		.init = dsi_7nm_phy_init,
	},
	.io_start = { 0xae94400, 0xae96400 },
	.num_dsi_phy = 2,
};

const struct msm_dsi_phy_cfg dsi_phy_7nm_8150_cfgs = {
	.type = MSM_DSI_PHY_7NM,
	.src_pll_truthtable = { {false, false}, {true, false} },
	.reg_cfg = {
		.num = 1,
		.regs = {
			{"vdds", 36000, 32},
		},
	},
	.ops = {
		.enable = dsi_7nm_phy_enable,
		.disable = dsi_7nm_phy_disable,
		.init = dsi_7nm_phy_init,
	},
	.io_start = { 0xae94400, 0xae96400 },
	.num_dsi_phy = 2,
};

/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2018, The Linux Foundation
 */

#include <linux/iopoll.h>

#include "dsi_phy.h"
#include "dsi.xml.h"

static int dsi_phy_hw_v3_0_is_pll_on(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->base;
	u32 data = 0;

	data = dsi_phy_read(base + REG_DSI_10nm_PHY_CMN_PLL_CNTRL);
	mb(); /* make sure read happened */

	return (data & BIT(0));
}

static void dsi_phy_hw_v3_0_config_lpcdrx(struct msm_dsi_phy *phy, bool enable)
{
	void __iomem *lane_base = phy->lane_base;
	int phy_lane_0 = 0;	/* TODO: Support all lane swap configs */

	/*
	 * LPRX and CDRX need to enabled only for physical data lane
	 * corresponding to the logical data lane 0
	 */
	if (enable)
		dsi_phy_write(lane_base +
			      REG_DSI_10nm_PHY_LN_LPRX_CTRL(phy_lane_0), 0x3);
	else
		dsi_phy_write(lane_base +
			      REG_DSI_10nm_PHY_LN_LPRX_CTRL(phy_lane_0), 0);
}

static void dsi_phy_hw_v3_0_lane_settings(struct msm_dsi_phy *phy)
{
	int i;
	u8 tx_dctrl[] = { 0x00, 0x00, 0x00, 0x04, 0x01 };
	void __iomem *lane_base = phy->lane_base;

	if (phy->cfg->quirks & V3_0_0_10NM_OLD_TIMINGS_QUIRK)
		tx_dctrl[3] = 0x02;

	/* Strength ctrl settings */
	for (i = 0; i < 5; i++) {
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_LPTX_STR_CTRL(i),
			      0x55);
		/*
		 * Disable LPRX and CDRX for all lanes. And later on, it will
		 * be only enabled for the physical data lane corresponding
		 * to the logical data lane 0
		 */
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_LPRX_CTRL(i), 0);
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_PIN_SWAP(i), 0x0);
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_HSTX_STR_CTRL(i),
			      0x88);
	}

	dsi_phy_hw_v3_0_config_lpcdrx(phy, true);

	/* other settings */
	for (i = 0; i < 5; i++) {
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_CFG0(i), 0x0);
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_CFG1(i), 0x0);
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_CFG2(i), 0x0);
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_CFG3(i),
			      i == 4 ? 0x80 : 0x0);
		dsi_phy_write(lane_base +
			      REG_DSI_10nm_PHY_LN_OFFSET_TOP_CTRL(i), 0x0);
		dsi_phy_write(lane_base +
			      REG_DSI_10nm_PHY_LN_OFFSET_BOT_CTRL(i), 0x0);
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_TX_DCTRL(i),
			      tx_dctrl[i]);
	}

	if (!(phy->cfg->quirks & V3_0_0_10NM_OLD_TIMINGS_QUIRK)) {
		/* Toggle BIT 0 to release freeze I/0 */
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_TX_DCTRL(3), 0x05);
		dsi_phy_write(lane_base + REG_DSI_10nm_PHY_LN_TX_DCTRL(3), 0x04);
	}
}

static int dsi_10nm_phy_enable(struct msm_dsi_phy *phy, int src_pll_id,
			       struct msm_dsi_phy_clk_request *clk_req)
{
	int ret;
	u32 status;
	u32 const delay_us = 5;
	u32 const timeout_us = 1000;
	struct msm_dsi_dphy_timing *timing = &phy->timing;
	void __iomem *base = phy->base;
	u32 data;

	DBG("");

	if (msm_dsi_dphy_timing_calc_v3(timing, clk_req)) {
		DRM_DEV_ERROR(&phy->pdev->dev,
			"%s: D-PHY timing calculation failed\n", __func__);
		return -EINVAL;
	}

	if (dsi_phy_hw_v3_0_is_pll_on(phy))
		pr_warn("PLL turned on before configuring PHY\n");

	/* wait for REFGEN READY */
	ret = readl_poll_timeout_atomic(base + REG_DSI_10nm_PHY_CMN_PHY_STATUS,
					status, (status & BIT(0)),
					delay_us, timeout_us);
	if (ret) {
		pr_err("Ref gen not ready. Aborting\n");
		return -EINVAL;
	}

	/* de-assert digital and pll power down */
	data = BIT(6) | BIT(5);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_CTRL_0, data);

	/* Assert PLL core reset */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_PLL_CNTRL, 0x00);

	/* turn off resync FIFO */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_RBUF_CTRL, 0x00);

	/* Select MS1 byte-clk */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_GLBL_CTRL, 0x10);

	/* Enable LDO */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_VREG_CTRL, 0x59);

	/* Configure PHY lane swap (TODO: we need to calculate this) */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_LANE_CFG0, 0x21);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_LANE_CFG1, 0x84);

	/* DSI PHY timings */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_0,
		      timing->hs_halfbyte_en);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_1,
		      timing->clk_zero);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_2,
		      timing->clk_prepare);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_3,
		      timing->clk_trail);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_4,
		      timing->hs_exit);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_5,
		      timing->hs_zero);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_6,
		      timing->hs_prepare);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_7,
		      timing->hs_trail);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_8,
		      timing->hs_rqst);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_9,
		      timing->ta_go | (timing->ta_sure << 3));
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_10,
		      timing->ta_get);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_TIMING_CTRL_11,
		      0x00);

	/* Remove power down from all blocks */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_CTRL_0, 0x7f);

	/* power up lanes */
	data = dsi_phy_read(base + REG_DSI_10nm_PHY_CMN_CTRL_0);

	/* TODO: only power up lanes that are used */
	data |= 0x1F;
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_CTRL_0, data);
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_LANE_CTRL0, 0x1F);

	/* Select full-rate mode */
	dsi_phy_write(base + REG_DSI_10nm_PHY_CMN_CTRL_2, 0x40);

	ret = msm_dsi_pll_set_usecase(phy->pll, phy->usecase);
	if (ret) {
		DRM_DEV_ERROR(&phy->pdev->dev, "%s: set pll usecase failed, %d\n",
			__func__, ret);
		return ret;
	}

	/* DSI lane settings */
	dsi_phy_hw_v3_0_lane_settings(phy);

	DBG("DSI%d PHY enabled", phy->id);

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
		DRM_DEV_ERROR(&pdev->dev, "%s: failed to map phy lane base\n",
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

const struct msm_dsi_phy_cfg dsi_phy_10nm_8998_cfgs = {
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
	.io_start = { 0xc994400, 0xc996400 },
	.num_dsi_phy = 2,
	.quirks = V3_0_0_10NM_OLD_TIMINGS_QUIRK,
};

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include "dsi_phy.h"
#include "dsi.xml.h"
#include "dsi_phy_20nm.xml.h"

static void dsi_20nm_dphy_set_timing(struct msm_dsi_phy *phy,
		struct msm_dsi_dphy_timing *timing)
{
	void __iomem *base = phy->base;

	writel(DSI_20nm_PHY_TIMING_CTRL_0_CLK_ZERO(timing->clk_zero),
	       base + REG_DSI_20nm_PHY_TIMING_CTRL_0);
	writel(DSI_20nm_PHY_TIMING_CTRL_1_CLK_TRAIL(timing->clk_trail),
	       base + REG_DSI_20nm_PHY_TIMING_CTRL_1);
	writel(DSI_20nm_PHY_TIMING_CTRL_2_CLK_PREPARE(timing->clk_prepare),
	       base + REG_DSI_20nm_PHY_TIMING_CTRL_2);
	if (timing->clk_zero & BIT(8))
		writel(DSI_20nm_PHY_TIMING_CTRL_3_CLK_ZERO_8,
		       base + REG_DSI_20nm_PHY_TIMING_CTRL_3);
	writel(DSI_20nm_PHY_TIMING_CTRL_4_HS_EXIT(timing->hs_exit),
	       base + REG_DSI_20nm_PHY_TIMING_CTRL_4);
	writel(DSI_20nm_PHY_TIMING_CTRL_5_HS_ZERO(timing->hs_zero),
	       base + REG_DSI_20nm_PHY_TIMING_CTRL_5);
	writel(DSI_20nm_PHY_TIMING_CTRL_6_HS_PREPARE(timing->hs_prepare),
	       base + REG_DSI_20nm_PHY_TIMING_CTRL_6);
	writel(DSI_20nm_PHY_TIMING_CTRL_7_HS_TRAIL(timing->hs_trail),
	       base + REG_DSI_20nm_PHY_TIMING_CTRL_7);
	writel(DSI_20nm_PHY_TIMING_CTRL_8_HS_RQST(timing->hs_rqst),
	       base + REG_DSI_20nm_PHY_TIMING_CTRL_8);
	writel(DSI_20nm_PHY_TIMING_CTRL_9_TA_GO(timing->ta_go) |
	       DSI_20nm_PHY_TIMING_CTRL_9_TA_SURE(timing->ta_sure),
	       base + REG_DSI_20nm_PHY_TIMING_CTRL_9);
	writel(DSI_20nm_PHY_TIMING_CTRL_10_TA_GET(timing->ta_get),
	       base + REG_DSI_20nm_PHY_TIMING_CTRL_10);
	writel(DSI_20nm_PHY_TIMING_CTRL_11_TRIG3_CMD(0),
	       base + REG_DSI_20nm_PHY_TIMING_CTRL_11);
}

static void dsi_20nm_phy_regulator_ctrl(struct msm_dsi_phy *phy, bool enable)
{
	void __iomem *base = phy->reg_base;

	if (!enable) {
		writel(0, base + REG_DSI_20nm_PHY_REGULATOR_CAL_PWR_CFG);
		return;
	}

	if (phy->regulator_ldo_mode) {
		writel(0x1d, phy->base + REG_DSI_20nm_PHY_LDO_CNTRL);
		return;
	}

	/* non LDO mode */
	writel(0x03, base + REG_DSI_20nm_PHY_REGULATOR_CTRL_1);
	writel(0x03, base + REG_DSI_20nm_PHY_REGULATOR_CTRL_2);
	writel(0x00, base + REG_DSI_20nm_PHY_REGULATOR_CTRL_3);
	writel(0x20, base + REG_DSI_20nm_PHY_REGULATOR_CTRL_4);
	writel(0x01, base + REG_DSI_20nm_PHY_REGULATOR_CAL_PWR_CFG);
	writel(0x00, phy->base + REG_DSI_20nm_PHY_LDO_CNTRL);
	writel(0x03, base + REG_DSI_20nm_PHY_REGULATOR_CTRL_0);
}

static int dsi_20nm_phy_enable(struct msm_dsi_phy *phy,
				struct msm_dsi_phy_clk_request *clk_req)
{
	struct msm_dsi_dphy_timing *timing = &phy->timing;
	int i;
	void __iomem *base = phy->base;
	u32 cfg_4[4] = {0x20, 0x40, 0x20, 0x00};
	u32 val;

	DBG("");

	if (msm_dsi_dphy_timing_calc(timing, clk_req)) {
		DRM_DEV_ERROR(&phy->pdev->dev,
			"%s: D-PHY timing calculation failed\n", __func__);
		return -EINVAL;
	}

	dsi_20nm_phy_regulator_ctrl(phy, true);

	writel(0xff, base + REG_DSI_20nm_PHY_STRENGTH_0);

	val = readl(base + REG_DSI_20nm_PHY_GLBL_TEST_CTRL);
	if (phy->id == DSI_1 && phy->usecase == MSM_DSI_PHY_STANDALONE)
		val |= DSI_20nm_PHY_GLBL_TEST_CTRL_BITCLK_HS_SEL;
	else
		val &= ~DSI_20nm_PHY_GLBL_TEST_CTRL_BITCLK_HS_SEL;
	writel(val, base + REG_DSI_20nm_PHY_GLBL_TEST_CTRL);

	for (i = 0; i < 4; i++) {
		writel((i >> 1) * 0x40, base + REG_DSI_20nm_PHY_LN_CFG_3(i));
		writel(0x01, base + REG_DSI_20nm_PHY_LN_TEST_STR_0(i));
		writel(0x46, base + REG_DSI_20nm_PHY_LN_TEST_STR_1(i));
		writel(0x02, base + REG_DSI_20nm_PHY_LN_CFG_0(i));
		writel(0xa0, base + REG_DSI_20nm_PHY_LN_CFG_1(i));
		writel(cfg_4[i], base + REG_DSI_20nm_PHY_LN_CFG_4(i));
	}

	writel(0x80, base + REG_DSI_20nm_PHY_LNCK_CFG_3);
	writel(0x01, base + REG_DSI_20nm_PHY_LNCK_TEST_STR0);
	writel(0x46, base + REG_DSI_20nm_PHY_LNCK_TEST_STR1);
	writel(0x00, base + REG_DSI_20nm_PHY_LNCK_CFG_0);
	writel(0xa0, base + REG_DSI_20nm_PHY_LNCK_CFG_1);
	writel(0x00, base + REG_DSI_20nm_PHY_LNCK_CFG_2);
	writel(0x00, base + REG_DSI_20nm_PHY_LNCK_CFG_4);

	dsi_20nm_dphy_set_timing(phy, timing);

	writel(0x00, base + REG_DSI_20nm_PHY_CTRL_1);

	writel(0x06, base + REG_DSI_20nm_PHY_STRENGTH_1);

	/* make sure everything is written before enable */
	wmb();
	writel(0x7f, base + REG_DSI_20nm_PHY_CTRL_0);

	return 0;
}

static void dsi_20nm_phy_disable(struct msm_dsi_phy *phy)
{
	writel(0, phy->base + REG_DSI_20nm_PHY_CTRL_0);
	dsi_20nm_phy_regulator_ctrl(phy, false);
}

static const struct regulator_bulk_data dsi_phy_20nm_regulators[] = {
	{ .supply = "vddio", .init_load_uA = 100000 },	/* 1.8 V */
	{ .supply = "vcca", .init_load_uA = 10000 },	/* 1.0 V */
};

const struct msm_dsi_phy_cfg dsi_phy_20nm_cfgs = {
	.has_phy_regulator = true,
	.regulator_data = dsi_phy_20nm_regulators,
	.num_regulators = ARRAY_SIZE(dsi_phy_20nm_regulators),
	.ops = {
		.enable = dsi_20nm_phy_enable,
		.disable = dsi_20nm_phy_disable,
	},
	.io_start = { 0xfd998500, 0xfd9a0500 },
	.num_dsi_phy = 2,
};


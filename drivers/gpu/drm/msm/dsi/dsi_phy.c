/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
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

#include "dsi.h"
#include "dsi.xml.h"

#define dsi_phy_read(offset) msm_readl((offset))
#define dsi_phy_write(offset, data) msm_writel((data), (offset))

struct dsi_dphy_timing {
	u32 clk_pre;
	u32 clk_post;
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
};

struct msm_dsi_phy {
	void __iomem *base;
	void __iomem *reg_base;
	int id;
	struct dsi_dphy_timing timing;
	int (*enable)(struct msm_dsi_phy *phy, bool is_dual_panel,
		const unsigned long bit_rate, const unsigned long esc_rate);
	int (*disable)(struct msm_dsi_phy *phy);
};

#define S_DIV_ROUND_UP(n, d)	\
	(((n) >= 0) ? (((n) + (d) - 1) / (d)) : (((n) - (d) + 1) / (d)))

static inline s32 linear_inter(s32 tmax, s32 tmin, s32 percent,
				s32 min_result, bool even)
{
	s32 v;
	v = (tmax - tmin) * percent;
	v = S_DIV_ROUND_UP(v, 100) + tmin;
	if (even && (v & 0x1))
		return max_t(s32, min_result, v - 1);
	else
		return max_t(s32, min_result, v);
}

static void dsi_dphy_timing_calc_clk_zero(struct dsi_dphy_timing *timing,
					s32 ui, s32 coeff, s32 pcnt)
{
	s32 tmax, tmin, clk_z;
	s32 temp;

	/* reset */
	temp = 300 * coeff - ((timing->clk_prepare >> 1) + 1) * 2 * ui;
	tmin = S_DIV_ROUND_UP(temp, ui) - 2;
	if (tmin > 255) {
		tmax = 511;
		clk_z = linear_inter(2 * tmin, tmin, pcnt, 0, true);
	} else {
		tmax = 255;
		clk_z = linear_inter(tmax, tmin, pcnt, 0, true);
	}

	/* adjust */
	temp = (timing->hs_rqst + timing->clk_prepare + clk_z) & 0x7;
	timing->clk_zero = clk_z + 8 - temp;
}

static int dsi_dphy_timing_calc(struct dsi_dphy_timing *timing,
	const unsigned long bit_rate, const unsigned long esc_rate)
{
	s32 ui, lpx;
	s32 tmax, tmin;
	s32 pcnt0 = 10;
	s32 pcnt1 = (bit_rate > 1200000000) ? 15 : 10;
	s32 pcnt2 = 10;
	s32 pcnt3 = (bit_rate > 180000000) ? 10 : 40;
	s32 coeff = 1000; /* Precision, should avoid overflow */
	s32 temp;

	if (!bit_rate || !esc_rate)
		return -EINVAL;

	ui = mult_frac(NSEC_PER_MSEC, coeff, bit_rate / 1000);
	lpx = mult_frac(NSEC_PER_MSEC, coeff, esc_rate / 1000);

	tmax = S_DIV_ROUND_UP(95 * coeff, ui) - 2;
	tmin = S_DIV_ROUND_UP(38 * coeff, ui) - 2;
	timing->clk_prepare = linear_inter(tmax, tmin, pcnt0, 0, true);

	temp = lpx / ui;
	if (temp & 0x1)
		timing->hs_rqst = temp;
	else
		timing->hs_rqst = max_t(s32, 0, temp - 2);

	/* Calculate clk_zero after clk_prepare and hs_rqst */
	dsi_dphy_timing_calc_clk_zero(timing, ui, coeff, pcnt2);

	temp = 105 * coeff + 12 * ui - 20 * coeff;
	tmax = S_DIV_ROUND_UP(temp, ui) - 2;
	tmin = S_DIV_ROUND_UP(60 * coeff, ui) - 2;
	timing->clk_trail = linear_inter(tmax, tmin, pcnt3, 0, true);

	temp = 85 * coeff + 6 * ui;
	tmax = S_DIV_ROUND_UP(temp, ui) - 2;
	temp = 40 * coeff + 4 * ui;
	tmin = S_DIV_ROUND_UP(temp, ui) - 2;
	timing->hs_prepare = linear_inter(tmax, tmin, pcnt1, 0, true);

	tmax = 255;
	temp = ((timing->hs_prepare >> 1) + 1) * 2 * ui + 2 * ui;
	temp = 145 * coeff + 10 * ui - temp;
	tmin = S_DIV_ROUND_UP(temp, ui) - 2;
	timing->hs_zero = linear_inter(tmax, tmin, pcnt2, 24, true);

	temp = 105 * coeff + 12 * ui - 20 * coeff;
	tmax = S_DIV_ROUND_UP(temp, ui) - 2;
	temp = 60 * coeff + 4 * ui;
	tmin = DIV_ROUND_UP(temp, ui) - 2;
	timing->hs_trail = linear_inter(tmax, tmin, pcnt3, 0, true);

	tmax = 255;
	tmin = S_DIV_ROUND_UP(100 * coeff, ui) - 2;
	timing->hs_exit = linear_inter(tmax, tmin, pcnt2, 0, true);

	tmax = 63;
	temp = ((timing->hs_exit >> 1) + 1) * 2 * ui;
	temp = 60 * coeff + 52 * ui - 24 * ui - temp;
	tmin = S_DIV_ROUND_UP(temp, 8 * ui) - 1;
	timing->clk_post = linear_inter(tmax, tmin, pcnt2, 0, false);

	tmax = 63;
	temp = ((timing->clk_prepare >> 1) + 1) * 2 * ui;
	temp += ((timing->clk_zero >> 1) + 1) * 2 * ui;
	temp += 8 * ui + lpx;
	tmin = S_DIV_ROUND_UP(temp, 8 * ui) - 1;
	if (tmin > tmax) {
		temp = linear_inter(2 * tmax, tmin, pcnt2, 0, false) >> 1;
		timing->clk_pre = temp >> 1;
		temp = (2 * tmax - tmin) * pcnt2;
	} else {
		timing->clk_pre = linear_inter(tmax, tmin, pcnt2, 0, false);
	}

	timing->ta_go = 3;
	timing->ta_sure = 0;
	timing->ta_get = 4;

	DBG("PHY timings: %d, %d, %d, %d, %d, %d, %d, %d, %d, %d",
		timing->clk_pre, timing->clk_post, timing->clk_zero,
		timing->clk_trail, timing->clk_prepare, timing->hs_exit,
		timing->hs_zero, timing->hs_prepare, timing->hs_trail,
		timing->hs_rqst);

	return 0;
}

static void dsi_28nm_phy_regulator_ctrl(struct msm_dsi_phy *phy, bool enable)
{
	void __iomem *base = phy->reg_base;

	if (!enable) {
		dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CAL_PWR_CFG, 0);
		return;
	}

	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_0, 0x0);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CAL_PWR_CFG, 1);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_5, 0);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_3, 0);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_2, 0x3);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_1, 0x9);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_0, 0x7);
	dsi_phy_write(base + REG_DSI_28nm_PHY_REGULATOR_CTRL_4, 0x20);
}

static int dsi_28nm_phy_enable(struct msm_dsi_phy *phy, bool is_dual_panel,
		const unsigned long bit_rate, const unsigned long esc_rate)
{
	struct dsi_dphy_timing *timing = &phy->timing;
	int i;
	void __iomem *base = phy->base;

	DBG("");

	if (dsi_dphy_timing_calc(timing, bit_rate, esc_rate)) {
		pr_err("%s: D-PHY timing calculation failed\n", __func__);
		return -EINVAL;
	}

	dsi_phy_write(base + REG_DSI_28nm_PHY_STRENGTH_0, 0xff);

	dsi_28nm_phy_regulator_ctrl(phy, true);

	dsi_phy_write(base + REG_DSI_28nm_PHY_LDO_CNTRL, 0x00);

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

	dsi_phy_write(base + REG_DSI_28nm_PHY_CTRL_1, 0x00);
	dsi_phy_write(base + REG_DSI_28nm_PHY_CTRL_0, 0x5f);

	dsi_phy_write(base + REG_DSI_28nm_PHY_STRENGTH_1, 0x6);

	for (i = 0; i < 4; i++) {
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_CFG_0(i), 0);
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_CFG_1(i), 0);
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_CFG_2(i), 0);
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_CFG_3(i), 0);
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_TEST_DATAPATH(i), 0);
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_DEBUG_SEL(i), 0);
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_TEST_STR_0(i), 0x1);
		dsi_phy_write(base + REG_DSI_28nm_PHY_LN_TEST_STR_1(i), 0x97);
	}
	dsi_phy_write(base + REG_DSI_28nm_PHY_LN_CFG_4(0), 0);
	dsi_phy_write(base + REG_DSI_28nm_PHY_LN_CFG_4(1), 0x5);
	dsi_phy_write(base + REG_DSI_28nm_PHY_LN_CFG_4(2), 0xa);
	dsi_phy_write(base + REG_DSI_28nm_PHY_LN_CFG_4(3), 0xf);

	dsi_phy_write(base + REG_DSI_28nm_PHY_LNCK_CFG_1, 0xc0);
	dsi_phy_write(base + REG_DSI_28nm_PHY_LNCK_TEST_STR0, 0x1);
	dsi_phy_write(base + REG_DSI_28nm_PHY_LNCK_TEST_STR1, 0xbb);

	dsi_phy_write(base + REG_DSI_28nm_PHY_CTRL_0, 0x5f);

	if (is_dual_panel && (phy->id != DSI_CLOCK_MASTER))
		dsi_phy_write(base + REG_DSI_28nm_PHY_GLBL_TEST_CTRL, 0x00);
	else
		dsi_phy_write(base + REG_DSI_28nm_PHY_GLBL_TEST_CTRL, 0x01);

	return 0;
}

static int dsi_28nm_phy_disable(struct msm_dsi_phy *phy)
{
	dsi_phy_write(phy->base + REG_DSI_28nm_PHY_CTRL_0, 0);
	dsi_28nm_phy_regulator_ctrl(phy, false);

	/*
	 * Wait for the registers writes to complete in order to
	 * ensure that the phy is completely disabled
	 */
	wmb();

	return 0;
}

#define dsi_phy_func_init(name)	\
	do {	\
		phy->enable = dsi_##name##_phy_enable;	\
		phy->disable = dsi_##name##_phy_disable;	\
	} while (0)

struct msm_dsi_phy *msm_dsi_phy_init(struct platform_device *pdev,
			enum msm_dsi_phy_type type, int id)
{
	struct msm_dsi_phy *phy;

	phy = devm_kzalloc(&pdev->dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return NULL;

	phy->base = msm_ioremap(pdev, "dsi_phy", "DSI_PHY");
	if (IS_ERR_OR_NULL(phy->base)) {
		pr_err("%s: failed to map phy base\n", __func__);
		return NULL;
	}
	phy->reg_base = msm_ioremap(pdev, "dsi_phy_regulator", "DSI_PHY_REG");
	if (IS_ERR_OR_NULL(phy->reg_base)) {
		pr_err("%s: failed to map phy regulator base\n", __func__);
		return NULL;
	}

	switch (type) {
	case MSM_DSI_PHY_28NM:
		dsi_phy_func_init(28nm);
		break;
	default:
		pr_err("%s: unsupported type, %d\n", __func__, type);
		return NULL;
	}

	phy->id = id;

	return phy;
}

int msm_dsi_phy_enable(struct msm_dsi_phy *phy, bool is_dual_panel,
	const unsigned long bit_rate, const unsigned long esc_rate)
{
	if (!phy || !phy->enable)
		return -EINVAL;
	return phy->enable(phy, is_dual_panel, bit_rate, esc_rate);
}

int msm_dsi_phy_disable(struct msm_dsi_phy *phy)
{
	if (!phy || !phy->disable)
		return -EINVAL;
	return phy->disable(phy);
}

void msm_dsi_phy_get_clk_pre_post(struct msm_dsi_phy *phy,
	u32 *clk_pre, u32 *clk_post)
{
	if (!phy)
		return;
	if (clk_pre)
		*clk_pre = phy->timing.clk_pre;
	if (clk_post)
		*clk_post = phy->timing.clk_post;
}


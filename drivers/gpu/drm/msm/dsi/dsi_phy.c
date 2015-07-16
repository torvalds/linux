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

#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include "dsi.h"
#include "dsi.xml.h"

#define dsi_phy_read(offset) msm_readl((offset))
#define dsi_phy_write(offset, data) msm_writel((data), (offset))

struct dsi_phy_ops {
	int (*enable)(struct msm_dsi_phy *phy, bool is_dual_panel,
		const unsigned long bit_rate, const unsigned long esc_rate);
	int (*disable)(struct msm_dsi_phy *phy);
};

struct dsi_phy_cfg {
	enum msm_dsi_phy_type type;
	struct dsi_reg_config reg_cfg;
	struct dsi_phy_ops ops;
};

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
	struct platform_device *pdev;
	void __iomem *base;
	void __iomem *reg_base;
	int id;

	struct clk *ahb_clk;
	struct regulator_bulk_data supplies[DSI_DEV_REGULATOR_MAX];

	struct dsi_dphy_timing timing;
	const struct dsi_phy_cfg *cfg;

	struct msm_dsi_pll *pll;
};

static int dsi_phy_regulator_init(struct msm_dsi_phy *phy)
{
	struct regulator_bulk_data *s = phy->supplies;
	const struct dsi_reg_entry *regs = phy->cfg->reg_cfg.regs;
	struct device *dev = &phy->pdev->dev;
	int num = phy->cfg->reg_cfg.num;
	int i, ret;

	for (i = 0; i < num; i++)
		s[i].supply = regs[i].name;

	ret = devm_regulator_bulk_get(&phy->pdev->dev, num, s);
	if (ret < 0) {
		dev_err(dev, "%s: failed to init regulator, ret=%d\n",
						__func__, ret);
		return ret;
	}

	for (i = 0; i < num; i++) {
		if ((regs[i].min_voltage >= 0) && (regs[i].max_voltage >= 0)) {
			ret = regulator_set_voltage(s[i].consumer,
				regs[i].min_voltage, regs[i].max_voltage);
			if (ret < 0) {
				dev_err(dev,
					"regulator %d set voltage failed, %d\n",
					i, ret);
				return ret;
			}
		}
	}

	return 0;
}

static void dsi_phy_regulator_disable(struct msm_dsi_phy *phy)
{
	struct regulator_bulk_data *s = phy->supplies;
	const struct dsi_reg_entry *regs = phy->cfg->reg_cfg.regs;
	int num = phy->cfg->reg_cfg.num;
	int i;

	DBG("");
	for (i = num - 1; i >= 0; i--)
		if (regs[i].disable_load >= 0)
			regulator_set_load(s[i].consumer,
						regs[i].disable_load);

	regulator_bulk_disable(num, s);
}

static int dsi_phy_regulator_enable(struct msm_dsi_phy *phy)
{
	struct regulator_bulk_data *s = phy->supplies;
	const struct dsi_reg_entry *regs = phy->cfg->reg_cfg.regs;
	struct device *dev = &phy->pdev->dev;
	int num = phy->cfg->reg_cfg.num;
	int ret, i;

	DBG("");
	for (i = 0; i < num; i++) {
		if (regs[i].enable_load >= 0) {
			ret = regulator_set_load(s[i].consumer,
							regs[i].enable_load);
			if (ret < 0) {
				dev_err(dev,
					"regulator %d set op mode failed, %d\n",
					i, ret);
				goto fail;
			}
		}
	}

	ret = regulator_bulk_enable(num, s);
	if (ret < 0) {
		dev_err(dev, "regulator enable failed, %d\n", ret);
		goto fail;
	}

	return 0;

fail:
	for (i--; i >= 0; i--)
		regulator_set_load(s[i].consumer, regs[i].disable_load);
	return ret;
}

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

static int dsi_phy_enable_resource(struct msm_dsi_phy *phy)
{
	int ret;

	pm_runtime_get_sync(&phy->pdev->dev);

	ret = clk_prepare_enable(phy->ahb_clk);
	if (ret) {
		pr_err("%s: can't enable ahb clk, %d\n", __func__, ret);
		pm_runtime_put_sync(&phy->pdev->dev);
	}

	return ret;
}

static void dsi_phy_disable_resource(struct msm_dsi_phy *phy)
{
	clk_disable_unprepare(phy->ahb_clk);
	pm_runtime_put_sync(&phy->pdev->dev);
}

static const struct dsi_phy_cfg dsi_phy_cfgs[MSM_DSI_PHY_MAX] = {
	[MSM_DSI_PHY_28NM_HPM] = {
		.type = MSM_DSI_PHY_28NM_HPM,
		.reg_cfg = {
			.num = 1,
			.regs = {
				{"vddio", 1800000, 1800000, 100000, 100},
			},
		},
		.ops = {
			.enable = dsi_28nm_phy_enable,
			.disable = dsi_28nm_phy_disable,
		}
	},
	[MSM_DSI_PHY_28NM_LP] = {
		.type = MSM_DSI_PHY_28NM_LP,
		.reg_cfg = {
			.num = 1,
			.regs = {
				{"vddio", 1800000, 1800000, 100000, 100},
			},
		},
		.ops = {
			.enable = dsi_28nm_phy_enable,
			.disable = dsi_28nm_phy_disable,
		}
	},
};

static const struct of_device_id dsi_phy_dt_match[] = {
	{ .compatible = "qcom,dsi-phy-28nm-hpm",
	  .data = &dsi_phy_cfgs[MSM_DSI_PHY_28NM_HPM],},
	{ .compatible = "qcom,dsi-phy-28nm-lp",
	  .data = &dsi_phy_cfgs[MSM_DSI_PHY_28NM_LP],},
	{}
};

static int dsi_phy_driver_probe(struct platform_device *pdev)
{
	struct msm_dsi_phy *phy;
	const struct of_device_id *match;
	int ret;

	phy = devm_kzalloc(&pdev->dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	match = of_match_node(dsi_phy_dt_match, pdev->dev.of_node);
	if (!match)
		return -ENODEV;

	phy->cfg = match->data;
	phy->pdev = pdev;

	ret = of_property_read_u32(pdev->dev.of_node,
				"qcom,dsi-phy-index", &phy->id);
	if (ret) {
		dev_err(&pdev->dev,
			"%s: PHY index not specified, ret=%d\n",
			__func__, ret);
		goto fail;
	}

	phy->base = msm_ioremap(pdev, "dsi_phy", "DSI_PHY");
	if (IS_ERR(phy->base)) {
		dev_err(&pdev->dev, "%s: failed to map phy base\n", __func__);
		ret = -ENOMEM;
		goto fail;
	}
	phy->reg_base = msm_ioremap(pdev, "dsi_phy_regulator", "DSI_PHY_REG");
	if (IS_ERR(phy->reg_base)) {
		dev_err(&pdev->dev,
			"%s: failed to map phy regulator base\n", __func__);
		ret = -ENOMEM;
		goto fail;
	}

	ret = dsi_phy_regulator_init(phy);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to init regulator\n", __func__);
		goto fail;
	}

	phy->ahb_clk = devm_clk_get(&pdev->dev, "iface_clk");
	if (IS_ERR(phy->ahb_clk)) {
		pr_err("%s: Unable to get ahb clk\n", __func__);
		ret = PTR_ERR(phy->ahb_clk);
		goto fail;
	}

	/* PLL init will call into clk_register which requires
	 * register access, so we need to enable power and ahb clock.
	 */
	ret = dsi_phy_enable_resource(phy);
	if (ret)
		goto fail;

	phy->pll = msm_dsi_pll_init(pdev, phy->cfg->type, phy->id);
	if (!phy->pll)
		dev_info(&pdev->dev,
			"%s: pll init failed, need separate pll clk driver\n",
			__func__);

	dsi_phy_disable_resource(phy);

	platform_set_drvdata(pdev, phy);

	return 0;

fail:
	return ret;
}

static int dsi_phy_driver_remove(struct platform_device *pdev)
{
	struct msm_dsi_phy *phy = platform_get_drvdata(pdev);

	if (phy && phy->pll) {
		msm_dsi_pll_destroy(phy->pll);
		phy->pll = NULL;
	}

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver dsi_phy_platform_driver = {
	.probe      = dsi_phy_driver_probe,
	.remove     = dsi_phy_driver_remove,
	.driver     = {
		.name   = "msm_dsi_phy",
		.of_match_table = dsi_phy_dt_match,
	},
};

void __init msm_dsi_phy_driver_register(void)
{
	platform_driver_register(&dsi_phy_platform_driver);
}

void __exit msm_dsi_phy_driver_unregister(void)
{
	platform_driver_unregister(&dsi_phy_platform_driver);
}

int msm_dsi_phy_enable(struct msm_dsi_phy *phy, bool is_dual_panel,
	const unsigned long bit_rate, const unsigned long esc_rate)
{
	int ret;

	if (!phy || !phy->cfg->ops.enable)
		return -EINVAL;

	ret = dsi_phy_regulator_enable(phy);
	if (ret) {
		dev_err(&phy->pdev->dev, "%s: regulator enable failed, %d\n",
			__func__, ret);
		return ret;
	}

	return phy->cfg->ops.enable(phy, is_dual_panel, bit_rate, esc_rate);
}

int msm_dsi_phy_disable(struct msm_dsi_phy *phy)
{
	if (!phy || !phy->cfg->ops.disable)
		return -EINVAL;

	phy->cfg->ops.disable(phy);
	dsi_phy_regulator_disable(phy);

	return 0;
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

struct msm_dsi_pll *msm_dsi_phy_get_pll(struct msm_dsi_phy *phy)
{
	if (!phy)
		return NULL;

	return phy->pll;
}


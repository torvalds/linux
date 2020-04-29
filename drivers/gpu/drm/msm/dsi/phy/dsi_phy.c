// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/platform_device.h>

#include "dsi_phy.h"

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

static void dsi_dphy_timing_calc_clk_zero(struct msm_dsi_dphy_timing *timing,
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

int msm_dsi_dphy_timing_calc(struct msm_dsi_dphy_timing *timing,
			     struct msm_dsi_phy_clk_request *clk_req)
{
	const unsigned long bit_rate = clk_req->bitclk_rate;
	const unsigned long esc_rate = clk_req->escclk_rate;
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
	timing->shared_timings.clk_post = linear_inter(tmax, tmin, pcnt2, 0,
						       false);
	tmax = 63;
	temp = ((timing->clk_prepare >> 1) + 1) * 2 * ui;
	temp += ((timing->clk_zero >> 1) + 1) * 2 * ui;
	temp += 8 * ui + lpx;
	tmin = S_DIV_ROUND_UP(temp, 8 * ui) - 1;
	if (tmin > tmax) {
		temp = linear_inter(2 * tmax, tmin, pcnt2, 0, false);
		timing->shared_timings.clk_pre = temp >> 1;
		timing->shared_timings.clk_pre_inc_by_2 = true;
	} else {
		timing->shared_timings.clk_pre =
				linear_inter(tmax, tmin, pcnt2, 0, false);
		timing->shared_timings.clk_pre_inc_by_2 = false;
	}

	timing->ta_go = 3;
	timing->ta_sure = 0;
	timing->ta_get = 4;

	DBG("PHY timings: %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d",
		timing->shared_timings.clk_pre, timing->shared_timings.clk_post,
		timing->shared_timings.clk_pre_inc_by_2, timing->clk_zero,
		timing->clk_trail, timing->clk_prepare, timing->hs_exit,
		timing->hs_zero, timing->hs_prepare, timing->hs_trail,
		timing->hs_rqst);

	return 0;
}

int msm_dsi_dphy_timing_calc_v2(struct msm_dsi_dphy_timing *timing,
				struct msm_dsi_phy_clk_request *clk_req)
{
	const unsigned long bit_rate = clk_req->bitclk_rate;
	const unsigned long esc_rate = clk_req->escclk_rate;
	s32 ui, ui_x8;
	s32 tmax, tmin;
	s32 pcnt0 = 50;
	s32 pcnt1 = 50;
	s32 pcnt2 = 10;
	s32 pcnt3 = 30;
	s32 pcnt4 = 10;
	s32 pcnt5 = 2;
	s32 coeff = 1000; /* Precision, should avoid overflow */
	s32 hb_en, hb_en_ckln, pd_ckln, pd;
	s32 val, val_ckln;
	s32 temp;

	if (!bit_rate || !esc_rate)
		return -EINVAL;

	timing->hs_halfbyte_en = 0;
	hb_en = 0;
	timing->hs_halfbyte_en_ckln = 0;
	hb_en_ckln = 0;
	timing->hs_prep_dly_ckln = (bit_rate > 100000000) ? 0 : 3;
	pd_ckln = timing->hs_prep_dly_ckln;
	timing->hs_prep_dly = (bit_rate > 120000000) ? 0 : 1;
	pd = timing->hs_prep_dly;

	val = (hb_en << 2) + (pd << 1);
	val_ckln = (hb_en_ckln << 2) + (pd_ckln << 1);

	ui = mult_frac(NSEC_PER_MSEC, coeff, bit_rate / 1000);
	ui_x8 = ui << 3;

	temp = S_DIV_ROUND_UP(38 * coeff - val_ckln * ui, ui_x8);
	tmin = max_t(s32, temp, 0);
	temp = (95 * coeff - val_ckln * ui) / ui_x8;
	tmax = max_t(s32, temp, 0);
	timing->clk_prepare = linear_inter(tmax, tmin, pcnt0, 0, false);

	temp = 300 * coeff - ((timing->clk_prepare << 3) + val_ckln) * ui;
	tmin = S_DIV_ROUND_UP(temp - 11 * ui, ui_x8) - 3;
	tmax = (tmin > 255) ? 511 : 255;
	timing->clk_zero = linear_inter(tmax, tmin, pcnt5, 0, false);

	tmin = DIV_ROUND_UP(60 * coeff + 3 * ui, ui_x8);
	temp = 105 * coeff + 12 * ui - 20 * coeff;
	tmax = (temp + 3 * ui) / ui_x8;
	timing->clk_trail = linear_inter(tmax, tmin, pcnt3, 0, false);

	temp = S_DIV_ROUND_UP(40 * coeff + 4 * ui - val * ui, ui_x8);
	tmin = max_t(s32, temp, 0);
	temp = (85 * coeff + 6 * ui - val * ui) / ui_x8;
	tmax = max_t(s32, temp, 0);
	timing->hs_prepare = linear_inter(tmax, tmin, pcnt1, 0, false);

	temp = 145 * coeff + 10 * ui - ((timing->hs_prepare << 3) + val) * ui;
	tmin = S_DIV_ROUND_UP(temp - 11 * ui, ui_x8) - 3;
	tmax = 255;
	timing->hs_zero = linear_inter(tmax, tmin, pcnt4, 0, false);

	tmin = DIV_ROUND_UP(60 * coeff + 4 * ui + 3 * ui, ui_x8);
	temp = 105 * coeff + 12 * ui - 20 * coeff;
	tmax = (temp + 3 * ui) / ui_x8;
	timing->hs_trail = linear_inter(tmax, tmin, pcnt3, 0, false);

	temp = 50 * coeff + ((hb_en << 2) - 8) * ui;
	timing->hs_rqst = S_DIV_ROUND_UP(temp, ui_x8);

	tmin = DIV_ROUND_UP(100 * coeff, ui_x8) - 1;
	tmax = 255;
	timing->hs_exit = linear_inter(tmax, tmin, pcnt2, 0, false);

	temp = 50 * coeff + ((hb_en_ckln << 2) - 8) * ui;
	timing->hs_rqst_ckln = S_DIV_ROUND_UP(temp, ui_x8);

	temp = 60 * coeff + 52 * ui - 43 * ui;
	tmin = DIV_ROUND_UP(temp, ui_x8) - 1;
	tmax = 63;
	timing->shared_timings.clk_post =
				linear_inter(tmax, tmin, pcnt2, 0, false);

	temp = 8 * ui + ((timing->clk_prepare << 3) + val_ckln) * ui;
	temp += (((timing->clk_zero + 3) << 3) + 11 - (pd_ckln << 1)) * ui;
	temp += hb_en_ckln ? (((timing->hs_rqst_ckln << 3) + 4) * ui) :
				(((timing->hs_rqst_ckln << 3) + 8) * ui);
	tmin = S_DIV_ROUND_UP(temp, ui_x8) - 1;
	tmax = 63;
	if (tmin > tmax) {
		temp = linear_inter(tmax << 1, tmin, pcnt2, 0, false);
		timing->shared_timings.clk_pre = temp >> 1;
		timing->shared_timings.clk_pre_inc_by_2 = 1;
	} else {
		timing->shared_timings.clk_pre =
				linear_inter(tmax, tmin, pcnt2, 0, false);
		timing->shared_timings.clk_pre_inc_by_2 = 0;
	}

	timing->ta_go = 3;
	timing->ta_sure = 0;
	timing->ta_get = 4;

	DBG("%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d",
	    timing->shared_timings.clk_pre, timing->shared_timings.clk_post,
	    timing->shared_timings.clk_pre_inc_by_2, timing->clk_zero,
	    timing->clk_trail, timing->clk_prepare, timing->hs_exit,
	    timing->hs_zero, timing->hs_prepare, timing->hs_trail,
	    timing->hs_rqst, timing->hs_rqst_ckln, timing->hs_halfbyte_en,
	    timing->hs_halfbyte_en_ckln, timing->hs_prep_dly,
	    timing->hs_prep_dly_ckln);

	return 0;
}

int msm_dsi_dphy_timing_calc_v3(struct msm_dsi_dphy_timing *timing,
	struct msm_dsi_phy_clk_request *clk_req)
{
	const unsigned long bit_rate = clk_req->bitclk_rate;
	const unsigned long esc_rate = clk_req->escclk_rate;
	s32 ui, ui_x8;
	s32 tmax, tmin;
	s32 pcnt0 = 50;
	s32 pcnt1 = 50;
	s32 pcnt2 = 10;
	s32 pcnt3 = 30;
	s32 pcnt4 = 10;
	s32 pcnt5 = 2;
	s32 coeff = 1000; /* Precision, should avoid overflow */
	s32 hb_en, hb_en_ckln;
	s32 temp;

	if (!bit_rate || !esc_rate)
		return -EINVAL;

	timing->hs_halfbyte_en = 0;
	hb_en = 0;
	timing->hs_halfbyte_en_ckln = 0;
	hb_en_ckln = 0;

	ui = mult_frac(NSEC_PER_MSEC, coeff, bit_rate / 1000);
	ui_x8 = ui << 3;

	temp = S_DIV_ROUND_UP(38 * coeff, ui_x8);
	tmin = max_t(s32, temp, 0);
	temp = (95 * coeff) / ui_x8;
	tmax = max_t(s32, temp, 0);
	timing->clk_prepare = linear_inter(tmax, tmin, pcnt0, 0, false);

	temp = 300 * coeff - (timing->clk_prepare << 3) * ui;
	tmin = S_DIV_ROUND_UP(temp, ui_x8) - 1;
	tmax = (tmin > 255) ? 511 : 255;
	timing->clk_zero = linear_inter(tmax, tmin, pcnt5, 0, false);

	tmin = DIV_ROUND_UP(60 * coeff + 3 * ui, ui_x8);
	temp = 105 * coeff + 12 * ui - 20 * coeff;
	tmax = (temp + 3 * ui) / ui_x8;
	timing->clk_trail = linear_inter(tmax, tmin, pcnt3, 0, false);

	temp = S_DIV_ROUND_UP(40 * coeff + 4 * ui, ui_x8);
	tmin = max_t(s32, temp, 0);
	temp = (85 * coeff + 6 * ui) / ui_x8;
	tmax = max_t(s32, temp, 0);
	timing->hs_prepare = linear_inter(tmax, tmin, pcnt1, 0, false);

	temp = 145 * coeff + 10 * ui - (timing->hs_prepare << 3) * ui;
	tmin = S_DIV_ROUND_UP(temp, ui_x8) - 1;
	tmax = 255;
	timing->hs_zero = linear_inter(tmax, tmin, pcnt4, 0, false);

	tmin = DIV_ROUND_UP(60 * coeff + 4 * ui, ui_x8) - 1;
	temp = 105 * coeff + 12 * ui - 20 * coeff;
	tmax = (temp / ui_x8) - 1;
	timing->hs_trail = linear_inter(tmax, tmin, pcnt3, 0, false);

	temp = 50 * coeff + ((hb_en << 2) - 8) * ui;
	timing->hs_rqst = S_DIV_ROUND_UP(temp, ui_x8);

	tmin = DIV_ROUND_UP(100 * coeff, ui_x8) - 1;
	tmax = 255;
	timing->hs_exit = linear_inter(tmax, tmin, pcnt2, 0, false);

	temp = 50 * coeff + ((hb_en_ckln << 2) - 8) * ui;
	timing->hs_rqst_ckln = S_DIV_ROUND_UP(temp, ui_x8);

	temp = 60 * coeff + 52 * ui - 43 * ui;
	tmin = DIV_ROUND_UP(temp, ui_x8) - 1;
	tmax = 63;
	timing->shared_timings.clk_post =
		linear_inter(tmax, tmin, pcnt2, 0, false);

	temp = 8 * ui + (timing->clk_prepare << 3) * ui;
	temp += (((timing->clk_zero + 3) << 3) + 11) * ui;
	temp += hb_en_ckln ? (((timing->hs_rqst_ckln << 3) + 4) * ui) :
		(((timing->hs_rqst_ckln << 3) + 8) * ui);
	tmin = S_DIV_ROUND_UP(temp, ui_x8) - 1;
	tmax = 63;
	if (tmin > tmax) {
		temp = linear_inter(tmax << 1, tmin, pcnt2, 0, false);
		timing->shared_timings.clk_pre = temp >> 1;
		timing->shared_timings.clk_pre_inc_by_2 = 1;
	} else {
		timing->shared_timings.clk_pre =
			linear_inter(tmax, tmin, pcnt2, 0, false);
			timing->shared_timings.clk_pre_inc_by_2 = 0;
	}

	timing->ta_go = 3;
	timing->ta_sure = 0;
	timing->ta_get = 4;

	DBG("%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d",
		timing->shared_timings.clk_pre, timing->shared_timings.clk_post,
		timing->shared_timings.clk_pre_inc_by_2, timing->clk_zero,
		timing->clk_trail, timing->clk_prepare, timing->hs_exit,
		timing->hs_zero, timing->hs_prepare, timing->hs_trail,
		timing->hs_rqst, timing->hs_rqst_ckln, timing->hs_halfbyte_en,
		timing->hs_halfbyte_en_ckln, timing->hs_prep_dly,
		timing->hs_prep_dly_ckln);

	return 0;
}

void msm_dsi_phy_set_src_pll(struct msm_dsi_phy *phy, int pll_id, u32 reg,
				u32 bit_mask)
{
	int phy_id = phy->id;
	u32 val;

	if ((phy_id >= DSI_MAX) || (pll_id >= DSI_MAX))
		return;

	val = dsi_phy_read(phy->base + reg);

	if (phy->cfg->src_pll_truthtable[phy_id][pll_id])
		dsi_phy_write(phy->base + reg, val | bit_mask);
	else
		dsi_phy_write(phy->base + reg, val & (~bit_mask));
}

static int dsi_phy_regulator_init(struct msm_dsi_phy *phy)
{
	struct regulator_bulk_data *s = phy->supplies;
	const struct dsi_reg_entry *regs = phy->cfg->reg_cfg.regs;
	struct device *dev = &phy->pdev->dev;
	int num = phy->cfg->reg_cfg.num;
	int i, ret;

	for (i = 0; i < num; i++)
		s[i].supply = regs[i].name;

	ret = devm_regulator_bulk_get(dev, num, s);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER) {
			DRM_DEV_ERROR(dev,
				      "%s: failed to init regulator, ret=%d\n",
				      __func__, ret);
		}

		return ret;
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
			regulator_set_load(s[i].consumer, regs[i].disable_load);

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
				DRM_DEV_ERROR(dev,
					"regulator %d set op mode failed, %d\n",
					i, ret);
				goto fail;
			}
		}
	}

	ret = regulator_bulk_enable(num, s);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "regulator enable failed, %d\n", ret);
		goto fail;
	}

	return 0;

fail:
	for (i--; i >= 0; i--)
		regulator_set_load(s[i].consumer, regs[i].disable_load);
	return ret;
}

static int dsi_phy_enable_resource(struct msm_dsi_phy *phy)
{
	struct device *dev = &phy->pdev->dev;
	int ret;

	pm_runtime_get_sync(dev);

	ret = clk_prepare_enable(phy->ahb_clk);
	if (ret) {
		DRM_DEV_ERROR(dev, "%s: can't enable ahb clk, %d\n", __func__, ret);
		pm_runtime_put_sync(dev);
	}

	return ret;
}

static void dsi_phy_disable_resource(struct msm_dsi_phy *phy)
{
	clk_disable_unprepare(phy->ahb_clk);
	pm_runtime_put_autosuspend(&phy->pdev->dev);
}

static const struct of_device_id dsi_phy_dt_match[] = {
#ifdef CONFIG_DRM_MSM_DSI_28NM_PHY
	{ .compatible = "qcom,dsi-phy-28nm-hpm",
	  .data = &dsi_phy_28nm_hpm_cfgs },
	{ .compatible = "qcom,dsi-phy-28nm-hpm-fam-b",
	  .data = &dsi_phy_28nm_hpm_famb_cfgs },
	{ .compatible = "qcom,dsi-phy-28nm-lp",
	  .data = &dsi_phy_28nm_lp_cfgs },
#endif
#ifdef CONFIG_DRM_MSM_DSI_20NM_PHY
	{ .compatible = "qcom,dsi-phy-20nm",
	  .data = &dsi_phy_20nm_cfgs },
#endif
#ifdef CONFIG_DRM_MSM_DSI_28NM_8960_PHY
	{ .compatible = "qcom,dsi-phy-28nm-8960",
	  .data = &dsi_phy_28nm_8960_cfgs },
#endif
#ifdef CONFIG_DRM_MSM_DSI_14NM_PHY
	{ .compatible = "qcom,dsi-phy-14nm",
	  .data = &dsi_phy_14nm_cfgs },
#endif
#ifdef CONFIG_DRM_MSM_DSI_10NM_PHY
	{ .compatible = "qcom,dsi-phy-10nm",
	  .data = &dsi_phy_10nm_cfgs },
	{ .compatible = "qcom,dsi-phy-10nm-8998",
	  .data = &dsi_phy_10nm_8998_cfgs },
#endif
	{}
};

/*
 * Currently, we only support one SoC for each PHY type. When we have multiple
 * SoCs for the same PHY, we can try to make the index searching a bit more
 * clever.
 */
static int dsi_phy_get_id(struct msm_dsi_phy *phy)
{
	struct platform_device *pdev = phy->pdev;
	const struct msm_dsi_phy_cfg *cfg = phy->cfg;
	struct resource *res;
	int i;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dsi_phy");
	if (!res)
		return -EINVAL;

	for (i = 0; i < cfg->num_dsi_phy; i++) {
		if (cfg->io_start[i] == res->start)
			return i;
	}

	return -EINVAL;
}

int msm_dsi_phy_init_common(struct msm_dsi_phy *phy)
{
	struct platform_device *pdev = phy->pdev;
	int ret = 0;

	phy->reg_base = msm_ioremap(pdev, "dsi_phy_regulator",
				"DSI_PHY_REG");
	if (IS_ERR(phy->reg_base)) {
		DRM_DEV_ERROR(&pdev->dev, "%s: failed to map phy regulator base\n",
			__func__);
		ret = -ENOMEM;
		goto fail;
	}

fail:
	return ret;
}

static int dsi_phy_driver_probe(struct platform_device *pdev)
{
	struct msm_dsi_phy *phy;
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	int ret;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	match = of_match_node(dsi_phy_dt_match, dev->of_node);
	if (!match)
		return -ENODEV;

	phy->cfg = match->data;
	phy->pdev = pdev;

	phy->id = dsi_phy_get_id(phy);
	if (phy->id < 0) {
		ret = phy->id;
		DRM_DEV_ERROR(dev, "%s: couldn't identify PHY index, %d\n",
			__func__, ret);
		goto fail;
	}

	phy->regulator_ldo_mode = of_property_read_bool(dev->of_node,
				"qcom,dsi-phy-regulator-ldo-mode");

	phy->base = msm_ioremap(pdev, "dsi_phy", "DSI_PHY");
	if (IS_ERR(phy->base)) {
		DRM_DEV_ERROR(dev, "%s: failed to map phy base\n", __func__);
		ret = -ENOMEM;
		goto fail;
	}

	ret = dsi_phy_regulator_init(phy);
	if (ret)
		goto fail;

	phy->ahb_clk = msm_clk_get(pdev, "iface");
	if (IS_ERR(phy->ahb_clk)) {
		DRM_DEV_ERROR(dev, "%s: Unable to get ahb clk\n", __func__);
		ret = PTR_ERR(phy->ahb_clk);
		goto fail;
	}

	if (phy->cfg->ops.init) {
		ret = phy->cfg->ops.init(phy);
		if (ret)
			goto fail;
	}

	/* PLL init will call into clk_register which requires
	 * register access, so we need to enable power and ahb clock.
	 */
	ret = dsi_phy_enable_resource(phy);
	if (ret)
		goto fail;

	phy->pll = msm_dsi_pll_init(pdev, phy->cfg->type, phy->id);
	if (IS_ERR_OR_NULL(phy->pll)) {
		DRM_DEV_INFO(dev,
			"%s: pll init failed: %ld, need separate pll clk driver\n",
			__func__, PTR_ERR(phy->pll));
		phy->pll = NULL;
	}

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

int msm_dsi_phy_enable(struct msm_dsi_phy *phy, int src_pll_id,
			struct msm_dsi_phy_clk_request *clk_req)
{
	struct device *dev = &phy->pdev->dev;
	int ret;

	if (!phy || !phy->cfg->ops.enable)
		return -EINVAL;

	ret = dsi_phy_enable_resource(phy);
	if (ret) {
		DRM_DEV_ERROR(dev, "%s: resource enable failed, %d\n",
			__func__, ret);
		goto res_en_fail;
	}

	ret = dsi_phy_regulator_enable(phy);
	if (ret) {
		DRM_DEV_ERROR(dev, "%s: regulator enable failed, %d\n",
			__func__, ret);
		goto reg_en_fail;
	}

	ret = phy->cfg->ops.enable(phy, src_pll_id, clk_req);
	if (ret) {
		DRM_DEV_ERROR(dev, "%s: phy enable failed, %d\n", __func__, ret);
		goto phy_en_fail;
	}

	/*
	 * Resetting DSI PHY silently changes its PLL registers to reset status,
	 * which will confuse clock driver and result in wrong output rate of
	 * link clocks. Restore PLL status if its PLL is being used as clock
	 * source.
	 */
	if (phy->usecase != MSM_DSI_PHY_SLAVE) {
		ret = msm_dsi_pll_restore_state(phy->pll);
		if (ret) {
			DRM_DEV_ERROR(dev, "%s: failed to restore pll state, %d\n",
				__func__, ret);
			goto pll_restor_fail;
		}
	}

	return 0;

pll_restor_fail:
	if (phy->cfg->ops.disable)
		phy->cfg->ops.disable(phy);
phy_en_fail:
	dsi_phy_regulator_disable(phy);
reg_en_fail:
	dsi_phy_disable_resource(phy);
res_en_fail:
	return ret;
}

void msm_dsi_phy_disable(struct msm_dsi_phy *phy)
{
	if (!phy || !phy->cfg->ops.disable)
		return;

	phy->cfg->ops.disable(phy);

	dsi_phy_regulator_disable(phy);
	dsi_phy_disable_resource(phy);
}

void msm_dsi_phy_get_shared_timings(struct msm_dsi_phy *phy,
			struct msm_dsi_phy_shared_timings *shared_timings)
{
	memcpy(shared_timings, &phy->timing.shared_timings,
	       sizeof(*shared_timings));
}

struct msm_dsi_pll *msm_dsi_phy_get_pll(struct msm_dsi_phy *phy)
{
	if (!phy)
		return NULL;

	return phy->pll;
}

void msm_dsi_phy_set_usecase(struct msm_dsi_phy *phy,
			     enum msm_dsi_phy_usecase uc)
{
	if (phy)
		phy->usecase = uc;
}

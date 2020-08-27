// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 */

/*
 * Display Port PLL driver block diagram for branch clocks
 *
 *              +------------------------------+
 *              |         DP_VCO_CLK           |
 *              |                              |
 *              |    +-------------------+     |
 *              |    |   (DP PLL/VCO)    |     |
 *              |    +---------+---------+     |
 *              |              v               |
 *              |   +----------+-----------+   |
 *              |   | hsclk_divsel_clk_src |   |
 *              |   +----------+-----------+   |
 *              +------------------------------+
 *                              |
 *          +---------<---------v------------>----------+
 *          |                                           |
 * +--------v---------+                                 |
 * |    dp_phy_pll    |                                 |
 * |     link_clk     |                                 |
 * +--------+---------+                                 |
 *          |                                           |
 *          |                                           |
 *          v                                           v
 * Input to DISPCC block                                |
 * for link clk, crypto clk                             |
 * and interface clock                                  |
 *                                                      |
 *                                                      |
 *      +--------<------------+-----------------+---<---+
 *      |                     |                 |
 * +----v---------+  +--------v-----+  +--------v------+
 * | vco_divided  |  | vco_divided  |  | vco_divided   |
 * |    _clk_src  |  |    _clk_src  |  |    _clk_src   |
 * |              |  |              |  |               |
 * |divsel_six    |  |  divsel_two  |  |  divsel_four  |
 * +-------+------+  +-----+--------+  +--------+------+
 *         |                 |                  |
 *         v---->----------v-------------<------v
 *                         |
 *              +----------+---------+
 *              |   dp_phy_pll_vco   |
 *              |       div_clk      |
 *              +---------+----------+
 *                        |
 *                        v
 *              Input to DISPCC block
 *              for DP pixel clock
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/regmap.h>
#include <linux/iopoll.h>

#include "dp_hpd.h"
#include "dp_pll.h"
#include "dp_pll_private.h"

#define NUM_PROVIDED_CLKS		2

#define DP_LINK_CLK_SRC			0
#define DP_PIXEL_CLK_SRC		1


static int dp_vco_set_rate_10nm(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate);

static unsigned long dp_vco_recalc_rate_10nm(struct clk_hw *hw,
				unsigned long parent_rate);

static long dp_vco_round_rate_10nm(struct clk_hw *hw,
				unsigned long rate, unsigned long *parent_rate);

static int dp_vco_prepare_10nm(struct clk_hw *hw);
static void dp_vco_unprepare_10nm(struct clk_hw *hw);

static struct dp_pll_db *dp_pdb;

static const struct clk_ops dp_10nm_vco_clk_ops = {
	.recalc_rate = dp_vco_recalc_rate_10nm,
	.set_rate = dp_vco_set_rate_10nm,
	.round_rate = dp_vco_round_rate_10nm,
	.prepare = dp_vco_prepare_10nm,
	.unprepare = dp_vco_unprepare_10nm,
};

struct dp_pll_10nm_pclksel {
	struct clk_hw hw;

	/* divider params */
	u8 shift;
	u8 width;
	u8 flags; /* same flags as used by clk_divider struct */

	struct dp_pll_db *pll;
};

#define to_pll_10nm_pclksel(_hw) \
	container_of(_hw, struct dp_pll_10nm_pclksel, hw)

static const struct clk_parent_data disp_cc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "dp_phy_pll_link_clk", .name = "dp_phy_pll_link_clk" },
	{ .fw_name = "core_bi_pll_test_se", .name = "core_bi_pll_test_se" },
};

static struct dp_pll_vco_clk dp_vco_clk = {
	.min_rate = DP_VCO_HSCLK_RATE_1620MHZDIV1000,
	.max_rate = DP_VCO_HSCLK_RATE_8100MHZDIV1000,
};

static int dp_pll_mux_set_parent_10nm(struct clk_hw *hw, u8 val)
{
	struct dp_pll_10nm_pclksel *pclksel = to_pll_10nm_pclksel(hw);
	struct dp_pll_db *dp_res = pclksel->pll;
	struct dp_io_pll *pll_io = &dp_res->base->pll_io;
	u32 auxclk_div;

	auxclk_div = PLL_REG_R(pll_io->phy_base, REG_DP_PHY_VCO_DIV);
	auxclk_div &= ~0x03;

	if (val == 0)
		auxclk_div |= 1;
	else if (val == 1)
		auxclk_div |= 2;
	else if (val == 2)
		auxclk_div |= 0;

	PLL_REG_W(pll_io->phy_base,
			REG_DP_PHY_VCO_DIV, auxclk_div);
	DRM_DEBUG_DP("%s: mux=%d auxclk_div=%x\n", __func__, val, auxclk_div);

	return 0;
}

static u8 dp_pll_mux_get_parent_10nm(struct clk_hw *hw)
{
	u32 auxclk_div = 0;
	struct dp_pll_10nm_pclksel *pclksel = to_pll_10nm_pclksel(hw);
	struct dp_pll_db *dp_res = pclksel->pll;
	struct dp_io_pll *pll_io = &dp_res->base->pll_io;
	u8 val = 0;

	auxclk_div = PLL_REG_R(pll_io->phy_base, REG_DP_PHY_VCO_DIV);
	auxclk_div &= 0x03;

	if (auxclk_div == 1) /* Default divider */
		val = 0;
	else if (auxclk_div == 2)
		val = 1;
	else if (auxclk_div == 0)
		val = 2;

	DRM_DEBUG_DP("%s: auxclk_div=%d, val=%d\n", __func__, auxclk_div, val);

	return val;
}

static int dp_pll_clk_mux_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	unsigned long rate = 0;

	rate = clk_get_rate(hw->clk);

	if (rate <= 0) {
		DRM_ERROR("Rate is not set properly\n");
		return -EINVAL;
	}

	req->rate = rate;

	DRM_DEBUG_DP("%s: rate=%ld\n", __func__, req->rate);
	return 0;
}

static unsigned long dp_pll_mux_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct clk_hw *div_clk_hw = NULL, *vco_clk_hw = NULL;
	struct dp_pll_vco_clk *vco;

	div_clk_hw = clk_hw_get_parent(hw);
	if (!div_clk_hw)
		return 0;

	vco_clk_hw = clk_hw_get_parent(div_clk_hw);
	if (!vco_clk_hw)
		return 0;

	vco = to_dp_vco_hw(vco_clk_hw);
	if (!vco)
		return 0;

	if (vco->rate == DP_VCO_HSCLK_RATE_8100MHZDIV1000)
		return (vco->rate / 6);
	else if (vco->rate == DP_VCO_HSCLK_RATE_5400MHZDIV1000)
		return (vco->rate / 4);
	else
		return (vco->rate / 2);
}

static int dp_pll_10nm_get_provider(struct msm_dp_pll *pll,
				     struct clk **link_clk_provider,
				     struct clk **pixel_clk_provider)
{
	struct clk_hw_onecell_data *hw_data = pll->hw_data;

	if (link_clk_provider)
		*link_clk_provider = hw_data->hws[DP_LINK_CLK_SRC]->clk;
	if (pixel_clk_provider)
		*pixel_clk_provider = hw_data->hws[DP_PIXEL_CLK_SRC]->clk;

	return 0;
}

static const struct clk_ops dp_10nm_pclksel_clk_ops = {
	.get_parent = dp_pll_mux_get_parent_10nm,
	.set_parent = dp_pll_mux_set_parent_10nm,
	.recalc_rate = dp_pll_mux_recalc_rate,
	.determine_rate = dp_pll_clk_mux_determine_rate,
};

static struct clk_hw *dp_pll_10nm_pixel_clk_sel(struct dp_pll_db *pll_10nm)
{
	struct device *dev = &pll_10nm->pdev->dev;
	struct dp_pll_10nm_pclksel *pll_pclksel;
	struct clk_init_data pclksel_init = {
		.parent_data = disp_cc_parent_data_0,
		.num_parents = 3,
		.name = "dp_phy_pll_vco_div_clk",
		.ops = &dp_10nm_pclksel_clk_ops,
	};
	int ret;

	pll_pclksel = devm_kzalloc(dev, sizeof(*pll_pclksel), GFP_KERNEL);
	if (!pll_pclksel)
		return ERR_PTR(-ENOMEM);

	pll_pclksel->pll = pll_10nm;
	pll_pclksel->shift = 0;
	pll_pclksel->width = 4;
	pll_pclksel->hw.init = &pclksel_init;

	ret = clk_hw_register(dev, &pll_pclksel->hw);
	if (ret)
		return ERR_PTR(ret);

	return &pll_pclksel->hw;
}

static void dp_pll_10nm_unregister(struct dp_pll_db *pll_10nm)
{
	int i = 0;
	struct clk_hw **hws;

	hws = pll_10nm->hws;

	for (i = 0; i < pll_10nm->num_hws; i++) {
		if (pll_10nm->fixed_factor_clk[i] == true)
			clk_hw_unregister_fixed_factor(hws[i]);
		else
			clk_hw_unregister(hws[i]);
	}
}

static int dp_pll_10nm_register(struct dp_pll_db *pll_10nm)
{
	struct clk_hw_onecell_data *hw_data;
	int ret = 0;
	struct clk_hw *hw;

	struct msm_dp_pll *pll = pll_10nm->base;
	struct device *dev = &pll_10nm->pdev->dev;
	struct clk_hw **hws = pll_10nm->hws;
	int num = 0;

	struct clk_init_data vco_init = {
		.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
		},
		.num_parents = 1,
		.name = "dp_vco_clk",
		.ops = &dp_10nm_vco_clk_ops,
	};

	if (!dev) {
		DRM_ERROR("DP dev node not available\n");
		return 0;
	}

	DRM_DEBUG_DP("DP->id = %d", pll_10nm->id);

	hw_data = devm_kzalloc(dev, sizeof(*hw_data) +
			       NUM_PROVIDED_CLKS * sizeof(struct clk_hw *),
			       GFP_KERNEL);
	if (!hw_data)
		return -ENOMEM;

	dp_vco_clk.hw.init = &vco_init;
	ret = clk_hw_register(dev, &dp_vco_clk.hw);
	if (ret)
		return ret;
	hws[num++] = &dp_vco_clk.hw;

	hw = clk_hw_register_fixed_factor(dev, "dp_phy_pll_link_clk",
				"dp_vco_clk", CLK_SET_RATE_PARENT, 1, 10);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	pll_10nm->fixed_factor_clk[num] = true;
	hws[num++] = hw;
	hw_data->hws[DP_LINK_CLK_SRC] = hw;

	hw = clk_hw_register_fixed_factor(dev, "dp_vco_divsel_two_clk_src",
					"dp_vco_clk",  0, 1, 2);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	pll_10nm->fixed_factor_clk[num] = true;
	hws[num++] = hw;

	hw = clk_hw_register_fixed_factor(dev, "dp_vco_divsel_four_clk_src",
					 "dp_vco_clk", 0, 1, 4);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	pll_10nm->fixed_factor_clk[num] = true;
	hws[num++] = hw;

	hw = clk_hw_register_fixed_factor(dev, "dp_vco_divsel_six_clk_src",
					 "dp_vco_clk", 0, 1, 6);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	pll_10nm->fixed_factor_clk[num] = true;
	hws[num++] = hw;

	hw = dp_pll_10nm_pixel_clk_sel(pll_10nm);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	hws[num++] = hw;
	hw_data->hws[DP_PIXEL_CLK_SRC] = hw;

	pll_10nm->num_hws = num;

	hw_data->num = NUM_PROVIDED_CLKS;
	pll->hw_data = hw_data;

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
				     pll->hw_data);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to register clk provider: %d\n",
				ret);
		return ret;
	}

	return ret;
}

void msm_dp_pll_10nm_deinit(struct msm_dp_pll *pll)
{
	dp_pll_10nm_unregister(pll->priv);
}

int msm_dp_pll_10nm_init(struct msm_dp_pll *pll, int id)
{
	struct dp_pll_db *dp_10nm_pll;
	struct platform_device *pdev = pll->pdev;
	int ret;

	dp_10nm_pll = devm_kzalloc(&pdev->dev,
					sizeof(*dp_10nm_pll), GFP_KERNEL);
	if (!dp_10nm_pll)
		return -ENOMEM;

	DRM_DEBUG_DP("DP PLL%d", id);

	dp_10nm_pll->base = pll;
	dp_10nm_pll->pdev = pll->pdev;
	dp_10nm_pll->id = id;
	dp_pdb = dp_10nm_pll;
	pll->priv = (void *)dp_10nm_pll;
	dp_vco_clk.priv = pll;
	dp_10nm_pll->index = 0;

	ret = dp_pll_10nm_register(dp_10nm_pll);
	if (ret) {
		DRM_DEV_ERROR(&pdev->dev, "failed to register PLL: %d\n", ret);
		return ret;
	}

	pll->get_provider = dp_pll_10nm_get_provider;

	return ret;
}

static int dp_vco_pll_init_db_10nm(struct msm_dp_pll *pll,
		unsigned long rate)
{
	u32 spare_value = 0;
	struct dp_io_pll *pll_io;
	struct dp_pll_db *dp_res = to_dp_pll_db(pll);

	pll_io = &pll->pll_io;
	spare_value = PLL_REG_R(pll_io->phy_base, REG_DP_PHY_SPARE0);
	dp_res->lane_cnt = spare_value & 0x0F;
	dp_res->orientation = (spare_value & 0xF0) >> 4;

	DRM_DEBUG_DP("%s: spare_value=0x%x, ln_cnt=0x%x, orientation=0x%x\n",
			__func__, spare_value, dp_res->lane_cnt,
			dp_res->orientation);

	switch (rate) {
	case DP_VCO_HSCLK_RATE_1620MHZDIV1000:
		DRM_DEBUG_DP("%s: VCO rate: %ld\n", __func__,
				DP_VCO_RATE_9720MHZDIV1000);
		dp_res->hsclk_sel = 0x0c;
		dp_res->dec_start_mode0 = 0x69;
		dp_res->div_frac_start1_mode0 = 0x00;
		dp_res->div_frac_start2_mode0 = 0x80;
		dp_res->div_frac_start3_mode0 = 0x07;
		dp_res->integloop_gain0_mode0 = 0x3f;
		dp_res->integloop_gain1_mode0 = 0x00;
		dp_res->vco_tune_map = 0x00;
		dp_res->lock_cmp1_mode0 = 0x6f;
		dp_res->lock_cmp2_mode0 = 0x08;
		dp_res->lock_cmp3_mode0 = 0x00;
		dp_res->phy_vco_div = 0x1;
		dp_res->lock_cmp_en = 0x00;
		break;
	case DP_VCO_HSCLK_RATE_2700MHZDIV1000:
		DRM_DEBUG_DP("%s: VCO rate: %ld\n", __func__,
				DP_VCO_RATE_10800MHZDIV1000);
		dp_res->hsclk_sel = 0x04;
		dp_res->dec_start_mode0 = 0x69;
		dp_res->div_frac_start1_mode0 = 0x00;
		dp_res->div_frac_start2_mode0 = 0x80;
		dp_res->div_frac_start3_mode0 = 0x07;
		dp_res->integloop_gain0_mode0 = 0x3f;
		dp_res->integloop_gain1_mode0 = 0x00;
		dp_res->vco_tune_map = 0x00;
		dp_res->lock_cmp1_mode0 = 0x0f;
		dp_res->lock_cmp2_mode0 = 0x0e;
		dp_res->lock_cmp3_mode0 = 0x00;
		dp_res->phy_vco_div = 0x1;
		dp_res->lock_cmp_en = 0x00;
		break;
	case DP_VCO_HSCLK_RATE_5400MHZDIV1000:
		DRM_DEBUG_DP("%s: VCO rate: %ld\n", __func__,
				DP_VCO_RATE_10800MHZDIV1000);
		dp_res->hsclk_sel = 0x00;
		dp_res->dec_start_mode0 = 0x8c;
		dp_res->div_frac_start1_mode0 = 0x00;
		dp_res->div_frac_start2_mode0 = 0x00;
		dp_res->div_frac_start3_mode0 = 0x0a;
		dp_res->integloop_gain0_mode0 = 0x3f;
		dp_res->integloop_gain1_mode0 = 0x00;
		dp_res->vco_tune_map = 0x00;
		dp_res->lock_cmp1_mode0 = 0x1f;
		dp_res->lock_cmp2_mode0 = 0x1c;
		dp_res->lock_cmp3_mode0 = 0x00;
		dp_res->phy_vco_div = 0x2;
		dp_res->lock_cmp_en = 0x00;
		break;
	case DP_VCO_HSCLK_RATE_8100MHZDIV1000:
		DRM_DEBUG_DP("%s: VCO rate: %ld\n", __func__,
				DP_VCO_RATE_8100MHZDIV1000);
		dp_res->hsclk_sel = 0x03;
		dp_res->dec_start_mode0 = 0x69;
		dp_res->div_frac_start1_mode0 = 0x00;
		dp_res->div_frac_start2_mode0 = 0x80;
		dp_res->div_frac_start3_mode0 = 0x07;
		dp_res->integloop_gain0_mode0 = 0x3f;
		dp_res->integloop_gain1_mode0 = 0x00;
		dp_res->vco_tune_map = 0x00;
		dp_res->lock_cmp1_mode0 = 0x2f;
		dp_res->lock_cmp2_mode0 = 0x2a;
		dp_res->lock_cmp3_mode0 = 0x00;
		dp_res->phy_vco_div = 0x0;
		dp_res->lock_cmp_en = 0x08;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int dp_config_vco_rate_10nm(struct dp_pll_vco_clk *vco,
		unsigned long rate)
{
	u32 res = 0;
	struct msm_dp_pll *pll = vco->priv;
	struct dp_io_pll *pll_io = &pll->pll_io;
	struct dp_pll_db *dp_res = to_dp_pll_db(pll);

	res = dp_vco_pll_init_db_10nm(pll, rate);
	if (res) {
		DRM_ERROR("VCO Init DB failed\n");
		return res;
	}

	if (dp_res->lane_cnt != 4) {
		if (dp_res->orientation == ORIENTATION_CC2)
			PLL_REG_W(pll_io->phy_base, REG_DP_PHY_PD_CTL, 0x6d);
		else
			PLL_REG_W(pll_io->phy_base, REG_DP_PHY_PD_CTL, 0x75);
	} else {
		PLL_REG_W(pll_io->phy_base, REG_DP_PHY_PD_CTL, 0x7d);
	}

	PLL_REG_W(pll_io->pll_base, QSERDES_COM_SVS_MODE_CLK_SEL, 0x01);
	PLL_REG_W(pll_io->pll_base, QSERDES_COM_SYSCLK_EN_SEL, 0x37);
	PLL_REG_W(pll_io->pll_base, QSERDES_COM_SYS_CLK_CTRL, 0x02);
	PLL_REG_W(pll_io->pll_base, QSERDES_COM_CLK_ENABLE1, 0x0e);
	PLL_REG_W(pll_io->pll_base, QSERDES_COM_SYSCLK_BUF_ENABLE, 0x06);
	PLL_REG_W(pll_io->pll_base, QSERDES_COM_CLK_SEL, 0x30);
	PLL_REG_W(pll_io->pll_base, QSERDES_COM_CMN_CONFIG, 0x02);

	/* Different for each clock rates */
	PLL_REG_W(pll_io->pll_base,
		QSERDES_COM_HSCLK_SEL, dp_res->hsclk_sel);
	PLL_REG_W(pll_io->pll_base,
		QSERDES_COM_DEC_START_MODE0, dp_res->dec_start_mode0);
	PLL_REG_W(pll_io->pll_base,
		QSERDES_COM_DIV_FRAC_START1_MODE0,
		dp_res->div_frac_start1_mode0);
	PLL_REG_W(pll_io->pll_base,
		QSERDES_COM_DIV_FRAC_START2_MODE0,
		dp_res->div_frac_start2_mode0);
	PLL_REG_W(pll_io->pll_base,
		QSERDES_COM_DIV_FRAC_START3_MODE0,
		dp_res->div_frac_start3_mode0);
	PLL_REG_W(pll_io->pll_base,
		QSERDES_COM_INTEGLOOP_GAIN0_MODE0,
		dp_res->integloop_gain0_mode0);
	PLL_REG_W(pll_io->pll_base,
		QSERDES_COM_INTEGLOOP_GAIN1_MODE0,
		dp_res->integloop_gain1_mode0);
	PLL_REG_W(pll_io->pll_base,
		QSERDES_COM_VCO_TUNE_MAP, dp_res->vco_tune_map);
	PLL_REG_W(pll_io->pll_base,
		QSERDES_COM_LOCK_CMP1_MODE0, dp_res->lock_cmp1_mode0);
	PLL_REG_W(pll_io->pll_base,
		QSERDES_COM_LOCK_CMP2_MODE0, dp_res->lock_cmp2_mode0);
	PLL_REG_W(pll_io->pll_base,
		QSERDES_COM_LOCK_CMP3_MODE0, dp_res->lock_cmp3_mode0);

	PLL_REG_W(pll_io->pll_base, QSERDES_COM_BG_TIMER, 0x0a);
	PLL_REG_W(pll_io->pll_base, QSERDES_COM_CORECLK_DIV_MODE0, 0x0a);
	PLL_REG_W(pll_io->pll_base, QSERDES_COM_VCO_TUNE_CTRL, 0x00);
	PLL_REG_W(pll_io->pll_base, QSERDES_COM_BIAS_EN_CLKBUFLR_EN, 0x3f);
	PLL_REG_W(pll_io->pll_base, QSERDES_COM_CORE_CLK_EN, 0x1f);
	PLL_REG_W(pll_io->pll_base, QSERDES_COM_PLL_IVCO, 0x07);
	PLL_REG_W(pll_io->pll_base,
		QSERDES_COM_LOCK_CMP_EN, dp_res->lock_cmp_en);
	PLL_REG_W(pll_io->pll_base, QSERDES_COM_PLL_CCTRL_MODE0, 0x36);
	PLL_REG_W(pll_io->pll_base, QSERDES_COM_PLL_RCTRL_MODE0, 0x16);
	PLL_REG_W(pll_io->pll_base, QSERDES_COM_CP_CTRL_MODE0, 0x06);

	if (dp_res->orientation == ORIENTATION_CC2)
		PLL_REG_W(pll_io->phy_base, REG_DP_PHY_MODE, 0x4c);
	else
		PLL_REG_W(pll_io->phy_base, REG_DP_PHY_MODE, 0x5c);

	/* TX Lane configuration */
	PLL_REG_W(pll_io->phy_base,
			REG_DP_PHY_TX0_TX1_LANE_CTL, 0x05);
	PLL_REG_W(pll_io->phy_base,
			REG_DP_PHY_TX2_TX3_LANE_CTL, 0x05);

	/* TX-0 register configuration */
	PLL_REG_W(pll_io->ln_tx0_base,
			REG_DP_PHY_TXn_TRANSCEIVER_BIAS_EN, 0x1a);
	PLL_REG_W(pll_io->ln_tx0_base,
			REG_DP_PHY_TXn_VMODE_CTRL1, 0x40);
	PLL_REG_W(pll_io->ln_tx0_base,
			REG_DP_PHY_TXn_PRE_STALL_LDO_BOOST_EN, 0x30);
	PLL_REG_W(pll_io->ln_tx0_base,
			REG_DP_PHY_TXn_INTERFACE_SELECT, 0x3d);
	PLL_REG_W(pll_io->ln_tx0_base,
			REG_DP_PHY_TXn_CLKBUF_ENABLE, 0x0f);
	PLL_REG_W(pll_io->ln_tx0_base,
			REG_DP_PHY_TXn_RESET_TSYNC_EN, 0x03);
	PLL_REG_W(pll_io->ln_tx0_base,
			REG_DP_PHY_TXn_TRAN_DRVR_EMP_EN, 0x03);
	PLL_REG_W(pll_io->ln_tx0_base,
			REG_DP_PHY_TXn_PARRATE_REC_DETECT_IDLE_EN, 0x00);
	PLL_REG_W(pll_io->ln_tx0_base,
			REG_DP_PHY_TXn_TX_INTERFACE_MODE, 0x00);
	PLL_REG_W(pll_io->ln_tx0_base, REG_DP_PHY_TXn_TX_BAND, 0x4);

	/* TX-1 register configuration */
	PLL_REG_W(pll_io->ln_tx1_base,
			REG_DP_PHY_TXn_TRANSCEIVER_BIAS_EN, 0x1a);
	PLL_REG_W(pll_io->ln_tx1_base,
			REG_DP_PHY_TXn_VMODE_CTRL1, 0x40);
	PLL_REG_W(pll_io->ln_tx1_base,
			REG_DP_PHY_TXn_PRE_STALL_LDO_BOOST_EN, 0x30);
	PLL_REG_W(pll_io->ln_tx1_base,
			REG_DP_PHY_TXn_INTERFACE_SELECT, 0x3d);
	PLL_REG_W(pll_io->ln_tx1_base,
			REG_DP_PHY_TXn_CLKBUF_ENABLE, 0x0f);
	PLL_REG_W(pll_io->ln_tx1_base,
			REG_DP_PHY_TXn_RESET_TSYNC_EN, 0x03);
	PLL_REG_W(pll_io->ln_tx1_base,
			REG_DP_PHY_TXn_TRAN_DRVR_EMP_EN, 0x03);
	PLL_REG_W(pll_io->ln_tx1_base,
			REG_DP_PHY_TXn_PARRATE_REC_DETECT_IDLE_EN, 0x00);
	PLL_REG_W(pll_io->ln_tx1_base,
			REG_DP_PHY_TXn_TX_INTERFACE_MODE, 0x00);
	PLL_REG_W(pll_io->ln_tx1_base,
			REG_DP_PHY_TXn_TX_BAND, 0x4);

	/* dependent on the vco frequency */
	PLL_REG_W(pll_io->phy_base,
			REG_DP_PHY_VCO_DIV, dp_res->phy_vco_div);

	return res;
}

static bool dp_10nm_pll_lock_status(struct dp_pll_db *dp_res)
{
	u32 status;
	bool pll_locked;
	struct dp_io_pll *pll_io = &dp_res->base->pll_io;

	/* poll for PLL lock status */
	if (readl_poll_timeout_atomic((pll_io->pll_base +
			QSERDES_COM_C_READY_STATUS),
			status,
			((status & BIT(0)) > 0),
			DP_PHY_PLL_POLL_SLEEP_US,
			DP_PHY_PLL_POLL_TIMEOUT_US)) {
		DRM_ERROR("%s: C_READY status is not high. Status=%x\n",
				__func__, status);
		pll_locked = false;
	} else {
		pll_locked = true;
	}

	return pll_locked;
}

static bool dp_10nm_phy_rdy_status(struct dp_pll_db *dp_res)
{
	u32 status;
	bool phy_ready = true;
	struct dp_io_pll *pll_io = &dp_res->base->pll_io;

	/* poll for PHY ready status */
	if (readl_poll_timeout_atomic((pll_io->phy_base +
			REG_DP_PHY_STATUS),
			status,
			((status & (BIT(1))) > 0),
			DP_PHY_PLL_POLL_SLEEP_US,
			DP_PHY_PLL_POLL_TIMEOUT_US)) {
		DRM_ERROR("%s: Phy_ready is not high. Status=%x\n",
				__func__, status);
		phy_ready = false;
	}

	return phy_ready;
}

static int dp_pll_enable_10nm(struct clk_hw *hw)
{
	int rc = 0;
	u32 bias_en, drvr_en;
	struct dp_io_pll *pll_io;
	struct dp_pll_vco_clk *vco = to_dp_vco_hw(hw);
	struct msm_dp_pll *pll = to_msm_dp_pll(vco);
	struct dp_pll_db *dp_res = to_dp_pll_db(pll);

	pll_io = &pll->pll_io;

	PLL_REG_W(pll_io->phy_base, REG_DP_PHY_AUX_CFG2, 0x04);
	PLL_REG_W(pll_io->phy_base, REG_DP_PHY_CFG, 0x01);
	PLL_REG_W(pll_io->phy_base, REG_DP_PHY_CFG, 0x05);
	PLL_REG_W(pll_io->phy_base, REG_DP_PHY_CFG, 0x01);
	PLL_REG_W(pll_io->phy_base, REG_DP_PHY_CFG, 0x09);

	PLL_REG_W(pll_io->pll_base, QSERDES_COM_RESETSM_CNTRL, 0x20);

	if (!dp_10nm_pll_lock_status(dp_res)) {
		rc = -EINVAL;
		goto lock_err;
	}

	PLL_REG_W(pll_io->phy_base, REG_DP_PHY_CFG, 0x19);
	/* poll for PHY ready status */
	if (!dp_10nm_phy_rdy_status(dp_res)) {
		rc = -EINVAL;
		goto lock_err;
	}

	DRM_DEBUG_DP("%s: PLL is locked\n", __func__);

	if (dp_res->lane_cnt == 1) {
		bias_en = 0x3e;
		drvr_en = 0x13;
	} else {
		bias_en = 0x3f;
		drvr_en = 0x10;
	}

	if (dp_res->lane_cnt != 4) {
		if (dp_res->orientation == ORIENTATION_CC1) {
			PLL_REG_W(pll_io->ln_tx1_base,
				REG_DP_PHY_TXn_HIGHZ_DRVR_EN, drvr_en);
			PLL_REG_W(pll_io->ln_tx1_base,
				REG_DP_PHY_TXn_TRANSCEIVER_BIAS_EN, bias_en);
		} else {
			PLL_REG_W(pll_io->ln_tx0_base,
				REG_DP_PHY_TXn_HIGHZ_DRVR_EN, drvr_en);
			PLL_REG_W(pll_io->ln_tx0_base,
				REG_DP_PHY_TXn_TRANSCEIVER_BIAS_EN, bias_en);
		}
	} else {
		PLL_REG_W(pll_io->ln_tx0_base,
				REG_DP_PHY_TXn_HIGHZ_DRVR_EN, drvr_en);
		PLL_REG_W(pll_io->ln_tx0_base,
				REG_DP_PHY_TXn_TRANSCEIVER_BIAS_EN, bias_en);
		PLL_REG_W(pll_io->ln_tx1_base,
				REG_DP_PHY_TXn_HIGHZ_DRVR_EN, drvr_en);
		PLL_REG_W(pll_io->ln_tx1_base,
				REG_DP_PHY_TXn_TRANSCEIVER_BIAS_EN, bias_en);
	}

	PLL_REG_W(pll_io->ln_tx0_base, REG_DP_PHY_TXn_TX_POL_INV, 0x0a);
	PLL_REG_W(pll_io->ln_tx1_base, REG_DP_PHY_TXn_TX_POL_INV, 0x0a);
	PLL_REG_W(pll_io->phy_base, REG_DP_PHY_CFG, 0x18);
	udelay(2000);

	PLL_REG_W(pll_io->phy_base, REG_DP_PHY_CFG, 0x19);

	/* poll for PHY ready status */
	if (!dp_10nm_phy_rdy_status(dp_res)) {
		rc = -EINVAL;
		goto lock_err;
	}

	PLL_REG_W(pll_io->ln_tx0_base, REG_DP_PHY_TXn_TX_DRV_LVL, 0x38);
	PLL_REG_W(pll_io->ln_tx1_base, REG_DP_PHY_TXn_TX_DRV_LVL, 0x38);
	PLL_REG_W(pll_io->ln_tx0_base, REG_DP_PHY_TXn_TX_EMP_POST1_LVL, 0x20);
	PLL_REG_W(pll_io->ln_tx1_base, REG_DP_PHY_TXn_TX_EMP_POST1_LVL, 0x20);
	PLL_REG_W(pll_io->ln_tx0_base,
			REG_DP_PHY_TXn_RES_CODE_LANE_OFFSET_TX, 0x06);
	PLL_REG_W(pll_io->ln_tx1_base,
			REG_DP_PHY_TXn_RES_CODE_LANE_OFFSET_TX, 0x06);
	PLL_REG_W(pll_io->ln_tx0_base,
			REG_DP_PHY_TXn_RES_CODE_LANE_OFFSET_RX, 0x07);
	PLL_REG_W(pll_io->ln_tx1_base,
			REG_DP_PHY_TXn_RES_CODE_LANE_OFFSET_RX, 0x07);

lock_err:
	return rc;
}

static int dp_pll_disable_10nm(struct clk_hw *hw)
{
	int rc = 0;
	struct dp_pll_vco_clk *vco = to_dp_vco_hw(hw);
	struct msm_dp_pll *pll = to_msm_dp_pll(vco);

	/* Assert DP PHY power down */
	PLL_REG_W(pll->pll_io.phy_base, REG_DP_PHY_PD_CTL, 0x2);

	return rc;
}


static int dp_vco_prepare_10nm(struct clk_hw *hw)
{
	int rc = 0;
	struct dp_pll_vco_clk *vco = to_dp_vco_hw(hw);
	struct msm_dp_pll *pll = (struct msm_dp_pll *)vco->priv;
	struct dp_pll_db *dp_res = to_dp_pll_db(pll);

	DRM_DEBUG_DP("%s: rate = %ld\n", __func__, vco->rate);
	if ((dp_res->vco_cached_rate != 0)
		&& (dp_res->vco_cached_rate == vco->rate)) {
		rc = dp_vco_set_rate_10nm(hw,
			dp_res->vco_cached_rate, dp_res->vco_cached_rate);
		if (rc) {
			DRM_ERROR("index=%d vco_set_rate failed. rc=%d\n",
				rc, dp_res->index);
			goto error;
		}
	}

	rc = dp_pll_enable_10nm(hw);
	if (rc) {
		DRM_ERROR("ndx=%d failed to enable dp pll\n",
					dp_res->index);
		goto error;
	}

	pll->pll_on = true;
error:
	return rc;
}

static void dp_vco_unprepare_10nm(struct clk_hw *hw)
{
	struct dp_pll_vco_clk *vco = to_dp_vco_hw(hw);
	struct msm_dp_pll *pll = to_msm_dp_pll(vco);
	struct dp_pll_db *dp_res = to_dp_pll_db(pll);

	if (!dp_res) {
		DRM_ERROR("Invalid input parameter\n");
		return;
	}

	if (!pll->pll_on) {
		DRM_ERROR("pll resource can't be enabled\n");
		return;
	}
	dp_res->vco_cached_rate = vco->rate;
	dp_pll_disable_10nm(hw);

	pll->pll_on = false;
}

static int dp_vco_set_rate_10nm(struct clk_hw *hw, unsigned long rate,
					unsigned long parent_rate)
{
	struct dp_pll_vco_clk *vco = to_dp_vco_hw(hw);
	int rc;

	DRM_DEBUG_DP("DP lane CLK rate=%ld\n", rate);

	rc = dp_config_vco_rate_10nm(vco, rate);
	if (rc)
		DRM_ERROR("%s: Failed to set clk rate\n", __func__);

	vco->rate = rate;

	return 0;
}

static unsigned long dp_vco_recalc_rate_10nm(struct clk_hw *hw,
					unsigned long parent_rate)
{
	u32 div, hsclk_div, link_clk_div = 0;
	u64 vco_rate;
	struct dp_io_pll *pll_io;
	struct dp_pll_vco_clk *vco = to_dp_vco_hw(hw);
	struct msm_dp_pll *pll = to_msm_dp_pll(vco);
	struct dp_pll_db *dp_res = to_dp_pll_db(pll);

	pll_io = &pll->pll_io;

	div = PLL_REG_R(pll_io->pll_base, QSERDES_COM_HSCLK_SEL);
	div &= 0x0f;

	if (div == 12)
		hsclk_div = 6; /* Default */
	else if (div == 4)
		hsclk_div = 4;
	else if (div == 0)
		hsclk_div = 2;
	else if (div == 3)
		hsclk_div = 1;
	else {
		DRM_DEBUG_DP("unknown divider. forcing to default\n");
		hsclk_div = 5;
	}

	div = PLL_REG_R(pll_io->phy_base, REG_DP_PHY_AUX_CFG2);
	div >>= 2;

	if ((div & 0x3) == 0)
		link_clk_div = 5;
	else if ((div & 0x3) == 1)
		link_clk_div = 10;
	else if ((div & 0x3) == 2)
		link_clk_div = 20;
	else
		DRM_ERROR("%s: unsupported div. Phy_mode: %d\n", __func__, div);

	if (link_clk_div == 20) {
		vco_rate = DP_VCO_HSCLK_RATE_2700MHZDIV1000;
	} else {
		if (hsclk_div == 6)
			vco_rate = DP_VCO_HSCLK_RATE_1620MHZDIV1000;
		else if (hsclk_div == 4)
			vco_rate = DP_VCO_HSCLK_RATE_2700MHZDIV1000;
		else if (hsclk_div == 2)
			vco_rate = DP_VCO_HSCLK_RATE_5400MHZDIV1000;
		else
			vco_rate = DP_VCO_HSCLK_RATE_8100MHZDIV1000;
	}

	DRM_DEBUG_DP("returning vco rate = %lu\n", (unsigned long)vco_rate);

	dp_res->vco_cached_rate = vco->rate = vco_rate;
	return (unsigned long)vco_rate;
}

long dp_vco_round_rate_10nm(struct clk_hw *hw, unsigned long rate,
			unsigned long *parent_rate)
{
	unsigned long rrate = rate;
	struct dp_pll_vco_clk *vco = to_dp_vco_hw(hw);

	if (rate <= vco->min_rate)
		rrate = vco->min_rate;
	else if (rate <= DP_VCO_HSCLK_RATE_2700MHZDIV1000)
		rrate = DP_VCO_HSCLK_RATE_2700MHZDIV1000;
	else if (rate <= DP_VCO_HSCLK_RATE_5400MHZDIV1000)
		rrate = DP_VCO_HSCLK_RATE_5400MHZDIV1000;
	else
		rrate = vco->max_rate;

	DRM_DEBUG_DP("%s: rrate=%ld\n", __func__, rrate);

	*parent_rate = rrate;
	return rrate;
}

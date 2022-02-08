// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#include <linux/bitfield.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/phy/phy.h>

#define EDP_PHY_GRF_CON0		0x0000
#define EDP_PHY_TX_IDLE			GENMASK(11, 8)
#define EDP_PHY_TX_PD			GENMASK(7, 4)
#define EDP_PHY_IDDQ_EN			BIT(1)
#define EDP_PHY_PD_PLL			BIT(0)
#define EDP_PHY_GRF_CON1		0x0004
#define EDP_PHY_PLL_DIV			GENMASK(14, 0)
#define EDP_PHY_GRF_CON2		0x0008
#define EDP_PHY_TX_RTERM		GENMASK(10, 8)
#define EDP_PHY_RATE			GENMASK(5, 4)
#define EDP_PHY_REF_DIV			GENMASK(3, 0)
#define EDP_PHY_GRF_CON3		0x000c
#define EDP_PHY_TX3_EMP			GENMASK(15, 12)
#define EDP_PHY_TX2_EMP			GENMASK(11, 8)
#define EDP_PHY_TX1_EMP			GENMASK(7, 4)
#define EDP_PHY_TX0_EMP			GENMASK(3, 0)
#define EDP_PHY_GRF_CON4		0x0010
#define EDP_PHY_TX3_AMP			GENMASK(14, 12)
#define EDP_PHY_TX2_AMP			GENMASK(10, 8)
#define EDP_PHY_TX1_AMP			GENMASK(6, 4)
#define EDP_PHY_TX0_AMP			GENMASK(2, 0)
#define EDP_PHY_GRF_CON5		0x0014
#define EDP_PHY_TX_MODE			GENMASK(9, 8)
#define EDP_PHY_TX3_AMP_SCALE		GENMASK(7, 6)
#define EDP_PHY_TX2_AMP_SCALE		GENMASK(5, 4)
#define EDP_PHY_TX1_AMP_SCALE		GENMASK(3, 2)
#define EDP_PHY_TX0_AMP_SCALE		GENMASK(1, 0)
#define EDP_PHY_GRF_CON6		0x0018
#define EDP_PHY_SSC_DEPTH		GENMASK(15, 12)
#define EDP_PHY_SSC_EN			BIT(11)
#define EDP_PHY_SSC_CNT			GENMASK(9, 0)
#define EDP_PHY_GRF_CON7		0x001c
#define EDP_PHY_GRF_CON8		0x0020
#define EDP_PHY_PLL_CTL_H		GENMASK(15, 0)
#define EDP_PHY_GRF_CON9		0x0024
#define EDP_PHY_TX_CTL			GENMASK(15, 0)
#define EDP_PHY_GRF_CON10		0x0028
#define EDP_PHY_AUX_RCV_PD_SEL		BIT(5)
#define EDP_PHY_AUX_DRV_PD_SEL		BIT(4)
#define EDP_PHY_AUX_IDLE		BIT(2)
#define EDP_PHY_AUX_RCV_PD		BIT(1)
#define EDP_PHY_AUX_DRV_PD		BIT(0)
#define EDP_PHY_GRF_CON11		0x002c
#define EDP_PHY_AUX_RCV_VCM		GENMASK(14, 12)
#define EDP_PHY_AUX_MODE		GENMASK(11, 10)
#define EDP_PHY_AUX_AMP_SCALE		GENMASK(9, 8)
#define EDP_PHY_AUX_AMP			GENMASK(6, 4)
#define EDP_PHY_AUX_RTERM		GENMASK(2, 0)
#define EDP_PHY_GRF_STATUS0		0x0030
#define PLL_RDY				BIT(0)
#define EDP_PHY_GRF_STATUS1		0x0034

struct rockchip_edp_phy {
	struct regmap *grf;
	struct device *dev;
	struct clk *refclk;
};

static inline int rockchip_grf_write(struct regmap *grf, unsigned int reg,
				     unsigned int mask, unsigned int val)
{
	return regmap_write(grf, reg, (mask << 16) | (val & mask));
}

static struct {
	int amp;
	int amp_scale;
	int emp;
} vp[4][4] = {
	{ {0x1, 0x1, 0x0}, {0x2, 0x1, 0x4}, {0x3, 0x1, 0x8}, {0x4, 0x1, 0xd} },
	{ {0x3, 0x1, 0x0}, {0x5, 0x1, 0x7}, {0x6, 0x1, 0x6}, { -1,  -1,  -1} },
	{ {0x5, 0x1, 0x0}, {0x7, 0x1, 0x4}, { -1,  -1,  -1}, { -1,  -1,  -1} },
	{ {0x7, 0x1, 0x0}, { -1,  -1,  -1}, { -1,  -1,  -1}, { -1,  -1,  -1} },
};

static void rockchip_edp_phy_set_voltage(struct rockchip_edp_phy *edpphy,
					 struct phy_configure_opts_dp *dp,
					 u8 lane)
{
	u32 amp, amp_scale, emp;

	amp = vp[dp->voltage[lane]][dp->pre[lane]].amp;
	amp_scale = vp[dp->voltage[lane]][dp->pre[lane]].amp_scale;
	emp = vp[dp->voltage[lane]][dp->pre[lane]].emp;

	switch (lane) {
	case 0:
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON3,
				   EDP_PHY_TX0_EMP,
				   FIELD_PREP(EDP_PHY_TX0_EMP, emp));
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON4,
				   EDP_PHY_TX0_AMP,
				   FIELD_PREP(EDP_PHY_TX0_AMP, amp));
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON5,
				   EDP_PHY_TX0_AMP_SCALE,
				   FIELD_PREP(EDP_PHY_TX0_AMP_SCALE, amp_scale));
		break;
	case 1:
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON3,
				   EDP_PHY_TX1_EMP,
				   FIELD_PREP(EDP_PHY_TX1_EMP, emp));
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON4,
				   EDP_PHY_TX1_AMP,
				   FIELD_PREP(EDP_PHY_TX1_AMP, amp));
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON5,
				   EDP_PHY_TX1_AMP_SCALE,
				   FIELD_PREP(EDP_PHY_TX1_AMP_SCALE, amp_scale));
		break;
	case 2:
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON3,
				   EDP_PHY_TX2_EMP,
				   FIELD_PREP(EDP_PHY_TX2_EMP, emp));
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON4,
				   EDP_PHY_TX2_AMP,
				   FIELD_PREP(EDP_PHY_TX2_AMP, amp));
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON5,
				   EDP_PHY_TX2_AMP_SCALE,
				   FIELD_PREP(EDP_PHY_TX2_AMP_SCALE, amp_scale));
		break;
	case 3:
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON3,
				   EDP_PHY_TX3_EMP,
				   FIELD_PREP(EDP_PHY_TX3_EMP, emp));
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON4,
				   EDP_PHY_TX3_AMP,
				   FIELD_PREP(EDP_PHY_TX3_AMP, amp));
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON5,
				   EDP_PHY_TX3_AMP_SCALE,
				   FIELD_PREP(EDP_PHY_TX3_AMP_SCALE, amp_scale));
		break;
	}
}

static int rockchip_edp_phy_set_voltages(struct rockchip_edp_phy *edpphy,
					 struct phy_configure_opts_dp *dp)
{
	u8 lane;

	for (lane = 0; lane < dp->lanes; lane++)
		rockchip_edp_phy_set_voltage(edpphy, dp, lane);

	return 0;
}

static int rockchip_edp_phy_set_rate(struct rockchip_edp_phy *edpphy,
				     struct phy_configure_opts_dp *dp)
{
	u32 value;
	int ret;

	rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON0,
			   EDP_PHY_TX_IDLE | EDP_PHY_TX_PD,
			   FIELD_PREP(EDP_PHY_TX_IDLE, 0xf) |
			   FIELD_PREP(EDP_PHY_TX_PD, 0xf));
	usleep_range(100, 101);
	rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON5, EDP_PHY_TX_MODE,
			   FIELD_PREP(EDP_PHY_TX_MODE, 0x3));
	rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON0, EDP_PHY_PD_PLL,
			   FIELD_PREP(EDP_PHY_PD_PLL, 0x1));

	switch (dp->link_rate) {
	case 1620:
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON1,
				   EDP_PHY_PLL_DIV,
				   FIELD_PREP(EDP_PHY_PLL_DIV, 0x4380));
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON2,
				   EDP_PHY_TX_RTERM | EDP_PHY_RATE | EDP_PHY_REF_DIV,
				   FIELD_PREP(EDP_PHY_TX_RTERM, 0x1) |
				   FIELD_PREP(EDP_PHY_RATE, 0x1) |
				   FIELD_PREP(EDP_PHY_REF_DIV, 0x0));
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON8,
				   EDP_PHY_PLL_CTL_H,
				   FIELD_PREP(EDP_PHY_PLL_CTL_H, 0x0800));
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON9,
				   EDP_PHY_TX_CTL,
				   FIELD_PREP(EDP_PHY_TX_CTL, 0x0000));
		break;
	case 2700:
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON1,
				   EDP_PHY_PLL_DIV,
				   FIELD_PREP(EDP_PHY_PLL_DIV, 0x3840));
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON2,
				   EDP_PHY_TX_RTERM | EDP_PHY_RATE | EDP_PHY_REF_DIV,
				   FIELD_PREP(EDP_PHY_TX_RTERM, 0x1) |
				   FIELD_PREP(EDP_PHY_RATE, 0x0) |
				   FIELD_PREP(EDP_PHY_REF_DIV, 0x0));
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON8,
				   EDP_PHY_PLL_CTL_H,
				   FIELD_PREP(EDP_PHY_PLL_CTL_H, 0x0800));
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON9,
				   EDP_PHY_TX_CTL,
				   FIELD_PREP(EDP_PHY_TX_CTL, 0x0000));
		break;
	}

	if (dp->ssc)
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON6,
				   EDP_PHY_SSC_DEPTH | EDP_PHY_SSC_EN | EDP_PHY_SSC_CNT,
				   FIELD_PREP(EDP_PHY_SSC_DEPTH, 0x9) |
				   FIELD_PREP(EDP_PHY_SSC_EN, 0x1) |
				   FIELD_PREP(EDP_PHY_SSC_CNT, 0x17d));
	else
		rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON6,
				   EDP_PHY_SSC_EN,
				   FIELD_PREP(EDP_PHY_SSC_EN, 0x0));

	rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON0, EDP_PHY_PD_PLL,
			   FIELD_PREP(EDP_PHY_PD_PLL, 0));
	rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON0, EDP_PHY_TX_PD,
			   FIELD_PREP(EDP_PHY_TX_PD, ~GENMASK(dp->lanes - 1, 0)));
	ret = regmap_read_poll_timeout(edpphy->grf, EDP_PHY_GRF_STATUS0,
				       value, value & PLL_RDY, 100, 1000);
	if (ret) {
		dev_err(edpphy->dev, "pll is not ready: %d\n", ret);
		return ret;
	}

	rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON5, EDP_PHY_TX_MODE,
			   FIELD_PREP(EDP_PHY_TX_MODE, 0x0));
	rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON0, EDP_PHY_TX_IDLE,
			   FIELD_PREP(EDP_PHY_TX_IDLE, ~GENMASK(dp->lanes - 1, 0)));

	return 0;
}

static int rockchip_edp_phy_verify_config(struct rockchip_edp_phy *edpphy,
					  struct phy_configure_opts_dp *dp)
{
	int i;

	/* If changing link rate was required, verify it's supported. */
	if (dp->set_rate) {
		switch (dp->link_rate) {
		case 1620:
		case 2700:
			/* valid bit rate */
			break;
		default:
			return -EINVAL;
		}
	}

	/* Verify lane count. */
	switch (dp->lanes) {
	case 1:
	case 2:
	case 4:
		/* valid lane count. */
		break;
	default:
		return -EINVAL;
	}

	/*
	 * If changing voltages is required, check swing and pre-emphasis
	 * levels, per-lane.
	 */
	if (dp->set_voltages) {
		/* Lane count verified previously. */
		for (i = 0; i < dp->lanes; i++) {
			if (dp->voltage[i] > 3 || dp->pre[i] > 3)
				return -EINVAL;

			/*
			 * Sum of voltage swing and pre-emphasis levels cannot
			 * exceed 3.
			 */
			if (dp->voltage[i] + dp->pre[i] > 3)
				return -EINVAL;
		}
	}

	return 0;
}

static int rockchip_edp_phy_configure(struct phy *phy,
				      union phy_configure_opts *opts)
{
	struct rockchip_edp_phy *edpphy = phy_get_drvdata(phy);
	int ret;

	ret = rockchip_edp_phy_verify_config(edpphy, &opts->dp);
	if (ret) {
		dev_err(edpphy->dev, "invalid params for phy configure\n");
		return ret;
	}

	if (opts->dp.set_rate) {
		ret = rockchip_edp_phy_set_rate(edpphy, &opts->dp);
		if (ret) {
			dev_err(edpphy->dev,
				"rockchip_edp_phy_set_rate failed\n");
			return ret;
		}
	}

	if (opts->dp.set_voltages) {
		ret = rockchip_edp_phy_set_voltages(edpphy, &opts->dp);
		if (ret) {
			dev_err(edpphy->dev,
				"rockchip_edp_phy_set_voltages failed\n");
			return ret;
		}
	}

	return 0;
}

static bool rockchip_edp_phy_enabled(struct rockchip_edp_phy *edpphy)
{
	u32 val;

	regmap_read(edpphy->grf, EDP_PHY_GRF_STATUS0, &val);

	return FIELD_GET(PLL_RDY, val);
}

static int rockchip_edp_phy_power_on(struct phy *phy)
{
	struct rockchip_edp_phy *edpphy = phy_get_drvdata(phy);

	clk_prepare_enable(edpphy->refclk);

	if (rockchip_edp_phy_enabled(edpphy))
		return 0;

	rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON10,
			   EDP_PHY_AUX_RCV_PD | EDP_PHY_AUX_DRV_PD | EDP_PHY_AUX_IDLE,
			   FIELD_PREP(EDP_PHY_AUX_RCV_PD, 0x1) |
			   FIELD_PREP(EDP_PHY_AUX_DRV_PD, 0x1) |
			   FIELD_PREP(EDP_PHY_AUX_IDLE, 0x1));
	rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON0,
			   EDP_PHY_TX_IDLE | EDP_PHY_TX_PD | EDP_PHY_PD_PLL,
			   FIELD_PREP(EDP_PHY_TX_IDLE, 0xf) |
			   FIELD_PREP(EDP_PHY_TX_PD, 0xf) |
			   FIELD_PREP(EDP_PHY_PD_PLL, 0x1));
	usleep_range(100, 101);

	rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON11,
			   EDP_PHY_AUX_RCV_VCM | EDP_PHY_AUX_MODE |
			   EDP_PHY_AUX_AMP_SCALE | EDP_PHY_AUX_AMP |
			   EDP_PHY_AUX_RTERM,
			   FIELD_PREP(EDP_PHY_AUX_RCV_VCM, 0x4) |
			   FIELD_PREP(EDP_PHY_AUX_MODE, 0x1) |
			   FIELD_PREP(EDP_PHY_AUX_AMP_SCALE, 0x1) |
			   FIELD_PREP(EDP_PHY_AUX_AMP, 0x3) |
			   FIELD_PREP(EDP_PHY_AUX_RTERM, 0x1));

	rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON10,
			   EDP_PHY_AUX_RCV_PD | EDP_PHY_AUX_DRV_PD,
			   FIELD_PREP(EDP_PHY_AUX_RCV_PD, 0x0) |
			   FIELD_PREP(EDP_PHY_AUX_DRV_PD, 0x0));
	usleep_range(100, 101);

	rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON10,
			   EDP_PHY_AUX_IDLE,
			   FIELD_PREP(EDP_PHY_AUX_IDLE, 0x0));
	usleep_range(10000, 11000);

	return 0;
}

static int rockchip_edp_phy_power_off(struct phy *phy)
{
	struct rockchip_edp_phy *edpphy = phy_get_drvdata(phy);

	rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON0,
			   EDP_PHY_TX_IDLE | EDP_PHY_TX_PD,
			   FIELD_PREP(EDP_PHY_TX_IDLE, 0xf) |
			   FIELD_PREP(EDP_PHY_TX_PD, 0xf));
	usleep_range(100, 101);
	rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON5, EDP_PHY_TX_MODE,
			   FIELD_PREP(EDP_PHY_TX_MODE, 0x3));
	rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON0, EDP_PHY_PD_PLL,
			   FIELD_PREP(EDP_PHY_PD_PLL, 0x1));
	rockchip_grf_write(edpphy->grf, EDP_PHY_GRF_CON10,
			   EDP_PHY_AUX_RCV_PD | EDP_PHY_AUX_DRV_PD | EDP_PHY_AUX_IDLE,
			   FIELD_PREP(EDP_PHY_AUX_RCV_PD, 0x1) |
			   FIELD_PREP(EDP_PHY_AUX_DRV_PD, 0x1) |
			   FIELD_PREP(EDP_PHY_AUX_IDLE, 0x1));

	clk_disable_unprepare(edpphy->refclk);

	return 0;
}

static const struct phy_ops rockchip_edp_phy_ops = {
	.power_on = rockchip_edp_phy_power_on,
	.power_off = rockchip_edp_phy_power_off,
	.configure = rockchip_edp_phy_configure,
	.owner = THIS_MODULE,
};

static int rockchip_edp_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_edp_phy *edpphy;
	struct phy *phy;
	struct phy_provider *phy_provider;
	int ret;

	edpphy = devm_kzalloc(dev, sizeof(*edpphy), GFP_KERNEL);
	if (!edpphy)
		return -ENOMEM;

	edpphy->dev = dev;

	edpphy->grf = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(edpphy->grf)) {
		ret = PTR_ERR(edpphy->grf);
		dev_err(dev, "failed to get grf: %d\n", ret);
		return ret;
	}

	edpphy->refclk = devm_clk_get(dev, "refclk");
	if (IS_ERR(edpphy->refclk)) {
		ret = PTR_ERR(edpphy->refclk);
		dev_err(dev, "failed to get refclk: %d\n", ret);
		return ret;
	}

	phy = devm_phy_create(dev, NULL, &rockchip_edp_phy_ops);
	if (IS_ERR(phy)) {
		ret = PTR_ERR(phy);
		dev_err(dev, "failed to create PHY: %d\n", ret);
		return ret;
	}

	phy_set_drvdata(phy, edpphy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(dev, "failed to register phy provider\n");
		return PTR_ERR(phy_provider);
	}

	return 0;
}

static const struct of_device_id rockchip_edp_phy_of_match[] = {
	{ .compatible = "rockchip,rk3568-edp-phy", },
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_edp_phy_of_match);

static struct platform_driver rockchip_edp_phy_driver = {
	.driver = {
		.name = "rockchip-edpphy-naneng",
		.of_match_table	= of_match_ptr(rockchip_edp_phy_of_match),
	},
	.probe = rockchip_edp_phy_probe,
};
module_platform_driver(rockchip_edp_phy_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip Naneng eDP Transmitter PHY driver");
MODULE_LICENSE("GPL v2");

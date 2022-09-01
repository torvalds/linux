// SPDX-License-Identifier: GPL-2.0-only
/*
 * UFS PHY driver for Samsung SoC
 *
 * Copyright (C) 2020 Samsung Electronics Co., Ltd.
 * Author: Seungwon Jeon <essuuj@gmail.com>
 * Author: Alim Akhtar <alim.akhtar@samsung.com>
 *
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "phy-samsung-ufs.h"

#define for_each_phy_lane(phy, i) \
	for (i = 0; i < (phy)->lane_cnt; i++)
#define for_each_phy_cfg(cfg) \
	for (; (cfg)->id; (cfg)++)

#define PHY_DEF_LANE_CNT	1

static void samsung_ufs_phy_config(struct samsung_ufs_phy *phy,
				   const struct samsung_ufs_phy_cfg *cfg,
				   u8 lane)
{
	enum {LANE_0, LANE_1}; /* lane index */

	switch (lane) {
	case LANE_0:
		writel(cfg->val, (phy)->reg_pma + cfg->off_0);
		break;
	case LANE_1:
		if (cfg->id == PHY_TRSV_BLK)
			writel(cfg->val, (phy)->reg_pma + cfg->off_1);
		break;
	}
}

static int samsung_ufs_phy_wait_for_lock_acq(struct phy *phy)
{
	struct samsung_ufs_phy *ufs_phy = get_samsung_ufs_phy(phy);
	const unsigned int timeout_us = 100000;
	const unsigned int sleep_us = 10;
	u32 val;
	int err;

	err = readl_poll_timeout(
			ufs_phy->reg_pma + PHY_APB_ADDR(PHY_PLL_LOCK_STATUS),
			val, (val & PHY_PLL_LOCK_BIT), sleep_us, timeout_us);
	if (err) {
		dev_err(ufs_phy->dev,
			"failed to get phy pll lock acquisition %d\n", err);
		goto out;
	}

	err = readl_poll_timeout(
			ufs_phy->reg_pma +
			PHY_APB_ADDR(ufs_phy->drvdata->cdr_lock_status_offset),
			val, (val & PHY_CDR_LOCK_BIT), sleep_us, timeout_us);
	if (err)
		dev_err(ufs_phy->dev,
			"failed to get phy cdr lock acquisition %d\n", err);
out:
	return err;
}

static int samsung_ufs_phy_calibrate(struct phy *phy)
{
	struct samsung_ufs_phy *ufs_phy = get_samsung_ufs_phy(phy);
	const struct samsung_ufs_phy_cfg * const *cfgs = ufs_phy->cfgs;
	const struct samsung_ufs_phy_cfg *cfg;
	int err = 0;
	int i;

	if (unlikely(ufs_phy->ufs_phy_state < CFG_PRE_INIT ||
		     ufs_phy->ufs_phy_state >= CFG_TAG_MAX)) {
		dev_err(ufs_phy->dev, "invalid phy config index %d\n", ufs_phy->ufs_phy_state);
		return -EINVAL;
	}

	cfg = cfgs[ufs_phy->ufs_phy_state];
	if (!cfg)
		goto out;

	for_each_phy_cfg(cfg) {
		for_each_phy_lane(ufs_phy, i) {
			samsung_ufs_phy_config(ufs_phy, cfg, i);
		}
	}

	if (ufs_phy->ufs_phy_state == CFG_POST_PWR_HS)
		err = samsung_ufs_phy_wait_for_lock_acq(phy);

	/**
	 * In Samsung ufshci, PHY need to be calibrated at different
	 * stages / state mainly before Linkstartup, after Linkstartup,
	 * before power mode change and after power mode change.
	 * Below state machine to make sure to calibrate PHY in each
	 * state. Here after configuring PHY in a given state, will
	 * change the state to next state so that next state phy
	 * calibration value can be programed
	 */
out:
	switch (ufs_phy->ufs_phy_state) {
	case CFG_PRE_INIT:
		ufs_phy->ufs_phy_state = CFG_POST_INIT;
		break;
	case CFG_POST_INIT:
		ufs_phy->ufs_phy_state = CFG_PRE_PWR_HS;
		break;
	case CFG_PRE_PWR_HS:
		ufs_phy->ufs_phy_state = CFG_POST_PWR_HS;
		break;
	case CFG_POST_PWR_HS:
		/* Change back to INIT state */
		ufs_phy->ufs_phy_state = CFG_PRE_INIT;
		break;
	default:
		dev_err(ufs_phy->dev, "wrong state for phy calibration\n");
	}

	return err;
}

static int samsung_ufs_phy_clks_init(struct samsung_ufs_phy *phy)
{
	int i;
	const struct samsung_ufs_phy_drvdata *drvdata = phy->drvdata;
	int num_clks = drvdata->num_clks;

	phy->clks = devm_kcalloc(phy->dev, num_clks, sizeof(*phy->clks),
				 GFP_KERNEL);
	if (!phy->clks)
		return -ENOMEM;

	for (i = 0; i < num_clks; i++)
		phy->clks[i].id = drvdata->clk_list[i];

	return devm_clk_bulk_get(phy->dev, num_clks, phy->clks);
}

static int samsung_ufs_phy_init(struct phy *phy)
{
	struct samsung_ufs_phy *ss_phy = get_samsung_ufs_phy(phy);

	ss_phy->lane_cnt = phy->attrs.bus_width;
	ss_phy->ufs_phy_state = CFG_PRE_INIT;

	return 0;
}

static int samsung_ufs_phy_power_on(struct phy *phy)
{
	struct samsung_ufs_phy *ss_phy = get_samsung_ufs_phy(phy);
	int ret;

	samsung_ufs_phy_ctrl_isol(ss_phy, false);

	ret = clk_bulk_prepare_enable(ss_phy->drvdata->num_clks, ss_phy->clks);
	if (ret) {
		dev_err(ss_phy->dev, "failed to enable ufs phy clocks\n");
		return ret;
	}

	if (ss_phy->ufs_phy_state == CFG_PRE_INIT) {
		ret = samsung_ufs_phy_calibrate(phy);
		if (ret)
			dev_err(ss_phy->dev, "ufs phy calibration failed\n");
	}

	return ret;
}

static int samsung_ufs_phy_power_off(struct phy *phy)
{
	struct samsung_ufs_phy *ss_phy = get_samsung_ufs_phy(phy);

	clk_bulk_disable_unprepare(ss_phy->drvdata->num_clks, ss_phy->clks);

	samsung_ufs_phy_ctrl_isol(ss_phy, true);

	return 0;
}

static int samsung_ufs_phy_set_mode(struct phy *generic_phy,
				    enum phy_mode mode, int submode)
{
	struct samsung_ufs_phy *ss_phy = get_samsung_ufs_phy(generic_phy);

	ss_phy->mode = PHY_MODE_INVALID;

	if (mode > 0)
		ss_phy->mode = mode;

	return 0;
}

static int samsung_ufs_phy_exit(struct phy *phy)
{
	struct samsung_ufs_phy *ss_phy = get_samsung_ufs_phy(phy);

	ss_phy->ufs_phy_state = CFG_TAG_MAX;

	return 0;
}

static const struct phy_ops samsung_ufs_phy_ops = {
	.init		= samsung_ufs_phy_init,
	.exit		= samsung_ufs_phy_exit,
	.power_on	= samsung_ufs_phy_power_on,
	.power_off	= samsung_ufs_phy_power_off,
	.calibrate	= samsung_ufs_phy_calibrate,
	.set_mode	= samsung_ufs_phy_set_mode,
	.owner          = THIS_MODULE,
};

static const struct of_device_id samsung_ufs_phy_match[];

static int samsung_ufs_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct samsung_ufs_phy *phy;
	struct phy *gen_phy;
	struct phy_provider *phy_provider;
	const struct samsung_ufs_phy_drvdata *drvdata;
	u32 isol_offset;
	int err = 0;

	match = of_match_node(samsung_ufs_phy_match, dev->of_node);
	if (!match) {
		err = -EINVAL;
		dev_err(dev, "failed to get match_node\n");
		goto out;
	}

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy) {
		err = -ENOMEM;
		goto out;
	}

	phy->reg_pma = devm_platform_ioremap_resource_byname(pdev, "phy-pma");
	if (IS_ERR(phy->reg_pma)) {
		err = PTR_ERR(phy->reg_pma);
		goto out;
	}

	phy->reg_pmu = syscon_regmap_lookup_by_phandle(
				dev->of_node, "samsung,pmu-syscon");
	if (IS_ERR(phy->reg_pmu)) {
		err = PTR_ERR(phy->reg_pmu);
		dev_err(dev, "failed syscon remap for pmu\n");
		goto out;
	}

	gen_phy = devm_phy_create(dev, NULL, &samsung_ufs_phy_ops);
	if (IS_ERR(gen_phy)) {
		err = PTR_ERR(gen_phy);
		dev_err(dev, "failed to create PHY for ufs-phy\n");
		goto out;
	}

	drvdata = match->data;
	phy->dev = dev;
	phy->drvdata = drvdata;
	phy->cfgs = drvdata->cfgs;
	memcpy(&phy->isol, &drvdata->isol, sizeof(phy->isol));

	if (!of_property_read_u32_index(dev->of_node, "samsung,pmu-syscon", 1,
					&isol_offset))
		phy->isol.offset = isol_offset;

	phy->lane_cnt = PHY_DEF_LANE_CNT;

	err = samsung_ufs_phy_clks_init(phy);
	if (err) {
		dev_err(dev, "failed to get phy clocks\n");
		goto out;
	}

	phy_set_drvdata(gen_phy, phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		err = PTR_ERR(phy_provider);
		dev_err(dev, "failed to register phy-provider\n");
		goto out;
	}
out:
	return err;
}

static const struct of_device_id samsung_ufs_phy_match[] = {
	{
		.compatible = "samsung,exynos7-ufs-phy",
		.data = &exynos7_ufs_phy,
	}, {
		.compatible = "samsung,exynosautov9-ufs-phy",
		.data = &exynosautov9_ufs_phy,
	}, {
		.compatible = "tesla,fsd-ufs-phy",
		.data = &fsd_ufs_phy,
	},
	{},
};
MODULE_DEVICE_TABLE(of, samsung_ufs_phy_match);

static struct platform_driver samsung_ufs_phy_driver = {
	.probe  = samsung_ufs_phy_probe,
	.driver = {
		.name = "samsung-ufs-phy",
		.of_match_table = samsung_ufs_phy_match,
	},
};
module_platform_driver(samsung_ufs_phy_driver);
MODULE_DESCRIPTION("Samsung SoC UFS PHY Driver");
MODULE_AUTHOR("Seungwon Jeon <essuuj@gmail.com>");
MODULE_AUTHOR("Alim Akhtar <alim.akhtar@samsung.com>");
MODULE_LICENSE("GPL v2");

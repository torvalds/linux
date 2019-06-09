// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 */

#include <linux/of_device.h>

#include "hdmi.h"

static int msm_hdmi_phy_resource_init(struct hdmi_phy *phy)
{
	struct hdmi_phy_cfg *cfg = phy->cfg;
	struct device *dev = &phy->pdev->dev;
	int i, ret;

	phy->regs = devm_kcalloc(dev, cfg->num_regs, sizeof(phy->regs[0]),
				 GFP_KERNEL);
	if (!phy->regs)
		return -ENOMEM;

	phy->clks = devm_kcalloc(dev, cfg->num_clks, sizeof(phy->clks[0]),
				 GFP_KERNEL);
	if (!phy->clks)
		return -ENOMEM;

	for (i = 0; i < cfg->num_regs; i++) {
		struct regulator *reg;

		reg = devm_regulator_get(dev, cfg->reg_names[i]);
		if (IS_ERR(reg)) {
			ret = PTR_ERR(reg);
			DRM_DEV_ERROR(dev, "failed to get phy regulator: %s (%d)\n",
				cfg->reg_names[i], ret);
			return ret;
		}

		phy->regs[i] = reg;
	}

	for (i = 0; i < cfg->num_clks; i++) {
		struct clk *clk;

		clk = msm_clk_get(phy->pdev, cfg->clk_names[i]);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			DRM_DEV_ERROR(dev, "failed to get phy clock: %s (%d)\n",
				cfg->clk_names[i], ret);
			return ret;
		}

		phy->clks[i] = clk;
	}

	return 0;
}

int msm_hdmi_phy_resource_enable(struct hdmi_phy *phy)
{
	struct hdmi_phy_cfg *cfg = phy->cfg;
	struct device *dev = &phy->pdev->dev;
	int i, ret = 0;

	pm_runtime_get_sync(dev);

	for (i = 0; i < cfg->num_regs; i++) {
		ret = regulator_enable(phy->regs[i]);
		if (ret)
			DRM_DEV_ERROR(dev, "failed to enable regulator: %s (%d)\n",
				cfg->reg_names[i], ret);
	}

	for (i = 0; i < cfg->num_clks; i++) {
		ret = clk_prepare_enable(phy->clks[i]);
		if (ret)
			DRM_DEV_ERROR(dev, "failed to enable clock: %s (%d)\n",
				cfg->clk_names[i], ret);
	}

	return ret;
}

void msm_hdmi_phy_resource_disable(struct hdmi_phy *phy)
{
	struct hdmi_phy_cfg *cfg = phy->cfg;
	struct device *dev = &phy->pdev->dev;
	int i;

	for (i = cfg->num_clks - 1; i >= 0; i--)
		clk_disable_unprepare(phy->clks[i]);

	for (i = cfg->num_regs - 1; i >= 0; i--)
		regulator_disable(phy->regs[i]);

	pm_runtime_put_sync(dev);
}

void msm_hdmi_phy_powerup(struct hdmi_phy *phy, unsigned long int pixclock)
{
	if (!phy || !phy->cfg->powerup)
		return;

	phy->cfg->powerup(phy, pixclock);
}

void msm_hdmi_phy_powerdown(struct hdmi_phy *phy)
{
	if (!phy || !phy->cfg->powerdown)
		return;

	phy->cfg->powerdown(phy);
}

static int msm_hdmi_phy_pll_init(struct platform_device *pdev,
			     enum hdmi_phy_type type)
{
	int ret;

	switch (type) {
	case MSM_HDMI_PHY_8960:
		ret = msm_hdmi_pll_8960_init(pdev);
		break;
	case MSM_HDMI_PHY_8996:
		ret = msm_hdmi_pll_8996_init(pdev);
		break;
	/*
	 * we don't have PLL support for these, don't report an error for now
	 */
	case MSM_HDMI_PHY_8x60:
	case MSM_HDMI_PHY_8x74:
	default:
		ret = 0;
		break;
	}

	return ret;
}

static int msm_hdmi_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hdmi_phy *phy;
	int ret;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENODEV;

	phy->cfg = (struct hdmi_phy_cfg *)of_device_get_match_data(dev);
	if (!phy->cfg)
		return -ENODEV;

	phy->mmio = msm_ioremap(pdev, "hdmi_phy", "HDMI_PHY");
	if (IS_ERR(phy->mmio)) {
		DRM_DEV_ERROR(dev, "%s: failed to map phy base\n", __func__);
		return -ENOMEM;
	}

	phy->pdev = pdev;

	ret = msm_hdmi_phy_resource_init(phy);
	if (ret)
		return ret;

	pm_runtime_enable(&pdev->dev);

	ret = msm_hdmi_phy_resource_enable(phy);
	if (ret)
		return ret;

	ret = msm_hdmi_phy_pll_init(pdev, phy->cfg->type);
	if (ret) {
		DRM_DEV_ERROR(dev, "couldn't init PLL\n");
		msm_hdmi_phy_resource_disable(phy);
		return ret;
	}

	msm_hdmi_phy_resource_disable(phy);

	platform_set_drvdata(pdev, phy);

	return 0;
}

static int msm_hdmi_phy_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id msm_hdmi_phy_dt_match[] = {
	{ .compatible = "qcom,hdmi-phy-8660",
	  .data = &msm_hdmi_phy_8x60_cfg },
	{ .compatible = "qcom,hdmi-phy-8960",
	  .data = &msm_hdmi_phy_8960_cfg },
	{ .compatible = "qcom,hdmi-phy-8974",
	  .data = &msm_hdmi_phy_8x74_cfg },
	{ .compatible = "qcom,hdmi-phy-8084",
	  .data = &msm_hdmi_phy_8x74_cfg },
	{ .compatible = "qcom,hdmi-phy-8996",
	  .data = &msm_hdmi_phy_8996_cfg },
	{}
};

static struct platform_driver msm_hdmi_phy_platform_driver = {
	.probe      = msm_hdmi_phy_probe,
	.remove     = msm_hdmi_phy_remove,
	.driver     = {
		.name   = "msm_hdmi_phy",
		.of_match_table = msm_hdmi_phy_dt_match,
	},
};

void __init msm_hdmi_phy_driver_register(void)
{
	platform_driver_register(&msm_hdmi_phy_platform_driver);
}

void __exit msm_hdmi_phy_driver_unregister(void)
{
	platform_driver_unregister(&msm_hdmi_phy_platform_driver);
}

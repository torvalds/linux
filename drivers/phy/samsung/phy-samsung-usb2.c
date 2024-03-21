// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung SoC USB 1.1/2.0 PHY driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Author: Kamil Debski <k.debski@samsung.com>
 */

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include "phy-samsung-usb2.h"

static int samsung_usb2_phy_power_on(struct phy *phy)
{
	struct samsung_usb2_phy_instance *inst = phy_get_drvdata(phy);
	struct samsung_usb2_phy_driver *drv = inst->drv;
	int ret;

	dev_dbg(drv->dev, "Request to power_on \"%s\" usb phy\n",
		inst->cfg->label);

	if (drv->vbus) {
		ret = regulator_enable(drv->vbus);
		if (ret)
			goto err_regulator;
	}

	ret = clk_prepare_enable(drv->clk);
	if (ret)
		goto err_main_clk;
	ret = clk_prepare_enable(drv->ref_clk);
	if (ret)
		goto err_instance_clk;
	if (inst->cfg->power_on) {
		spin_lock(&drv->lock);
		ret = inst->cfg->power_on(inst);
		spin_unlock(&drv->lock);
		if (ret)
			goto err_power_on;
	}

	return 0;

err_power_on:
	clk_disable_unprepare(drv->ref_clk);
err_instance_clk:
	clk_disable_unprepare(drv->clk);
err_main_clk:
	if (drv->vbus)
		regulator_disable(drv->vbus);
err_regulator:
	return ret;
}

static int samsung_usb2_phy_power_off(struct phy *phy)
{
	struct samsung_usb2_phy_instance *inst = phy_get_drvdata(phy);
	struct samsung_usb2_phy_driver *drv = inst->drv;
	int ret = 0;

	dev_dbg(drv->dev, "Request to power_off \"%s\" usb phy\n",
		inst->cfg->label);
	if (inst->cfg->power_off) {
		spin_lock(&drv->lock);
		ret = inst->cfg->power_off(inst);
		spin_unlock(&drv->lock);
		if (ret)
			return ret;
	}
	clk_disable_unprepare(drv->ref_clk);
	clk_disable_unprepare(drv->clk);
	if (drv->vbus)
		ret = regulator_disable(drv->vbus);

	return ret;
}

static const struct phy_ops samsung_usb2_phy_ops = {
	.power_on	= samsung_usb2_phy_power_on,
	.power_off	= samsung_usb2_phy_power_off,
	.owner		= THIS_MODULE,
};

static struct phy *samsung_usb2_phy_xlate(struct device *dev,
					const struct of_phandle_args *args)
{
	struct samsung_usb2_phy_driver *drv;

	drv = dev_get_drvdata(dev);
	if (!drv)
		return ERR_PTR(-EINVAL);

	if (WARN_ON(args->args[0] >= drv->cfg->num_phys))
		return ERR_PTR(-ENODEV);

	return drv->instances[args->args[0]].phy;
}

static const struct of_device_id samsung_usb2_phy_of_match[] = {
#ifdef CONFIG_PHY_EXYNOS4X12_USB2
	{
		.compatible = "samsung,exynos3250-usb2-phy",
		.data = &exynos3250_usb2_phy_config,
	},
#endif
#ifdef CONFIG_PHY_EXYNOS4210_USB2
	{
		.compatible = "samsung,exynos4210-usb2-phy",
		.data = &exynos4210_usb2_phy_config,
	},
#endif
#ifdef CONFIG_PHY_EXYNOS4X12_USB2
	{
		.compatible = "samsung,exynos4x12-usb2-phy",
		.data = &exynos4x12_usb2_phy_config,
	},
#endif
#ifdef CONFIG_PHY_EXYNOS5250_USB2
	{
		.compatible = "samsung,exynos5250-usb2-phy",
		.data = &exynos5250_usb2_phy_config,
	},
	{
		.compatible = "samsung,exynos5420-usb2-phy",
		.data = &exynos5420_usb2_phy_config,
	},
#endif
#ifdef CONFIG_PHY_S5PV210_USB2
	{
		.compatible = "samsung,s5pv210-usb2-phy",
		.data = &s5pv210_usb2_phy_config,
	},
#endif
	{ },
};
MODULE_DEVICE_TABLE(of, samsung_usb2_phy_of_match);

static int samsung_usb2_phy_probe(struct platform_device *pdev)
{
	const struct samsung_usb2_phy_config *cfg;
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	struct samsung_usb2_phy_driver *drv;
	int i, ret;

	if (!pdev->dev.of_node) {
		dev_err(dev, "This driver is required to be instantiated from device tree\n");
		return -EINVAL;
	}

	cfg = of_device_get_match_data(dev);
	if (!cfg)
		return -EINVAL;

	drv = devm_kzalloc(dev, struct_size(drv, instances, cfg->num_phys),
			   GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	dev_set_drvdata(dev, drv);
	spin_lock_init(&drv->lock);

	drv->cfg = cfg;
	drv->dev = dev;

	drv->reg_phy = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drv->reg_phy)) {
		dev_err(dev, "Failed to map register memory (phy)\n");
		return PTR_ERR(drv->reg_phy);
	}

	drv->reg_pmu = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
		"samsung,pmureg-phandle");
	if (IS_ERR(drv->reg_pmu)) {
		dev_err(dev, "Failed to map PMU registers (via syscon)\n");
		return PTR_ERR(drv->reg_pmu);
	}

	if (drv->cfg->has_mode_switch) {
		drv->reg_sys = syscon_regmap_lookup_by_phandle(
				pdev->dev.of_node, "samsung,sysreg-phandle");
		if (IS_ERR(drv->reg_sys)) {
			dev_err(dev, "Failed to map system registers (via syscon)\n");
			return PTR_ERR(drv->reg_sys);
		}
	}

	drv->clk = devm_clk_get(dev, "phy");
	if (IS_ERR(drv->clk)) {
		dev_err(dev, "Failed to get clock of phy controller\n");
		return PTR_ERR(drv->clk);
	}

	drv->ref_clk = devm_clk_get(dev, "ref");
	if (IS_ERR(drv->ref_clk)) {
		dev_err(dev, "Failed to get reference clock for the phy controller\n");
		return PTR_ERR(drv->ref_clk);
	}

	drv->ref_rate = clk_get_rate(drv->ref_clk);
	if (drv->cfg->rate_to_clk) {
		ret = drv->cfg->rate_to_clk(drv->ref_rate, &drv->ref_reg_val);
		if (ret)
			return ret;
	}

	drv->vbus = devm_regulator_get(dev, "vbus");
	if (IS_ERR(drv->vbus)) {
		ret = PTR_ERR(drv->vbus);
		if (ret == -EPROBE_DEFER)
			return ret;
		drv->vbus = NULL;
	}

	for (i = 0; i < drv->cfg->num_phys; i++) {
		char *label = drv->cfg->phys[i].label;
		struct samsung_usb2_phy_instance *p = &drv->instances[i];

		dev_dbg(dev, "Creating phy \"%s\"\n", label);
		p->phy = devm_phy_create(dev, NULL, &samsung_usb2_phy_ops);
		if (IS_ERR(p->phy)) {
			dev_err(drv->dev, "Failed to create usb2_phy \"%s\"\n",
				label);
			return PTR_ERR(p->phy);
		}

		p->cfg = &drv->cfg->phys[i];
		p->drv = drv;
		phy_set_bus_width(p->phy, 8);
		phy_set_drvdata(p->phy, p);
	}

	phy_provider = devm_of_phy_provider_register(dev,
							samsung_usb2_phy_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(drv->dev, "Failed to register phy provider\n");
		return PTR_ERR(phy_provider);
	}

	return 0;
}

static struct platform_driver samsung_usb2_phy_driver = {
	.probe	= samsung_usb2_phy_probe,
	.driver = {
		.of_match_table	= samsung_usb2_phy_of_match,
		.name		= "samsung-usb2-phy",
		.suppress_bind_attrs = true,
	}
};

module_platform_driver(samsung_usb2_phy_driver);
MODULE_DESCRIPTION("Samsung S5P/Exynos SoC USB PHY driver");
MODULE_AUTHOR("Kamil Debski <k.debski@samsung.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:samsung-usb2-phy");

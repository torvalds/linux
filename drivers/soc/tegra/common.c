// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 NVIDIA CORPORATION.  All rights reserved.
 */

#define dev_fmt(fmt)	"tegra-soc: " fmt

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/of.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>

#include <soc/tegra/common.h>
#include <soc/tegra/fuse.h>

static const struct of_device_id tegra_machine_match[] = {
	{ .compatible = "nvidia,tegra20", },
	{ .compatible = "nvidia,tegra30", },
	{ .compatible = "nvidia,tegra114", },
	{ .compatible = "nvidia,tegra124", },
	{ .compatible = "nvidia,tegra132", },
	{ .compatible = "nvidia,tegra210", },
	{ }
};

bool soc_is_tegra(void)
{
	const struct of_device_id *match;
	struct device_node *root;

	root = of_find_node_by_path("/");
	if (!root)
		return false;

	match = of_match_node(tegra_machine_match, root);
	of_node_put(root);

	return match != NULL;
}

static int tegra_core_dev_init_opp_state(struct device *dev)
{
	unsigned long rate;
	struct clk *clk;
	bool rpm_enabled;
	int err;

	clk = devm_clk_get(dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get clk: %pe\n", clk);
		return PTR_ERR(clk);
	}

	rate = clk_get_rate(clk);
	if (!rate) {
		dev_err(dev, "failed to get clk rate\n");
		return -EINVAL;
	}

	/*
	 * Runtime PM of the device must be enabled in order to set up
	 * GENPD's performance properly because GENPD core checks whether
	 * device is suspended and this check doesn't work while RPM is
	 * disabled. This makes sure the OPP vote below gets cached in
	 * GENPD for the device. Instead, the vote is done the next time
	 * the device gets runtime resumed.
	 */
	rpm_enabled = pm_runtime_enabled(dev);
	if (!rpm_enabled)
		pm_runtime_enable(dev);

	/* should never happen in practice */
	if (!pm_runtime_enabled(dev)) {
		dev_WARN(dev, "failed to enable runtime PM\n");
		pm_runtime_disable(dev);
		return -EINVAL;
	}

	/* first dummy rate-setting initializes voltage vote */
	err = dev_pm_opp_set_rate(dev, rate);

	if (!rpm_enabled)
		pm_runtime_disable(dev);

	if (err) {
		dev_err(dev, "failed to initialize OPP clock: %d\n", err);
		return err;
	}

	return 0;
}

/**
 * devm_tegra_core_dev_init_opp_table() - initialize OPP table
 * @dev: device for which OPP table is initialized
 * @params: pointer to the OPP table configuration
 *
 * This function will initialize OPP table and sync OPP state of a Tegra SoC
 * core device.
 *
 * Return: 0 on success or errorno.
 */
int devm_tegra_core_dev_init_opp_table(struct device *dev,
				       struct tegra_core_opp_params *params)
{
	u32 hw_version;
	int err;

	err = devm_pm_opp_set_clkname(dev, NULL);
	if (err) {
		dev_err(dev, "failed to set OPP clk: %d\n", err);
		return err;
	}

	/* Tegra114+ doesn't support OPP yet */
	if (!of_machine_is_compatible("nvidia,tegra20") &&
	    !of_machine_is_compatible("nvidia,tegra30"))
		return -ENODEV;

	if (of_machine_is_compatible("nvidia,tegra20"))
		hw_version = BIT(tegra_sku_info.soc_process_id);
	else
		hw_version = BIT(tegra_sku_info.soc_speedo_id);

	err = devm_pm_opp_set_supported_hw(dev, &hw_version, 1);
	if (err) {
		dev_err(dev, "failed to set OPP supported HW: %d\n", err);
		return err;
	}

	/*
	 * Older device-trees have an empty OPP table, we will get
	 * -ENODEV from devm_pm_opp_of_add_table() in this case.
	 */
	err = devm_pm_opp_of_add_table(dev);
	if (err) {
		if (err != -ENODEV)
			dev_err(dev, "failed to add OPP table: %d\n", err);

		return err;
	}

	if (params->init_state) {
		err = tegra_core_dev_init_opp_state(dev);
		if (err)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(devm_tegra_core_dev_init_opp_table);

/*
 * Tegra124 DFLL FCPU clock source driver
 *
 * Copyright (C) 2012-2014 NVIDIA Corporation.  All rights reserved.
 *
 * Aleksandr Frid <afrid@nvidia.com>
 * Paul Walmsley <pwalmsley@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <soc/tegra/fuse.h>

#include "clk.h"
#include "clk-dfll.h"
#include "cvb.h"

/* Maximum CPU frequency, indexed by CPU speedo id */
static const unsigned long cpu_max_freq_table[] = {
	[0] = 2014500000UL,
	[1] = 2320500000UL,
	[2] = 2116500000UL,
	[3] = 2524500000UL,
};

static const struct cvb_table tegra124_cpu_cvb_tables[] = {
	{
		.speedo_id = -1,
		.process_id = -1,
		.min_millivolts = 900,
		.max_millivolts = 1260,
		.alignment = {
			.step_uv = 10000, /* 10mV */
		},
		.speedo_scale = 100,
		.voltage_scale = 1000,
		.entries = {
			{  204000000UL, { 1112619, -29295, 402 } },
			{  306000000UL, { 1150460, -30585, 402 } },
			{  408000000UL, { 1190122, -31865, 402 } },
			{  510000000UL, { 1231606, -33155, 402 } },
			{  612000000UL, { 1274912, -34435, 402 } },
			{  714000000UL, { 1320040, -35725, 402 } },
			{  816000000UL, { 1366990, -37005, 402 } },
			{  918000000UL, { 1415762, -38295, 402 } },
			{ 1020000000UL, { 1466355, -39575, 402 } },
			{ 1122000000UL, { 1518771, -40865, 402 } },
			{ 1224000000UL, { 1573009, -42145, 402 } },
			{ 1326000000UL, { 1629068, -43435, 402 } },
			{ 1428000000UL, { 1686950, -44715, 402 } },
			{ 1530000000UL, { 1746653, -46005, 402 } },
			{ 1632000000UL, { 1808179, -47285, 402 } },
			{ 1734000000UL, { 1871526, -48575, 402 } },
			{ 1836000000UL, { 1936696, -49855, 402 } },
			{ 1938000000UL, { 2003687, -51145, 402 } },
			{ 2014500000UL, { 2054787, -52095, 402 } },
			{ 2116500000UL, { 2124957, -53385, 402 } },
			{ 2218500000UL, { 2196950, -54665, 402 } },
			{ 2320500000UL, { 2270765, -55955, 402 } },
			{ 2422500000UL, { 2346401, -57235, 402 } },
			{ 2524500000UL, { 2437299, -58535, 402 } },
			{          0UL, {       0,      0,   0 } },
		},
		.cpu_dfll_data = {
			.tune0_low = 0x005020ff,
			.tune0_high = 0x005040ff,
			.tune1 = 0x00000060,
		}
	},
};

static int tegra124_dfll_fcpu_probe(struct platform_device *pdev)
{
	int process_id, speedo_id, speedo_value, err;
	struct tegra_dfll_soc_data *soc;

	process_id = tegra_sku_info.cpu_process_id;
	speedo_id = tegra_sku_info.cpu_speedo_id;
	speedo_value = tegra_sku_info.cpu_speedo_value;

	if (speedo_id >= ARRAY_SIZE(cpu_max_freq_table)) {
		dev_err(&pdev->dev, "unknown max CPU freq for speedo_id=%d\n",
			speedo_id);
		return -ENODEV;
	}

	soc = devm_kzalloc(&pdev->dev, sizeof(*soc), GFP_KERNEL);
	if (!soc)
		return -ENOMEM;

	soc->dev = get_cpu_device(0);
	if (!soc->dev) {
		dev_err(&pdev->dev, "no CPU0 device\n");
		return -ENODEV;
	}

	soc->max_freq = cpu_max_freq_table[speedo_id];

	soc->cvb = tegra_cvb_add_opp_table(soc->dev, tegra124_cpu_cvb_tables,
					   ARRAY_SIZE(tegra124_cpu_cvb_tables),
					   process_id, speedo_id, speedo_value,
					   soc->max_freq);
	if (IS_ERR(soc->cvb)) {
		dev_err(&pdev->dev, "couldn't add OPP table: %ld\n",
			PTR_ERR(soc->cvb));
		return PTR_ERR(soc->cvb);
	}

	err = tegra_dfll_register(pdev, soc);
	if (err < 0) {
		tegra_cvb_remove_opp_table(soc->dev, soc->cvb, soc->max_freq);
		return err;
	}

	return 0;
}

static int tegra124_dfll_fcpu_remove(struct platform_device *pdev)
{
	struct tegra_dfll_soc_data *soc;

	soc = tegra_dfll_unregister(pdev);
	if (IS_ERR(soc)) {
		dev_err(&pdev->dev, "failed to unregister DFLL: %ld\n",
			PTR_ERR(soc));
		return PTR_ERR(soc);
	}

	tegra_cvb_remove_opp_table(soc->dev, soc->cvb, soc->max_freq);

	return 0;
}

static const struct of_device_id tegra124_dfll_fcpu_of_match[] = {
	{ .compatible = "nvidia,tegra124-dfll", },
	{ },
};

static const struct dev_pm_ops tegra124_dfll_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_dfll_runtime_suspend,
			   tegra_dfll_runtime_resume, NULL)
};

static struct platform_driver tegra124_dfll_fcpu_driver = {
	.probe = tegra124_dfll_fcpu_probe,
	.remove = tegra124_dfll_fcpu_remove,
	.driver = {
		.name = "tegra124-dfll",
		.of_match_table = tegra124_dfll_fcpu_of_match,
		.pm = &tegra124_dfll_pm_ops,
	},
};
builtin_platform_driver(tegra124_dfll_fcpu_driver);

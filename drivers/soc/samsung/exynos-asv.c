// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Samsung Electronics Co., Ltd.
 *	      http://www.samsung.com/
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * Samsung Exynos SoC Adaptive Supply Voltage support
 */

#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regmap.h>
#include <linux/soc/samsung/exynos-chipid.h>

#include "exynos-asv.h"
#include "exynos5422-asv.h"

#define MHZ 1000000U

static int exynos_asv_update_cpu_opps(struct exynos_asv *asv,
				      struct device *cpu)
{
	struct exynos_asv_subsys *subsys = NULL;
	struct dev_pm_opp *opp;
	unsigned int opp_freq;
	int i;

	for (i = 0; i < ARRAY_SIZE(asv->subsys); i++) {
		if (of_device_is_compatible(cpu->of_node,
					    asv->subsys[i].cpu_dt_compat)) {
			subsys = &asv->subsys[i];
			break;
		}
	}
	if (!subsys)
		return -EINVAL;

	for (i = 0; i < subsys->table.num_rows; i++) {
		unsigned int new_volt, volt;
		int ret;

		opp_freq = exynos_asv_opp_get_frequency(subsys, i);

		opp = dev_pm_opp_find_freq_exact(cpu, opp_freq * MHZ, true);
		if (IS_ERR(opp)) {
			dev_info(asv->dev, "cpu%d opp%d, freq: %u missing\n",
				 cpu->id, i, opp_freq);

			continue;
		}

		volt = dev_pm_opp_get_voltage(opp);
		new_volt = asv->opp_get_voltage(subsys, i, volt);
		dev_pm_opp_put(opp);

		if (new_volt == volt)
			continue;

		ret = dev_pm_opp_adjust_voltage(cpu, opp_freq * MHZ,
						new_volt, new_volt, new_volt);
		if (ret < 0)
			dev_err(asv->dev,
				"Failed to adjust OPP %u Hz/%u uV for cpu%d\n",
				opp_freq, new_volt, cpu->id);
		else
			dev_dbg(asv->dev,
				"Adjusted OPP %u Hz/%u -> %u uV, cpu%d\n",
				opp_freq, volt, new_volt, cpu->id);
	}

	return 0;
}

static int exynos_asv_update_opps(struct exynos_asv *asv)
{
	struct opp_table *last_opp_table = NULL;
	struct device *cpu;
	int ret, cpuid;

	for_each_possible_cpu(cpuid) {
		struct opp_table *opp_table;

		cpu = get_cpu_device(cpuid);
		if (!cpu)
			continue;

		opp_table = dev_pm_opp_get_opp_table(cpu);
		if (IS_ERR(opp_table))
			continue;

		if (!last_opp_table || opp_table != last_opp_table) {
			last_opp_table = opp_table;

			ret = exynos_asv_update_cpu_opps(asv, cpu);
			if (ret < 0)
				dev_err(asv->dev, "Couldn't udate OPPs for cpu%d\n",
					cpuid);
		}

		dev_pm_opp_put_opp_table(opp_table);
	}

	return	0;
}

static int exynos_asv_probe(struct platform_device *pdev)
{
	int (*probe_func)(struct exynos_asv *asv);
	struct exynos_asv *asv;
	struct device *cpu_dev;
	u32 product_id = 0;
	int ret, i;

	asv = devm_kzalloc(&pdev->dev, sizeof(*asv), GFP_KERNEL);
	if (!asv)
		return -ENOMEM;

	asv->chipid_regmap = device_node_to_regmap(pdev->dev.of_node);
	if (IS_ERR(asv->chipid_regmap)) {
		dev_err(&pdev->dev, "Could not find syscon regmap\n");
		return PTR_ERR(asv->chipid_regmap);
	}

	ret = regmap_read(asv->chipid_regmap, EXYNOS_CHIPID_REG_PRO_ID,
			  &product_id);
	if (ret < 0) {
		dev_err(&pdev->dev, "Cannot read revision from ChipID: %d\n",
			ret);
		return -ENODEV;
	}

	switch (product_id & EXYNOS_MASK) {
	case 0xE5422000:
		probe_func = exynos5422_asv_init;
		break;
	default:
		return -ENODEV;
	}

	cpu_dev = get_cpu_device(0);
	ret = dev_pm_opp_get_opp_count(cpu_dev);
	if (ret < 0)
		return -EPROBE_DEFER;

	ret = of_property_read_u32(pdev->dev.of_node, "samsung,asv-bin",
				   &asv->of_bin);
	if (ret < 0)
		asv->of_bin = -EINVAL;

	asv->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, asv);

	for (i = 0; i < ARRAY_SIZE(asv->subsys); i++)
		asv->subsys[i].asv = asv;

	ret = probe_func(asv);
	if (ret < 0)
		return ret;

	return exynos_asv_update_opps(asv);
}

static const struct of_device_id exynos_asv_of_device_ids[] = {
	{ .compatible = "samsung,exynos4210-chipid" },
	{}
};

static struct platform_driver exynos_asv_driver = {
	.driver = {
		.name = "exynos-asv",
		.of_match_table = exynos_asv_of_device_ids,
	},
	.probe	= exynos_asv_probe,
};
module_platform_driver(exynos_asv_driver);

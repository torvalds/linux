// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARM Integrator Logical Module bus driver
 * Copyright (C) 2020 Linaro Ltd.
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * See the device tree bindings for this block for more details on the
 * hardware.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/bitops.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

/* All information about the connected logic modules are in here */
#define INTEGRATOR_SC_DEC_OFFSET	0x10

/* Base address for the expansion modules */
#define INTEGRATOR_AP_EXP_BASE		0xc0000000
#define INTEGRATOR_AP_EXP_STRIDE	0x10000000

static int integrator_lm_populate(int num, struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *child;
	u32 base;
	int ret;

	base = INTEGRATOR_AP_EXP_BASE + (num * INTEGRATOR_AP_EXP_STRIDE);

	/* Walk over the child nodes and see what chipselects we use */
	for_each_available_child_of_node(np, child) {
		struct resource res;

		ret = of_address_to_resource(child, 0, &res);
		if (ret) {
			dev_info(dev, "no valid address on child\n");
			continue;
		}

		/* First populate the syscon then any devices */
		if (res.start == base) {
			dev_info(dev, "populate module @0x%08x from DT\n",
				 base);
			ret = of_platform_default_populate(child, NULL, dev);
			if (ret) {
				dev_err(dev, "failed to populate module\n");
				of_node_put(child);
				return ret;
			}
		}
	}

	return 0;
}

static const struct of_device_id integrator_ap_syscon_match[] = {
	{ .compatible = "arm,integrator-ap-syscon"},
	{ },
};

static int integrator_ap_lm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *syscon;
	static struct regmap *map;
	u32 val;
	int ret;
	int i;

	/* Look up the system controller */
	syscon = of_find_matching_node(NULL, integrator_ap_syscon_match);
	if (!syscon) {
		dev_err(dev,
			"could not find Integrator/AP system controller\n");
		return -ENODEV;
	}
	map = syscon_node_to_regmap(syscon);
	if (IS_ERR(map)) {
		dev_err(dev,
			"could not find Integrator/AP system controller\n");
		return PTR_ERR(map);
	}

	ret = regmap_read(map, INTEGRATOR_SC_DEC_OFFSET, &val);
	if (ret) {
		dev_err(dev, "could not read from Integrator/AP syscon\n");
		return ret;
	}

	/* Loop over the connected modules */
	for (i = 0; i < 4; i++) {
		if (!(val & BIT(4 + i)))
			continue;

		dev_info(dev, "detected module in slot %d\n", i);
		ret = integrator_lm_populate(i, dev);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct of_device_id integrator_ap_lm_match[] = {
	{ .compatible = "arm,integrator-ap-lm"},
	{ },
};

static struct platform_driver integrator_ap_lm_driver = {
	.probe = integrator_ap_lm_probe,
	.driver = {
		.name = "integratorap-lm",
		.of_match_table = integrator_ap_lm_match,
	},
};
module_platform_driver(integrator_ap_lm_driver);
MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("Integrator AP Logical Module driver");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Generic Syscon Poweroff Driver
 *
 * Copyright (c) 2015, National Instruments Corp.
 * Author: Moritz Fischer <moritz.fischer@ettus.com>
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/notifier.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regmap.h>

static struct regmap *map;
static u32 offset;
static u32 value;
static u32 mask;

static void syscon_poweroff(void)
{
	/* Issue the poweroff */
	regmap_update_bits(map, offset, mask, value);

	mdelay(1000);

	pr_emerg("Unable to poweroff system\n");
}

static int syscon_poweroff_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int mask_err, value_err;

	map = syscon_regmap_lookup_by_phandle(dev->of_node, "regmap");
	if (IS_ERR(map)) {
		map = syscon_node_to_regmap(dev->parent->of_node);
		if (IS_ERR(map)) {
			dev_err(dev, "unable to get syscon");
			return PTR_ERR(map);
		}
	}

	if (of_property_read_u32(dev->of_node, "offset", &offset)) {
		dev_err(dev, "unable to read 'offset'");
		return -EINVAL;
	}

	value_err = of_property_read_u32(dev->of_node, "value", &value);
	mask_err = of_property_read_u32(dev->of_node, "mask", &mask);
	if (value_err && mask_err) {
		dev_err(dev, "unable to read 'value' and 'mask'");
		return -EINVAL;
	}

	if (value_err) {
		/* support old binding */
		value = mask;
		mask = 0xFFFFFFFF;
	} else if (mask_err) {
		/* support value without mask*/
		mask = 0xFFFFFFFF;
	}

	if (pm_power_off) {
		dev_err(dev, "pm_power_off already claimed for %ps",
			pm_power_off);
		return -EBUSY;
	}

	pm_power_off = syscon_poweroff;

	return 0;
}

static void syscon_poweroff_remove(struct platform_device *pdev)
{
	if (pm_power_off == syscon_poweroff)
		pm_power_off = NULL;
}

static const struct of_device_id syscon_poweroff_of_match[] = {
	{ .compatible = "syscon-poweroff" },
	{}
};

static struct platform_driver syscon_poweroff_driver = {
	.probe = syscon_poweroff_probe,
	.remove_new = syscon_poweroff_remove,
	.driver = {
		.name = "syscon-poweroff",
		.of_match_table = syscon_poweroff_of_match,
	},
};
builtin_platform_driver(syscon_poweroff_driver);

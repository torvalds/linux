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
#include <linux/reboot.h>
#include <linux/regmap.h>

struct syscon_poweroff_data {
	struct regmap *map;
	u32 offset;
	u32 value;
	u32 mask;
};

static int syscon_poweroff(struct sys_off_data *off_data)
{
	struct syscon_poweroff_data *data = off_data->cb_data;

	/* Issue the poweroff */
	regmap_update_bits(data->map, data->offset, data->mask, data->value);

	mdelay(1000);

	pr_emerg("Unable to poweroff system\n");

	return NOTIFY_DONE;
}

static int syscon_poweroff_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct syscon_poweroff_data *data;
	int mask_err, value_err;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->map = syscon_regmap_lookup_by_phandle(dev->of_node, "regmap");
	if (IS_ERR(data->map)) {
		data->map = syscon_node_to_regmap(dev->parent->of_node);
		if (IS_ERR(data->map)) {
			dev_err(dev, "unable to get syscon");
			return PTR_ERR(data->map);
		}
	}

	if (of_property_read_u32(dev->of_node, "offset", &data->offset)) {
		dev_err(dev, "unable to read 'offset'");
		return -EINVAL;
	}

	value_err = of_property_read_u32(dev->of_node, "value", &data->value);
	mask_err = of_property_read_u32(dev->of_node, "mask", &data->mask);
	if (value_err && mask_err) {
		dev_err(dev, "unable to read 'value' and 'mask'");
		return -EINVAL;
	}

	if (value_err) {
		/* support old binding */
		data->value = data->mask;
		data->mask = 0xFFFFFFFF;
	} else if (mask_err) {
		/* support value without mask*/
		data->mask = 0xFFFFFFFF;
	}

	return devm_register_sys_off_handler(&pdev->dev,
					     SYS_OFF_MODE_POWER_OFF,
					     SYS_OFF_PRIO_DEFAULT,
					     syscon_poweroff, data);
}

static const struct of_device_id syscon_poweroff_of_match[] = {
	{ .compatible = "syscon-poweroff" },
	{}
};

static struct platform_driver syscon_poweroff_driver = {
	.probe = syscon_poweroff_probe,
	.driver = {
		.name = "syscon-poweroff",
		.of_match_table = syscon_poweroff_of_match,
	},
};
builtin_platform_driver(syscon_poweroff_driver);

// SPDX-License-Identifier: GPL-2.0
/*
 * Intel PCH pinctrl/GPIO driver
 *
 * Copyright (C) 2021-2023, Intel Corporation
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/property.h>
#include <linux/string_helpers.h>

#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-intel.h"

struct intel_platform_pins {
	struct pinctrl_pin_desc *pins;
	size_t npins;
};

static int intel_platform_pinctrl_prepare_pins(struct device *dev, size_t base,
					       const char *name, u32 size,
					       struct intel_platform_pins *pins)
{
	struct pinctrl_pin_desc *descs;
	char **pin_names;
	unsigned int i;

	pin_names = devm_kasprintf_strarray(dev, name, size);
	if (IS_ERR(pin_names))
		return PTR_ERR(pin_names);

	descs = devm_krealloc_array(dev, pins->pins, base + size, sizeof(*descs), GFP_KERNEL);
	if (!descs)
		return -ENOMEM;

	for (i = 0; i < size; i++) {
		unsigned int pin_number = base + i;
		char *pin_name = pin_names[i];
		struct pinctrl_pin_desc *desc;

		/* Unify delimiter for pin name */
		strreplace(pin_name, '-', '_');

		desc = &descs[pin_number];
		desc->number = pin_number;
		desc->name = pin_name;
	}

	pins->pins = descs;
	pins->npins = base + size;

	return 0;
}

static int intel_platform_pinctrl_prepare_group(struct device *dev,
						struct fwnode_handle *child,
						struct intel_padgroup *gpp,
						struct intel_platform_pins *pins)
{
	size_t base = pins->npins;
	const char *name;
	u32 size;
	int ret;

	ret = fwnode_property_read_string(child, "intc-gpio-group-name", &name);
	if (ret)
		return ret;

	ret = fwnode_property_read_u32(child, "intc-gpio-pad-count", &size);
	if (ret)
		return ret;

	ret = intel_platform_pinctrl_prepare_pins(dev, base, name, size, pins);
	if (ret)
		return ret;

	gpp->base = base;
	gpp->size = size;
	gpp->gpio_base = INTEL_GPIO_BASE_MATCH;

	return 0;
}

static int intel_platform_pinctrl_prepare_community(struct device *dev,
						    struct intel_community *community,
						    struct intel_platform_pins *pins)
{
	struct fwnode_handle *child;
	struct intel_padgroup *gpps;
	unsigned int group;
	size_t ngpps;
	u32 offset;
	int ret;

	ret = device_property_read_u32(dev, "intc-gpio-pad-ownership-offset", &offset);
	if (ret)
		return ret;
	community->padown_offset = offset;

	ret = device_property_read_u32(dev, "intc-gpio-pad-configuration-lock-offset", &offset);
	if (ret)
		return ret;
	community->padcfglock_offset = offset;

	ret = device_property_read_u32(dev, "intc-gpio-host-software-pad-ownership-offset", &offset);
	if (ret)
		return ret;
	community->hostown_offset = offset;

	ret = device_property_read_u32(dev, "intc-gpio-gpi-interrupt-status-offset", &offset);
	if (ret)
		return ret;
	community->is_offset = offset;

	ret = device_property_read_u32(dev, "intc-gpio-gpi-interrupt-enable-offset", &offset);
	if (ret)
		return ret;
	community->ie_offset = offset;

	ngpps = device_get_child_node_count(dev);
	if (!ngpps)
		return -ENODEV;

	gpps = devm_kcalloc(dev, ngpps, sizeof(*gpps), GFP_KERNEL);
	if (!gpps)
		return -ENOMEM;

	group = 0;
	device_for_each_child_node(dev, child) {
		struct intel_padgroup *gpp = &gpps[group];

		gpp->reg_num = group;

		ret = intel_platform_pinctrl_prepare_group(dev, child, gpp, pins);
		if (ret)
			return ret;

		group++;
	}

	community->ngpps = ngpps;
	community->gpps = gpps;

	return 0;
}

static int intel_platform_pinctrl_prepare_soc_data(struct device *dev,
						   struct intel_pinctrl_soc_data *data)
{
	struct intel_platform_pins pins = {};
	struct intel_community *communities;
	size_t ncommunities;
	unsigned int i;
	int ret;

	/* Version 1.0 of the specification assumes only a single community per device node */
	ncommunities = 1,
	communities = devm_kcalloc(dev, ncommunities, sizeof(*communities), GFP_KERNEL);
	if (!communities)
		return -ENOMEM;

	for (i = 0; i < ncommunities; i++) {
		struct intel_community *community = &communities[i];

		community->barno = i;
		community->pin_base = pins.npins;

		ret = intel_platform_pinctrl_prepare_community(dev, community, &pins);
		if (ret)
			return ret;

		community->npins = pins.npins - community->pin_base;
	}

	data->ncommunities = ncommunities;
	data->communities = communities;

	data->npins = pins.npins;
	data->pins = pins.pins;

	return 0;
}

static int intel_platform_pinctrl_probe(struct platform_device *pdev)
{
	struct intel_pinctrl_soc_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = intel_platform_pinctrl_prepare_soc_data(dev, data);
	if (ret)
		return ret;

	return intel_pinctrl_probe(pdev, data);
}

static const struct acpi_device_id intel_platform_pinctrl_acpi_match[] = {
	{ "INTC105F" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, intel_platform_pinctrl_acpi_match);

static struct platform_driver intel_platform_pinctrl_driver = {
	.probe = intel_platform_pinctrl_probe,
	.driver = {
		.name = "intel-pinctrl",
		.acpi_match_table = intel_platform_pinctrl_acpi_match,
		.pm = pm_sleep_ptr(&intel_pinctrl_pm_ops),
	},
};
module_platform_driver(intel_platform_pinctrl_driver);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_DESCRIPTION("Intel PCH pinctrl/GPIO driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(PINCTRL_INTEL);

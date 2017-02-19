/*
 * Pinconf driver for TI DA850/OMAP-L138/AM18XX pullup/pulldown groups
 *
 * Copyright (C) 2016  David Lechner
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>

#define DA850_PUPD_ENA		0x00
#define DA850_PUPD_SEL		0x04

struct da850_pupd_data {
	void __iomem *base;
	struct pinctrl_desc desc;
	struct pinctrl_dev *pinctrl;
};

static const char * const da850_pupd_group_names[] = {
	"cp0", "cp1", "cp2", "cp3", "cp4", "cp5", "cp6", "cp7",
	"cp8", "cp9", "cp10", "cp11", "cp12", "cp13", "cp14", "cp15",
	"cp16", "cp17", "cp18", "cp19", "cp20", "cp21", "cp22", "cp23",
	"cp24", "cp25", "cp26", "cp27", "cp28", "cp29", "cp30", "cp31",
};

static int da850_pupd_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(da850_pupd_group_names);
}

static const char *da850_pupd_get_group_name(struct pinctrl_dev *pctldev,
					     unsigned int selector)
{
	return da850_pupd_group_names[selector];
}

static int da850_pupd_get_group_pins(struct pinctrl_dev *pctldev,
				     unsigned int selector,
				     const unsigned int **pins,
				     unsigned int *num_pins)
{
	*num_pins = 0;

	return 0;
}

static const struct pinctrl_ops da850_pupd_pctlops = {
	.get_groups_count	= da850_pupd_get_groups_count,
	.get_group_name		= da850_pupd_get_group_name,
	.get_group_pins		= da850_pupd_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_group,
	.dt_free_map		= pinconf_generic_dt_free_map,
};

static int da850_pupd_pin_config_group_get(struct pinctrl_dev *pctldev,
					   unsigned int selector,
					   unsigned long *config)
{
	struct da850_pupd_data *data = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 val;
	u16 arg;

	val = readl(data->base + DA850_PUPD_ENA);
	arg = !!(~val & BIT(selector));

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (arg) {
			/* bias is disabled */
			arg = 0;
			break;
		}
		val = readl(data->base + DA850_PUPD_SEL);
		if (param == PIN_CONFIG_BIAS_PULL_DOWN)
			val = ~val;
		arg = !!(val & BIT(selector));
		break;
	default:
		return -EINVAL;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int da850_pupd_pin_config_group_set(struct pinctrl_dev *pctldev,
					   unsigned int selector,
					   unsigned long *configs,
					   unsigned int num_configs)
{
	struct da850_pupd_data *data = pinctrl_dev_get_drvdata(pctldev);
	u32 ena, sel;
	enum pin_config_param param;
	u16 arg;
	int i;

	ena = readl(data->base + DA850_PUPD_ENA);
	sel = readl(data->base + DA850_PUPD_SEL);

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			ena &= ~BIT(selector);
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			ena |= BIT(selector);
			sel |= BIT(selector);
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			ena |= BIT(selector);
			sel &= ~BIT(selector);
			break;
		default:
			return -EINVAL;
		}
	}

	writel(sel, data->base + DA850_PUPD_SEL);
	writel(ena, data->base + DA850_PUPD_ENA);

	return 0;
}

static const struct pinconf_ops da850_pupd_confops = {
	.is_generic		= true,
	.pin_config_group_get	= da850_pupd_pin_config_group_get,
	.pin_config_group_set	= da850_pupd_pin_config_group_set,
};

static int da850_pupd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct da850_pupd_data *data;
	struct resource *res;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->base)) {
		dev_err(dev, "Could not map resource\n");
		return PTR_ERR(data->base);
	}

	data->desc.name = dev_name(dev);
	data->desc.pctlops = &da850_pupd_pctlops;
	data->desc.confops = &da850_pupd_confops;
	data->desc.owner = THIS_MODULE;

	data->pinctrl = devm_pinctrl_register(dev, &data->desc, data);
	if (IS_ERR(data->pinctrl)) {
		dev_err(dev, "Failed to register pinctrl\n");
		return PTR_ERR(data->pinctrl);
	}

	platform_set_drvdata(pdev, data);

	return 0;
}

static int da850_pupd_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id da850_pupd_of_match[] = {
	{ .compatible = "ti,da850-pupd" },
	{ }
};

static struct platform_driver da850_pupd_driver = {
	.driver	= {
		.name		= "ti-da850-pupd",
		.of_match_table	= da850_pupd_of_match,
	},
	.probe	= da850_pupd_probe,
	.remove	= da850_pupd_remove,
};
module_platform_driver(da850_pupd_driver);

MODULE_AUTHOR("David Lechner <david@lechnology.com>");
MODULE_DESCRIPTION("TI DA850/OMAP-L138/AM18XX pullup/pulldown configuration");
MODULE_LICENSE("GPL");

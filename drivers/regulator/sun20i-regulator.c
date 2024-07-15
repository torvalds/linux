// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021-2022 Samuel Holland <samuel@sholland.org>
//

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>

#define SUN20I_SYS_LDO_CTRL_REG		0x150

struct sun20i_regulator_data {
	const struct regulator_desc	*descs;
	unsigned int			ndescs;
};

/* regulator_list_voltage_linear() modified for the non-integral uV_step. */
static int sun20i_d1_system_ldo_list_voltage(struct regulator_dev *rdev,
					     unsigned int selector)
{
	const struct regulator_desc *desc = rdev->desc;
	unsigned int fraction, uV;

	if (selector >= desc->n_voltages)
		return -EINVAL;

	uV = desc->min_uV + (desc->uV_step * selector);
	fraction = selector + (desc->min_uV % 4);

	if (uV > 1606667)
		uV += 6667;
	else
		fraction++;

	/* Produce correctly-rounded absolute voltages. */
	return uV + (fraction / 3);
}

static const struct regulator_ops sun20i_d1_system_ldo_ops = {
	.list_voltage		= sun20i_d1_system_ldo_list_voltage,
	.map_voltage		= regulator_map_voltage_ascend,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
};

static const struct regulator_desc sun20i_d1_system_ldo_descs[] = {
	{
		.name		= "ldoa",
		.supply_name	= "ldo-in",
		.of_match	= "ldoa",
		.ops		= &sun20i_d1_system_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
		.n_voltages	= 32,
		.min_uV		= 1593333,
		.uV_step	= 13333, /* repeating */
		.vsel_reg	= SUN20I_SYS_LDO_CTRL_REG,
		.vsel_mask	= GENMASK(7, 0),
	},
	{
		.name		= "ldob",
		.supply_name	= "ldo-in",
		.of_match	= "ldob",
		.ops		= &sun20i_d1_system_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
		.n_voltages	= 64,
		.min_uV		= 1166666,
		.uV_step	= 13333, /* repeating */
		.vsel_reg	= SUN20I_SYS_LDO_CTRL_REG,
		.vsel_mask	= GENMASK(15, 8),
	},
};

static const struct sun20i_regulator_data sun20i_d1_system_ldos = {
	.descs	= sun20i_d1_system_ldo_descs,
	.ndescs	= ARRAY_SIZE(sun20i_d1_system_ldo_descs),
};

static struct regmap *sun20i_regulator_get_regmap(struct device *dev)
{
	struct regmap *regmap;

	/*
	 * First try the syscon interface. The system control device is not
	 * compatible with "syscon", so fall back to getting the regmap from
	 * its platform device. This is ugly, but required for devicetree
	 * backward compatibility.
	 */
	regmap = syscon_node_to_regmap(dev->parent->of_node);
	if (!IS_ERR(regmap))
		return regmap;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (regmap)
		return regmap;

	return ERR_PTR(-EPROBE_DEFER);
}

static int sun20i_regulator_probe(struct platform_device *pdev)
{
	const struct sun20i_regulator_data *data;
	struct device *dev = &pdev->dev;
	struct regulator_config config;
	struct regmap *regmap;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	regmap = sun20i_regulator_get_regmap(dev);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Failed to get regmap\n");

	config = (struct regulator_config) {
		.dev	= dev,
		.regmap	= regmap,
	};

	for (unsigned int i = 0; i < data->ndescs; ++i) {
		const struct regulator_desc *desc = &data->descs[i];
		struct regulator_dev *rdev;

		rdev = devm_regulator_register(dev, desc, &config);
		if (IS_ERR(rdev))
			return PTR_ERR(rdev);
	}

	return 0;
}

static const struct of_device_id sun20i_regulator_of_match[] = {
	{
		.compatible = "allwinner,sun20i-d1-system-ldos",
		.data = &sun20i_d1_system_ldos,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, sun20i_regulator_of_match);

static struct platform_driver sun20i_regulator_driver = {
	.probe	= sun20i_regulator_probe,
	.driver	= {
		.name		= "sun20i-regulator",
		.of_match_table	= sun20i_regulator_of_match,
	},
};
module_platform_driver(sun20i_regulator_driver);

MODULE_AUTHOR("Samuel Holland <samuel@sholland.org>");
MODULE_DESCRIPTION("Allwinner D1 internal LDO driver");
MODULE_LICENSE("GPL");

/*
 * Regulators driver for Marvell 88PM800
 *
 * Copyright (C) 2012 Marvell International Ltd.
 * Joseph(Yossi) Hanin <yhanin@marvell.com>
 * Yi Zhang <yizhang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/88pm80x.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/regulator/of_regulator.h>

/* LDO1 with DVC[0..3] */
#define PM800_LDO1_VOUT		(0x08) /* VOUT1 */
#define PM800_LDO1_VOUT_2	(0x09)
#define PM800_LDO1_VOUT_3	(0x0A)
#define PM800_LDO2_VOUT		(0x0B)
#define PM800_LDO3_VOUT		(0x0C)
#define PM800_LDO4_VOUT		(0x0D)
#define PM800_LDO5_VOUT		(0x0E)
#define PM800_LDO6_VOUT		(0x0F)
#define PM800_LDO7_VOUT		(0x10)
#define PM800_LDO8_VOUT		(0x11)
#define PM800_LDO9_VOUT		(0x12)
#define PM800_LDO10_VOUT	(0x13)
#define PM800_LDO11_VOUT	(0x14)
#define PM800_LDO12_VOUT	(0x15)
#define PM800_LDO13_VOUT	(0x16)
#define PM800_LDO14_VOUT	(0x17)
#define PM800_LDO15_VOUT	(0x18)
#define PM800_LDO16_VOUT	(0x19)
#define PM800_LDO17_VOUT	(0x1A)
#define PM800_LDO18_VOUT	(0x1B)
#define PM800_LDO19_VOUT	(0x1C)

/* BUCK1 with DVC[0..3] */
#define PM800_BUCK1		(0x3C)
#define PM800_BUCK1_1		(0x3D)
#define PM800_BUCK1_2		(0x3E)
#define PM800_BUCK1_3		(0x3F)
#define PM800_BUCK2		(0x40)
#define PM800_BUCK3		(0x41)
#define PM800_BUCK3		(0x41)
#define PM800_BUCK4		(0x42)
#define PM800_BUCK4_1		(0x43)
#define PM800_BUCK4_2		(0x44)
#define PM800_BUCK4_3		(0x45)
#define PM800_BUCK5		(0x46)

#define PM800_BUCK_ENA		(0x50)
#define PM800_LDO_ENA1_1	(0x51)
#define PM800_LDO_ENA1_2	(0x52)
#define PM800_LDO_ENA1_3	(0x53)

#define PM800_LDO_ENA2_1	(0x56)
#define PM800_LDO_ENA2_2	(0x57)
#define PM800_LDO_ENA2_3	(0x58)

#define PM800_BUCK1_MISC1	(0x78)
#define PM800_BUCK3_MISC1	(0x7E)
#define PM800_BUCK4_MISC1	(0x81)
#define PM800_BUCK5_MISC1	(0x84)

struct pm800_regulator_info {
	struct regulator_desc desc;
	int max_ua;
};

struct pm800_regulators {
	struct regulator_dev *regulators[PM800_ID_RG_MAX];
	struct pm80x_chip *chip;
	struct regmap *map;
};

/*
 * vreg - the buck regs string.
 * ereg - the string for the enable register.
 * ebit - the bit number in the enable register.
 * amax - the current
 * Buck has 2 kinds of voltage steps. It is easy to find voltage by ranges,
 * not the constant voltage table.
 * n_volt - Number of available selectors
 */
#define PM800_BUCK(vreg, ereg, ebit, amax, volt_ranges, n_volt)		\
{									\
	.desc	= {							\
		.name	= #vreg,					\
		.ops	= &pm800_volt_range_ops,			\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= PM800_ID_##vreg,				\
		.owner	= THIS_MODULE,					\
		.n_voltages		= n_volt,			\
		.linear_ranges		= volt_ranges,			\
		.n_linear_ranges	= ARRAY_SIZE(volt_ranges),	\
		.vsel_reg		= PM800_##vreg,			\
		.vsel_mask		= 0x7f,				\
		.enable_reg		= PM800_##ereg,			\
		.enable_mask		= 1 << (ebit),			\
	},								\
	.max_ua		= (amax),					\
}

/*
 * vreg - the LDO regs string
 * ereg -  the string for the enable register.
 * ebit - the bit number in the enable register.
 * amax - the current
 * volt_table - the LDO voltage table
 * For all the LDOes, there are too many ranges. Using volt_table will be
 * simpler and faster.
 */
#define PM800_LDO(vreg, ereg, ebit, amax, ldo_volt_table)		\
{									\
	.desc	= {							\
		.name	= #vreg,					\
		.ops	= &pm800_volt_table_ops,			\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= PM800_ID_##vreg,				\
		.owner	= THIS_MODULE,					\
		.n_voltages = ARRAY_SIZE(ldo_volt_table),		\
		.vsel_reg	= PM800_##vreg##_VOUT,			\
		.vsel_mask	= 0x1f,					\
		.enable_reg	= PM800_##ereg,				\
		.enable_mask	= 1 << (ebit),				\
		.volt_table	= ldo_volt_table,			\
	},								\
	.max_ua		= (amax),					\
}

/* Ranges are sorted in ascending order. */
static const struct regulator_linear_range buck1_volt_range[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 0x4f, 12500),
	REGULATOR_LINEAR_RANGE(1600000, 0x50, 0x54, 50000),
};

/* BUCK 2~5 have same ranges. */
static const struct regulator_linear_range buck2_5_volt_range[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 0x4f, 12500),
	REGULATOR_LINEAR_RANGE(1600000, 0x50, 0x72, 50000),
};

static const unsigned int ldo1_volt_table[] = {
	600000,  650000,  700000,  750000,  800000,  850000,  900000,  950000,
	1000000, 1050000, 1100000, 1150000, 1200000, 1300000, 1400000, 1500000,
};

static const unsigned int ldo2_volt_table[] = {
	1700000, 1800000, 1900000, 2000000, 2100000, 2500000, 2700000, 2800000,
};

/* LDO 3~17 have same voltage table. */
static const unsigned int ldo3_17_volt_table[] = {
	1200000, 1250000, 1700000, 1800000, 1850000, 1900000, 2500000, 2600000,
	2700000, 2750000, 2800000, 2850000, 2900000, 3000000, 3100000, 3300000,
};

/* LDO 18~19 have same voltage table. */
static const unsigned int ldo18_19_volt_table[] = {
	1700000, 1800000, 1900000, 2500000, 2800000, 2900000, 3100000, 3300000,
};

static int pm800_get_current_limit(struct regulator_dev *rdev)
{
	struct pm800_regulator_info *info = rdev_get_drvdata(rdev);

	return info->max_ua;
}

static struct regulator_ops pm800_volt_range_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_current_limit = pm800_get_current_limit,
};

static struct regulator_ops pm800_volt_table_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_current_limit = pm800_get_current_limit,
};

/* The array is indexed by id(PM800_ID_XXX) */
static struct pm800_regulator_info pm800_regulator_info[] = {
	PM800_BUCK(BUCK1, BUCK_ENA, 0, 3000000, buck1_volt_range, 0x55),
	PM800_BUCK(BUCK2, BUCK_ENA, 1, 1200000, buck2_5_volt_range, 0x73),
	PM800_BUCK(BUCK3, BUCK_ENA, 2, 1200000, buck2_5_volt_range, 0x73),
	PM800_BUCK(BUCK4, BUCK_ENA, 3, 1200000, buck2_5_volt_range, 0x73),
	PM800_BUCK(BUCK5, BUCK_ENA, 4, 1200000, buck2_5_volt_range, 0x73),

	PM800_LDO(LDO1, LDO_ENA1_1, 0, 200000, ldo1_volt_table),
	PM800_LDO(LDO2, LDO_ENA1_1, 1, 10000, ldo2_volt_table),
	PM800_LDO(LDO3, LDO_ENA1_1, 2, 300000, ldo3_17_volt_table),
	PM800_LDO(LDO4, LDO_ENA1_1, 3, 300000, ldo3_17_volt_table),
	PM800_LDO(LDO5, LDO_ENA1_1, 4, 300000, ldo3_17_volt_table),
	PM800_LDO(LDO6, LDO_ENA1_1, 5, 300000, ldo3_17_volt_table),
	PM800_LDO(LDO7, LDO_ENA1_1, 6, 300000, ldo3_17_volt_table),
	PM800_LDO(LDO8, LDO_ENA1_1, 7, 300000, ldo3_17_volt_table),
	PM800_LDO(LDO9, LDO_ENA1_2, 0, 300000, ldo3_17_volt_table),
	PM800_LDO(LDO10, LDO_ENA1_2, 1, 300000, ldo3_17_volt_table),
	PM800_LDO(LDO11, LDO_ENA1_2, 2, 300000, ldo3_17_volt_table),
	PM800_LDO(LDO12, LDO_ENA1_2, 3, 300000, ldo3_17_volt_table),
	PM800_LDO(LDO13, LDO_ENA1_2, 4, 300000, ldo3_17_volt_table),
	PM800_LDO(LDO14, LDO_ENA1_2, 5, 300000, ldo3_17_volt_table),
	PM800_LDO(LDO15, LDO_ENA1_2, 6, 300000, ldo3_17_volt_table),
	PM800_LDO(LDO16, LDO_ENA1_2, 7, 300000, ldo3_17_volt_table),
	PM800_LDO(LDO17, LDO_ENA1_3, 0, 300000, ldo3_17_volt_table),
	PM800_LDO(LDO18, LDO_ENA1_3, 1, 200000, ldo18_19_volt_table),
	PM800_LDO(LDO19, LDO_ENA1_3, 2, 200000, ldo18_19_volt_table),
};

#define PM800_REGULATOR_OF_MATCH(_name, _id)				\
	[PM800_ID_##_id] = {						\
		.name = #_name,						\
		.driver_data = &pm800_regulator_info[PM800_ID_##_id],	\
	}

static struct of_regulator_match pm800_regulator_matches[] = {
	PM800_REGULATOR_OF_MATCH(buck1, BUCK1),
	PM800_REGULATOR_OF_MATCH(buck2, BUCK2),
	PM800_REGULATOR_OF_MATCH(buck3, BUCK3),
	PM800_REGULATOR_OF_MATCH(buck4, BUCK4),
	PM800_REGULATOR_OF_MATCH(buck5, BUCK5),
	PM800_REGULATOR_OF_MATCH(ldo1, LDO1),
	PM800_REGULATOR_OF_MATCH(ldo2, LDO2),
	PM800_REGULATOR_OF_MATCH(ldo3, LDO3),
	PM800_REGULATOR_OF_MATCH(ldo4, LDO4),
	PM800_REGULATOR_OF_MATCH(ldo5, LDO5),
	PM800_REGULATOR_OF_MATCH(ldo6, LDO6),
	PM800_REGULATOR_OF_MATCH(ldo7, LDO7),
	PM800_REGULATOR_OF_MATCH(ldo8, LDO8),
	PM800_REGULATOR_OF_MATCH(ldo9, LDO9),
	PM800_REGULATOR_OF_MATCH(ldo10, LDO10),
	PM800_REGULATOR_OF_MATCH(ldo11, LDO11),
	PM800_REGULATOR_OF_MATCH(ldo12, LDO12),
	PM800_REGULATOR_OF_MATCH(ldo13, LDO13),
	PM800_REGULATOR_OF_MATCH(ldo14, LDO14),
	PM800_REGULATOR_OF_MATCH(ldo15, LDO15),
	PM800_REGULATOR_OF_MATCH(ldo16, LDO16),
	PM800_REGULATOR_OF_MATCH(ldo17, LDO17),
	PM800_REGULATOR_OF_MATCH(ldo18, LDO18),
	PM800_REGULATOR_OF_MATCH(ldo19, LDO19),
};

static int pm800_regulator_dt_init(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;

	ret = of_regulator_match(&pdev->dev, np,
				 pm800_regulator_matches,
				 ARRAY_SIZE(pm800_regulator_matches));
	if (ret < 0)
		return ret;

	return 0;
}

static int pm800_regulator_probe(struct platform_device *pdev)
{
	struct pm80x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm80x_platform_data *pdata = dev_get_platdata(pdev->dev.parent);
	struct pm800_regulators *pm800_data;
	struct pm800_regulator_info *info;
	struct regulator_config config = { };
	struct regulator_init_data *init_data;
	int i, ret;

	if (!pdata || pdata->num_regulators == 0) {
		if (IS_ENABLED(CONFIG_OF)) {
			ret = pm800_regulator_dt_init(pdev);
			if (ret)
				return ret;
		} else {
			return -ENODEV;
		}
	} else if (pdata->num_regulators) {
		unsigned int count = 0;

		/* Check whether num_regulator is valid. */
		for (i = 0; i < ARRAY_SIZE(pdata->regulators); i++) {
			if (pdata->regulators[i])
				count++;
		}
		if (count != pdata->num_regulators)
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	pm800_data = devm_kzalloc(&pdev->dev, sizeof(*pm800_data),
					GFP_KERNEL);
	if (!pm800_data) {
		dev_err(&pdev->dev, "Failed to allocate pm800_regualtors");
		return -ENOMEM;
	}

	pm800_data->map = chip->subchip->regmap_power;
	pm800_data->chip = chip;

	platform_set_drvdata(pdev, pm800_data);

	for (i = 0; i < PM800_ID_RG_MAX; i++) {
		if (!pdata || pdata->num_regulators == 0)
			init_data = pm800_regulator_matches[i].init_data;
		else
			init_data = pdata->regulators[i];
		if (!init_data)
			continue;
		info = pm800_regulator_matches[i].driver_data;
		config.dev = &pdev->dev;
		config.init_data = init_data;
		config.driver_data = info;
		config.regmap = pm800_data->map;
		config.of_node = pm800_regulator_matches[i].of_node;

		pm800_data->regulators[i] =
				regulator_register(&info->desc, &config);
		if (IS_ERR(pm800_data->regulators[i])) {
			ret = PTR_ERR(pm800_data->regulators[i]);
			dev_err(&pdev->dev, "Failed to register %s\n",
				info->desc.name);

			while (--i >= 0)
				regulator_unregister(pm800_data->regulators[i]);

			return ret;
		}
	}

	return 0;
}

static int pm800_regulator_remove(struct platform_device *pdev)
{
	struct pm800_regulators *pm800_data = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < PM800_ID_RG_MAX; i++)
		regulator_unregister(pm800_data->regulators[i]);

	return 0;
}

static struct platform_driver pm800_regulator_driver = {
	.driver		= {
		.name	= "88pm80x-regulator",
		.owner	= THIS_MODULE,
	},
	.probe		= pm800_regulator_probe,
	.remove		= pm800_regulator_remove,
};

module_platform_driver(pm800_regulator_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joseph(Yossi) Hanin <yhanin@marvell.com>");
MODULE_DESCRIPTION("Regulator Driver for Marvell 88PM800 PMIC");
MODULE_ALIAS("platform:88pm800-regulator");

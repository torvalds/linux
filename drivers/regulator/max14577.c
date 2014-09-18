/*
 * max14577.c - Regulator driver for the Maxim 14577/77836
 *
 * Copyright (C) 2013,2014 Samsung Electronics
 * Krzysztof Kozlowski <k.kozlowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/mfd/max14577.h>
#include <linux/mfd/max14577-private.h>
#include <linux/regulator/of_regulator.h>

/*
 * Valid limits of current for max14577 and max77836 chargers.
 * They must correspond to MBCICHWRCL and MBCICHWRCH fields in CHGCTRL4
 * register for given chipset.
 */
struct maxim_charger_current {
	/* Minimal current, set in CHGCTRL4/MBCICHWRCL, uA */
	unsigned int min;
	/*
	 * Minimal current when high setting is active,
	 * set in CHGCTRL4/MBCICHWRCH, uA
	 */
	unsigned int high_start;
	/* Value of one step in high setting, uA */
	unsigned int high_step;
	/* Maximum current of high setting, uA */
	unsigned int max;
};

/* Table of valid charger currents for different Maxim chipsets */
static const struct maxim_charger_current maxim_charger_currents[] = {
	[MAXIM_DEVICE_TYPE_UNKNOWN] = { 0, 0, 0, 0 },
	[MAXIM_DEVICE_TYPE_MAX14577] = {
		.min		= MAX14577_REGULATOR_CURRENT_LIMIT_MIN,
		.high_start	= MAX14577_REGULATOR_CURRENT_LIMIT_HIGH_START,
		.high_step	= MAX14577_REGULATOR_CURRENT_LIMIT_HIGH_STEP,
		.max		= MAX14577_REGULATOR_CURRENT_LIMIT_MAX,
	},
	[MAXIM_DEVICE_TYPE_MAX77836] = {
		.min		= MAX77836_REGULATOR_CURRENT_LIMIT_MIN,
		.high_start	= MAX77836_REGULATOR_CURRENT_LIMIT_HIGH_START,
		.high_step	= MAX77836_REGULATOR_CURRENT_LIMIT_HIGH_STEP,
		.max		= MAX77836_REGULATOR_CURRENT_LIMIT_MAX,
	},
};

static int max14577_reg_is_enabled(struct regulator_dev *rdev)
{
	int rid = rdev_get_id(rdev);
	struct regmap *rmap = rdev->regmap;
	u8 reg_data;

	switch (rid) {
	case MAX14577_CHARGER:
		max14577_read_reg(rmap, MAX14577_CHG_REG_CHG_CTRL2, &reg_data);
		if ((reg_data & CHGCTRL2_MBCHOSTEN_MASK) == 0)
			return 0;
		max14577_read_reg(rmap, MAX14577_CHG_REG_STATUS3, &reg_data);
		if ((reg_data & STATUS3_CGMBC_MASK) == 0)
			return 0;
		/* MBCHOSTEN and CGMBC are on */
		return 1;
	default:
		return -EINVAL;
	}
}

static int max14577_reg_get_current_limit(struct regulator_dev *rdev)
{
	u8 reg_data;
	struct regmap *rmap = rdev->regmap;
	struct max14577 *max14577 = rdev_get_drvdata(rdev);
	const struct maxim_charger_current *limits =
		&maxim_charger_currents[max14577->dev_type];

	if (rdev_get_id(rdev) != MAX14577_CHARGER)
		return -EINVAL;

	max14577_read_reg(rmap, MAX14577_CHG_REG_CHG_CTRL4, &reg_data);

	if ((reg_data & CHGCTRL4_MBCICHWRCL_MASK) == 0)
		return limits->min;

	reg_data = ((reg_data & CHGCTRL4_MBCICHWRCH_MASK) >>
			CHGCTRL4_MBCICHWRCH_SHIFT);
	return limits->high_start + reg_data * limits->high_step;
}

static int max14577_reg_set_current_limit(struct regulator_dev *rdev,
		int min_uA, int max_uA)
{
	int i, current_bits = 0xf;
	u8 reg_data;
	struct max14577 *max14577 = rdev_get_drvdata(rdev);
	const struct maxim_charger_current *limits =
		&maxim_charger_currents[max14577->dev_type];

	if (rdev_get_id(rdev) != MAX14577_CHARGER)
		return -EINVAL;

	if (min_uA > limits->max || max_uA < limits->min)
		return -EINVAL;

	if (max_uA < limits->high_start) {
		/*
		 * Less than high_start,
		 * so set the minimal current (turn only Low Bit off)
		 */
		u8 reg_data = 0x0 << CHGCTRL4_MBCICHWRCL_SHIFT;
		return max14577_update_reg(rdev->regmap,
				MAX14577_CHG_REG_CHG_CTRL4,
				CHGCTRL4_MBCICHWRCL_MASK, reg_data);
	}

	/*
	 * max_uA is in range: <high_start, inifinite>, so search for
	 * valid current starting from maximum current.
	 */
	for (i = limits->max; i >= limits->high_start; i -= limits->high_step) {
		if (i <= max_uA)
			break;
		current_bits--;
	}
	BUG_ON(current_bits < 0); /* Cannot happen */

	/* Turn Low Bit on (use range high_start-max)... */
	reg_data = 0x1 << CHGCTRL4_MBCICHWRCL_SHIFT;
	/* and set proper High Bits */
	reg_data |= current_bits << CHGCTRL4_MBCICHWRCH_SHIFT;

	return max14577_update_reg(rdev->regmap, MAX14577_CHG_REG_CHG_CTRL4,
			CHGCTRL4_MBCICHWRCL_MASK | CHGCTRL4_MBCICHWRCH_MASK,
			reg_data);
}

static struct regulator_ops max14577_safeout_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.list_voltage		= regulator_list_voltage_linear,
};

static struct regulator_ops max14577_charger_ops = {
	.is_enabled		= max14577_reg_is_enabled,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_current_limit	= max14577_reg_get_current_limit,
	.set_current_limit	= max14577_reg_set_current_limit,
};

static const struct regulator_desc max14577_supported_regulators[] = {
	[MAX14577_SAFEOUT] = {
		.name		= "SAFEOUT",
		.id		= MAX14577_SAFEOUT,
		.ops		= &max14577_safeout_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
		.n_voltages	= 1,
		.min_uV		= MAX14577_REGULATOR_SAFEOUT_VOLTAGE,
		.enable_reg	= MAX14577_REG_CONTROL2,
		.enable_mask	= CTRL2_SFOUTORD_MASK,
	},
	[MAX14577_CHARGER] = {
		.name		= "CHARGER",
		.id		= MAX14577_CHARGER,
		.ops		= &max14577_charger_ops,
		.type		= REGULATOR_CURRENT,
		.owner		= THIS_MODULE,
		.enable_reg	= MAX14577_CHG_REG_CHG_CTRL2,
		.enable_mask	= CHGCTRL2_MBCHOSTEN_MASK,
	},
};

static struct regulator_ops max77836_ldo_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	/* TODO: add .set_suspend_mode */
};

static const struct regulator_desc max77836_supported_regulators[] = {
	[MAX14577_SAFEOUT] = {
		.name		= "SAFEOUT",
		.id		= MAX14577_SAFEOUT,
		.ops		= &max14577_safeout_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
		.n_voltages	= 1,
		.min_uV		= MAX14577_REGULATOR_SAFEOUT_VOLTAGE,
		.enable_reg	= MAX14577_REG_CONTROL2,
		.enable_mask	= CTRL2_SFOUTORD_MASK,
	},
	[MAX14577_CHARGER] = {
		.name		= "CHARGER",
		.id		= MAX14577_CHARGER,
		.ops		= &max14577_charger_ops,
		.type		= REGULATOR_CURRENT,
		.owner		= THIS_MODULE,
		.enable_reg	= MAX14577_CHG_REG_CHG_CTRL2,
		.enable_mask	= CHGCTRL2_MBCHOSTEN_MASK,
	},
	[MAX77836_LDO1] = {
		.name		= "LDO1",
		.id		= MAX77836_LDO1,
		.ops		= &max77836_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
		.n_voltages	= MAX77836_REGULATOR_LDO_VOLTAGE_STEPS_NUM,
		.min_uV		= MAX77836_REGULATOR_LDO_VOLTAGE_MIN,
		.uV_step	= MAX77836_REGULATOR_LDO_VOLTAGE_STEP,
		.enable_reg	= MAX77836_LDO_REG_CNFG1_LDO1,
		.enable_mask	= MAX77836_CNFG1_LDO_PWRMD_MASK,
		.vsel_reg	= MAX77836_LDO_REG_CNFG1_LDO1,
		.vsel_mask	= MAX77836_CNFG1_LDO_TV_MASK,
	},
	[MAX77836_LDO2] = {
		.name		= "LDO2",
		.id		= MAX77836_LDO2,
		.ops		= &max77836_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
		.n_voltages	= MAX77836_REGULATOR_LDO_VOLTAGE_STEPS_NUM,
		.min_uV		= MAX77836_REGULATOR_LDO_VOLTAGE_MIN,
		.uV_step	= MAX77836_REGULATOR_LDO_VOLTAGE_STEP,
		.enable_reg	= MAX77836_LDO_REG_CNFG1_LDO2,
		.enable_mask	= MAX77836_CNFG1_LDO_PWRMD_MASK,
		.vsel_reg	= MAX77836_LDO_REG_CNFG1_LDO2,
		.vsel_mask	= MAX77836_CNFG1_LDO_TV_MASK,
	},
};

#ifdef CONFIG_OF
static struct of_regulator_match max14577_regulator_matches[] = {
	{ .name	= "SAFEOUT", },
	{ .name = "CHARGER", },
};

static struct of_regulator_match max77836_regulator_matches[] = {
	{ .name	= "SAFEOUT", },
	{ .name = "CHARGER", },
	{ .name = "LDO1", },
	{ .name = "LDO2", },
};

static int max14577_regulator_dt_parse_pdata(struct platform_device *pdev,
		enum maxim_device_type dev_type)
{
	int ret;
	struct device_node *np;
	struct of_regulator_match *regulator_matches;
	unsigned int regulator_matches_size;

	np = of_get_child_by_name(pdev->dev.parent->of_node, "regulators");
	if (!np) {
		dev_err(&pdev->dev, "Failed to get child OF node for regulators\n");
		return -EINVAL;
	}

	switch (dev_type) {
	case MAXIM_DEVICE_TYPE_MAX77836:
		regulator_matches = max77836_regulator_matches;
		regulator_matches_size = ARRAY_SIZE(max77836_regulator_matches);
		break;
	case MAXIM_DEVICE_TYPE_MAX14577:
	default:
		regulator_matches = max14577_regulator_matches;
		regulator_matches_size = ARRAY_SIZE(max14577_regulator_matches);
	}

	ret = of_regulator_match(&pdev->dev, np, regulator_matches,
			regulator_matches_size);
	if (ret < 0)
		dev_err(&pdev->dev, "Error parsing regulator init data: %d\n", ret);
	else
		ret = 0;

	of_node_put(np);

	return ret;
}

static inline struct regulator_init_data *match_init_data(int index,
		enum maxim_device_type dev_type)
{
	switch (dev_type) {
	case MAXIM_DEVICE_TYPE_MAX77836:
		return max77836_regulator_matches[index].init_data;

	case MAXIM_DEVICE_TYPE_MAX14577:
	default:
		return max14577_regulator_matches[index].init_data;
	}
}

static inline struct device_node *match_of_node(int index,
		enum maxim_device_type dev_type)
{
	switch (dev_type) {
	case MAXIM_DEVICE_TYPE_MAX77836:
		return max77836_regulator_matches[index].of_node;

	case MAXIM_DEVICE_TYPE_MAX14577:
	default:
		return max14577_regulator_matches[index].of_node;
	}
}
#else /* CONFIG_OF */
static int max14577_regulator_dt_parse_pdata(struct platform_device *pdev,
		enum maxim_device_type dev_type)
{
	return 0;
}
static inline struct regulator_init_data *match_init_data(int index,
		enum maxim_device_type dev_type)
{
	return NULL;
}

static inline struct device_node *match_of_node(int index,
		enum maxim_device_type dev_type)
{
	return NULL;
}
#endif /* CONFIG_OF */

/**
 * Registers for regulators of max77836 use different I2C slave addresses so
 * different regmaps must be used for them.
 *
 * Returns proper regmap for accessing regulator passed by id.
 */
static struct regmap *max14577_get_regmap(struct max14577 *max14577,
		int reg_id)
{
	switch (max14577->dev_type) {
	case MAXIM_DEVICE_TYPE_MAX77836:
		switch (reg_id) {
		case MAX77836_SAFEOUT ... MAX77836_CHARGER:
			return max14577->regmap;
		default:
			/* MAX77836_LDO1 ... MAX77836_LDO2 */
			return max14577->regmap_pmic;
		}

	case MAXIM_DEVICE_TYPE_MAX14577:
	default:
		return max14577->regmap;
	}
}

static int max14577_regulator_probe(struct platform_device *pdev)
{
	struct max14577 *max14577 = dev_get_drvdata(pdev->dev.parent);
	struct max14577_platform_data *pdata = dev_get_platdata(max14577->dev);
	int i, ret;
	struct regulator_config config = {};
	const struct regulator_desc *supported_regulators;
	unsigned int supported_regulators_size;
	enum maxim_device_type dev_type = max14577->dev_type;

	ret = max14577_regulator_dt_parse_pdata(pdev, dev_type);
	if (ret)
		return ret;

	switch (dev_type) {
	case MAXIM_DEVICE_TYPE_MAX77836:
		supported_regulators = max77836_supported_regulators;
		supported_regulators_size = ARRAY_SIZE(max77836_supported_regulators);
		break;
	case MAXIM_DEVICE_TYPE_MAX14577:
	default:
		supported_regulators = max14577_supported_regulators;
		supported_regulators_size = ARRAY_SIZE(max14577_supported_regulators);
	}

	config.dev = &pdev->dev;
	config.driver_data = max14577;

	for (i = 0; i < supported_regulators_size; i++) {
		struct regulator_dev *regulator;
		/*
		 * Index of supported_regulators[] is also the id and must
		 * match index of pdata->regulators[].
		 */
		if (pdata && pdata->regulators) {
			config.init_data = pdata->regulators[i].initdata;
			config.of_node = pdata->regulators[i].of_node;
		} else {
			config.init_data = match_init_data(i, dev_type);
			config.of_node = match_of_node(i, dev_type);
		}
		config.regmap = max14577_get_regmap(max14577,
				supported_regulators[i].id);

		regulator = devm_regulator_register(&pdev->dev,
				&supported_regulators[i], &config);
		if (IS_ERR(regulator)) {
			ret = PTR_ERR(regulator);
			dev_err(&pdev->dev,
					"Regulator init failed for %d/%s with error: %d\n",
					i, supported_regulators[i].name, ret);
			return ret;
		}
	}

	return ret;
}

static const struct platform_device_id max14577_regulator_id[] = {
	{ "max14577-regulator", MAXIM_DEVICE_TYPE_MAX14577, },
	{ "max77836-regulator", MAXIM_DEVICE_TYPE_MAX77836, },
	{ }
};
MODULE_DEVICE_TABLE(platform, max14577_regulator_id);

static struct platform_driver max14577_regulator_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "max14577-regulator",
		   },
	.probe		= max14577_regulator_probe,
	.id_table	= max14577_regulator_id,
};

static int __init max14577_regulator_init(void)
{
	/* Check for valid values for charger */
	BUILD_BUG_ON(MAX14577_REGULATOR_CURRENT_LIMIT_HIGH_START +
			MAX14577_REGULATOR_CURRENT_LIMIT_HIGH_STEP * 0xf !=
			MAX14577_REGULATOR_CURRENT_LIMIT_MAX);
	BUILD_BUG_ON(MAX77836_REGULATOR_CURRENT_LIMIT_HIGH_START +
			MAX77836_REGULATOR_CURRENT_LIMIT_HIGH_STEP * 0xf !=
			MAX77836_REGULATOR_CURRENT_LIMIT_MAX);
	/* Valid charger current values must be provided for each chipset */
	BUILD_BUG_ON(ARRAY_SIZE(maxim_charger_currents) != MAXIM_DEVICE_TYPE_NUM);

	BUILD_BUG_ON(ARRAY_SIZE(max14577_supported_regulators) != MAX14577_REGULATOR_NUM);
	BUILD_BUG_ON(ARRAY_SIZE(max77836_supported_regulators) != MAX77836_REGULATOR_NUM);

	BUILD_BUG_ON(MAX77836_REGULATOR_LDO_VOLTAGE_MIN +
			(MAX77836_REGULATOR_LDO_VOLTAGE_STEP *
			  (MAX77836_REGULATOR_LDO_VOLTAGE_STEPS_NUM - 1)) !=
			MAX77836_REGULATOR_LDO_VOLTAGE_MAX);

	return platform_driver_register(&max14577_regulator_driver);
}
subsys_initcall(max14577_regulator_init);

static void __exit max14577_regulator_exit(void)
{
	platform_driver_unregister(&max14577_regulator_driver);
}
module_exit(max14577_regulator_exit);

MODULE_AUTHOR("Krzysztof Kozlowski <k.kozlowski@samsung.com>");
MODULE_DESCRIPTION("Maxim 14577/77836 regulator driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:max14577-regulator");

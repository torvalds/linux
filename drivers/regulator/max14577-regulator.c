// SPDX-License-Identifier: GPL-2.0+
//
// max14577.c - Regulator driver for the Maxim 14577/77836
//
// Copyright (C) 2013,2014 Samsung Electronics
// Krzysztof Kozlowski <krzk@kernel.org>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/mfd/max14577.h>
#include <linux/mfd/max14577-private.h>
#include <linux/regulator/of_regulator.h>

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
	u8 reg_data;
	int ret;
	struct max14577 *max14577 = rdev_get_drvdata(rdev);
	const struct maxim_charger_current *limits =
		&maxim_charger_currents[max14577->dev_type];

	if (rdev_get_id(rdev) != MAX14577_CHARGER)
		return -EINVAL;

	ret = maxim_charger_calc_reg_current(limits, min_uA, max_uA, &reg_data);
	if (ret)
		return ret;

	return max14577_update_reg(rdev->regmap, MAX14577_CHG_REG_CHG_CTRL4,
			CHGCTRL4_MBCICHWRCL_MASK | CHGCTRL4_MBCICHWRCH_MASK,
			reg_data);
}

static const struct regulator_ops max14577_safeout_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.list_voltage		= regulator_list_voltage_linear,
};

static const struct regulator_ops max14577_charger_ops = {
	.is_enabled		= max14577_reg_is_enabled,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_current_limit	= max14577_reg_get_current_limit,
	.set_current_limit	= max14577_reg_set_current_limit,
};

#define MAX14577_SAFEOUT_REG	{ \
	.name		= "SAFEOUT", \
	.of_match	= of_match_ptr("SAFEOUT"), \
	.regulators_node = of_match_ptr("regulators"), \
	.id		= MAX14577_SAFEOUT, \
	.ops		= &max14577_safeout_ops, \
	.type		= REGULATOR_VOLTAGE, \
	.owner		= THIS_MODULE, \
	.n_voltages	= 1, \
	.min_uV		= MAX14577_REGULATOR_SAFEOUT_VOLTAGE, \
	.enable_reg	= MAX14577_REG_CONTROL2, \
	.enable_mask	= CTRL2_SFOUTORD_MASK, \
}
#define MAX14577_CHARGER_REG	{ \
	.name		= "CHARGER", \
	.of_match	= of_match_ptr("CHARGER"), \
	.regulators_node = of_match_ptr("regulators"), \
	.id		= MAX14577_CHARGER, \
	.ops		= &max14577_charger_ops, \
	.type		= REGULATOR_CURRENT, \
	.owner		= THIS_MODULE, \
	.enable_reg	= MAX14577_CHG_REG_CHG_CTRL2, \
	.enable_mask	= CHGCTRL2_MBCHOSTEN_MASK, \
}

static const struct regulator_desc max14577_supported_regulators[] = {
	[MAX14577_SAFEOUT] = MAX14577_SAFEOUT_REG,
	[MAX14577_CHARGER] = MAX14577_CHARGER_REG,
};

static const struct regulator_ops max77836_ldo_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	/* TODO: add .set_suspend_mode */
};

#define MAX77836_LDO_REG(num)	{ \
	.name		= "LDO" # num, \
	.of_match	= of_match_ptr("LDO" # num), \
	.regulators_node = of_match_ptr("regulators"), \
	.id		= MAX77836_LDO ## num, \
	.ops		= &max77836_ldo_ops, \
	.type		= REGULATOR_VOLTAGE, \
	.owner		= THIS_MODULE, \
	.n_voltages	= MAX77836_REGULATOR_LDO_VOLTAGE_STEPS_NUM, \
	.min_uV		= MAX77836_REGULATOR_LDO_VOLTAGE_MIN, \
	.uV_step	= MAX77836_REGULATOR_LDO_VOLTAGE_STEP, \
	.enable_reg	= MAX77836_LDO_REG_CNFG1_LDO ## num, \
	.enable_mask	= MAX77836_CNFG1_LDO_PWRMD_MASK, \
	.vsel_reg	= MAX77836_LDO_REG_CNFG1_LDO ## num, \
	.vsel_mask	= MAX77836_CNFG1_LDO_TV_MASK, \
}

static const struct regulator_desc max77836_supported_regulators[] = {
	[MAX14577_SAFEOUT] = MAX14577_SAFEOUT_REG,
	[MAX14577_CHARGER] = MAX14577_CHARGER_REG,
	[MAX77836_LDO1] = MAX77836_LDO_REG(1),
	[MAX77836_LDO2] = MAX77836_LDO_REG(2),
};

/*
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
	int i, ret = 0;
	struct regulator_config config = {};
	const struct regulator_desc *supported_regulators;
	unsigned int supported_regulators_size;
	enum maxim_device_type dev_type = max14577->dev_type;

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

	config.dev = max14577->dev;
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
		   .name = "max14577-regulator",
		   },
	.probe		= max14577_regulator_probe,
	.id_table	= max14577_regulator_id,
};

static int __init max14577_regulator_init(void)
{
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

MODULE_AUTHOR("Krzysztof Kozlowski <krzk@kernel.org>");
MODULE_DESCRIPTION("Maxim 14577/77836 regulator driver");
MODULE_LICENSE("GPL");

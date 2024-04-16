// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Voltage regulation driver for active-semi ACT8945A PMIC
 *
 * Copyright (C) 2015 Atmel Corporation
 *
 * Author: Wenyou Yang <wenyou.yang@atmel.com>
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <dt-bindings/regulator/active-semi,8945a-regulator.h>

/**
 * ACT8945A Global Register Map.
 */
#define ACT8945A_SYS_MODE	0x00
#define ACT8945A_SYS_CTRL	0x01
#define ACT8945A_SYS_UNLK_REGS	0x0b
#define ACT8945A_DCDC1_VSET1	0x20
#define ACT8945A_DCDC1_VSET2	0x21
#define ACT8945A_DCDC1_CTRL	0x22
#define ACT8945A_DCDC1_SUS	0x24
#define ACT8945A_DCDC2_VSET1	0x30
#define ACT8945A_DCDC2_VSET2	0x31
#define ACT8945A_DCDC2_CTRL	0x32
#define ACT8945A_DCDC2_SUS	0x34
#define ACT8945A_DCDC3_VSET1	0x40
#define ACT8945A_DCDC3_VSET2	0x41
#define ACT8945A_DCDC3_CTRL	0x42
#define ACT8945A_DCDC3_SUS	0x44
#define ACT8945A_LDO1_VSET	0x50
#define ACT8945A_LDO1_CTRL	0x51
#define ACT8945A_LDO1_SUS	0x52
#define ACT8945A_LDO2_VSET	0x54
#define ACT8945A_LDO2_CTRL	0x55
#define ACT8945A_LDO2_SUS	0x56
#define ACT8945A_LDO3_VSET	0x60
#define ACT8945A_LDO3_CTRL	0x61
#define ACT8945A_LDO3_SUS	0x62
#define ACT8945A_LDO4_VSET	0x64
#define ACT8945A_LDO4_CTRL	0x65
#define ACT8945A_LDO4_SUS	0x66

/**
 * Field Definitions.
 */
#define ACT8945A_ENA		0x80	/* ON - [7] */
#define ACT8945A_VSEL_MASK	0x3F	/* VSET - [5:0] */

/**
 * ACT8945A Voltage Number
 */
#define ACT8945A_VOLTAGE_NUM	64

enum {
	ACT8945A_ID_DCDC1,
	ACT8945A_ID_DCDC2,
	ACT8945A_ID_DCDC3,
	ACT8945A_ID_LDO1,
	ACT8945A_ID_LDO2,
	ACT8945A_ID_LDO3,
	ACT8945A_ID_LDO4,
	ACT8945A_ID_MAX,
};

struct act8945a_pmic {
	struct regmap *regmap;
	u32 op_mode[ACT8945A_ID_MAX];
};

static const struct linear_range act8945a_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 23, 25000),
	REGULATOR_LINEAR_RANGE(1200000, 24, 47, 50000),
	REGULATOR_LINEAR_RANGE(2400000, 48, 63, 100000),
};

static int act8945a_set_suspend_state(struct regulator_dev *rdev, bool enable)
{
	struct regmap *regmap = rdev->regmap;
	int id = rdev_get_id(rdev);
	int reg, val;

	switch (id) {
	case ACT8945A_ID_DCDC1:
		reg = ACT8945A_DCDC1_SUS;
		val = 0xa8;
		break;
	case ACT8945A_ID_DCDC2:
		reg = ACT8945A_DCDC2_SUS;
		val = 0xa8;
		break;
	case ACT8945A_ID_DCDC3:
		reg = ACT8945A_DCDC3_SUS;
		val = 0xa8;
		break;
	case ACT8945A_ID_LDO1:
		reg = ACT8945A_LDO1_SUS;
		val = 0xe8;
		break;
	case ACT8945A_ID_LDO2:
		reg = ACT8945A_LDO2_SUS;
		val = 0xe8;
		break;
	case ACT8945A_ID_LDO3:
		reg = ACT8945A_LDO3_SUS;
		val = 0xe8;
		break;
	case ACT8945A_ID_LDO4:
		reg = ACT8945A_LDO4_SUS;
		val = 0xe8;
		break;
	default:
		return -EINVAL;
	}

	if (enable)
		val |= BIT(4);

	/*
	 * Ask the PMIC to enable/disable this output when entering hibernate
	 * mode.
	 */
	return regmap_write(regmap, reg, val);
}

static int act8945a_set_suspend_enable(struct regulator_dev *rdev)
{
	return act8945a_set_suspend_state(rdev, true);
}

static int act8945a_set_suspend_disable(struct regulator_dev *rdev)
{
	return act8945a_set_suspend_state(rdev, false);
}

static unsigned int act8945a_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case ACT8945A_REGULATOR_MODE_FIXED:
	case ACT8945A_REGULATOR_MODE_NORMAL:
		return REGULATOR_MODE_NORMAL;
	case ACT8945A_REGULATOR_MODE_LOWPOWER:
		return REGULATOR_MODE_STANDBY;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static int act8945a_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct act8945a_pmic *act8945a = rdev_get_drvdata(rdev);
	struct regmap *regmap = rdev->regmap;
	int id = rdev_get_id(rdev);
	int reg, ret, val = 0;

	switch (id) {
	case ACT8945A_ID_DCDC1:
		reg = ACT8945A_DCDC1_CTRL;
		break;
	case ACT8945A_ID_DCDC2:
		reg = ACT8945A_DCDC2_CTRL;
		break;
	case ACT8945A_ID_DCDC3:
		reg = ACT8945A_DCDC3_CTRL;
		break;
	case ACT8945A_ID_LDO1:
		reg = ACT8945A_LDO1_CTRL;
		break;
	case ACT8945A_ID_LDO2:
		reg = ACT8945A_LDO2_CTRL;
		break;
	case ACT8945A_ID_LDO3:
		reg = ACT8945A_LDO3_CTRL;
		break;
	case ACT8945A_ID_LDO4:
		reg = ACT8945A_LDO4_CTRL;
		break;
	default:
		return -EINVAL;
	}

	switch (mode) {
	case REGULATOR_MODE_STANDBY:
		if (id > ACT8945A_ID_DCDC3)
			val = BIT(5);
		break;
	case REGULATOR_MODE_NORMAL:
		if (id <= ACT8945A_ID_DCDC3)
			val = BIT(5);
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_update_bits(regmap, reg, BIT(5), val);
	if (ret)
		return ret;

	act8945a->op_mode[id] = mode;

	return 0;
}

static unsigned int act8945a_get_mode(struct regulator_dev *rdev)
{
	struct act8945a_pmic *act8945a = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);

	if (id < ACT8945A_ID_DCDC1 || id >= ACT8945A_ID_MAX)
		return -EINVAL;

	return act8945a->op_mode[id];
}

static const struct regulator_ops act8945a_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.set_mode		= act8945a_set_mode,
	.get_mode		= act8945a_get_mode,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_suspend_enable	= act8945a_set_suspend_enable,
	.set_suspend_disable	= act8945a_set_suspend_disable,
};

#define ACT89xx_REG(_name, _family, _id, _vsel_reg, _supply)		\
	[_family##_ID_##_id] = {					\
		.name			= _name,			\
		.supply_name		= _supply,			\
		.of_match		= of_match_ptr("REG_"#_id),	\
		.of_map_mode		= act8945a_of_map_mode,		\
		.regulators_node	= of_match_ptr("regulators"),	\
		.id			= _family##_ID_##_id,		\
		.type			= REGULATOR_VOLTAGE,		\
		.ops			= &act8945a_ops,		\
		.n_voltages		= ACT8945A_VOLTAGE_NUM,		\
		.linear_ranges		= act8945a_voltage_ranges,	\
		.n_linear_ranges	= ARRAY_SIZE(act8945a_voltage_ranges), \
		.vsel_reg		= _family##_##_id##_##_vsel_reg, \
		.vsel_mask		= ACT8945A_VSEL_MASK,		\
		.enable_reg		= _family##_##_id##_CTRL,	\
		.enable_mask		= ACT8945A_ENA,			\
		.owner			= THIS_MODULE,			\
	}

static const struct regulator_desc act8945a_regulators[] = {
	ACT89xx_REG("DCDC_REG1", ACT8945A, DCDC1, VSET1, "vp1"),
	ACT89xx_REG("DCDC_REG2", ACT8945A, DCDC2, VSET1, "vp2"),
	ACT89xx_REG("DCDC_REG3", ACT8945A, DCDC3, VSET1, "vp3"),
	ACT89xx_REG("LDO_REG1", ACT8945A, LDO1, VSET, "inl45"),
	ACT89xx_REG("LDO_REG2", ACT8945A, LDO2, VSET, "inl45"),
	ACT89xx_REG("LDO_REG3", ACT8945A, LDO3, VSET, "inl67"),
	ACT89xx_REG("LDO_REG4", ACT8945A, LDO4, VSET, "inl67"),
};

static const struct regulator_desc act8945a_alt_regulators[] = {
	ACT89xx_REG("DCDC_REG1", ACT8945A, DCDC1, VSET2, "vp1"),
	ACT89xx_REG("DCDC_REG2", ACT8945A, DCDC2, VSET2, "vp2"),
	ACT89xx_REG("DCDC_REG3", ACT8945A, DCDC3, VSET2, "vp3"),
	ACT89xx_REG("LDO_REG1", ACT8945A, LDO1, VSET, "inl45"),
	ACT89xx_REG("LDO_REG2", ACT8945A, LDO2, VSET, "inl45"),
	ACT89xx_REG("LDO_REG3", ACT8945A, LDO3, VSET, "inl67"),
	ACT89xx_REG("LDO_REG4", ACT8945A, LDO4, VSET, "inl67"),
};

static int act8945a_pmic_probe(struct platform_device *pdev)
{
	struct regulator_config config = { };
	const struct regulator_desc *regulators;
	struct act8945a_pmic *act8945a;
	struct regulator_dev *rdev;
	int i, num_regulators;
	bool voltage_select;

	act8945a = devm_kzalloc(&pdev->dev, sizeof(*act8945a), GFP_KERNEL);
	if (!act8945a)
		return -ENOMEM;

	act8945a->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!act8945a->regmap) {
		dev_err(&pdev->dev,
			"could not retrieve regmap from parent device\n");
		return -EINVAL;
	}

	voltage_select = of_property_read_bool(pdev->dev.parent->of_node,
					       "active-semi,vsel-high");

	if (voltage_select) {
		regulators = act8945a_alt_regulators;
		num_regulators = ARRAY_SIZE(act8945a_alt_regulators);
	} else {
		regulators = act8945a_regulators;
		num_regulators = ARRAY_SIZE(act8945a_regulators);
	}

	config.dev = &pdev->dev;
	config.dev->of_node = pdev->dev.parent->of_node;
	config.driver_data = act8945a;
	for (i = 0; i < num_regulators; i++) {
		rdev = devm_regulator_register(&pdev->dev, &regulators[i],
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev,
				"failed to register %s regulator\n",
				regulators[i].name);
			return PTR_ERR(rdev);
		}
	}

	platform_set_drvdata(pdev, act8945a);

	/* Unlock expert registers. */
	return regmap_write(act8945a->regmap, ACT8945A_SYS_UNLK_REGS, 0xef);
}

static int __maybe_unused act8945a_suspend(struct device *pdev)
{
	struct act8945a_pmic *act8945a = dev_get_drvdata(pdev);

	/*
	 * Ask the PMIC to enter the suspend mode on the next PWRHLD
	 * transition.
	 */
	return regmap_write(act8945a->regmap, ACT8945A_SYS_CTRL, 0x42);
}

static SIMPLE_DEV_PM_OPS(act8945a_pm, act8945a_suspend, NULL);

static void act8945a_pmic_shutdown(struct platform_device *pdev)
{
	struct act8945a_pmic *act8945a = platform_get_drvdata(pdev);

	/*
	 * Ask the PMIC to shutdown everything on the next PWRHLD transition.
	 */
	regmap_write(act8945a->regmap, ACT8945A_SYS_CTRL, 0x0);
}

static struct platform_driver act8945a_pmic_driver = {
	.driver = {
		.name = "act8945a-regulator",
		.pm = &act8945a_pm,
	},
	.probe = act8945a_pmic_probe,
	.shutdown = act8945a_pmic_shutdown,
};
module_platform_driver(act8945a_pmic_driver);

MODULE_DESCRIPTION("Active-semi ACT8945A voltage regulator driver");
MODULE_AUTHOR("Wenyou Yang <wenyou.yang@atmel.com>");
MODULE_LICENSE("GPL");

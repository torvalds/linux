// SPDX-License-Identifier: GPL-2.0+
//
// s2dos05.c - Regulator driver for the Samsung s2dos05
//
// Copyright (C) 2025 Dzmitry Sankouski <dsankouski@gmail.com>

#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/samsung/core.h>
#include <linux/regulator/s2dos05.h>
#include <linux/i2c.h>

struct s2dos05_data {
	struct regmap *regmap;
	struct device *dev;
};

#define _BUCK(macro)	S2DOS05_BUCK##macro
#define _buck_ops(num)	s2dos05_ops##num
#define _LDO(macro)	S2DOS05_LDO##macro
#define _REG(ctrl)	S2DOS05_REG##ctrl
#define _ldo_ops(num)	s2dos05_ops##num
#define _MASK(macro)	S2DOS05_ENABLE_MASK##macro
#define _TIME(macro)	S2DOS05_ENABLE_TIME##macro

#define BUCK_DESC(_name, _id, _ops, m, s, v, e, em, t, a) {	\
	.name		= _name,				\
	.id		= _id,					\
	.ops		= _ops,					\
	.of_match = of_match_ptr(_name),			\
	.of_match_full_name = true,				\
	.regulators_node = of_match_ptr("regulators"),		\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= m,					\
	.uV_step	= s,					\
	.n_voltages	= S2DOS05_BUCK_N_VOLTAGES,		\
	.vsel_reg	= v,					\
	.vsel_mask	= S2DOS05_BUCK_VSEL_MASK,		\
	.enable_reg	= e,					\
	.enable_mask	= em,					\
	.enable_time	= t,					\
	.active_discharge_off = 0,				\
	.active_discharge_on = S2DOS05_BUCK_FD_MASK,		\
	.active_discharge_reg	= a,				\
	.active_discharge_mask	= S2DOS05_BUCK_FD_MASK		\
}

#define LDO_DESC(_name, _id, _ops, m, s, v, e, em, t, a) {	\
	.name		= _name,				\
	.id		= _id,					\
	.ops		= _ops,					\
	.of_match = of_match_ptr(_name),			\
	.of_match_full_name = true,				\
	.regulators_node = of_match_ptr("regulators"),		\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= m,					\
	.uV_step	= s,					\
	.n_voltages	= S2DOS05_LDO_N_VOLTAGES,		\
	.vsel_reg	= v,					\
	.vsel_mask	= S2DOS05_LDO_VSEL_MASK,		\
	.enable_reg	= e,					\
	.enable_mask	= em,					\
	.enable_time	= t,					\
	.active_discharge_off = 0,				\
	.active_discharge_on = S2DOS05_LDO_FD_MASK,		\
	.active_discharge_reg	= a,				\
	.active_discharge_mask	= S2DOS05_LDO_FD_MASK		\
}

static const struct regulator_ops s2dos05_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_active_discharge	= regulator_set_active_discharge_regmap,
};

static const struct regulator_desc regulators[S2DOS05_REGULATOR_MAX] = {
		// name, id, ops, min_uv, uV_step, vsel_reg, enable_reg
		LDO_DESC("ldo1", _LDO(1), &_ldo_ops(), _LDO(_MIN1),
			_LDO(_STEP1), _REG(_LDO1_CFG),
			_REG(_EN), _MASK(_L1), _TIME(_LDO), _REG(_LDO1_CFG)),
		LDO_DESC("ldo2", _LDO(2), &_ldo_ops(), _LDO(_MIN1),
			_LDO(_STEP1), _REG(_LDO2_CFG),
			_REG(_EN), _MASK(_L2), _TIME(_LDO), _REG(_LDO2_CFG)),
		LDO_DESC("ldo3", _LDO(3), &_ldo_ops(), _LDO(_MIN2),
			_LDO(_STEP1), _REG(_LDO3_CFG),
			_REG(_EN), _MASK(_L3), _TIME(_LDO), _REG(_LDO3_CFG)),
		LDO_DESC("ldo4", _LDO(4), &_ldo_ops(), _LDO(_MIN2),
			_LDO(_STEP1), _REG(_LDO4_CFG),
			_REG(_EN), _MASK(_L4), _TIME(_LDO), _REG(_LDO4_CFG)),
		BUCK_DESC("buck", _BUCK(1), &_buck_ops(), _BUCK(_MIN1),
			_BUCK(_STEP1), _REG(_BUCK_VOUT),
			_REG(_EN), _MASK(_B1), _TIME(_BUCK), _REG(_BUCK_CFG)),
};

static int s2dos05_pmic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sec_pmic_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct s2dos05_data *s2dos05;
	struct regulator_config config = { };
	unsigned int rdev_num = ARRAY_SIZE(regulators);

	s2dos05 = devm_kzalloc(dev, sizeof(*s2dos05), GFP_KERNEL);
	if (!s2dos05)
		return -ENOMEM;

	platform_set_drvdata(pdev, s2dos05);

	s2dos05->regmap = iodev->regmap_pmic;
	s2dos05->dev = dev;
	if (!dev->of_node)
		dev->of_node = dev->parent->of_node;

	config.dev = dev;
	config.driver_data = s2dos05;

	for (int i = 0; i < rdev_num; i++) {
		struct regulator_dev *regulator;

		regulator = devm_regulator_register(&pdev->dev,
						&regulators[i], &config);
		if (IS_ERR(regulator)) {
			return dev_err_probe(&pdev->dev, PTR_ERR(regulator),
					"regulator init failed for %d\n", i);
		}
	}

	return 0;
}

static const struct platform_device_id s2dos05_pmic_id[] = {
	{ "s2dos05-regulator" },
	{ },
};
MODULE_DEVICE_TABLE(platform, s2dos05_pmic_id);

static struct platform_driver s2dos05_platform_driver = {
	.driver = {
		.name = "s2dos05",
	},
	.probe = s2dos05_pmic_probe,
	.id_table = s2dos05_pmic_id,
};
module_platform_driver(s2dos05_platform_driver);

MODULE_AUTHOR("Dzmitry Sankouski <dsankouski@gmail.com>");
MODULE_DESCRIPTION("Samsung S2DOS05 Regulator Driver");
MODULE_LICENSE("GPL");

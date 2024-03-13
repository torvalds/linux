// SPDX-License-Identifier: GPL-2.0
//
// Regulator driver for TPS68470 PMIC
//
// Copyright (c) 2021 Red Hat Inc.
// Copyright (C) 2018 Intel Corporation
//
// Authors:
//	Hans de Goede <hdegoede@redhat.com>
//	Zaikuo Wang <zaikuo.wang@intel.com>
//	Tianshu Qiu <tian.shu.qiu@intel.com>
//	Jian Xu Zheng <jian.xu.zheng@intel.com>
//	Yuning Pu <yuning.pu@intel.com>
//	Rajmohan Mani <rajmohan.mani@intel.com>

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/tps68470.h>
#include <linux/module.h>
#include <linux/platform_data/tps68470.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

struct tps68470_regulator_data {
	struct clk *clk;
};

#define TPS68470_REGULATOR(_name, _id, _ops, _n,			\
			   _vr, _vm, _er, _em, _lr, _nlr)		\
	[TPS68470_ ## _name] = {					\
		.name			= # _name,			\
		.id			= _id,				\
		.ops			= &_ops,			\
		.n_voltages		= _n,				\
		.type			= REGULATOR_VOLTAGE,		\
		.owner			= THIS_MODULE,			\
		.vsel_reg		= _vr,				\
		.vsel_mask		= _vm,				\
		.enable_reg		= _er,				\
		.enable_mask		= _em,				\
		.linear_ranges		= _lr,				\
		.n_linear_ranges	= _nlr,				\
	}

static const struct linear_range tps68470_ldo_ranges[] = {
	REGULATOR_LINEAR_RANGE(875000, 0, 125, 17800),
};

static const struct linear_range tps68470_core_ranges[] = {
	REGULATOR_LINEAR_RANGE(900000, 0, 42, 25000),
};

static int tps68470_regulator_enable(struct regulator_dev *rdev)
{
	struct tps68470_regulator_data *data = rdev->reg_data;
	int ret;

	/* The Core buck regulator needs the PMIC's PLL to be enabled */
	if (rdev->desc->id == TPS68470_CORE) {
		ret = clk_prepare_enable(data->clk);
		if (ret) {
			dev_err(&rdev->dev, "Error enabling TPS68470 clock\n");
			return ret;
		}
	}

	return regulator_enable_regmap(rdev);
}

static int tps68470_regulator_disable(struct regulator_dev *rdev)
{
	struct tps68470_regulator_data *data = rdev->reg_data;

	if (rdev->desc->id == TPS68470_CORE)
		clk_disable_unprepare(data->clk);

	return regulator_disable_regmap(rdev);
}

/* Operations permitted on DCDCx, LDO2, LDO3 and LDO4 */
static const struct regulator_ops tps68470_regulator_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= tps68470_regulator_enable,
	.disable		= tps68470_regulator_disable,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
};

static const struct regulator_ops tps68470_always_on_reg_ops = {
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
};

static const struct regulator_desc regulators[] = {
	TPS68470_REGULATOR(CORE, TPS68470_CORE, tps68470_regulator_ops, 43,
			   TPS68470_REG_VDVAL, TPS68470_VDVAL_DVOLT_MASK,
			   TPS68470_REG_VDCTL, TPS68470_VDCTL_EN_MASK,
			   tps68470_core_ranges, ARRAY_SIZE(tps68470_core_ranges)),
	TPS68470_REGULATOR(ANA, TPS68470_ANA, tps68470_regulator_ops, 126,
			   TPS68470_REG_VAVAL, TPS68470_VAVAL_AVOLT_MASK,
			   TPS68470_REG_VACTL, TPS68470_VACTL_EN_MASK,
			   tps68470_ldo_ranges, ARRAY_SIZE(tps68470_ldo_ranges)),
	TPS68470_REGULATOR(VCM, TPS68470_VCM, tps68470_regulator_ops, 126,
			   TPS68470_REG_VCMVAL, TPS68470_VCMVAL_VCVOLT_MASK,
			   TPS68470_REG_VCMCTL, TPS68470_VCMCTL_EN_MASK,
			   tps68470_ldo_ranges, ARRAY_SIZE(tps68470_ldo_ranges)),
	TPS68470_REGULATOR(VIO, TPS68470_VIO, tps68470_always_on_reg_ops, 126,
			   TPS68470_REG_VIOVAL, TPS68470_VIOVAL_IOVOLT_MASK,
			   0, 0,
			   tps68470_ldo_ranges, ARRAY_SIZE(tps68470_ldo_ranges)),
/*
 * (1) This regulator must have the same voltage as VIO if S_IO LDO is used to
 *     power a sensor/VCM which I2C is daisy chained behind the PMIC.
 * (2) If there is no I2C daisy chain it can be set freely.
 */
	TPS68470_REGULATOR(VSIO, TPS68470_VSIO, tps68470_regulator_ops, 126,
			   TPS68470_REG_VSIOVAL, TPS68470_VSIOVAL_IOVOLT_MASK,
			   TPS68470_REG_S_I2C_CTL, TPS68470_S_I2C_CTL_EN_MASK,
			   tps68470_ldo_ranges, ARRAY_SIZE(tps68470_ldo_ranges)),
	TPS68470_REGULATOR(AUX1, TPS68470_AUX1, tps68470_regulator_ops, 126,
			   TPS68470_REG_VAUX1VAL, TPS68470_VAUX1VAL_AUX1VOLT_MASK,
			   TPS68470_REG_VAUX1CTL, TPS68470_VAUX1CTL_EN_MASK,
			   tps68470_ldo_ranges, ARRAY_SIZE(tps68470_ldo_ranges)),
	TPS68470_REGULATOR(AUX2, TPS68470_AUX2, tps68470_regulator_ops, 126,
			   TPS68470_REG_VAUX2VAL, TPS68470_VAUX2VAL_AUX2VOLT_MASK,
			   TPS68470_REG_VAUX2CTL, TPS68470_VAUX2CTL_EN_MASK,
			   tps68470_ldo_ranges, ARRAY_SIZE(tps68470_ldo_ranges)),
};

static int tps68470_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tps68470_regulator_platform_data *pdata = dev_get_platdata(dev);
	struct tps68470_regulator_data *data;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	int i;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->clk = devm_clk_get(dev, "tps68470-clk");
	if (IS_ERR(data->clk))
		return dev_err_probe(dev, PTR_ERR(data->clk), "getting tps68470-clk\n");

	config.dev = dev->parent;
	config.regmap = dev_get_drvdata(dev->parent);
	config.driver_data = data;

	for (i = 0; i < TPS68470_NUM_REGULATORS; i++) {
		if (pdata)
			config.init_data = pdata->reg_init_data[i];
		else
			config.init_data = NULL;

		rdev = devm_regulator_register(dev, &regulators[i], &config);
		if (IS_ERR(rdev))
			return dev_err_probe(dev, PTR_ERR(rdev),
					     "registering %s regulator\n",
					     regulators[i].name);
	}

	return 0;
}

static struct platform_driver tps68470_regulator_driver = {
	.driver = {
		.name = "tps68470-regulator",
	},
	.probe = tps68470_regulator_probe,
};

/*
 * The ACPI tps68470 probe-ordering depends on the clk/gpio/regulator drivers
 * registering before the drivers for the camera-sensors which use them bind.
 * subsys_initcall() ensures this when the drivers are builtin.
 */
static int __init tps68470_regulator_init(void)
{
	return platform_driver_register(&tps68470_regulator_driver);
}
subsys_initcall(tps68470_regulator_init);

static void __exit tps68470_regulator_exit(void)
{
	platform_driver_unregister(&tps68470_regulator_driver);
}
module_exit(tps68470_regulator_exit);

MODULE_ALIAS("platform:tps68470-regulator");
MODULE_DESCRIPTION("TPS68470 voltage regulator driver");
MODULE_LICENSE("GPL v2");

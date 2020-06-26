// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012, 2016, 2018, 2020, The Linux Foundation.
 * All rights reserved.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/regulator/debug-regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

struct regulator_stub {
	struct regulator_desc	rdesc;
	int			voltage;
	bool			enabled;
	unsigned int		mode;
	int			hpm_min_load_uA;
	int			system_load_uA;
};

static int regulator_stub_set_voltage(struct regulator_dev *rdev, int min_uV,
				  int max_uV, unsigned int *selector)
{
	struct regulator_stub *vreg = rdev_get_drvdata(rdev);

	vreg->voltage = min_uV;

	return 0;
}

static int regulator_stub_get_voltage(struct regulator_dev *rdev)
{
	struct regulator_stub *vreg = rdev_get_drvdata(rdev);

	return vreg->voltage;
}

static int regulator_stub_list_voltage(struct regulator_dev *rdev,
				    unsigned int selector)
{
	struct regulation_constraints *constraints = rdev->constraints;

	if (selector >= 2)
		return -EINVAL;
	else if (selector == 0)
		return constraints->min_uV;
	else
		return constraints->max_uV;
}

static unsigned int regulator_stub_get_mode(struct regulator_dev *rdev)
{
	struct regulator_stub *vreg = rdev_get_drvdata(rdev);

	return vreg->mode;
}

static int regulator_stub_set_mode(struct regulator_dev *rdev,
				   unsigned int mode)
{
	struct regulator_stub *vreg = rdev_get_drvdata(rdev);

	if (mode != REGULATOR_MODE_NORMAL && mode != REGULATOR_MODE_IDLE) {
		dev_err(&rdev->dev, "%s: invalid mode requested %u\n",
			__func__, mode);
		return -EINVAL;
	}
	vreg->mode = mode;

	return 0;
}

static unsigned int regulator_stub_get_optimum_mode(struct regulator_dev *rdev,
		int input_uV, int output_uV, int load_uA)
{
	struct regulator_stub *vreg = rdev_get_drvdata(rdev);
	unsigned int mode;

	if (load_uA + vreg->system_load_uA >= vreg->hpm_min_load_uA)
		mode = REGULATOR_MODE_NORMAL;
	else
		mode = REGULATOR_MODE_IDLE;

	return mode;
}

static int regulator_stub_enable(struct regulator_dev *rdev)
{
	struct regulator_stub *vreg = rdev_get_drvdata(rdev);

	vreg->enabled = true;

	return 0;
}

static int regulator_stub_disable(struct regulator_dev *rdev)
{
	struct regulator_stub *vreg = rdev_get_drvdata(rdev);

	vreg->enabled = false;

	return 0;
}

static int regulator_stub_is_enabled(struct regulator_dev *rdev)
{
	struct regulator_stub *vreg = rdev_get_drvdata(rdev);

	return vreg->enabled;
}

static const struct regulator_ops regulator_stub_ops = {
	.enable			= regulator_stub_enable,
	.disable		= regulator_stub_disable,
	.is_enabled		= regulator_stub_is_enabled,
	.set_voltage		= regulator_stub_set_voltage,
	.get_voltage		= regulator_stub_get_voltage,
	.list_voltage		= regulator_stub_list_voltage,
	.set_mode		= regulator_stub_set_mode,
	.get_mode		= regulator_stub_get_mode,
	.get_optimum_mode	= regulator_stub_get_optimum_mode,
};

static int regulator_stub_probe(struct platform_device *pdev)
{
	struct regulator_config reg_config = {};
	struct regulator_init_data *init_data;
	struct device *dev = &pdev->dev;
	struct regulator_stub *vreg;
	struct regulator_dev *rdev;
	int rc;

	if (!dev->of_node) {
		dev_err(dev, "%s: device node missing\n", __func__);
		return -ENODEV;
	}

	vreg = devm_kzalloc(dev, sizeof(*vreg), GFP_KERNEL);
	if (!vreg)
		return -ENOMEM;

	init_data = of_get_regulator_init_data(dev, dev->of_node, &vreg->rdesc);
	if (!init_data)
		return -ENOMEM;

	if (!init_data->constraints.name) {
		dev_err(dev, "%s: regulator name not specified\n", __func__);
		return -EINVAL;
	}

	of_property_read_u32(dev->of_node, "qcom,system-load",
				&vreg->system_load_uA);
	of_property_read_u32(dev->of_node, "qcom,hpm-min-load",
				&vreg->hpm_min_load_uA);

	vreg->voltage = init_data->constraints.min_uV;
	if (vreg->system_load_uA >= vreg->hpm_min_load_uA)
		vreg->mode = REGULATOR_MODE_NORMAL;
	else
		vreg->mode = REGULATOR_MODE_IDLE;

	init_data->constraints.input_uV	= init_data->constraints.max_uV;
	init_data->constraints.valid_ops_mask |= REGULATOR_CHANGE_MODE |
						 REGULATOR_CHANGE_DRMS;
	init_data->constraints.valid_modes_mask
			= REGULATOR_MODE_NORMAL | REGULATOR_MODE_IDLE;

	/*
	 * Ensure that voltage set points are handled correctly for regulators
	 * which have a specified voltage constraint range, as well as those
	 * that do not.
	 */
	if (init_data->constraints.min_uV == 0 &&
	    init_data->constraints.max_uV == 0)
		vreg->rdesc.n_voltages = 0;
	else
		vreg->rdesc.n_voltages = 2;

	vreg->rdesc.name = devm_kstrdup(dev, init_data->constraints.name,
					GFP_KERNEL);
	if (!vreg->rdesc.name)
		return -ENOMEM;

	vreg->rdesc.supply_name	= "parent";
	vreg->rdesc.ops		= &regulator_stub_ops;
	vreg->rdesc.owner	= THIS_MODULE;
	vreg->rdesc.type	= REGULATOR_VOLTAGE;

	reg_config.dev		= dev;
	reg_config.init_data	= init_data;
	reg_config.driver_data	= vreg;
	reg_config.of_node	= dev->of_node;

	rdev = devm_regulator_register(dev, &vreg->rdesc, &reg_config);
	if (IS_ERR(rdev)) {
		rc = PTR_ERR(rdev);
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "%s: regulator_register failed\n",
				__func__);
		return rc;
	}

	rc = devm_regulator_debug_register(dev, rdev);
	if (rc)
		dev_err(dev, "failed to register debug regulator, rc=%d\n", rc);

	return 0;
}

static const struct of_device_id regulator_stub_match_table[] = {
	{ .compatible = "qcom,stub-regulator", },
	{}
};

static struct platform_driver regulator_stub_driver = {
	.driver	= {
		.name = "stub-regulator",
		.of_match_table = of_match_ptr(regulator_stub_match_table),
	},
	.probe	= regulator_stub_probe,
};

int __init regulator_stub_init(void)
{
	return platform_driver_register(&regulator_stub_driver);
}
postcore_initcall(regulator_stub_init);

static void __exit regulator_stub_exit(void)
{
	platform_driver_unregister(&regulator_stub_driver);
}
module_exit(regulator_stub_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("stub regulator driver");

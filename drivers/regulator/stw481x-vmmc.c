/*
 * Regulator driver for STw4810/STw4811 VMMC regulator.
 *
 * Copyright (C) 2013 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/mfd/stw481x.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

static const unsigned int stw481x_vmmc_voltages[] = {
	1800000,
	1800000,
	2850000,
	3000000,
	1850000,
	2600000,
	2700000,
	3300000,
};

static struct regulator_ops stw481x_vmmc_ops = {
	.list_voltage = regulator_list_voltage_table,
	.enable      = regulator_enable_regmap,
	.disable     = regulator_disable_regmap,
	.is_enabled  = regulator_is_enabled_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
};

static struct regulator_desc vmmc_regulator = {
	.name = "VMMC",
	.id   = 0,
	.ops  = &stw481x_vmmc_ops,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.n_voltages = ARRAY_SIZE(stw481x_vmmc_voltages),
	.volt_table = stw481x_vmmc_voltages,
	.enable_time = 200, /* FIXME: look this up */
	.enable_reg = STW_CONF1,
	.enable_mask = STW_CONF1_PDN_VMMC,
	.vsel_reg = STW_CONF1,
	.vsel_mask = STW_CONF1_VMMC_MASK,
};

static int stw481x_vmmc_regulator_probe(struct platform_device *pdev)
{
	struct stw481x *stw481x = dev_get_platdata(&pdev->dev);
	struct regulator_config config = { };
	int ret;

	/* First disable the external VMMC if it's active */
	ret = regmap_update_bits(stw481x->map, STW_CONF2,
				 STW_CONF2_VMMC_EXT, 0);
	if (ret) {
		dev_err(&pdev->dev, "could not disable external VMMC\n");
		return ret;
	}

	/* Register VMMC regulator */
	config.dev = &pdev->dev;
	config.driver_data = stw481x;
	config.regmap = stw481x->map;
	config.of_node = pdev->dev.of_node;
	config.init_data = of_get_regulator_init_data(&pdev->dev,
						      pdev->dev.of_node,
						      &vmmc_regulator);

	stw481x->vmmc_regulator = devm_regulator_register(&pdev->dev,
						&vmmc_regulator, &config);
	if (IS_ERR(stw481x->vmmc_regulator)) {
		dev_err(&pdev->dev,
			"error initializing STw481x VMMC regulator\n");
		return PTR_ERR(stw481x->vmmc_regulator);
	}

	dev_info(&pdev->dev, "initialized STw481x VMMC regulator\n");
	return 0;
}

static const struct of_device_id stw481x_vmmc_match[] = {
	{ .compatible = "st,stw481x-vmmc", },
	{},
};

static struct platform_driver stw481x_vmmc_regulator_driver = {
	.driver = {
		.name  = "stw481x-vmmc-regulator",
		.owner = THIS_MODULE,
		.of_match_table = stw481x_vmmc_match,
	},
	.probe = stw481x_vmmc_regulator_probe,
};

module_platform_driver(stw481x_vmmc_regulator_driver);

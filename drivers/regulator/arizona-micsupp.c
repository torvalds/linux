// SPDX-License-Identifier: GPL-2.0+
//
// arizona-micsupp.c  --  Microphone supply for Arizona devices
//
// Copyright 2012 Wolfson Microelectronics PLC.
//
// Author: Mark Brown <broonie@opensource.wolfsonmicro.com>

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <sound/soc.h>

#include <linux/mfd/arizona/core.h>
#include <linux/mfd/arizona/pdata.h>
#include <linux/mfd/arizona/registers.h>

#include <linux/mfd/madera/core.h>
#include <linux/mfd/madera/pdata.h>
#include <linux/mfd/madera/registers.h>

#include <linux/regulator/arizona-micsupp.h>

struct arizona_micsupp {
	struct regulator_dev *regulator;
	struct regmap *regmap;
	struct snd_soc_dapm_context **dapm;
	unsigned int enable_reg;
	struct device *dev;

	struct regulator_consumer_supply supply;
	struct regulator_init_data init_data;

	struct work_struct check_cp_work;
};

static void arizona_micsupp_check_cp(struct work_struct *work)
{
	struct arizona_micsupp *micsupp =
		container_of(work, struct arizona_micsupp, check_cp_work);
	struct snd_soc_dapm_context *dapm = *micsupp->dapm;
	struct snd_soc_component *component;
	unsigned int val;
	int ret;

	ret = regmap_read(micsupp->regmap, micsupp->enable_reg, &val);
	if (ret != 0) {
		dev_err(micsupp->dev,
			"Failed to read CP state: %d\n", ret);
		return;
	}

	if (dapm) {
		component = snd_soc_dapm_to_component(dapm);

		if ((val & (ARIZONA_CPMIC_ENA | ARIZONA_CPMIC_BYPASS)) ==
		    ARIZONA_CPMIC_ENA)
			snd_soc_component_force_enable_pin(component,
							   "MICSUPP");
		else
			snd_soc_component_disable_pin(component, "MICSUPP");

		snd_soc_dapm_sync(dapm);
	}
}

static int arizona_micsupp_enable(struct regulator_dev *rdev)
{
	struct arizona_micsupp *micsupp = rdev_get_drvdata(rdev);
	int ret;

	ret = regulator_enable_regmap(rdev);

	if (ret == 0)
		schedule_work(&micsupp->check_cp_work);

	return ret;
}

static int arizona_micsupp_disable(struct regulator_dev *rdev)
{
	struct arizona_micsupp *micsupp = rdev_get_drvdata(rdev);
	int ret;

	ret = regulator_disable_regmap(rdev);
	if (ret == 0)
		schedule_work(&micsupp->check_cp_work);

	return ret;
}

static int arizona_micsupp_set_bypass(struct regulator_dev *rdev, bool ena)
{
	struct arizona_micsupp *micsupp = rdev_get_drvdata(rdev);
	int ret;

	ret = regulator_set_bypass_regmap(rdev, ena);
	if (ret == 0)
		schedule_work(&micsupp->check_cp_work);

	return ret;
}

static const struct regulator_ops arizona_micsupp_ops = {
	.enable = arizona_micsupp_enable,
	.disable = arizona_micsupp_disable,
	.is_enabled = regulator_is_enabled_regmap,

	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,

	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,

	.get_bypass = regulator_get_bypass_regmap,
	.set_bypass = arizona_micsupp_set_bypass,
};

static const struct linear_range arizona_micsupp_ranges[] = {
	REGULATOR_LINEAR_RANGE(1700000, 0,    0x1e, 50000),
	REGULATOR_LINEAR_RANGE(3300000, 0x1f, 0x1f, 0),
};

static const struct regulator_desc arizona_micsupp = {
	.name = "MICVDD",
	.supply_name = "CPVDD",
	.type = REGULATOR_VOLTAGE,
	.n_voltages = 32,
	.ops = &arizona_micsupp_ops,

	.vsel_reg = ARIZONA_LDO2_CONTROL_1,
	.vsel_mask = ARIZONA_LDO2_VSEL_MASK,
	.enable_reg = ARIZONA_MIC_CHARGE_PUMP_1,
	.enable_mask = ARIZONA_CPMIC_ENA,
	.bypass_reg = ARIZONA_MIC_CHARGE_PUMP_1,
	.bypass_mask = ARIZONA_CPMIC_BYPASS,

	.linear_ranges = arizona_micsupp_ranges,
	.n_linear_ranges = ARRAY_SIZE(arizona_micsupp_ranges),

	.enable_time = 3000,

	.owner = THIS_MODULE,
};

static const struct linear_range arizona_micsupp_ext_ranges[] = {
	REGULATOR_LINEAR_RANGE(900000,  0,    0x14, 25000),
	REGULATOR_LINEAR_RANGE(1500000, 0x15, 0x27, 100000),
};

static const struct regulator_desc arizona_micsupp_ext = {
	.name = "MICVDD",
	.supply_name = "CPVDD",
	.type = REGULATOR_VOLTAGE,
	.n_voltages = 40,
	.ops = &arizona_micsupp_ops,

	.vsel_reg = ARIZONA_LDO2_CONTROL_1,
	.vsel_mask = ARIZONA_LDO2_VSEL_MASK,
	.enable_reg = ARIZONA_MIC_CHARGE_PUMP_1,
	.enable_mask = ARIZONA_CPMIC_ENA,
	.bypass_reg = ARIZONA_MIC_CHARGE_PUMP_1,
	.bypass_mask = ARIZONA_CPMIC_BYPASS,

	.linear_ranges = arizona_micsupp_ext_ranges,
	.n_linear_ranges = ARRAY_SIZE(arizona_micsupp_ext_ranges),

	.enable_time = 3000,

	.owner = THIS_MODULE,
};

static const struct regulator_init_data arizona_micsupp_default = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS |
				REGULATOR_CHANGE_VOLTAGE |
				REGULATOR_CHANGE_BYPASS,
		.min_uV = 1700000,
		.max_uV = 3300000,
	},

	.num_consumer_supplies = 1,
};

static const struct regulator_init_data arizona_micsupp_ext_default = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS |
				REGULATOR_CHANGE_VOLTAGE |
				REGULATOR_CHANGE_BYPASS,
		.min_uV = 900000,
		.max_uV = 3300000,
	},

	.num_consumer_supplies = 1,
};

static const struct regulator_desc madera_micsupp = {
	.name = "MICVDD",
	.supply_name = "CPVDD1",
	.type = REGULATOR_VOLTAGE,
	.n_voltages = 40,
	.ops = &arizona_micsupp_ops,

	.vsel_reg = MADERA_LDO2_CONTROL_1,
	.vsel_mask = MADERA_LDO2_VSEL_MASK,
	.enable_reg = MADERA_MIC_CHARGE_PUMP_1,
	.enable_mask = MADERA_CPMIC_ENA,
	.bypass_reg = MADERA_MIC_CHARGE_PUMP_1,
	.bypass_mask = MADERA_CPMIC_BYPASS,

	.linear_ranges = arizona_micsupp_ext_ranges,
	.n_linear_ranges = ARRAY_SIZE(arizona_micsupp_ext_ranges),

	.enable_time = 3000,

	.owner = THIS_MODULE,
};

static int arizona_micsupp_of_get_pdata(struct arizona_micsupp_pdata *pdata,
					struct regulator_config *config,
					const struct regulator_desc *desc)
{
	struct arizona_micsupp *micsupp = config->driver_data;
	struct device_node *np;
	struct regulator_init_data *init_data;

	np = of_get_child_by_name(config->dev->of_node, "micvdd");

	if (np) {
		config->of_node = np;

		init_data = of_get_regulator_init_data(config->dev, np, desc);

		if (init_data) {
			init_data->consumer_supplies = &micsupp->supply;
			init_data->num_consumer_supplies = 1;

			pdata->init_data = init_data;
		}
	}

	return 0;
}

static int arizona_micsupp_common_init(struct platform_device *pdev,
				       struct arizona_micsupp *micsupp,
				       const struct regulator_desc *desc,
				       struct arizona_micsupp_pdata *pdata)
{
	struct regulator_config config = { };
	int ret;

	INIT_WORK(&micsupp->check_cp_work, arizona_micsupp_check_cp);

	micsupp->init_data.consumer_supplies = &micsupp->supply;
	micsupp->supply.supply = "MICVDD";
	micsupp->supply.dev_name = dev_name(micsupp->dev);
	micsupp->enable_reg = desc->enable_reg;

	config.dev = micsupp->dev;
	config.driver_data = micsupp;
	config.regmap = micsupp->regmap;

	if (IS_ENABLED(CONFIG_OF)) {
		if (!dev_get_platdata(micsupp->dev)) {
			ret = arizona_micsupp_of_get_pdata(pdata, &config,
							   desc);
			if (ret < 0)
				return ret;
		}
	}

	if (pdata->init_data)
		config.init_data = pdata->init_data;
	else
		config.init_data = &micsupp->init_data;

	/* Default to regulated mode */
	regmap_update_bits(micsupp->regmap, micsupp->enable_reg,
			   ARIZONA_CPMIC_BYPASS, 0);

	micsupp->regulator = devm_regulator_register(&pdev->dev,
						     desc,
						     &config);

	of_node_put(config.of_node);

	if (IS_ERR(micsupp->regulator)) {
		ret = PTR_ERR(micsupp->regulator);
		dev_err(micsupp->dev, "Failed to register mic supply: %d\n",
			ret);
		return ret;
	}

	platform_set_drvdata(pdev, micsupp);

	return 0;
}

static int arizona_micsupp_probe(struct platform_device *pdev)
{
	struct arizona *arizona = dev_get_drvdata(pdev->dev.parent);
	const struct regulator_desc *desc;
	struct arizona_micsupp *micsupp;

	micsupp = devm_kzalloc(&pdev->dev, sizeof(*micsupp), GFP_KERNEL);
	if (!micsupp)
		return -ENOMEM;

	micsupp->regmap = arizona->regmap;
	micsupp->dapm = &arizona->dapm;
	micsupp->dev = arizona->dev;

	/*
	 * Since the chip usually supplies itself we provide some
	 * default init_data for it.  This will be overridden with
	 * platform data if provided.
	 */
	switch (arizona->type) {
	case WM5110:
	case WM8280:
		desc = &arizona_micsupp_ext;
		micsupp->init_data = arizona_micsupp_ext_default;
		break;
	default:
		desc = &arizona_micsupp;
		micsupp->init_data = arizona_micsupp_default;
		break;
	}

	return arizona_micsupp_common_init(pdev, micsupp, desc,
					   &arizona->pdata.micvdd);
}

static int madera_micsupp_probe(struct platform_device *pdev)
{
	struct madera *madera = dev_get_drvdata(pdev->dev.parent);
	struct arizona_micsupp *micsupp;

	micsupp = devm_kzalloc(&pdev->dev, sizeof(*micsupp), GFP_KERNEL);
	if (!micsupp)
		return -ENOMEM;

	micsupp->regmap = madera->regmap;
	micsupp->dapm = &madera->dapm;
	micsupp->dev = madera->dev;
	micsupp->init_data = arizona_micsupp_ext_default;

	return arizona_micsupp_common_init(pdev, micsupp, &madera_micsupp,
					   &madera->pdata.micvdd);
}

static struct platform_driver arizona_micsupp_driver = {
	.probe = arizona_micsupp_probe,
	.driver		= {
		.name	= "arizona-micsupp",
	},
};

static struct platform_driver madera_micsupp_driver = {
	.probe = madera_micsupp_probe,
	.driver		= {
		.name	= "madera-micsupp",
	},
};

static struct platform_driver * const arizona_micsupp_drivers[] = {
	&arizona_micsupp_driver,
	&madera_micsupp_driver,
};

static int __init arizona_micsupp_init(void)
{
	return platform_register_drivers(arizona_micsupp_drivers,
					 ARRAY_SIZE(arizona_micsupp_drivers));
}
module_init(arizona_micsupp_init);

static void __exit arizona_micsupp_exit(void)
{
	platform_unregister_drivers(arizona_micsupp_drivers,
				    ARRAY_SIZE(arizona_micsupp_drivers));
}
module_exit(arizona_micsupp_exit);

/* Module information */
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("Arizona microphone supply driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:arizona-micsupp");
MODULE_ALIAS("platform:madera-micsupp");

// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 ROHM Semiconductors
// ROHM BD9576MUF/BD9573MUF regulator driver

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/rohm-bd957x.h>
#include <linux/mfd/rohm-generic.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>

#define BD957X_VOUTS1_VOLT	3300000
#define BD957X_VOUTS4_BASE_VOLT	1030000
#define BD957X_VOUTS34_NUM_VOLT	32

static int vout1_volt_table[] = {5000000, 4900000, 4800000, 4700000, 4600000,
				 4500000, 4500000, 4500000, 5000000, 5100000,
				 5200000, 5300000, 5400000, 5500000, 5500000,
				 5500000};

static int vout2_volt_table[] = {1800000, 1780000, 1760000, 1740000, 1720000,
				 1700000, 1680000, 1660000, 1800000, 1820000,
				 1840000, 1860000, 1880000, 1900000, 1920000,
				 1940000};

static int voutl1_volt_table[] = {2500000, 2540000, 2580000, 2620000, 2660000,
				  2700000, 2740000, 2780000, 2500000, 2460000,
				  2420000, 2380000, 2340000, 2300000, 2260000,
				  2220000};

struct bd957x_regulator_data {
	struct regulator_desc desc;
	int base_voltage;
};

static int bd957x_vout34_list_voltage(struct regulator_dev *rdev,
				      unsigned int selector)
{
	const struct regulator_desc *desc = rdev->desc;
	int multiplier = selector & desc->vsel_mask & 0x7f;
	int tune;

	/* VOUT3 and 4 has 10mV step */
	tune = multiplier * 10000;

	if (!(selector & 0x80))
		return desc->fixed_uV - tune;

	return desc->fixed_uV + tune;
}

static int bd957x_list_voltage(struct regulator_dev *rdev,
			       unsigned int selector)
{
	const struct regulator_desc *desc = rdev->desc;
	int index = selector & desc->vsel_mask & 0x7f;

	if (!(selector & 0x80))
		index += desc->n_voltages/2;

	if (index >= desc->n_voltages)
		return -EINVAL;

	return desc->volt_table[index];
}

static const struct regulator_ops bd957x_vout34_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = bd957x_vout34_list_voltage,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static const struct regulator_ops bd957X_vouts1_regulator_ops = {
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct regulator_ops bd957x_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = bd957x_list_voltage,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static struct bd957x_regulator_data bd9576_regulators[] = {
	{
		.desc = {
			.name = "VD50",
			.of_match = of_match_ptr("regulator-vd50"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD957X_VD50,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd957x_ops,
			.volt_table = &vout1_volt_table[0],
			.n_voltages = ARRAY_SIZE(vout1_volt_table),
			.vsel_reg = BD957X_REG_VOUT1_TUNE,
			.vsel_mask = BD957X_MASK_VOUT1_TUNE,
			.enable_reg = BD957X_REG_POW_TRIGGER1,
			.enable_mask = BD957X_REGULATOR_EN_MASK,
			.enable_val = BD957X_REGULATOR_DIS_VAL,
			.enable_is_inverted = true,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "VD18",
			.of_match = of_match_ptr("regulator-vd18"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD957X_VD18,
			.type = REGULATOR_VOLTAGE,
			.ops = &bd957x_ops,
			.volt_table = &vout2_volt_table[0],
			.n_voltages = ARRAY_SIZE(vout2_volt_table),
			.vsel_reg = BD957X_REG_VOUT2_TUNE,
			.vsel_mask = BD957X_MASK_VOUT2_TUNE,
			.enable_reg = BD957X_REG_POW_TRIGGER2,
			.enable_mask = BD957X_REGULATOR_EN_MASK,
			.enable_val = BD957X_REGULATOR_DIS_VAL,
			.enable_is_inverted = true,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "VDDDR",
			.of_match = of_match_ptr("regulator-vdddr"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD957X_VDDDR,
			.ops = &bd957x_vout34_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = BD957X_VOUTS34_NUM_VOLT,
			.vsel_reg = BD957X_REG_VOUT3_TUNE,
			.vsel_mask = BD957X_MASK_VOUT3_TUNE,
			.enable_reg = BD957X_REG_POW_TRIGGER3,
			.enable_mask = BD957X_REGULATOR_EN_MASK,
			.enable_val = BD957X_REGULATOR_DIS_VAL,
			.enable_is_inverted = true,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "VD10",
			.of_match = of_match_ptr("regulator-vd10"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD957X_VD10,
			.ops = &bd957x_vout34_ops,
			.type = REGULATOR_VOLTAGE,
			.fixed_uV = BD957X_VOUTS4_BASE_VOLT,
			.n_voltages = BD957X_VOUTS34_NUM_VOLT,
			.vsel_reg = BD957X_REG_VOUT4_TUNE,
			.vsel_mask = BD957X_MASK_VOUT4_TUNE,
			.enable_reg = BD957X_REG_POW_TRIGGER4,
			.enable_mask = BD957X_REGULATOR_EN_MASK,
			.enable_val = BD957X_REGULATOR_DIS_VAL,
			.enable_is_inverted = true,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "VOUTL1",
			.of_match = of_match_ptr("regulator-voutl1"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD957X_VOUTL1,
			.ops = &bd957x_ops,
			.type = REGULATOR_VOLTAGE,
			.volt_table = &voutl1_volt_table[0],
			.n_voltages = ARRAY_SIZE(voutl1_volt_table),
			.vsel_reg = BD957X_REG_VOUTL1_TUNE,
			.vsel_mask = BD957X_MASK_VOUTL1_TUNE,
			.enable_reg = BD957X_REG_POW_TRIGGERL1,
			.enable_mask = BD957X_REGULATOR_EN_MASK,
			.enable_val = BD957X_REGULATOR_DIS_VAL,
			.enable_is_inverted = true,
			.owner = THIS_MODULE,
		},
	},
	{
		.desc = {
			.name = "VOUTS1",
			.of_match = of_match_ptr("regulator-vouts1"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD957X_VOUTS1,
			.ops = &bd957X_vouts1_regulator_ops,
			.type = REGULATOR_VOLTAGE,
			.n_voltages = 1,
			.fixed_uV = BD957X_VOUTS1_VOLT,
			.enable_reg = BD957X_REG_POW_TRIGGERS1,
			.enable_mask = BD957X_REGULATOR_EN_MASK,
			.enable_val = BD957X_REGULATOR_DIS_VAL,
			.enable_is_inverted = true,
			.owner = THIS_MODULE,
		},
	},
};

static int bd957x_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	struct regulator_config config = { 0 };
	int i, err;
	bool vout_mode, ddr_sel;
	const struct bd957x_regulator_data *reg_data = &bd9576_regulators[0];
	unsigned int num_reg_data = ARRAY_SIZE(bd9576_regulators);
	enum rohm_chip_type chip = platform_get_device_id(pdev)->driver_data;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap) {
		dev_err(&pdev->dev, "No regmap\n");
		return -EINVAL;
	}
	vout_mode = of_property_read_bool(pdev->dev.parent->of_node,
					 "rohm,vout1-en-low");
	if (vout_mode) {
		struct gpio_desc *en;

		dev_dbg(&pdev->dev, "GPIO controlled mode\n");

		/* VOUT1 enable state judged by VOUT1_EN pin */
		/* See if we have GPIO defined */
		en = devm_gpiod_get_from_of_node(&pdev->dev,
						 pdev->dev.parent->of_node,
						 "rohm,vout1-en-gpios", 0,
						 GPIOD_OUT_LOW, "vout1-en");
		if (!IS_ERR(en)) {
			/* VOUT1_OPS gpio ctrl */
			/*
			 * Regulator core prioritizes the ena_gpio over
			 * enable/disable/is_enabled callbacks so no need to
			 * clear them. We can still use same ops
			 */
			config.ena_gpiod = en;
		} else {
			/*
			 * In theory it is possible someone wants to set
			 * vout1-en LOW during OTP loading and set VOUT1 to be
			 * controlled by GPIO - but control the GPIO from some
			 * where else than this driver. For that to work we
			 * should unset the is_enabled callback here.
			 *
			 * I believe such case where rohm,vout1-en-low is set
			 * and vout1-en-gpios is not is likely to be a
			 * misconfiguration. So let's just err out for now.
			 */
			dev_err(&pdev->dev,
				"Failed to get VOUT1 control GPIO\n");
			return PTR_ERR(en);
		}
	}

	/*
	 * If more than one PMIC needs to be controlled by same processor then
	 * allocate the regulator data array here and use bd9576_regulators as
	 * template. At the moment I see no such use-case so I spare some
	 * bytes and use bd9576_regulators directly for non-constant configs
	 * like DDR voltage selection.
	 */
	ddr_sel =  of_property_read_bool(pdev->dev.parent->of_node,
					 "rohm,ddr-sel-low");
	if (ddr_sel)
		bd9576_regulators[2].desc.fixed_uV = 1350000;
	else
		bd9576_regulators[2].desc.fixed_uV = 1500000;

	switch (chip) {
	case ROHM_CHIP_TYPE_BD9576:
		dev_dbg(&pdev->dev, "Found BD9576MUF\n");
		break;
	case ROHM_CHIP_TYPE_BD9573:
		dev_dbg(&pdev->dev, "Found BD9573MUF\n");
		break;
	default:
		dev_err(&pdev->dev, "Unsupported chip type\n");
		err = -EINVAL;
		goto err;
	}

	config.dev = pdev->dev.parent;
	config.regmap = regmap;

	for (i = 0; i < num_reg_data; i++) {

		const struct regulator_desc *desc;
		struct regulator_dev *rdev;
		const struct bd957x_regulator_data *r;

		r = &reg_data[i];
		desc = &r->desc;

		rdev = devm_regulator_register(&pdev->dev, desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev,
				"failed to register %s regulator\n",
				desc->name);
			err = PTR_ERR(rdev);
			goto err;
		}
		/*
		 * Clear the VOUT1 GPIO setting - rest of the regulators do not
		 * support GPIO control
		 */
		config.ena_gpiod = NULL;
	}

err:
	return err;
}

static const struct platform_device_id bd957x_pmic_id[] = {
	{ "bd9573-pmic", ROHM_CHIP_TYPE_BD9573 },
	{ "bd9576-pmic", ROHM_CHIP_TYPE_BD9576 },
	{ },
};
MODULE_DEVICE_TABLE(platform, bd957x_pmic_id);

static struct platform_driver bd957x_regulator = {
	.driver = {
		.name = "bd957x-pmic",
	},
	.probe = bd957x_probe,
	.id_table = bd957x_pmic_id,
};

module_platform_driver(bd957x_regulator);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("ROHM BD9576/BD9573 voltage regulator driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:bd957x-pmic");

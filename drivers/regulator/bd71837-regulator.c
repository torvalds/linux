// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 ROHM Semiconductors
// bd71837-regulator.c ROHM BD71837MWV regulator driver

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/mfd/bd71837.h>
#include <linux/regulator/of_regulator.h>

struct bd71837_pmic {
	struct regulator_desc descs[BD71837_REGULATOR_CNT];
	struct bd71837 *mfd;
	struct platform_device *pdev;
	struct regulator_dev *rdev[BD71837_REGULATOR_CNT];
};

/*
 * BUCK1/2/3/4
 * BUCK1RAMPRATE[1:0] BUCK1 DVS ramp rate setting
 * 00: 10.00mV/usec 10mV 1uS
 * 01: 5.00mV/usec	10mV 2uS
 * 10: 2.50mV/usec	10mV 4uS
 * 11: 1.25mV/usec	10mV 8uS
 */
static int bd71837_buck1234_set_ramp_delay(struct regulator_dev *rdev,
					   int ramp_delay)
{
	struct bd71837_pmic *pmic = rdev_get_drvdata(rdev);
	struct bd71837 *mfd = pmic->mfd;
	int id = rdev->desc->id;
	unsigned int ramp_value = BUCK_RAMPRATE_10P00MV;

	dev_dbg(&(pmic->pdev->dev), "Buck[%d] Set Ramp = %d\n", id + 1,
		ramp_delay);
	switch (ramp_delay) {
	case 1 ... 1250:
		ramp_value = BUCK_RAMPRATE_1P25MV;
		break;
	case 1251 ... 2500:
		ramp_value = BUCK_RAMPRATE_2P50MV;
		break;
	case 2501 ... 5000:
		ramp_value = BUCK_RAMPRATE_5P00MV;
		break;
	case 5001 ... 10000:
		ramp_value = BUCK_RAMPRATE_10P00MV;
		break;
	default:
		ramp_value = BUCK_RAMPRATE_10P00MV;
		dev_err(&pmic->pdev->dev,
			"%s: ramp_delay: %d not supported, setting 10000mV//us\n",
			rdev->desc->name, ramp_delay);
	}

	return regmap_update_bits(mfd->regmap, BD71837_REG_BUCK1_CTRL + id,
				  BUCK_RAMPRATE_MASK, ramp_value << 6);
}

/* Bucks 1 to 4 support DVS. PWM mode is used when voltage is changed.
 * Bucks 5 to 8 and LDOs can use PFM and must be disabled when voltage
 * is changed. Hence we return -EBUSY for these if voltage is changed
 * when BUCK/LDO is enabled.
 */
static int bd71837_set_voltage_sel_restricted(struct regulator_dev *rdev,
						    unsigned int sel)
{
	int ret;

	ret = regulator_is_enabled_regmap(rdev);
	if (!ret)
		ret = regulator_set_voltage_sel_regmap(rdev, sel);
	else if (ret == 1)
		ret = -EBUSY;
	return ret;
}

static struct regulator_ops bd71837_ldo_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = bd71837_set_voltage_sel_restricted,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static struct regulator_ops bd71837_ldo_regulator_nolinear_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_table,
	.set_voltage_sel = bd71837_set_voltage_sel_restricted,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static struct regulator_ops bd71837_buck_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = bd71837_set_voltage_sel_restricted,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
};

static struct regulator_ops bd71837_buck_regulator_nolinear_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_table,
	.set_voltage_sel = bd71837_set_voltage_sel_restricted,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
};

static struct regulator_ops bd71837_buck1234_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_ramp_delay = bd71837_buck1234_set_ramp_delay,
};

/*
 * BUCK1/2/3/4
 * 0.70 to 1.30V (10mV step)
 */
static const struct regulator_linear_range bd71837_buck1234_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(700000, 0x00, 0x3C, 10000),
	REGULATOR_LINEAR_RANGE(1300000, 0x3D, 0x3F, 0),
};

/*
 * BUCK5
 * 0.9V to 1.35V ()
 */
static const struct regulator_linear_range bd71837_buck5_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(700000, 0x00, 0x03, 100000),
	REGULATOR_LINEAR_RANGE(1050000, 0x04, 0x05, 50000),
	REGULATOR_LINEAR_RANGE(1200000, 0x06, 0x07, 150000),
};

/*
 * BUCK6
 * 3.0V to 3.3V (step 100mV)
 */
static const struct regulator_linear_range bd71837_buck6_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(3000000, 0x00, 0x03, 100000),
};

/*
 * BUCK7
 * 000 = 1.605V
 * 001 = 1.695V
 * 010 = 1.755V
 * 011 = 1.8V (Initial)
 * 100 = 1.845V
 * 101 = 1.905V
 * 110 = 1.95V
 * 111 = 1.995V
 */
static const unsigned int buck_7_volts[] = {
	1605000, 1695000, 1755000, 1800000, 1845000, 1905000, 1950000, 1995000
};

/*
 * BUCK8
 * 0.8V to 1.40V (step 10mV)
 */
static const struct regulator_linear_range bd71837_buck8_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0x00, 0x3C, 10000),
	REGULATOR_LINEAR_RANGE(1400000, 0x3D, 0x3F, 0),
};

/*
 * LDO1
 * 3.0 to 3.3V (100mV step)
 */
static const struct regulator_linear_range bd71837_ldo1_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(3000000, 0x00, 0x03, 100000),
};

/*
 * LDO2
 * 0.8 or 0.9V
 */
const unsigned int ldo_2_volts[] = {
	900000, 800000
};

/*
 * LDO3
 * 1.8 to 3.3V (100mV step)
 */
static const struct regulator_linear_range bd71837_ldo3_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0x00, 0x0F, 100000),
};

/*
 * LDO4
 * 0.9 to 1.8V (100mV step)
 */
static const struct regulator_linear_range bd71837_ldo4_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(900000, 0x00, 0x09, 100000),
	REGULATOR_LINEAR_RANGE(1800000, 0x0A, 0x0F, 0),
};

/*
 * LDO5
 * 1.8 to 3.3V (100mV step)
 */
static const struct regulator_linear_range bd71837_ldo5_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0x00, 0x0F, 100000),
};

/*
 * LDO6
 * 0.9 to 1.8V (100mV step)
 */
static const struct regulator_linear_range bd71837_ldo6_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(900000, 0x00, 0x09, 100000),
	REGULATOR_LINEAR_RANGE(1800000, 0x0A, 0x0F, 0),
};

/*
 * LDO7
 * 1.8 to 3.3V (100mV step)
 */
static const struct regulator_linear_range bd71837_ldo7_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0x00, 0x0F, 100000),
};

static const struct regulator_desc bd71837_regulators[] = {
	{
		.name = "buck1",
		.of_match = of_match_ptr("BUCK1"),
		.regulators_node = of_match_ptr("regulators"),
		.id = BD71837_BUCK1,
		.ops = &bd71837_buck1234_regulator_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = BD71837_BUCK1_VOLTAGE_NUM,
		.linear_ranges = bd71837_buck1234_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(bd71837_buck1234_voltage_ranges),
		.vsel_reg = BD71837_REG_BUCK1_VOLT_RUN,
		.vsel_mask = BUCK1_RUN_MASK,
		.enable_reg = BD71837_REG_BUCK1_CTRL,
		.enable_mask = BD71837_BUCK_EN,
		.owner = THIS_MODULE,
	},
	{
		.name = "buck2",
		.of_match = of_match_ptr("BUCK2"),
		.regulators_node = of_match_ptr("regulators"),
		.id = BD71837_BUCK2,
		.ops = &bd71837_buck1234_regulator_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = BD71837_BUCK2_VOLTAGE_NUM,
		.linear_ranges = bd71837_buck1234_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(bd71837_buck1234_voltage_ranges),
		.vsel_reg = BD71837_REG_BUCK2_VOLT_RUN,
		.vsel_mask = BUCK2_RUN_MASK,
		.enable_reg = BD71837_REG_BUCK2_CTRL,
		.enable_mask = BD71837_BUCK_EN,
		.owner = THIS_MODULE,
	},
	{
		.name = "buck3",
		.of_match = of_match_ptr("BUCK3"),
		.regulators_node = of_match_ptr("regulators"),
		.id = BD71837_BUCK3,
		.ops = &bd71837_buck1234_regulator_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = BD71837_BUCK3_VOLTAGE_NUM,
		.linear_ranges = bd71837_buck1234_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(bd71837_buck1234_voltage_ranges),
		.vsel_reg = BD71837_REG_BUCK3_VOLT_RUN,
		.vsel_mask = BUCK3_RUN_MASK,
		.enable_reg = BD71837_REG_BUCK3_CTRL,
		.enable_mask = BD71837_BUCK_EN,
		.owner = THIS_MODULE,
	},
	{
		.name = "buck4",
		.of_match = of_match_ptr("BUCK4"),
		.regulators_node = of_match_ptr("regulators"),
			.id = BD71837_BUCK4,
		.ops = &bd71837_buck1234_regulator_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = BD71837_BUCK4_VOLTAGE_NUM,
		.linear_ranges = bd71837_buck1234_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(bd71837_buck1234_voltage_ranges),
		.vsel_reg = BD71837_REG_BUCK4_VOLT_RUN,
		.vsel_mask = BUCK4_RUN_MASK,
		.enable_reg = BD71837_REG_BUCK4_CTRL,
		.enable_mask = BD71837_BUCK_EN,
		.owner = THIS_MODULE,
	},
	{
		.name = "buck5",
		.of_match = of_match_ptr("BUCK5"),
		.regulators_node = of_match_ptr("regulators"),
		.id = BD71837_BUCK5,
		.ops = &bd71837_buck_regulator_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = BD71837_BUCK5_VOLTAGE_NUM,
		.linear_ranges = bd71837_buck5_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(bd71837_buck5_voltage_ranges),
		.vsel_reg = BD71837_REG_BUCK5_VOLT,
		.vsel_mask = BUCK5_MASK,
		.enable_reg = BD71837_REG_BUCK5_CTRL,
		.enable_mask = BD71837_BUCK_EN,
		.owner = THIS_MODULE,
	},
	{
		.name = "buck6",
		.of_match = of_match_ptr("BUCK6"),
		.regulators_node = of_match_ptr("regulators"),
		.id = BD71837_BUCK6,
		.ops = &bd71837_buck_regulator_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = BD71837_BUCK6_VOLTAGE_NUM,
		.linear_ranges = bd71837_buck6_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(bd71837_buck6_voltage_ranges),
		.vsel_reg = BD71837_REG_BUCK6_VOLT,
		.vsel_mask = BUCK6_MASK,
		.enable_reg = BD71837_REG_BUCK6_CTRL,
		.enable_mask = BD71837_BUCK_EN,
		.owner = THIS_MODULE,
	},
	{
		.name = "buck7",
		.of_match = of_match_ptr("BUCK7"),
		.regulators_node = of_match_ptr("regulators"),
		.id = BD71837_BUCK7,
		.ops = &bd71837_buck_regulator_nolinear_ops,
		.type = REGULATOR_VOLTAGE,
		.volt_table = &buck_7_volts[0],
		.n_voltages = ARRAY_SIZE(buck_7_volts),
		.vsel_reg = BD71837_REG_BUCK7_VOLT,
		.vsel_mask = BUCK7_MASK,
		.enable_reg = BD71837_REG_BUCK7_CTRL,
		.enable_mask = BD71837_BUCK_EN,
		.owner = THIS_MODULE,
	},
	{
		.name = "buck8",
		.of_match = of_match_ptr("BUCK8"),
		.regulators_node = of_match_ptr("regulators"),
		.id = BD71837_BUCK8,
		.ops = &bd71837_buck_regulator_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = BD71837_BUCK8_VOLTAGE_NUM,
		.linear_ranges = bd71837_buck8_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(bd71837_buck8_voltage_ranges),
		.vsel_reg = BD71837_REG_BUCK8_VOLT,
		.vsel_mask = BUCK8_MASK,
		.enable_reg = BD71837_REG_BUCK8_CTRL,
		.enable_mask = BD71837_BUCK_EN,
		.owner = THIS_MODULE,
	},
	{
		.name = "ldo1",
		.of_match = of_match_ptr("LDO1"),
		.regulators_node = of_match_ptr("regulators"),
		.id = BD71837_LDO1,
		.ops = &bd71837_ldo_regulator_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = BD71837_LDO1_VOLTAGE_NUM,
		.linear_ranges = bd71837_ldo1_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(bd71837_ldo1_voltage_ranges),
		.vsel_reg = BD71837_REG_LDO1_VOLT,
		.vsel_mask = LDO1_MASK,
		.enable_reg = BD71837_REG_LDO1_VOLT,
		.enable_mask = BD71837_LDO_EN,
		.owner = THIS_MODULE,
	},
	{
		.name = "ldo2",
		.of_match = of_match_ptr("LDO2"),
		.regulators_node = of_match_ptr("regulators"),
		.id = BD71837_LDO2,
		.ops = &bd71837_ldo_regulator_nolinear_ops,
		.type = REGULATOR_VOLTAGE,
		.volt_table = &ldo_2_volts[0],
		.vsel_reg = BD71837_REG_LDO2_VOLT,
		.vsel_mask = LDO2_MASK,
		.n_voltages = ARRAY_SIZE(ldo_2_volts),
		.n_voltages = BD71837_LDO2_VOLTAGE_NUM,
		.enable_reg = BD71837_REG_LDO2_VOLT,
		.enable_mask = BD71837_LDO_EN,
		.owner = THIS_MODULE,
	},
	{
		.name = "ldo3",
		.of_match = of_match_ptr("LDO3"),
		.regulators_node = of_match_ptr("regulators"),
		.id = BD71837_LDO3,
		.ops = &bd71837_ldo_regulator_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = BD71837_LDO3_VOLTAGE_NUM,
		.linear_ranges = bd71837_ldo3_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(bd71837_ldo3_voltage_ranges),
		.vsel_reg = BD71837_REG_LDO3_VOLT,
		.vsel_mask = LDO3_MASK,
		.enable_reg = BD71837_REG_LDO3_VOLT,
		.enable_mask = BD71837_LDO_EN,
		.owner = THIS_MODULE,
	},
	{
		.name = "ldo4",
		.of_match = of_match_ptr("LDO4"),
		.regulators_node = of_match_ptr("regulators"),
		.id = BD71837_LDO4,
		.ops = &bd71837_ldo_regulator_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = BD71837_LDO4_VOLTAGE_NUM,
		.linear_ranges = bd71837_ldo4_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(bd71837_ldo4_voltage_ranges),
		.vsel_reg = BD71837_REG_LDO4_VOLT,
		.vsel_mask = LDO4_MASK,
		.enable_reg = BD71837_REG_LDO4_VOLT,
		.enable_mask = BD71837_LDO_EN,
		.owner = THIS_MODULE,
	},
	{
		.name = "ldo5",
		.of_match = of_match_ptr("LDO5"),
		.regulators_node = of_match_ptr("regulators"),
		.id = BD71837_LDO5,
		.ops = &bd71837_ldo_regulator_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = BD71837_LDO5_VOLTAGE_NUM,
		.linear_ranges = bd71837_ldo5_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(bd71837_ldo5_voltage_ranges),
		/* LDO5 is supplied by buck6 */
		.supply_name = "buck6",
		.vsel_reg = BD71837_REG_LDO5_VOLT,
		.vsel_mask = LDO5_MASK,
		.enable_reg = BD71837_REG_LDO5_VOLT,
		.enable_mask = BD71837_LDO_EN,
		.owner = THIS_MODULE,
	},
	{
		.name = "ldo6",
		.of_match = of_match_ptr("LDO6"),
		.regulators_node = of_match_ptr("regulators"),
		.id = BD71837_LDO6,
		.ops = &bd71837_ldo_regulator_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = BD71837_LDO6_VOLTAGE_NUM,
		.linear_ranges = bd71837_ldo6_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(bd71837_ldo6_voltage_ranges),
		/* LDO6 is supplied by buck7 */
		.supply_name = "buck7",
		.vsel_reg = BD71837_REG_LDO6_VOLT,
		.vsel_mask = LDO6_MASK,
		.enable_reg = BD71837_REG_LDO6_VOLT,
		.enable_mask = BD71837_LDO_EN,
		.owner = THIS_MODULE,
	},
	{
		.name = "ldo7",
		.of_match = of_match_ptr("LDO7"),
		.regulators_node = of_match_ptr("regulators"),
		.id = BD71837_LDO7,
		.ops = &bd71837_ldo_regulator_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = BD71837_LDO7_VOLTAGE_NUM,
		.linear_ranges = bd71837_ldo7_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(bd71837_ldo7_voltage_ranges),
		.vsel_reg = BD71837_REG_LDO7_VOLT,
		.vsel_mask = LDO7_MASK,
		.enable_reg = BD71837_REG_LDO7_VOLT,
		.enable_mask = BD71837_LDO_EN,
		.owner = THIS_MODULE,
	},
};

struct reg_init {
	unsigned int reg;
	unsigned int mask;
};

static int bd71837_probe(struct platform_device *pdev)
{
	struct bd71837_pmic *pmic;
	struct bd71837_board *pdata;
	struct regulator_config config = { 0 };
	struct reg_init pmic_regulator_inits[] = {
		{
			.reg = BD71837_REG_BUCK1_CTRL,
			.mask = BD71837_BUCK_SEL,
		}, {
			.reg = BD71837_REG_BUCK2_CTRL,
			.mask = BD71837_BUCK_SEL,
		}, {
			.reg = BD71837_REG_BUCK3_CTRL,
			.mask = BD71837_BUCK_SEL,
		}, {
			.reg = BD71837_REG_BUCK4_CTRL,
			.mask = BD71837_BUCK_SEL,
		}, {
			.reg = BD71837_REG_BUCK5_CTRL,
			.mask = BD71837_BUCK_SEL,
		}, {
			.reg = BD71837_REG_BUCK6_CTRL,
			.mask = BD71837_BUCK_SEL,
		}, {
			.reg = BD71837_REG_BUCK7_CTRL,
			.mask = BD71837_BUCK_SEL,
		}, {
			.reg = BD71837_REG_BUCK8_CTRL,
			.mask = BD71837_BUCK_SEL,
		}, {
			.reg = BD71837_REG_LDO1_VOLT,
			.mask = BD71837_LDO_SEL,
		}, {
			.reg = BD71837_REG_LDO2_VOLT,
			.mask = BD71837_LDO_SEL,
		}, {
			.reg = BD71837_REG_LDO3_VOLT,
			.mask = BD71837_LDO_SEL,
		}, {
			.reg = BD71837_REG_LDO4_VOLT,
			.mask = BD71837_LDO_SEL,
		}, {
			.reg = BD71837_REG_LDO5_VOLT,
			.mask = BD71837_LDO_SEL,
		}, {
			.reg = BD71837_REG_LDO6_VOLT,
			.mask = BD71837_LDO_SEL,
		}, {
			.reg = BD71837_REG_LDO7_VOLT,
			.mask = BD71837_LDO_SEL,
		}
	};

	int i, err;

	pmic = devm_kzalloc(&pdev->dev, sizeof(struct bd71837_pmic),
			    GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	memcpy(pmic->descs, bd71837_regulators, sizeof(pmic->descs));

	pmic->pdev = pdev;
	pmic->mfd = dev_get_drvdata(pdev->dev.parent);

	if (!pmic->mfd) {
		dev_err(&pdev->dev, "No MFD driver data\n");
		err = -EINVAL;
		goto err;
	}
	platform_set_drvdata(pdev, pmic);
	pdata = dev_get_platdata(pmic->mfd->dev);

	/* Register LOCK release */
	err = regmap_update_bits(pmic->mfd->regmap, BD71837_REG_REGLOCK,
				 (REGLOCK_PWRSEQ | REGLOCK_VREG), 0);
	if (err) {
		dev_err(&pmic->pdev->dev, "Failed to unlock PMIC (%d)\n", err);
		goto err;
	} else {
		dev_dbg(&pmic->pdev->dev, "%s: Unlocked lock register 0x%x\n",
			__func__, BD71837_REG_REGLOCK);
	}

	for (i = 0; i < ARRAY_SIZE(pmic_regulator_inits); i++) {

		struct regulator_desc *desc;
		struct regulator_dev *rdev;

		desc = &pmic->descs[i];

		if (pdata)
			config.init_data = pdata->init_data[i];

		config.dev = pdev->dev.parent;
		config.driver_data = pmic;
		config.regmap = pmic->mfd->regmap;

		rdev = devm_regulator_register(&pdev->dev, desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(pmic->mfd->dev,
				"failed to register %s regulator\n",
				desc->name);
			err = PTR_ERR(rdev);
			goto err;
		}
		/* Regulator register gets the regulator constraints and
		 * applies them (set_machine_constraints). This should have
		 * turned the control register(s) to correct values and we
		 * can now switch the control from PMIC state machine to the
		 * register interface
		 */
		err = regmap_update_bits(pmic->mfd->regmap,
					 pmic_regulator_inits[i].reg,
					 pmic_regulator_inits[i].mask,
					 0xFFFFFFFF);
		if (err) {
			dev_err(&pmic->pdev->dev,
				"Failed to write BUCK/LDO SEL bit for (%s)\n",
				desc->name);
			goto err;
		}

		pmic->rdev[i] = rdev;
	}

	return 0;

err:
	return err;
}

static struct platform_driver bd71837_regulator = {
	.driver = {
		.name = "bd71837-pmic",
		.owner = THIS_MODULE,
	},
	.probe = bd71837_probe,
};

module_platform_driver(bd71837_regulator);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("BD71837 voltage regulator driver");
MODULE_LICENSE("GPL");

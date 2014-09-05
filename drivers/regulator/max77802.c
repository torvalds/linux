/*
 * max77802.c - Regulator driver for the Maxim 77802
 *
 * Copyright (C) 2013-2014 Google, Inc
 * Simon Glass <sjg@chromium.org>
 *
 * Copyright (C) 2012 Samsung Electronics
 * Chiwoong Byun <woong.byun@smasung.com>
 * Jonghwa Lee <jonghwa3.lee@samsung.com>
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
 *
 * This driver is based on max8997.c
 */

#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/max77686.h>
#include <linux/mfd/max77686-private.h>

/* Default ramp delay in case it is not manually set */
#define MAX77802_RAMP_DELAY		100000		/* uV/us */

#define MAX77802_OPMODE_SHIFT_LDO	6
#define MAX77802_OPMODE_BUCK234_SHIFT	4
#define MAX77802_OPMODE_MASK		0x3

#define MAX77802_VSEL_MASK		0x3F
#define MAX77802_DVS_VSEL_MASK		0xFF

#define MAX77802_RAMP_RATE_MASK_2BIT	0xC0
#define MAX77802_RAMP_RATE_SHIFT_2BIT	6
#define MAX77802_RAMP_RATE_MASK_4BIT	0xF0
#define MAX77802_RAMP_RATE_SHIFT_4BIT	4

/* MAX77802 has two register formats: 2-bit and 4-bit */
static const unsigned int ramp_table_77802_2bit[] = {
	12500,
	25000,
	50000,
	100000,
};

static unsigned int ramp_table_77802_4bit[] = {
	1000,	2000,	3030,	4000,
	5000,	5880,	7140,	8330,
	9090,	10000,	11110,	12500,
	16670,	25000,	50000,	100000,
};

struct max77802_regulator_prv {
	unsigned int opmode[MAX77802_REG_MAX];
};

static int max77802_get_opmode_shift(int id)
{
	if (id == MAX77802_BUCK1 || (id >= MAX77802_BUCK5 &&
				     id <= MAX77802_BUCK10))
		return 0;

	if (id >= MAX77802_BUCK2 && id <= MAX77802_BUCK4)
		return MAX77802_OPMODE_BUCK234_SHIFT;

	if (id >= MAX77802_LDO1 && id <= MAX77802_LDO35)
		return MAX77802_OPMODE_SHIFT_LDO;

	return -EINVAL;
}

/*
 * Some BUCKS supports Normal[ON/OFF] mode during suspend
 *
 * BUCK 1, 6, 2-4, 5, 7-10 (all)
 *
 * The other mode (0x02) will make PWRREQ switch between normal
 * and low power.
 */
static int max77802_buck_set_suspend_disable(struct regulator_dev *rdev)
{
	unsigned int val = MAX77802_OPMODE_STANDBY;
	struct max77802_regulator_prv *max77802 = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	int shift = max77802_get_opmode_shift(id);

	max77802->opmode[id] = val;
	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, val << shift);
}

/*
 * Some LDOs supports LPM-ON/OFF/Normal-ON mode during suspend state
 * (Enable Control Logic1 by PWRREQ)
 *
 * LDOs 2, 4-19, 22-35.
 *
 */
static int max77802_ldo_set_suspend_mode_logic1(struct regulator_dev *rdev,
						unsigned int mode)
{
	struct max77802_regulator_prv *max77802 = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	unsigned int val;
	int shift = max77802_get_opmode_shift(id);

	switch (mode) {
	case REGULATOR_MODE_IDLE:			/* ON in LP Mode */
		val = MAX77802_OPMODE_LP;
		break;
	case REGULATOR_MODE_NORMAL:			/* ON in Normal Mode */
		val = MAX77802_OPMODE_NORMAL;
		break;
	case REGULATOR_MODE_STANDBY:			/* ON/OFF by PWRREQ */
		val = MAX77802_OPMODE_STANDBY;
		break;
	default:
		dev_warn(&rdev->dev, "%s: regulator mode: 0x%x not supported\n",
			 rdev->desc->name, mode);
		return -EINVAL;
	}

	max77802->opmode[id] = val;
	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, val << shift);
}

/*
 * Mode 1 (Output[ON/OFF] by PWRREQ) is not supported on some LDOs
 * (Enable Control Logic2 by PWRREQ)
 *
 * LDOs 1, 20, 21, and 3,
 *
 */
static int max77802_ldo_set_suspend_mode_logic2(struct regulator_dev *rdev,
						unsigned int mode)
{
	struct max77802_regulator_prv *max77802 = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	unsigned int val;
	int shift = max77802_get_opmode_shift(id);

	switch (mode) {
	case REGULATOR_MODE_IDLE:			/* ON in LP Mode */
		val = MAX77802_OPMODE_LP;
		break;
	case REGULATOR_MODE_NORMAL:			/* ON in Normal Mode */
		val = MAX77802_OPMODE_NORMAL;
		break;
	default:
		dev_warn(&rdev->dev, "%s: regulator mode: 0x%x not supported\n",
			 rdev->desc->name, mode);
		return -EINVAL;
	}

	max77802->opmode[id] = val;
	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, val << shift);
}

static int max77802_enable(struct regulator_dev *rdev)
{
	struct max77802_regulator_prv *max77802 = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	int shift = max77802_get_opmode_shift(id);

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask,
				  max77802->opmode[id] << shift);
}

static int max77802_find_ramp_value(struct regulator_dev *rdev,
				    const unsigned int limits[], int size,
				    unsigned int ramp_delay)
{
	int i;

	for (i = 0; i < size; i++) {
		if (ramp_delay <= limits[i])
			return i;
	}

	/* Use maximum value for no ramp control */
	dev_warn(&rdev->dev, "%s: ramp_delay: %d not supported, setting 100000\n",
		 rdev->desc->name, ramp_delay);
	return size - 1;
}

/* Used for BUCKs 2-4 */
static int max77802_set_ramp_delay_2bit(struct regulator_dev *rdev,
					int ramp_delay)
{
	int id = rdev_get_id(rdev);
	unsigned int ramp_value;

	if (id > MAX77802_BUCK4) {
			dev_warn(&rdev->dev,
				 "%s: regulator: ramp delay not supported\n",
				 rdev->desc->name);
		return -EINVAL;
	}
	ramp_value = max77802_find_ramp_value(rdev, ramp_table_77802_2bit,
				ARRAY_SIZE(ramp_table_77802_2bit), ramp_delay);

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  MAX77802_RAMP_RATE_MASK_2BIT,
				  ramp_value << MAX77802_RAMP_RATE_SHIFT_2BIT);
}

/* For BUCK1, 6 */
static int max77802_set_ramp_delay_4bit(struct regulator_dev *rdev,
					    int ramp_delay)
{
	unsigned int ramp_value;

	ramp_value = max77802_find_ramp_value(rdev, ramp_table_77802_4bit,
				ARRAY_SIZE(ramp_table_77802_4bit), ramp_delay);

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  MAX77802_RAMP_RATE_MASK_4BIT,
				  ramp_value << MAX77802_RAMP_RATE_SHIFT_4BIT);
}

/*
 * LDOs 2, 4-19, 22-35
 */
static struct regulator_ops max77802_ldo_ops_logic1 = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77802_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_mode	= max77802_ldo_set_suspend_mode_logic1,
};

/*
 * LDOs 1, 20, 21, 3
 */
static struct regulator_ops max77802_ldo_ops_logic2 = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77802_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_mode	= max77802_ldo_set_suspend_mode_logic2,
};

/* BUCKS 1, 6 */
static struct regulator_ops max77802_buck_16_dvs_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77802_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_ramp_delay		= max77802_set_ramp_delay_4bit,
	.set_suspend_disable	= max77802_buck_set_suspend_disable,
};

/* BUCKs 2-4, 5, 7-10 */
static struct regulator_ops max77802_buck_dvs_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= max77802_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_ramp_delay		= max77802_set_ramp_delay_2bit,
	.set_suspend_disable	= max77802_buck_set_suspend_disable,
};

/* LDOs 3-7, 9-14, 18-26, 28, 29, 32-34 */
#define regulator_77802_desc_p_ldo(num, supply, log)	{		\
	.name		= "LDO"#num,					\
	.id		= MAX77802_LDO##num,				\
	.supply_name	= "inl"#supply,					\
	.ops		= &max77802_ldo_ops_logic##log,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 800000,					\
	.uV_step	= 50000,					\
	.ramp_delay	= MAX77802_RAMP_DELAY,				\
	.n_voltages	= 1 << 6,					\
	.vsel_reg	= MAX77802_REG_LDO1CTRL1 + num - 1,		\
	.vsel_mask	= MAX77802_VSEL_MASK,				\
	.enable_reg	= MAX77802_REG_LDO1CTRL1 + num - 1,		\
	.enable_mask	= MAX77802_OPMODE_MASK << MAX77802_OPMODE_SHIFT_LDO, \
}

/* LDOs 1, 2, 8, 15, 17, 27, 30, 35 */
#define regulator_77802_desc_n_ldo(num, supply, log)   {		\
	.name		= "LDO"#num,					\
	.id		= MAX77802_LDO##num,				\
	.supply_name	= "inl"#supply,					\
	.ops		= &max77802_ldo_ops_logic##log,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 800000,					\
	.uV_step	= 25000,					\
	.ramp_delay	= MAX77802_RAMP_DELAY,				\
	.n_voltages	= 1 << 6,					\
	.vsel_reg	= MAX77802_REG_LDO1CTRL1 + num - 1,		\
	.vsel_mask	= MAX77802_VSEL_MASK,				\
	.enable_reg	= MAX77802_REG_LDO1CTRL1 + num - 1,		\
	.enable_mask	= MAX77802_OPMODE_MASK << MAX77802_OPMODE_SHIFT_LDO, \
}

/* BUCKs 1, 6 */
#define regulator_77802_desc_16_buck(num)	{		\
	.name		= "BUCK"#num,					\
	.id		= MAX77802_BUCK##num,				\
	.supply_name	= "inb"#num,					\
	.ops		= &max77802_buck_16_dvs_ops,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 612500,					\
	.uV_step	= 6250,						\
	.ramp_delay	= MAX77802_RAMP_DELAY,				\
	.n_voltages	= 1 << 8,					\
	.vsel_reg	= MAX77802_REG_BUCK ## num ## DVS1,		\
	.vsel_mask	= MAX77802_DVS_VSEL_MASK,			\
	.enable_reg	= MAX77802_REG_BUCK ## num ## CTRL,		\
	.enable_mask	= MAX77802_OPMODE_MASK,				\
}

/* BUCKS 2-4 */
#define regulator_77802_desc_234_buck(num)	{		\
	.name		= "BUCK"#num,					\
	.id		= MAX77802_BUCK##num,				\
	.supply_name	= "inb"#num,					\
	.ops		= &max77802_buck_dvs_ops,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 600000,					\
	.uV_step	= 6250,						\
	.ramp_delay	= MAX77802_RAMP_DELAY,				\
	.n_voltages	= 0x91,						\
	.vsel_reg	= MAX77802_REG_BUCK ## num ## DVS1,		\
	.vsel_mask	= MAX77802_DVS_VSEL_MASK,			\
	.enable_reg	= MAX77802_REG_BUCK ## num ## CTRL1,		\
	.enable_mask	= MAX77802_OPMODE_MASK <<			\
				MAX77802_OPMODE_BUCK234_SHIFT,		\
}

/* BUCK 5 */
#define regulator_77802_desc_buck5(num)		{		\
	.name		= "BUCK"#num,					\
	.id		= MAX77802_BUCK##num,				\
	.supply_name	= "inb"#num,					\
	.ops		= &max77802_buck_dvs_ops,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 750000,					\
	.uV_step	= 50000,					\
	.ramp_delay	= MAX77802_RAMP_DELAY,				\
	.n_voltages	= 1 << 6,					\
	.vsel_reg	= MAX77802_REG_BUCK5OUT,			\
	.vsel_mask	= MAX77802_VSEL_MASK,				\
	.enable_reg	= MAX77802_REG_BUCK5CTRL,			\
	.enable_mask	= MAX77802_OPMODE_MASK,				\
}

/* BUCKs 7-10 */
#define regulator_77802_desc_buck7_10(num)	{		\
	.name		= "BUCK"#num,					\
	.id		= MAX77802_BUCK##num,				\
	.supply_name	= "inb"#num,					\
	.ops		= &max77802_buck_dvs_ops,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= 750000,					\
	.uV_step	= 50000,					\
	.ramp_delay	= MAX77802_RAMP_DELAY,				\
	.n_voltages	= 1 << 6,					\
	.vsel_reg	= MAX77802_REG_BUCK7OUT + (num - 7) * 3,	\
	.vsel_mask	= MAX77802_VSEL_MASK,				\
	.enable_reg	= MAX77802_REG_BUCK7CTRL + (num - 7) * 3,	\
	.enable_mask	= MAX77802_OPMODE_MASK,				\
}

static struct regulator_desc regulators[] = {
	regulator_77802_desc_16_buck(1),
	regulator_77802_desc_234_buck(2),
	regulator_77802_desc_234_buck(3),
	regulator_77802_desc_234_buck(4),
	regulator_77802_desc_buck5(5),
	regulator_77802_desc_16_buck(6),
	regulator_77802_desc_buck7_10(7),
	regulator_77802_desc_buck7_10(8),
	regulator_77802_desc_buck7_10(9),
	regulator_77802_desc_buck7_10(10),
	regulator_77802_desc_n_ldo(1, 10, 2),
	regulator_77802_desc_n_ldo(2, 10, 1),
	regulator_77802_desc_p_ldo(3, 3, 2),
	regulator_77802_desc_p_ldo(4, 6, 1),
	regulator_77802_desc_p_ldo(5, 3, 1),
	regulator_77802_desc_p_ldo(6, 3, 1),
	regulator_77802_desc_p_ldo(7, 3, 1),
	regulator_77802_desc_n_ldo(8, 1, 1),
	regulator_77802_desc_p_ldo(9, 5, 1),
	regulator_77802_desc_p_ldo(10, 4, 1),
	regulator_77802_desc_p_ldo(11, 4, 1),
	regulator_77802_desc_p_ldo(12, 9, 1),
	regulator_77802_desc_p_ldo(13, 4, 1),
	regulator_77802_desc_p_ldo(14, 4, 1),
	regulator_77802_desc_n_ldo(15, 1, 1),
	regulator_77802_desc_n_ldo(17, 2, 1),
	regulator_77802_desc_p_ldo(18, 7, 1),
	regulator_77802_desc_p_ldo(19, 5, 1),
	regulator_77802_desc_p_ldo(20, 7, 2),
	regulator_77802_desc_p_ldo(21, 6, 2),
	regulator_77802_desc_p_ldo(23, 9, 1),
	regulator_77802_desc_p_ldo(24, 6, 1),
	regulator_77802_desc_p_ldo(25, 9, 1),
	regulator_77802_desc_p_ldo(26, 9, 1),
	regulator_77802_desc_n_ldo(27, 2, 1),
	regulator_77802_desc_p_ldo(28, 7, 1),
	regulator_77802_desc_p_ldo(29, 7, 1),
	regulator_77802_desc_n_ldo(30, 2, 1),
	regulator_77802_desc_p_ldo(32, 9, 1),
	regulator_77802_desc_p_ldo(33, 6, 1),
	regulator_77802_desc_p_ldo(34, 9, 1),
	regulator_77802_desc_n_ldo(35, 2, 1),
};

#ifdef CONFIG_OF
static int max77802_pmic_dt_parse_pdata(struct platform_device *pdev,
					struct max77686_platform_data *pdata)
{
	struct max77686_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct device_node *pmic_np, *regulators_np;
	struct max77686_regulator_data *rdata;
	struct of_regulator_match rmatch;
	unsigned int i;

	pmic_np = iodev->dev->of_node;
	regulators_np = of_get_child_by_name(pmic_np, "regulators");
	if (!regulators_np) {
		dev_err(&pdev->dev, "could not find regulators sub-node\n");
		return -EINVAL;
	}

	pdata->num_regulators = ARRAY_SIZE(regulators);
	rdata = devm_kzalloc(&pdev->dev, sizeof(*rdata) *
			     pdata->num_regulators, GFP_KERNEL);
	if (!rdata) {
		of_node_put(regulators_np);
		return -ENOMEM;
	}

	for (i = 0; i < pdata->num_regulators; i++) {
		rmatch.name = regulators[i].name;
		rmatch.init_data = NULL;
		rmatch.of_node = NULL;
		if (of_regulator_match(&pdev->dev, regulators_np, &rmatch,
				       1) != 1) {
			dev_warn(&pdev->dev, "No matching regulator for '%s'\n",
				 rmatch.name);
			continue;
		}
		rdata[i].initdata = rmatch.init_data;
		rdata[i].of_node = rmatch.of_node;
		rdata[i].id = regulators[i].id;
	}

	pdata->regulators = rdata;
	of_node_put(regulators_np);

	return 0;
}
#else
static int max77802_pmic_dt_parse_pdata(struct platform_device *pdev,
					struct max77686_platform_data *pdata)
{
	return 0;
}
#endif /* CONFIG_OF */

static int max77802_pmic_probe(struct platform_device *pdev)
{
	struct max77686_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max77686_platform_data *pdata = dev_get_platdata(iodev->dev);
	struct max77802_regulator_prv *max77802;
	int i, ret = 0, val;
	struct regulator_config config = { };

	/* This is allocated by the MFD driver */
	if (!pdata) {
		dev_err(&pdev->dev, "no platform data found for regulator\n");
		return -ENODEV;
	}

	max77802 = devm_kzalloc(&pdev->dev,
				sizeof(struct max77802_regulator_prv),
				GFP_KERNEL);
	if (!max77802)
		return -ENOMEM;

	if (iodev->dev->of_node) {
		ret = max77802_pmic_dt_parse_pdata(pdev, pdata);
		if (ret)
			return ret;
	}

	config.dev = iodev->dev;
	config.regmap = iodev->regmap;
	config.driver_data = max77802;
	platform_set_drvdata(pdev, max77802);

	for (i = 0; i < MAX77802_REG_MAX; i++) {
		struct regulator_dev *rdev;
		int id = pdata->regulators[i].id;
		int shift = max77802_get_opmode_shift(id);

		config.init_data = pdata->regulators[i].initdata;
		config.of_node = pdata->regulators[i].of_node;

		ret = regmap_read(iodev->regmap, regulators[i].enable_reg, &val);
		val = val >> shift & MAX77802_OPMODE_MASK;

		/*
		 * If the regulator is disabled and the system warm rebooted,
		 * the hardware reports OFF as the regulator operating mode.
		 * Default to operating mode NORMAL in that case.
		 */
		if (val == MAX77802_OPMODE_OFF)
			max77802->opmode[id] = MAX77802_OPMODE_NORMAL;
		else
			max77802->opmode[id] = val;

		rdev = devm_regulator_register(&pdev->dev,
					       &regulators[i], &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev,
				"regulator init failed for %d\n", i);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct platform_device_id max77802_pmic_id[] = {
	{"max77802-pmic", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, max77802_pmic_id);

static struct platform_driver max77802_pmic_driver = {
	.driver = {
		.name = "max77802-pmic",
		.owner = THIS_MODULE,
	},
	.probe = max77802_pmic_probe,
	.id_table = max77802_pmic_id,
};

module_platform_driver(max77802_pmic_driver);

MODULE_DESCRIPTION("MAXIM 77802 Regulator Driver");
MODULE_AUTHOR("Simon Glass <sjg@chromium.org>");
MODULE_LICENSE("GPL");

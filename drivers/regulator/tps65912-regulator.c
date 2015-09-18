/*
 * tps65912.c  --  TI tps65912
 *
 * Copyright 2011 Texas Instruments Inc.
 *
 * Author: Margarita Olaya Cabrera <magi@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 * This driver is based on wm8350 implementation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/mfd/tps65912.h>

/* DCDC's */
#define TPS65912_REG_DCDC1	0
#define TPS65912_REG_DCDC2	1
#define TPS65912_REG_DCDC3	2
#define TPS65912_REG_DCDC4	3

/* LDOs */
#define TPS65912_REG_LDO1	4
#define TPS65912_REG_LDO2	5
#define TPS65912_REG_LDO3	6
#define TPS65912_REG_LDO4	7
#define TPS65912_REG_LDO5	8
#define TPS65912_REG_LDO6	9
#define TPS65912_REG_LDO7	10
#define TPS65912_REG_LDO8	11
#define TPS65912_REG_LDO9	12
#define TPS65912_REG_LDO10	13

/* Number of step-down converters available */
#define TPS65912_NUM_DCDC	4

/* Number of LDO voltage regulators  available */
#define TPS65912_NUM_LDO	10

/* Number of total regulators available */
#define TPS65912_NUM_REGULATOR		(TPS65912_NUM_DCDC + TPS65912_NUM_LDO)

#define TPS65912_REG_ENABLED	0x80
#define OP_SELREG_MASK		0x40
#define OP_SELREG_SHIFT		6

struct tps_info {
	const char *name;
};

static struct tps_info tps65912_regs[] = {
	{
		.name = "DCDC1",
	},
	{
		.name = "DCDC2",
	},
	{
		.name = "DCDC3",
	},
	{
		.name = "DCDC4",
	},
	{
		.name = "LDO1",
	},
	{
		.name = "LDO2",
	},
	{
		.name = "LDO3",
	},
	{
		.name = "LDO4",
	},
	{
		.name = "LDO5",
	},
	{
		.name = "LDO6",
	},
	{
		.name = "LDO7",
	},
	{
		.name = "LDO8",
	},
	{
		.name = "LDO9",
	},
	{
		.name = "LDO10",
	},
};

struct tps65912_reg {
	struct regulator_desc desc[TPS65912_NUM_REGULATOR];
	struct tps65912 *mfd;
	struct regulator_dev *rdev[TPS65912_NUM_REGULATOR];
	struct tps_info *info[TPS65912_NUM_REGULATOR];
	/* for read/write access */
	struct mutex io_lock;
	int mode;
	int (*get_ctrl_reg)(int);
	int dcdc_range[TPS65912_NUM_DCDC];
	int pwm_mode_reg;
	int eco_reg;
};

static const struct regulator_linear_range tps65912_ldo_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0, 32, 25000),
	REGULATOR_LINEAR_RANGE(1650000, 33, 60, 50000),
	REGULATOR_LINEAR_RANGE(3100000, 61, 63, 100000),
};

static int tps65912_get_range(struct tps65912_reg *pmic, int id)
{
	struct tps65912 *mfd = pmic->mfd;
	int range;

	switch (id) {
	case TPS65912_REG_DCDC1:
		range = tps65912_reg_read(mfd, TPS65912_DCDC1_LIMIT);
		break;
	case TPS65912_REG_DCDC2:
		range = tps65912_reg_read(mfd, TPS65912_DCDC2_LIMIT);
		break;
	case TPS65912_REG_DCDC3:
		range = tps65912_reg_read(mfd, TPS65912_DCDC3_LIMIT);
		break;
	case TPS65912_REG_DCDC4:
		range = tps65912_reg_read(mfd, TPS65912_DCDC4_LIMIT);
		break;
	default:
		return 0;
	}

	if (range >= 0)
		range = (range & DCDC_LIMIT_RANGE_MASK)
			>> DCDC_LIMIT_RANGE_SHIFT;

	pmic->dcdc_range[id] = range;
	return range;
}

static unsigned long tps65912_vsel_to_uv_range0(u8 vsel)
{
	unsigned long uv;

	uv = ((vsel * 12500) + 500000);
	return uv;
}

static unsigned long tps65912_vsel_to_uv_range1(u8 vsel)
{
	unsigned long uv;

	 uv = ((vsel * 12500) + 700000);
	return uv;
}

static unsigned long tps65912_vsel_to_uv_range2(u8 vsel)
{
	unsigned long uv;

	uv = ((vsel * 25000) + 500000);
	return uv;
}

static unsigned long tps65912_vsel_to_uv_range3(u8 vsel)
{
	unsigned long uv;

	if (vsel == 0x3f)
		uv = 3800000;
	else
		uv = ((vsel * 50000) + 500000);

	return uv;
}

static int tps65912_get_ctrl_register(int id)
{
	if (id >= TPS65912_REG_DCDC1 && id <= TPS65912_REG_LDO4)
		return id * 3 + TPS65912_DCDC1_AVS;
	else if (id >= TPS65912_REG_LDO5 && id <= TPS65912_REG_LDO10)
		return id - TPS65912_REG_LDO5 + TPS65912_LDO5;
	else
		return -EINVAL;
}

static int tps65912_get_sel_register(struct tps65912_reg *pmic, int id)
{
	struct tps65912 *mfd = pmic->mfd;
	int opvsel;
	u8 reg = 0;

	if (id >= TPS65912_REG_DCDC1 && id <= TPS65912_REG_LDO4) {
		opvsel = tps65912_reg_read(mfd, id * 3 + TPS65912_DCDC1_OP);
		if (opvsel & OP_SELREG_MASK)
			reg = id * 3 + TPS65912_DCDC1_AVS;
		else
			reg = id * 3 + TPS65912_DCDC1_OP;
	} else if (id >= TPS65912_REG_LDO5 && id <= TPS65912_REG_LDO10) {
		reg = id - TPS65912_REG_LDO5 + TPS65912_LDO5;
	} else {
		return -EINVAL;
	}

	return reg;
}

static int tps65912_get_mode_regiters(struct tps65912_reg *pmic, int id)
{
	switch (id) {
	case TPS65912_REG_DCDC1:
		pmic->pwm_mode_reg = TPS65912_DCDC1_CTRL;
		pmic->eco_reg = TPS65912_DCDC1_AVS;
		break;
	case TPS65912_REG_DCDC2:
		pmic->pwm_mode_reg = TPS65912_DCDC2_CTRL;
		pmic->eco_reg = TPS65912_DCDC2_AVS;
		break;
	case TPS65912_REG_DCDC3:
		pmic->pwm_mode_reg = TPS65912_DCDC3_CTRL;
		pmic->eco_reg = TPS65912_DCDC3_AVS;
		break;
	case TPS65912_REG_DCDC4:
		pmic->pwm_mode_reg = TPS65912_DCDC4_CTRL;
		pmic->eco_reg = TPS65912_DCDC4_AVS;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tps65912_reg_is_enabled(struct regulator_dev *dev)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	struct tps65912 *mfd = pmic->mfd;
	int reg, value, id = rdev_get_id(dev);

	if (id < TPS65912_REG_DCDC1 || id > TPS65912_REG_LDO10)
		return -EINVAL;

	reg = pmic->get_ctrl_reg(id);
	if (reg < 0)
		return reg;

	value = tps65912_reg_read(mfd, reg);
	if (value < 0)
		return value;

	return value & TPS65912_REG_ENABLED;
}

static int tps65912_reg_enable(struct regulator_dev *dev)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	struct tps65912 *mfd = pmic->mfd;
	int id = rdev_get_id(dev);
	int reg;

	if (id < TPS65912_REG_DCDC1 || id > TPS65912_REG_LDO10)
		return -EINVAL;

	reg = pmic->get_ctrl_reg(id);
	if (reg < 0)
		return reg;

	return tps65912_set_bits(mfd, reg, TPS65912_REG_ENABLED);
}

static int tps65912_reg_disable(struct regulator_dev *dev)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	struct tps65912 *mfd = pmic->mfd;
	int id = rdev_get_id(dev), reg;

	reg = pmic->get_ctrl_reg(id);
	if (reg < 0)
		return reg;

	return tps65912_clear_bits(mfd, reg, TPS65912_REG_ENABLED);
}

static int tps65912_set_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	struct tps65912 *mfd = pmic->mfd;
	int pwm_mode, eco, id = rdev_get_id(dev);

	tps65912_get_mode_regiters(pmic, id);

	pwm_mode = tps65912_reg_read(mfd, pmic->pwm_mode_reg);
	eco = tps65912_reg_read(mfd, pmic->eco_reg);

	pwm_mode &= DCDCCTRL_DCDC_MODE_MASK;
	eco &= DCDC_AVS_ECO_MASK;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		/* Verify if mode alredy set */
		if (pwm_mode && !eco)
			break;
		tps65912_set_bits(mfd, pmic->pwm_mode_reg, DCDCCTRL_DCDC_MODE_MASK);
		tps65912_clear_bits(mfd, pmic->eco_reg, DCDC_AVS_ECO_MASK);
		break;
	case REGULATOR_MODE_NORMAL:
	case REGULATOR_MODE_IDLE:
		if (!pwm_mode && !eco)
			break;
		tps65912_clear_bits(mfd, pmic->pwm_mode_reg, DCDCCTRL_DCDC_MODE_MASK);
		tps65912_clear_bits(mfd, pmic->eco_reg, DCDC_AVS_ECO_MASK);
		break;
	case REGULATOR_MODE_STANDBY:
		if (!pwm_mode && eco)
			break;
		tps65912_clear_bits(mfd, pmic->pwm_mode_reg, DCDCCTRL_DCDC_MODE_MASK);
		tps65912_set_bits(mfd, pmic->eco_reg, DCDC_AVS_ECO_MASK);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static unsigned int tps65912_get_mode(struct regulator_dev *dev)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	struct tps65912 *mfd = pmic->mfd;
	int pwm_mode, eco, mode = 0, id = rdev_get_id(dev);

	tps65912_get_mode_regiters(pmic, id);

	pwm_mode = tps65912_reg_read(mfd, pmic->pwm_mode_reg);
	eco = tps65912_reg_read(mfd, pmic->eco_reg);

	pwm_mode &= DCDCCTRL_DCDC_MODE_MASK;
	eco &= DCDC_AVS_ECO_MASK;

	if (pwm_mode && !eco)
		mode = REGULATOR_MODE_FAST;
	else if (!pwm_mode && !eco)
		mode = REGULATOR_MODE_NORMAL;
	else if (!pwm_mode && eco)
		mode = REGULATOR_MODE_STANDBY;

	return mode;
}

static int tps65912_list_voltage(struct regulator_dev *dev, unsigned selector)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	int range, voltage = 0, id = rdev_get_id(dev);

	if (id > TPS65912_REG_DCDC4)
		return -EINVAL;

	range = pmic->dcdc_range[id];

	switch (range) {
	case 0:
		/* 0.5 - 1.2875V in 12.5mV steps */
		voltage = tps65912_vsel_to_uv_range0(selector);
		break;
	case 1:
		/* 0.7 - 1.4875V in 12.5mV steps */
		voltage = tps65912_vsel_to_uv_range1(selector);
		break;
	case 2:
		/* 0.5 - 2.075V in 25mV steps */
		voltage = tps65912_vsel_to_uv_range2(selector);
		break;
	case 3:
		/* 0.5 - 3.8V in 50mV steps */
		voltage = tps65912_vsel_to_uv_range3(selector);
		break;
	}
	return voltage;
}

static int tps65912_get_voltage_sel(struct regulator_dev *dev)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	struct tps65912 *mfd = pmic->mfd;
	int id = rdev_get_id(dev);
	int reg, vsel;

	reg = tps65912_get_sel_register(pmic, id);
	if (reg < 0)
		return reg;

	vsel = tps65912_reg_read(mfd, reg);
	vsel &= 0x3F;

	return vsel;
}

static int tps65912_set_voltage_sel(struct regulator_dev *dev,
					 unsigned selector)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	struct tps65912 *mfd = pmic->mfd;
	int id = rdev_get_id(dev);
	int value;
	u8 reg;

	reg = tps65912_get_sel_register(pmic, id);
	value = tps65912_reg_read(mfd, reg);
	value &= 0xC0;
	return tps65912_reg_write(mfd, reg, selector | value);
}

/* Operations permitted on DCDCx */
static struct regulator_ops tps65912_ops_dcdc = {
	.is_enabled = tps65912_reg_is_enabled,
	.enable = tps65912_reg_enable,
	.disable = tps65912_reg_disable,
	.set_mode = tps65912_set_mode,
	.get_mode = tps65912_get_mode,
	.get_voltage_sel = tps65912_get_voltage_sel,
	.set_voltage_sel = tps65912_set_voltage_sel,
	.list_voltage = tps65912_list_voltage,
};

/* Operations permitted on LDOx */
static struct regulator_ops tps65912_ops_ldo = {
	.is_enabled = tps65912_reg_is_enabled,
	.enable = tps65912_reg_enable,
	.disable = tps65912_reg_disable,
	.get_voltage_sel = tps65912_get_voltage_sel,
	.set_voltage_sel = tps65912_set_voltage_sel,
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
};

static int tps65912_probe(struct platform_device *pdev)
{
	struct tps65912 *tps65912 = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = { };
	struct tps_info *info;
	struct regulator_init_data *reg_data;
	struct regulator_dev *rdev;
	struct tps65912_reg *pmic;
	struct tps65912_board *pmic_plat_data;
	int i;

	pmic_plat_data = dev_get_platdata(tps65912->dev);
	if (!pmic_plat_data)
		return -EINVAL;

	reg_data = pmic_plat_data->tps65912_pmic_init_data;

	pmic = devm_kzalloc(&pdev->dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	mutex_init(&pmic->io_lock);
	pmic->mfd = tps65912;
	platform_set_drvdata(pdev, pmic);

	pmic->get_ctrl_reg = &tps65912_get_ctrl_register;
	info = tps65912_regs;

	for (i = 0; i < TPS65912_NUM_REGULATOR; i++, info++, reg_data++) {
		int range = 0;
		/* Register the regulators */
		pmic->info[i] = info;

		pmic->desc[i].name = info->name;
		pmic->desc[i].id = i;
		pmic->desc[i].n_voltages = 64;
		if (i > TPS65912_REG_DCDC4) {
			pmic->desc[i].ops = &tps65912_ops_ldo;
			pmic->desc[i].linear_ranges = tps65912_ldo_ranges;
			pmic->desc[i].n_linear_ranges =
					ARRAY_SIZE(tps65912_ldo_ranges);
		} else {
			pmic->desc[i].ops = &tps65912_ops_dcdc;
		}
		pmic->desc[i].type = REGULATOR_VOLTAGE;
		pmic->desc[i].owner = THIS_MODULE;
		range = tps65912_get_range(pmic, i);

		config.dev = tps65912->dev;
		config.init_data = reg_data;
		config.driver_data = pmic;

		rdev = devm_regulator_register(&pdev->dev, &pmic->desc[i],
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(tps65912->dev,
				"failed to register %s regulator\n",
				pdev->name);
			return PTR_ERR(rdev);
		}

		/* Save regulator for cleanup */
		pmic->rdev[i] = rdev;
	}
	return 0;
}

static struct platform_driver tps65912_driver = {
	.driver = {
		.name = "tps65912-pmic",
	},
	.probe = tps65912_probe,
};

static int __init tps65912_init(void)
{
	return platform_driver_register(&tps65912_driver);
}
subsys_initcall(tps65912_init);

static void __exit tps65912_cleanup(void)
{
	platform_driver_unregister(&tps65912_driver);
}
module_exit(tps65912_cleanup);

MODULE_AUTHOR("Margarita Olaya Cabrera <magi@slimlogic.co.uk>");
MODULE_DESCRIPTION("TPS65912 voltage regulator driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:tps65912-pmic");

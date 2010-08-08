/*
 * tps6507x-regulator.c
 *
 * Regulator driver for TPS65073 PMIC
 *
 * Copyright (C) 2009 Texas Instrument Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/tps6507x.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mfd/tps6507x.h>

/* DCDC's */
#define TPS6507X_DCDC_1				0
#define TPS6507X_DCDC_2				1
#define TPS6507X_DCDC_3				2
/* LDOs */
#define TPS6507X_LDO_1				3
#define TPS6507X_LDO_2				4

#define TPS6507X_MAX_REG_ID			TPS6507X_LDO_2

/* Number of step-down converters available */
#define TPS6507X_NUM_DCDC			3
/* Number of LDO voltage regulators  available */
#define TPS6507X_NUM_LDO			2
/* Number of total regulators available */
#define TPS6507X_NUM_REGULATOR		(TPS6507X_NUM_DCDC + TPS6507X_NUM_LDO)

/* Supported voltage values for regulators (in milliVolts) */
static const u16 VDCDCx_VSEL_table[] = {
	725, 750, 775, 800,
	825, 850, 875, 900,
	925, 950, 975, 1000,
	1025, 1050, 1075, 1100,
	1125, 1150, 1175, 1200,
	1225, 1250, 1275, 1300,
	1325, 1350, 1375, 1400,
	1425, 1450, 1475, 1500,
	1550, 1600, 1650, 1700,
	1750, 1800, 1850, 1900,
	1950, 2000, 2050, 2100,
	2150, 2200, 2250, 2300,
	2350, 2400, 2450, 2500,
	2550, 2600, 2650, 2700,
	2750, 2800, 2850, 2900,
	3000, 3100, 3200, 3300,
};

static const u16 LDO1_VSEL_table[] = {
	1000, 1100, 1200, 1250,
	1300, 1350, 1400, 1500,
	1600, 1800, 2500, 2750,
	2800, 3000, 3100, 3300,
};

static const u16 LDO2_VSEL_table[] = {
	725, 750, 775, 800,
	825, 850, 875, 900,
	925, 950, 975, 1000,
	1025, 1050, 1075, 1100,
	1125, 1150, 1175, 1200,
	1225, 1250, 1275, 1300,
	1325, 1350, 1375, 1400,
	1425, 1450, 1475, 1500,
	1550, 1600, 1650, 1700,
	1750, 1800, 1850, 1900,
	1950, 2000, 2050, 2100,
	2150, 2200, 2250, 2300,
	2350, 2400, 2450, 2500,
	2550, 2600, 2650, 2700,
	2750, 2800, 2850, 2900,
	3000, 3100, 3200, 3300,
};

static unsigned int num_voltages[] = {ARRAY_SIZE(VDCDCx_VSEL_table),
				ARRAY_SIZE(VDCDCx_VSEL_table),
				ARRAY_SIZE(VDCDCx_VSEL_table),
				ARRAY_SIZE(LDO1_VSEL_table),
				ARRAY_SIZE(LDO2_VSEL_table)};

struct tps_info {
	const char *name;
	unsigned min_uV;
	unsigned max_uV;
	u8 table_len;
	const u16 *table;

	/* Does DCDC high or the low register defines output voltage? */
	bool defdcdc_default;
};

static struct tps_info tps6507x_pmic_regs[] = {
	{
		.name = "VDCDC1",
		.min_uV = 725000,
		.max_uV = 3300000,
		.table_len = ARRAY_SIZE(VDCDCx_VSEL_table),
		.table = VDCDCx_VSEL_table,
	},
	{
		.name = "VDCDC2",
		.min_uV = 725000,
		.max_uV = 3300000,
		.table_len = ARRAY_SIZE(VDCDCx_VSEL_table),
		.table = VDCDCx_VSEL_table,
	},
	{
		.name = "VDCDC3",
		.min_uV = 725000,
		.max_uV = 3300000,
		.table_len = ARRAY_SIZE(VDCDCx_VSEL_table),
		.table = VDCDCx_VSEL_table,
	},
	{
		.name = "LDO1",
		.min_uV = 1000000,
		.max_uV = 3300000,
		.table_len = ARRAY_SIZE(LDO1_VSEL_table),
		.table = LDO1_VSEL_table,
	},
	{
		.name = "LDO2",
		.min_uV = 725000,
		.max_uV = 3300000,
		.table_len = ARRAY_SIZE(LDO2_VSEL_table),
		.table = LDO2_VSEL_table,
	},
};

struct tps6507x_pmic {
	struct regulator_desc desc[TPS6507X_NUM_REGULATOR];
	struct tps6507x_dev *mfd;
	struct regulator_dev *rdev[TPS6507X_NUM_REGULATOR];
	struct tps_info *info[TPS6507X_NUM_REGULATOR];
	struct mutex io_lock;
};
static inline int tps6507x_pmic_read(struct tps6507x_pmic *tps, u8 reg)
{
	u8 val;
	int err;

	err = tps->mfd->read_dev(tps->mfd, reg, 1, &val);

	if (err)
		return err;

	return val;
}

static inline int tps6507x_pmic_write(struct tps6507x_pmic *tps, u8 reg, u8 val)
{
	return tps->mfd->write_dev(tps->mfd, reg, 1, &val);
}

static int tps6507x_pmic_set_bits(struct tps6507x_pmic *tps, u8 reg, u8 mask)
{
	int err, data;

	mutex_lock(&tps->io_lock);

	data = tps6507x_pmic_read(tps, reg);
	if (data < 0) {
		dev_err(tps->mfd->dev, "Read from reg 0x%x failed\n", reg);
		err = data;
		goto out;
	}

	data |= mask;
	err = tps6507x_pmic_write(tps, reg, data);
	if (err)
		dev_err(tps->mfd->dev, "Write for reg 0x%x failed\n", reg);

out:
	mutex_unlock(&tps->io_lock);
	return err;
}

static int tps6507x_pmic_clear_bits(struct tps6507x_pmic *tps, u8 reg, u8 mask)
{
	int err, data;

	mutex_lock(&tps->io_lock);

	data = tps6507x_pmic_read(tps, reg);
	if (data < 0) {
		dev_err(tps->mfd->dev, "Read from reg 0x%x failed\n", reg);
		err = data;
		goto out;
	}

	data &= ~mask;
	err = tps6507x_pmic_write(tps, reg, data);
	if (err)
		dev_err(tps->mfd->dev, "Write for reg 0x%x failed\n", reg);

out:
	mutex_unlock(&tps->io_lock);
	return err;
}

static int tps6507x_pmic_reg_read(struct tps6507x_pmic *tps, u8 reg)
{
	int data;

	mutex_lock(&tps->io_lock);

	data = tps6507x_pmic_read(tps, reg);
	if (data < 0)
		dev_err(tps->mfd->dev, "Read from reg 0x%x failed\n", reg);

	mutex_unlock(&tps->io_lock);
	return data;
}

static int tps6507x_pmic_reg_write(struct tps6507x_pmic *tps, u8 reg, u8 val)
{
	int err;

	mutex_lock(&tps->io_lock);

	err = tps6507x_pmic_write(tps, reg, val);
	if (err < 0)
		dev_err(tps->mfd->dev, "Write for reg 0x%x failed\n", reg);

	mutex_unlock(&tps->io_lock);
	return err;
}

static int tps6507x_pmic_dcdc_is_enabled(struct regulator_dev *dev)
{
	struct tps6507x_pmic *tps = rdev_get_drvdata(dev);
	int data, dcdc = rdev_get_id(dev);
	u8 shift;

	if (dcdc < TPS6507X_DCDC_1 || dcdc > TPS6507X_DCDC_3)
		return -EINVAL;

	shift = TPS6507X_MAX_REG_ID - dcdc;
	data = tps6507x_pmic_reg_read(tps, TPS6507X_REG_CON_CTRL1);

	if (data < 0)
		return data;
	else
		return (data & 1<<shift) ? 1 : 0;
}

static int tps6507x_pmic_ldo_is_enabled(struct regulator_dev *dev)
{
	struct tps6507x_pmic *tps = rdev_get_drvdata(dev);
	int data, ldo = rdev_get_id(dev);
	u8 shift;

	if (ldo < TPS6507X_LDO_1 || ldo > TPS6507X_LDO_2)
		return -EINVAL;

	shift = TPS6507X_MAX_REG_ID - ldo;
	data = tps6507x_pmic_reg_read(tps, TPS6507X_REG_CON_CTRL1);

	if (data < 0)
		return data;
	else
		return (data & 1<<shift) ? 1 : 0;
}

static int tps6507x_pmic_dcdc_enable(struct regulator_dev *dev)
{
	struct tps6507x_pmic *tps = rdev_get_drvdata(dev);
	int dcdc = rdev_get_id(dev);
	u8 shift;

	if (dcdc < TPS6507X_DCDC_1 || dcdc > TPS6507X_DCDC_3)
		return -EINVAL;

	shift = TPS6507X_MAX_REG_ID - dcdc;
	return tps6507x_pmic_set_bits(tps, TPS6507X_REG_CON_CTRL1, 1 << shift);
}

static int tps6507x_pmic_dcdc_disable(struct regulator_dev *dev)
{
	struct tps6507x_pmic *tps = rdev_get_drvdata(dev);
	int dcdc = rdev_get_id(dev);
	u8 shift;

	if (dcdc < TPS6507X_DCDC_1 || dcdc > TPS6507X_DCDC_3)
		return -EINVAL;

	shift = TPS6507X_MAX_REG_ID - dcdc;
	return tps6507x_pmic_clear_bits(tps, TPS6507X_REG_CON_CTRL1,
					1 << shift);
}

static int tps6507x_pmic_ldo_enable(struct regulator_dev *dev)
{
	struct tps6507x_pmic *tps = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev);
	u8 shift;

	if (ldo < TPS6507X_LDO_1 || ldo > TPS6507X_LDO_2)
		return -EINVAL;

	shift = TPS6507X_MAX_REG_ID - ldo;
	return tps6507x_pmic_set_bits(tps, TPS6507X_REG_CON_CTRL1, 1 << shift);
}

static int tps6507x_pmic_ldo_disable(struct regulator_dev *dev)
{
	struct tps6507x_pmic *tps = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev);
	u8 shift;

	if (ldo < TPS6507X_LDO_1 || ldo > TPS6507X_LDO_2)
		return -EINVAL;

	shift = TPS6507X_MAX_REG_ID - ldo;
	return tps6507x_pmic_clear_bits(tps, TPS6507X_REG_CON_CTRL1,
					1 << shift);
}

static int tps6507x_pmic_dcdc_get_voltage(struct regulator_dev *dev)
{
	struct tps6507x_pmic *tps = rdev_get_drvdata(dev);
	int data, dcdc = rdev_get_id(dev);
	u8 reg;

	switch (dcdc) {
	case TPS6507X_DCDC_1:
		reg = TPS6507X_REG_DEFDCDC1;
		break;
	case TPS6507X_DCDC_2:
		if (tps->info[dcdc]->defdcdc_default)
			reg = TPS6507X_REG_DEFDCDC2_HIGH;
		else
			reg = TPS6507X_REG_DEFDCDC2_LOW;
		break;
	case TPS6507X_DCDC_3:
		if (tps->info[dcdc]->defdcdc_default)
			reg = TPS6507X_REG_DEFDCDC3_HIGH;
		else
			reg = TPS6507X_REG_DEFDCDC3_LOW;
		break;
	default:
		return -EINVAL;
	}

	data = tps6507x_pmic_reg_read(tps, reg);
	if (data < 0)
		return data;

	data &= TPS6507X_DEFDCDCX_DCDC_MASK;
	return tps->info[dcdc]->table[data] * 1000;
}

static int tps6507x_pmic_dcdc_set_voltage(struct regulator_dev *dev,
				int min_uV, int max_uV)
{
	struct tps6507x_pmic *tps = rdev_get_drvdata(dev);
	int data, vsel, dcdc = rdev_get_id(dev);
	u8 reg;

	switch (dcdc) {
	case TPS6507X_DCDC_1:
		reg = TPS6507X_REG_DEFDCDC1;
		break;
	case TPS6507X_DCDC_2:
		if (tps->info[dcdc]->defdcdc_default)
			reg = TPS6507X_REG_DEFDCDC2_HIGH;
		else
			reg = TPS6507X_REG_DEFDCDC2_LOW;
		break;
	case TPS6507X_DCDC_3:
		if (tps->info[dcdc]->defdcdc_default)
			reg = TPS6507X_REG_DEFDCDC3_HIGH;
		else
			reg = TPS6507X_REG_DEFDCDC3_LOW;
		break;
	default:
		return -EINVAL;
	}

	if (min_uV < tps->info[dcdc]->min_uV
		|| min_uV > tps->info[dcdc]->max_uV)
		return -EINVAL;
	if (max_uV < tps->info[dcdc]->min_uV
		|| max_uV > tps->info[dcdc]->max_uV)
		return -EINVAL;

	for (vsel = 0; vsel < tps->info[dcdc]->table_len; vsel++) {
		int mV = tps->info[dcdc]->table[vsel];
		int uV = mV * 1000;

		/* Break at the first in-range value */
		if (min_uV <= uV && uV <= max_uV)
			break;
	}

	/* write to the register in case we found a match */
	if (vsel == tps->info[dcdc]->table_len)
		return -EINVAL;

	data = tps6507x_pmic_reg_read(tps, reg);
	if (data < 0)
		return data;

	data &= ~TPS6507X_DEFDCDCX_DCDC_MASK;
	data |= vsel;

	return tps6507x_pmic_reg_write(tps, reg, data);
}

static int tps6507x_pmic_ldo_get_voltage(struct regulator_dev *dev)
{
	struct tps6507x_pmic *tps = rdev_get_drvdata(dev);
	int data, ldo = rdev_get_id(dev);
	u8 reg, mask;

	if (ldo < TPS6507X_LDO_1 || ldo > TPS6507X_LDO_2)
		return -EINVAL;
	else {
		reg = (ldo == TPS6507X_LDO_1 ?
			TPS6507X_REG_LDO_CTRL1 : TPS6507X_REG_DEFLDO2);
		mask = (ldo == TPS6507X_LDO_1 ?
			TPS6507X_REG_LDO_CTRL1_LDO1_MASK :
				TPS6507X_REG_DEFLDO2_LDO2_MASK);
	}

	data = tps6507x_pmic_reg_read(tps, reg);
	if (data < 0)
		return data;

	data &= mask;
	return tps->info[ldo]->table[data] * 1000;
}

static int tps6507x_pmic_ldo_set_voltage(struct regulator_dev *dev,
				int min_uV, int max_uV)
{
	struct tps6507x_pmic *tps = rdev_get_drvdata(dev);
	int data, vsel, ldo = rdev_get_id(dev);
	u8 reg, mask;

	if (ldo < TPS6507X_LDO_1 || ldo > TPS6507X_LDO_2)
		return -EINVAL;
	else {
		reg = (ldo == TPS6507X_LDO_1 ?
			TPS6507X_REG_LDO_CTRL1 : TPS6507X_REG_DEFLDO2);
		mask = (ldo == TPS6507X_LDO_1 ?
			TPS6507X_REG_LDO_CTRL1_LDO1_MASK :
				TPS6507X_REG_DEFLDO2_LDO2_MASK);
	}

	if (min_uV < tps->info[ldo]->min_uV || min_uV > tps->info[ldo]->max_uV)
		return -EINVAL;
	if (max_uV < tps->info[ldo]->min_uV || max_uV > tps->info[ldo]->max_uV)
		return -EINVAL;

	for (vsel = 0; vsel < tps->info[ldo]->table_len; vsel++) {
		int mV = tps->info[ldo]->table[vsel];
		int uV = mV * 1000;

		/* Break at the first in-range value */
		if (min_uV <= uV && uV <= max_uV)
			break;
	}

	if (vsel == tps->info[ldo]->table_len)
		return -EINVAL;

	data = tps6507x_pmic_reg_read(tps, reg);
	if (data < 0)
		return data;

	data &= ~mask;
	data |= vsel;

	return tps6507x_pmic_reg_write(tps, reg, data);
}

static int tps6507x_pmic_dcdc_list_voltage(struct regulator_dev *dev,
					unsigned selector)
{
	struct tps6507x_pmic *tps = rdev_get_drvdata(dev);
	int dcdc = rdev_get_id(dev);

	if (dcdc < TPS6507X_DCDC_1 || dcdc > TPS6507X_DCDC_3)
		return -EINVAL;

	if (selector >= tps->info[dcdc]->table_len)
		return -EINVAL;
	else
		return tps->info[dcdc]->table[selector] * 1000;
}

static int tps6507x_pmic_ldo_list_voltage(struct regulator_dev *dev,
					unsigned selector)
{
	struct tps6507x_pmic *tps = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev);

	if (ldo < TPS6507X_LDO_1 || ldo > TPS6507X_LDO_2)
		return -EINVAL;

	if (selector >= tps->info[ldo]->table_len)
		return -EINVAL;
	else
		return tps->info[ldo]->table[selector] * 1000;
}

/* Operations permitted on VDCDCx */
static struct regulator_ops tps6507x_pmic_dcdc_ops = {
	.is_enabled = tps6507x_pmic_dcdc_is_enabled,
	.enable = tps6507x_pmic_dcdc_enable,
	.disable = tps6507x_pmic_dcdc_disable,
	.get_voltage = tps6507x_pmic_dcdc_get_voltage,
	.set_voltage = tps6507x_pmic_dcdc_set_voltage,
	.list_voltage = tps6507x_pmic_dcdc_list_voltage,
};

/* Operations permitted on LDOx */
static struct regulator_ops tps6507x_pmic_ldo_ops = {
	.is_enabled = tps6507x_pmic_ldo_is_enabled,
	.enable = tps6507x_pmic_ldo_enable,
	.disable = tps6507x_pmic_ldo_disable,
	.get_voltage = tps6507x_pmic_ldo_get_voltage,
	.set_voltage = tps6507x_pmic_ldo_set_voltage,
	.list_voltage = tps6507x_pmic_ldo_list_voltage,
};

static __devinit
int tps6507x_pmic_probe(struct platform_device *pdev)
{
	struct tps6507x_dev *tps6507x_dev = dev_get_drvdata(pdev->dev.parent);
	static int desc_id;
	struct tps_info *info = &tps6507x_pmic_regs[0];
	struct regulator_init_data *init_data;
	struct regulator_dev *rdev;
	struct tps6507x_pmic *tps;
	struct tps6507x_board *tps_board;
	int i;
	int error;

	/**
	 * tps_board points to pmic related constants
	 * coming from the board-evm file.
	 */

	tps_board = dev_get_platdata(tps6507x_dev->dev);
	if (!tps_board)
		return -EINVAL;

	/**
	 * init_data points to array of regulator_init structures
	 * coming from the board-evm file.
	 */
	init_data = tps_board->tps6507x_pmic_init_data;
	if (!init_data)
		return -EINVAL;

	tps = kzalloc(sizeof(*tps), GFP_KERNEL);
	if (!tps)
		return -ENOMEM;

	mutex_init(&tps->io_lock);

	/* common for all regulators */
	tps->mfd = tps6507x_dev;

	for (i = 0; i < TPS6507X_NUM_REGULATOR; i++, info++, init_data++) {
		/* Register the regulators */
		tps->info[i] = info;
		if (init_data->driver_data) {
			struct tps6507x_reg_platform_data *data =
							init_data->driver_data;
			tps->info[i]->defdcdc_default = data->defdcdc_default;
		}

		tps->desc[i].name = info->name;
		tps->desc[i].id = desc_id++;
		tps->desc[i].n_voltages = num_voltages[i];
		tps->desc[i].ops = (i > TPS6507X_DCDC_3 ?
		&tps6507x_pmic_ldo_ops : &tps6507x_pmic_dcdc_ops);
		tps->desc[i].type = REGULATOR_VOLTAGE;
		tps->desc[i].owner = THIS_MODULE;

		rdev = regulator_register(&tps->desc[i],
					tps6507x_dev->dev, init_data, tps);
		if (IS_ERR(rdev)) {
			dev_err(tps6507x_dev->dev,
				"failed to register %s regulator\n",
				pdev->name);
			error = PTR_ERR(rdev);
			goto fail;
		}

		/* Save regulator for cleanup */
		tps->rdev[i] = rdev;
	}

	tps6507x_dev->pmic = tps;

	return 0;

fail:
	while (--i >= 0)
		regulator_unregister(tps->rdev[i]);

	kfree(tps);
	return error;
}

/**
 * tps6507x_remove - TPS6507x driver i2c remove handler
 * @client: i2c driver client device structure
 *
 * Unregister TPS driver as an i2c client device driver
 */
static int __devexit tps6507x_pmic_remove(struct platform_device *pdev)
{
	struct tps6507x_dev *tps6507x_dev = platform_get_drvdata(pdev);
	struct tps6507x_pmic *tps = tps6507x_dev->pmic;
	int i;

	for (i = 0; i < TPS6507X_NUM_REGULATOR; i++)
		regulator_unregister(tps->rdev[i]);

	kfree(tps);

	return 0;
}

static struct platform_driver tps6507x_pmic_driver = {
	.driver = {
		.name = "tps6507x-pmic",
		.owner = THIS_MODULE,
	},
	.probe = tps6507x_pmic_probe,
	.remove = __devexit_p(tps6507x_pmic_remove),
};

/**
 * tps6507x_pmic_init
 *
 * Module init function
 */
static int __init tps6507x_pmic_init(void)
{
	return platform_driver_register(&tps6507x_pmic_driver);
}
subsys_initcall(tps6507x_pmic_init);

/**
 * tps6507x_pmic_cleanup
 *
 * Module exit function
 */
static void __exit tps6507x_pmic_cleanup(void)
{
	platform_driver_unregister(&tps6507x_pmic_driver);
}
module_exit(tps6507x_pmic_cleanup);

MODULE_AUTHOR("Texas Instruments");
MODULE_DESCRIPTION("TPS6507x voltage regulator driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:tps6507x-pmic");

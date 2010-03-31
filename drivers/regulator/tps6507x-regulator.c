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
#include <linux/i2c.h>
#include <linux/delay.h>

/* Register definitions */
#define	TPS6507X_REG_PPATH1				0X01
#define	TPS6507X_REG_INT				0X02
#define	TPS6507X_REG_CHGCONFIG0				0X03
#define	TPS6507X_REG_CHGCONFIG1				0X04
#define	TPS6507X_REG_CHGCONFIG2				0X05
#define	TPS6507X_REG_CHGCONFIG3				0X06
#define	TPS6507X_REG_REG_ADCONFIG			0X07
#define	TPS6507X_REG_TSCMODE				0X08
#define	TPS6507X_REG_ADRESULT_1				0X09
#define	TPS6507X_REG_ADRESULT_2				0X0A
#define	TPS6507X_REG_PGOOD				0X0B
#define	TPS6507X_REG_PGOODMASK				0X0C
#define	TPS6507X_REG_CON_CTRL1				0X0D
#define	TPS6507X_REG_CON_CTRL2				0X0E
#define	TPS6507X_REG_CON_CTRL3				0X0F
#define	TPS6507X_REG_DEFDCDC1				0X10
#define	TPS6507X_REG_DEFDCDC2_LOW			0X11
#define	TPS6507X_REG_DEFDCDC2_HIGH			0X12
#define	TPS6507X_REG_DEFDCDC3_LOW			0X13
#define	TPS6507X_REG_DEFDCDC3_HIGH			0X14
#define	TPS6507X_REG_DEFSLEW				0X15
#define	TPS6507X_REG_LDO_CTRL1				0X16
#define	TPS6507X_REG_DEFLDO2				0X17
#define	TPS6507X_REG_WLED_CTRL1				0X18
#define	TPS6507X_REG_WLED_CTRL2				0X19

/* CON_CTRL1 bitfields */
#define	TPS6507X_CON_CTRL1_DCDC1_ENABLE		BIT(4)
#define	TPS6507X_CON_CTRL1_DCDC2_ENABLE		BIT(3)
#define	TPS6507X_CON_CTRL1_DCDC3_ENABLE		BIT(2)
#define	TPS6507X_CON_CTRL1_LDO1_ENABLE		BIT(1)
#define	TPS6507X_CON_CTRL1_LDO2_ENABLE		BIT(0)

/* DEFDCDC1 bitfields */
#define TPS6507X_DEFDCDC1_DCDC1_EXT_ADJ_EN	BIT(7)
#define TPS6507X_DEFDCDC1_DCDC1_MASK		0X3F

/* DEFDCDC2_LOW bitfields */
#define TPS6507X_DEFDCDC2_LOW_DCDC2_MASK	0X3F

/* DEFDCDC2_HIGH bitfields */
#define TPS6507X_DEFDCDC2_HIGH_DCDC2_MASK	0X3F

/* DEFDCDC3_LOW bitfields */
#define TPS6507X_DEFDCDC3_LOW_DCDC3_MASK	0X3F

/* DEFDCDC3_HIGH bitfields */
#define TPS6507X_DEFDCDC3_HIGH_DCDC3_MASK	0X3F

/* TPS6507X_REG_LDO_CTRL1 bitfields */
#define TPS6507X_REG_LDO_CTRL1_LDO1_MASK	0X0F

/* TPS6507X_REG_DEFLDO2 bitfields */
#define TPS6507X_REG_DEFLDO2_LDO2_MASK		0X3F

/* VDCDC MASK */
#define TPS6507X_DEFDCDCX_DCDC_MASK		0X3F

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
};

struct tps_pmic {
	struct regulator_desc desc[TPS6507X_NUM_REGULATOR];
	struct i2c_client *client;
	struct regulator_dev *rdev[TPS6507X_NUM_REGULATOR];
	const struct tps_info *info[TPS6507X_NUM_REGULATOR];
	struct mutex io_lock;
};

static inline int tps_6507x_read(struct tps_pmic *tps, u8 reg)
{
	return i2c_smbus_read_byte_data(tps->client, reg);
}

static inline int tps_6507x_write(struct tps_pmic *tps, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(tps->client, reg, val);
}

static int tps_6507x_set_bits(struct tps_pmic *tps, u8 reg, u8 mask)
{
	int err, data;

	mutex_lock(&tps->io_lock);

	data = tps_6507x_read(tps, reg);
	if (data < 0) {
		dev_err(&tps->client->dev, "Read from reg 0x%x failed\n", reg);
		err = data;
		goto out;
	}

	data |= mask;
	err = tps_6507x_write(tps, reg, data);
	if (err)
		dev_err(&tps->client->dev, "Write for reg 0x%x failed\n", reg);

out:
	mutex_unlock(&tps->io_lock);
	return err;
}

static int tps_6507x_clear_bits(struct tps_pmic *tps, u8 reg, u8 mask)
{
	int err, data;

	mutex_lock(&tps->io_lock);

	data = tps_6507x_read(tps, reg);
	if (data < 0) {
		dev_err(&tps->client->dev, "Read from reg 0x%x failed\n", reg);
		err = data;
		goto out;
	}

	data &= ~mask;
	err = tps_6507x_write(tps, reg, data);
	if (err)
		dev_err(&tps->client->dev, "Write for reg 0x%x failed\n", reg);

out:
	mutex_unlock(&tps->io_lock);
	return err;
}

static int tps_6507x_reg_read(struct tps_pmic *tps, u8 reg)
{
	int data;

	mutex_lock(&tps->io_lock);

	data = tps_6507x_read(tps, reg);
	if (data < 0)
		dev_err(&tps->client->dev, "Read from reg 0x%x failed\n", reg);

	mutex_unlock(&tps->io_lock);
	return data;
}

static int tps_6507x_reg_write(struct tps_pmic *tps, u8 reg, u8 val)
{
	int err;

	mutex_lock(&tps->io_lock);

	err = tps_6507x_write(tps, reg, val);
	if (err < 0)
		dev_err(&tps->client->dev, "Write for reg 0x%x failed\n", reg);

	mutex_unlock(&tps->io_lock);
	return err;
}

static int tps6507x_dcdc_is_enabled(struct regulator_dev *dev)
{
	struct tps_pmic *tps = rdev_get_drvdata(dev);
	int data, dcdc = rdev_get_id(dev);
	u8 shift;

	if (dcdc < TPS6507X_DCDC_1 || dcdc > TPS6507X_DCDC_3)
		return -EINVAL;

	shift = TPS6507X_MAX_REG_ID - dcdc;
	data = tps_6507x_reg_read(tps, TPS6507X_REG_CON_CTRL1);

	if (data < 0)
		return data;
	else
		return (data & 1<<shift) ? 1 : 0;
}

static int tps6507x_ldo_is_enabled(struct regulator_dev *dev)
{
	struct tps_pmic *tps = rdev_get_drvdata(dev);
	int data, ldo = rdev_get_id(dev);
	u8 shift;

	if (ldo < TPS6507X_LDO_1 || ldo > TPS6507X_LDO_2)
		return -EINVAL;

	shift = TPS6507X_MAX_REG_ID - ldo;
	data = tps_6507x_reg_read(tps, TPS6507X_REG_CON_CTRL1);

	if (data < 0)
		return data;
	else
		return (data & 1<<shift) ? 1 : 0;
}

static int tps6507x_dcdc_enable(struct regulator_dev *dev)
{
	struct tps_pmic *tps = rdev_get_drvdata(dev);
	int dcdc = rdev_get_id(dev);
	u8 shift;

	if (dcdc < TPS6507X_DCDC_1 || dcdc > TPS6507X_DCDC_3)
		return -EINVAL;

	shift = TPS6507X_MAX_REG_ID - dcdc;
	return tps_6507x_set_bits(tps, TPS6507X_REG_CON_CTRL1, 1 << shift);
}

static int tps6507x_dcdc_disable(struct regulator_dev *dev)
{
	struct tps_pmic *tps = rdev_get_drvdata(dev);
	int dcdc = rdev_get_id(dev);
	u8 shift;

	if (dcdc < TPS6507X_DCDC_1 || dcdc > TPS6507X_DCDC_3)
		return -EINVAL;

	shift = TPS6507X_MAX_REG_ID - dcdc;
	return tps_6507x_clear_bits(tps, TPS6507X_REG_CON_CTRL1, 1 << shift);
}

static int tps6507x_ldo_enable(struct regulator_dev *dev)
{
	struct tps_pmic *tps = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev);
	u8 shift;

	if (ldo < TPS6507X_LDO_1 || ldo > TPS6507X_LDO_2)
		return -EINVAL;

	shift = TPS6507X_MAX_REG_ID - ldo;
	return tps_6507x_set_bits(tps, TPS6507X_REG_CON_CTRL1, 1 << shift);
}

static int tps6507x_ldo_disable(struct regulator_dev *dev)
{
	struct tps_pmic *tps = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev);
	u8 shift;

	if (ldo < TPS6507X_LDO_1 || ldo > TPS6507X_LDO_2)
		return -EINVAL;

	shift = TPS6507X_MAX_REG_ID - ldo;
	return tps_6507x_clear_bits(tps, TPS6507X_REG_CON_CTRL1, 1 << shift);
}

static int tps6507x_dcdc_get_voltage(struct regulator_dev *dev)
{
	struct tps_pmic *tps = rdev_get_drvdata(dev);
	int data, dcdc = rdev_get_id(dev);
	u8 reg;

	switch (dcdc) {
	case TPS6507X_DCDC_1:
		reg = TPS6507X_REG_DEFDCDC1;
		break;
	case TPS6507X_DCDC_2:
		reg = TPS6507X_REG_DEFDCDC2_LOW;
		break;
	case TPS6507X_DCDC_3:
		reg = TPS6507X_REG_DEFDCDC3_LOW;
		break;
	default:
		return -EINVAL;
	}

	data = tps_6507x_reg_read(tps, reg);
	if (data < 0)
		return data;

	data &= TPS6507X_DEFDCDCX_DCDC_MASK;
	return tps->info[dcdc]->table[data] * 1000;
}

static int tps6507x_dcdc_set_voltage(struct regulator_dev *dev,
				int min_uV, int max_uV)
{
	struct tps_pmic *tps = rdev_get_drvdata(dev);
	int data, vsel, dcdc = rdev_get_id(dev);
	u8 reg;

	switch (dcdc) {
	case TPS6507X_DCDC_1:
		reg = TPS6507X_REG_DEFDCDC1;
		break;
	case TPS6507X_DCDC_2:
		reg = TPS6507X_REG_DEFDCDC2_LOW;
		break;
	case TPS6507X_DCDC_3:
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

	data = tps_6507x_reg_read(tps, reg);
	if (data < 0)
		return data;

	data &= ~TPS6507X_DEFDCDCX_DCDC_MASK;
	data |= vsel;

	return tps_6507x_reg_write(tps, reg, data);
}

static int tps6507x_ldo_get_voltage(struct regulator_dev *dev)
{
	struct tps_pmic *tps = rdev_get_drvdata(dev);
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

	data = tps_6507x_reg_read(tps, reg);
	if (data < 0)
		return data;

	data &= mask;
	return tps->info[ldo]->table[data] * 1000;
}

static int tps6507x_ldo_set_voltage(struct regulator_dev *dev,
				int min_uV, int max_uV)
{
	struct tps_pmic *tps = rdev_get_drvdata(dev);
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

	data = tps_6507x_reg_read(tps, reg);
	if (data < 0)
		return data;

	data &= ~mask;
	data |= vsel;

	return tps_6507x_reg_write(tps, reg, data);
}

static int tps6507x_dcdc_list_voltage(struct regulator_dev *dev,
					unsigned selector)
{
	struct tps_pmic *tps = rdev_get_drvdata(dev);
	int dcdc = rdev_get_id(dev);

	if (dcdc < TPS6507X_DCDC_1 || dcdc > TPS6507X_DCDC_3)
		return -EINVAL;

	if (selector >= tps->info[dcdc]->table_len)
		return -EINVAL;
	else
		return tps->info[dcdc]->table[selector] * 1000;
}

static int tps6507x_ldo_list_voltage(struct regulator_dev *dev,
					unsigned selector)
{
	struct tps_pmic *tps = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev);

	if (ldo < TPS6507X_LDO_1 || ldo > TPS6507X_LDO_2)
		return -EINVAL;

	if (selector >= tps->info[ldo]->table_len)
		return -EINVAL;
	else
		return tps->info[ldo]->table[selector] * 1000;
}

/* Operations permitted on VDCDCx */
static struct regulator_ops tps6507x_dcdc_ops = {
	.is_enabled = tps6507x_dcdc_is_enabled,
	.enable = tps6507x_dcdc_enable,
	.disable = tps6507x_dcdc_disable,
	.get_voltage = tps6507x_dcdc_get_voltage,
	.set_voltage = tps6507x_dcdc_set_voltage,
	.list_voltage = tps6507x_dcdc_list_voltage,
};

/* Operations permitted on LDOx */
static struct regulator_ops tps6507x_ldo_ops = {
	.is_enabled = tps6507x_ldo_is_enabled,
	.enable = tps6507x_ldo_enable,
	.disable = tps6507x_ldo_disable,
	.get_voltage = tps6507x_ldo_get_voltage,
	.set_voltage = tps6507x_ldo_set_voltage,
	.list_voltage = tps6507x_ldo_list_voltage,
};

static int __devinit tps_6507x_probe(struct i2c_client *client,
				     const struct i2c_device_id *id)
{
	static int desc_id;
	const struct tps_info *info = (void *)id->driver_data;
	struct regulator_init_data *init_data;
	struct regulator_dev *rdev;
	struct tps_pmic *tps;
	int i;
	int error;

	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	/**
	 * init_data points to array of regulator_init structures
	 * coming from the board-evm file.
	 */
	init_data = client->dev.platform_data;
	if (!init_data)
		return -EIO;

	tps = kzalloc(sizeof(*tps), GFP_KERNEL);
	if (!tps)
		return -ENOMEM;

	mutex_init(&tps->io_lock);

	/* common for all regulators */
	tps->client = client;

	for (i = 0; i < TPS6507X_NUM_REGULATOR; i++, info++, init_data++) {
		/* Register the regulators */
		tps->info[i] = info;
		tps->desc[i].name = info->name;
		tps->desc[i].id = desc_id++;
		tps->desc[i].n_voltages = num_voltages[i];
		tps->desc[i].ops = (i > TPS6507X_DCDC_3 ?
				&tps6507x_ldo_ops : &tps6507x_dcdc_ops);
		tps->desc[i].type = REGULATOR_VOLTAGE;
		tps->desc[i].owner = THIS_MODULE;

		rdev = regulator_register(&tps->desc[i],
					&client->dev, init_data, tps);
		if (IS_ERR(rdev)) {
			dev_err(&client->dev, "failed to register %s\n",
				id->name);
			error = PTR_ERR(rdev);
			goto fail;
		}

		/* Save regulator for cleanup */
		tps->rdev[i] = rdev;
	}

	i2c_set_clientdata(client, tps);

	return 0;

fail:
	while (--i >= 0)
		regulator_unregister(tps->rdev[i]);

	kfree(tps);
	return error;
}

/**
 * tps_6507x_remove - TPS6507x driver i2c remove handler
 * @client: i2c driver client device structure
 *
 * Unregister TPS driver as an i2c client device driver
 */
static int __devexit tps_6507x_remove(struct i2c_client *client)
{
	struct tps_pmic *tps = i2c_get_clientdata(client);
	int i;

	/* clear the client data in i2c */
	i2c_set_clientdata(client, NULL);

	for (i = 0; i < TPS6507X_NUM_REGULATOR; i++)
		regulator_unregister(tps->rdev[i]);

	kfree(tps);

	return 0;
}

static const struct tps_info tps6507x_regs[] = {
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

static const struct i2c_device_id tps_6507x_id[] = {
	{.name = "tps6507x",
	.driver_data = (unsigned long) tps6507x_regs,},
	{ },
};
MODULE_DEVICE_TABLE(i2c, tps_6507x_id);

static struct i2c_driver tps_6507x_i2c_driver = {
	.driver = {
		.name = "tps6507x",
		.owner = THIS_MODULE,
	},
	.probe = tps_6507x_probe,
	.remove = __devexit_p(tps_6507x_remove),
	.id_table = tps_6507x_id,
};

/**
 * tps_6507x_init
 *
 * Module init function
 */
static int __init tps_6507x_init(void)
{
	return i2c_add_driver(&tps_6507x_i2c_driver);
}
subsys_initcall(tps_6507x_init);

/**
 * tps_6507x_cleanup
 *
 * Module exit function
 */
static void __exit tps_6507x_cleanup(void)
{
	i2c_del_driver(&tps_6507x_i2c_driver);
}
module_exit(tps_6507x_cleanup);

MODULE_AUTHOR("Texas Instruments");
MODULE_DESCRIPTION("TPS6507x voltage regulator driver");
MODULE_LICENSE("GPL v2");

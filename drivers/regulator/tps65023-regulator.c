/*
 * tps65023-regulator.c
 *
 * Supports TPS65023 Regulator
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
#include <linux/slab.h>
#include <linux/regmap.h>

/* Register definitions */
#define	TPS65023_REG_VERSION		0
#define	TPS65023_REG_PGOODZ		1
#define	TPS65023_REG_MASK		2
#define	TPS65023_REG_REG_CTRL		3
#define	TPS65023_REG_CON_CTRL		4
#define	TPS65023_REG_CON_CTRL2		5
#define	TPS65023_REG_DEF_CORE		6
#define	TPS65023_REG_DEFSLEW		7
#define	TPS65023_REG_LDO_CTRL		8

/* PGOODZ bitfields */
#define	TPS65023_PGOODZ_PWRFAILZ	BIT(7)
#define	TPS65023_PGOODZ_LOWBATTZ	BIT(6)
#define	TPS65023_PGOODZ_VDCDC1		BIT(5)
#define	TPS65023_PGOODZ_VDCDC2		BIT(4)
#define	TPS65023_PGOODZ_VDCDC3		BIT(3)
#define	TPS65023_PGOODZ_LDO2		BIT(2)
#define	TPS65023_PGOODZ_LDO1		BIT(1)

/* MASK bitfields */
#define	TPS65023_MASK_PWRFAILZ		BIT(7)
#define	TPS65023_MASK_LOWBATTZ		BIT(6)
#define	TPS65023_MASK_VDCDC1		BIT(5)
#define	TPS65023_MASK_VDCDC2		BIT(4)
#define	TPS65023_MASK_VDCDC3		BIT(3)
#define	TPS65023_MASK_LDO2		BIT(2)
#define	TPS65023_MASK_LDO1		BIT(1)

/* REG_CTRL bitfields */
#define TPS65023_REG_CTRL_VDCDC1_EN	BIT(5)
#define TPS65023_REG_CTRL_VDCDC2_EN	BIT(4)
#define TPS65023_REG_CTRL_VDCDC3_EN	BIT(3)
#define TPS65023_REG_CTRL_LDO2_EN	BIT(2)
#define TPS65023_REG_CTRL_LDO1_EN	BIT(1)

/* REG_CTRL2 bitfields */
#define TPS65023_REG_CTRL2_GO		BIT(7)
#define TPS65023_REG_CTRL2_CORE_ADJ	BIT(6)
#define TPS65023_REG_CTRL2_DCDC2	BIT(2)
#define TPS65023_REG_CTRL2_DCDC1	BIT(1)
#define TPS65023_REG_CTRL2_DCDC3	BIT(0)

/* Number of step-down converters available */
#define TPS65023_NUM_DCDC		3
/* Number of LDO voltage regulators  available */
#define TPS65023_NUM_LDO		2
/* Number of total regulators available */
#define TPS65023_NUM_REGULATOR	(TPS65023_NUM_DCDC + TPS65023_NUM_LDO)

/* DCDCs */
#define TPS65023_DCDC_1			0
#define TPS65023_DCDC_2			1
#define TPS65023_DCDC_3			2
/* LDOs */
#define TPS65023_LDO_1			3
#define TPS65023_LDO_2			4

#define TPS65023_MAX_REG_ID		TPS65023_LDO_2

/* Supported voltage values for regulators */
static const unsigned int VCORE_VSEL_table[] = {
	800000, 825000, 850000, 875000,
	900000, 925000, 950000, 975000,
	1000000, 1025000, 1050000, 1075000,
	1100000, 1125000, 1150000, 1175000,
	1200000, 1225000, 1250000, 1275000,
	1300000, 1325000, 1350000, 1375000,
	1400000, 1425000, 1450000, 1475000,
	1500000, 1525000, 1550000, 1600000,
};

static const unsigned int DCDC_FIXED_3300000_VSEL_table[] = {
	3300000,
};

static const unsigned int DCDC_FIXED_1800000_VSEL_table[] = {
	1800000,
};

/* Supported voltage values for LDO regulators for tps65020 */
static const unsigned int TPS65020_LDO1_VSEL_table[] = {
	1000000, 1050000, 1100000, 1300000,
	1800000, 2500000, 3000000, 3300000,
};

static const unsigned int TPS65020_LDO2_VSEL_table[] = {
	1000000, 1050000, 1100000, 1300000,
	1800000, 2500000, 3000000, 3300000,
};

/* Supported voltage values for LDO regulators
 * for tps65021 and tps65023 */
static const unsigned int TPS65023_LDO1_VSEL_table[] = {
	1000000, 1100000, 1300000, 1800000,
	2200000, 2600000, 2800000, 3150000,
};

static const unsigned int TPS65023_LDO2_VSEL_table[] = {
	1050000, 1200000, 1300000, 1800000,
	2500000, 2800000, 3000000, 3300000,
};

/* Regulator specific details */
struct tps_info {
	const char *name;
	u8 table_len;
	const unsigned int *table;
};

/* PMIC details */
struct tps_pmic {
	struct regulator_desc desc[TPS65023_NUM_REGULATOR];
	struct regulator_dev *rdev[TPS65023_NUM_REGULATOR];
	const struct tps_info *info[TPS65023_NUM_REGULATOR];
	struct regmap *regmap;
	u8 core_regulator;
};

/* Struct passed as driver data */
struct tps_driver_data {
	const struct tps_info *info;
	u8 core_regulator;
};

static int tps65023_dcdc_get_voltage_sel(struct regulator_dev *dev)
{
	struct tps_pmic *tps = rdev_get_drvdata(dev);
	int ret;
	int data, dcdc = rdev_get_id(dev);

	if (dcdc < TPS65023_DCDC_1 || dcdc > TPS65023_DCDC_3)
		return -EINVAL;

	if (dcdc == tps->core_regulator) {
		ret = regmap_read(tps->regmap, TPS65023_REG_DEF_CORE, &data);
		if (ret != 0)
			return ret;
		data &= (tps->info[dcdc]->table_len - 1);
		return data;
	} else
		return 0;
}

static int tps65023_dcdc_set_voltage_sel(struct regulator_dev *dev,
					 unsigned selector)
{
	struct tps_pmic *tps = rdev_get_drvdata(dev);
	int dcdc = rdev_get_id(dev);
	int ret;

	if (dcdc != tps->core_regulator)
		return -EINVAL;

	ret = regmap_write(tps->regmap, TPS65023_REG_DEF_CORE, selector);
	if (ret)
		goto out;

	/* Tell the chip that we have changed the value in DEFCORE
	 * and its time to update the core voltage
	 */
	ret = regmap_update_bits(tps->regmap, TPS65023_REG_CON_CTRL2,
				 TPS65023_REG_CTRL2_GO, TPS65023_REG_CTRL2_GO);

out:
	return ret;
}

/* Operations permitted on VDCDCx */
static struct regulator_ops tps65023_dcdc_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.get_voltage_sel = tps65023_dcdc_get_voltage_sel,
	.set_voltage_sel = tps65023_dcdc_set_voltage_sel,
	.list_voltage = regulator_list_voltage_table,
};

/* Operations permitted on LDOx */
static struct regulator_ops tps65023_ldo_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_table,
};

static struct regmap_config tps65023_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int __devinit tps_65023_probe(struct i2c_client *client,
				     const struct i2c_device_id *id)
{
	const struct tps_driver_data *drv_data = (void *)id->driver_data;
	const struct tps_info *info = drv_data->info;
	struct regulator_config config = { };
	struct regulator_init_data *init_data;
	struct regulator_dev *rdev;
	struct tps_pmic *tps;
	int i;
	int error;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	/**
	 * init_data points to array of regulator_init structures
	 * coming from the board-evm file.
	 */
	init_data = client->dev.platform_data;
	if (!init_data)
		return -EIO;

	tps = devm_kzalloc(&client->dev, sizeof(*tps), GFP_KERNEL);
	if (!tps)
		return -ENOMEM;

	tps->regmap = devm_regmap_init_i2c(client, &tps65023_regmap_config);
	if (IS_ERR(tps->regmap)) {
		error = PTR_ERR(tps->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			error);
		return error;
	}

	/* common for all regulators */
	tps->core_regulator = drv_data->core_regulator;

	for (i = 0; i < TPS65023_NUM_REGULATOR; i++, info++, init_data++) {
		/* Store regulator specific information */
		tps->info[i] = info;

		tps->desc[i].name = info->name;
		tps->desc[i].id = i;
		tps->desc[i].n_voltages = info->table_len;
		tps->desc[i].volt_table = info->table;
		tps->desc[i].ops = (i > TPS65023_DCDC_3 ?
					&tps65023_ldo_ops : &tps65023_dcdc_ops);
		tps->desc[i].type = REGULATOR_VOLTAGE;
		tps->desc[i].owner = THIS_MODULE;

		tps->desc[i].enable_reg = TPS65023_REG_REG_CTRL;
		switch (i) {
		case TPS65023_LDO_1:
			tps->desc[i].vsel_reg = TPS65023_REG_LDO_CTRL;
			tps->desc[i].vsel_mask = 0x07;
			tps->desc[i].enable_mask = 1 << 1;
			break;
		case TPS65023_LDO_2:
			tps->desc[i].vsel_reg = TPS65023_REG_LDO_CTRL;
			tps->desc[i].vsel_mask = 0x70;
			tps->desc[i].enable_mask = 1 << 2;
			break;
		default: /* DCDCx */
			tps->desc[i].enable_mask =
					1 << (TPS65023_NUM_REGULATOR - i);
		}

		config.dev = &client->dev;
		config.init_data = init_data;
		config.driver_data = tps;
		config.regmap = tps->regmap;

		/* Register the regulators */
		rdev = regulator_register(&tps->desc[i], &config);
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

	/* Enable setting output voltage by I2C */
	regmap_update_bits(tps->regmap, TPS65023_REG_CON_CTRL2,
			TPS65023_REG_CTRL2_CORE_ADJ, TPS65023_REG_CTRL2_CORE_ADJ);

	return 0;

 fail:
	while (--i >= 0)
		regulator_unregister(tps->rdev[i]);
	return error;
}

static int __devexit tps_65023_remove(struct i2c_client *client)
{
	struct tps_pmic *tps = i2c_get_clientdata(client);
	int i;

	for (i = 0; i < TPS65023_NUM_REGULATOR; i++)
		regulator_unregister(tps->rdev[i]);
	return 0;
}

static const struct tps_info tps65020_regs[] = {
	{
		.name = "VDCDC1",
		.table_len = ARRAY_SIZE(DCDC_FIXED_3300000_VSEL_table),
		.table = DCDC_FIXED_3300000_VSEL_table,
	},
	{
		.name = "VDCDC2",
		.table_len = ARRAY_SIZE(DCDC_FIXED_1800000_VSEL_table),
		.table = DCDC_FIXED_1800000_VSEL_table,
	},
	{
		.name = "VDCDC3",
		.table_len = ARRAY_SIZE(VCORE_VSEL_table),
		.table = VCORE_VSEL_table,
	},
	{
		.name = "LDO1",
		.table_len = ARRAY_SIZE(TPS65020_LDO1_VSEL_table),
		.table = TPS65020_LDO1_VSEL_table,
	},
	{
		.name = "LDO2",
		.table_len = ARRAY_SIZE(TPS65020_LDO2_VSEL_table),
		.table = TPS65020_LDO2_VSEL_table,
	},
};

static const struct tps_info tps65021_regs[] = {
	{
		.name = "VDCDC1",
		.table_len = ARRAY_SIZE(DCDC_FIXED_3300000_VSEL_table),
		.table = DCDC_FIXED_3300000_VSEL_table,
	},
	{
		.name = "VDCDC2",
		.table_len = ARRAY_SIZE(DCDC_FIXED_1800000_VSEL_table),
		.table = DCDC_FIXED_1800000_VSEL_table,
	},
	{
		.name = "VDCDC3",
		.table_len = ARRAY_SIZE(VCORE_VSEL_table),
		.table = VCORE_VSEL_table,
	},
	{
		.name = "LDO1",
		.table_len = ARRAY_SIZE(TPS65023_LDO1_VSEL_table),
		.table = TPS65023_LDO1_VSEL_table,
	},
	{
		.name = "LDO2",
		.table_len = ARRAY_SIZE(TPS65023_LDO2_VSEL_table),
		.table = TPS65023_LDO2_VSEL_table,
	},
};

static const struct tps_info tps65023_regs[] = {
	{
		.name = "VDCDC1",
		.table_len = ARRAY_SIZE(VCORE_VSEL_table),
		.table = VCORE_VSEL_table,
	},
	{
		.name = "VDCDC2",
		.table_len = ARRAY_SIZE(DCDC_FIXED_3300000_VSEL_table),
		.table = DCDC_FIXED_3300000_VSEL_table,
	},
	{
		.name = "VDCDC3",
		.table_len = ARRAY_SIZE(DCDC_FIXED_1800000_VSEL_table),
		.table = DCDC_FIXED_1800000_VSEL_table,
	},
	{
		.name = "LDO1",
		.table_len = ARRAY_SIZE(TPS65023_LDO1_VSEL_table),
		.table = TPS65023_LDO1_VSEL_table,
	},
	{
		.name = "LDO2",
		.table_len = ARRAY_SIZE(TPS65023_LDO2_VSEL_table),
		.table = TPS65023_LDO2_VSEL_table,
	},
};

static struct tps_driver_data tps65020_drv_data = {
	.info = tps65020_regs,
	.core_regulator = TPS65023_DCDC_3,
};

static struct tps_driver_data tps65021_drv_data = {
	.info = tps65021_regs,
	.core_regulator = TPS65023_DCDC_3,
};

static struct tps_driver_data tps65023_drv_data = {
	.info = tps65023_regs,
	.core_regulator = TPS65023_DCDC_1,
};

static const struct i2c_device_id tps_65023_id[] = {
	{.name = "tps65023",
	.driver_data = (unsigned long) &tps65023_drv_data},
	{.name = "tps65021",
	.driver_data = (unsigned long) &tps65021_drv_data,},
	{.name = "tps65020",
	.driver_data = (unsigned long) &tps65020_drv_data},
	{ },
};

MODULE_DEVICE_TABLE(i2c, tps_65023_id);

static struct i2c_driver tps_65023_i2c_driver = {
	.driver = {
		.name = "tps65023",
		.owner = THIS_MODULE,
	},
	.probe = tps_65023_probe,
	.remove = __devexit_p(tps_65023_remove),
	.id_table = tps_65023_id,
};

static int __init tps_65023_init(void)
{
	return i2c_add_driver(&tps_65023_i2c_driver);
}
subsys_initcall(tps_65023_init);

static void __exit tps_65023_cleanup(void)
{
	i2c_del_driver(&tps_65023_i2c_driver);
}
module_exit(tps_65023_cleanup);

MODULE_AUTHOR("Texas Instruments");
MODULE_DESCRIPTION("TPS65023 voltage regulator driver");
MODULE_LICENSE("GPL v2");

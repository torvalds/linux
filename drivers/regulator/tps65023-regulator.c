// SPDX-License-Identifier: GPL-2.0-only
/*
 * tps65023-regulator.c
 *
 * Supports TPS65023 Regulator
 *
 * Copyright (C) 2009 Texas Instrument Incorporated - https://www.ti.com/
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

#define TPS65023_REGULATOR_DCDC(_num, _t, _em)			\
	{							\
		.name		= "VDCDC"#_num,			\
		.of_match	= of_match_ptr("VDCDC"#_num),	\
		.regulators_node = of_match_ptr("regulators"),	\
		.id		= TPS65023_DCDC_##_num,		\
		.n_voltages     = ARRAY_SIZE(_t),		\
		.ops		= &tps65023_dcdc_ops,		\
		.type		= REGULATOR_VOLTAGE,		\
		.owner		= THIS_MODULE,			\
		.volt_table	= _t,				\
		.vsel_reg	= TPS65023_REG_DEF_CORE,	\
		.vsel_mask	= ARRAY_SIZE(_t) - 1,		\
		.enable_mask	= _em,				\
		.enable_reg	= TPS65023_REG_REG_CTRL,	\
		.apply_reg	= TPS65023_REG_CON_CTRL2,	\
		.apply_bit	= TPS65023_REG_CTRL2_GO,	\
	}							\

#define TPS65023_REGULATOR_LDO(_num, _t, _vm)			\
	{							\
		.name		= "LDO"#_num,			\
		.of_match	= of_match_ptr("LDO"#_num),	\
		.regulators_node = of_match_ptr("regulators"),	\
		.id		= TPS65023_LDO_##_num,		\
		.n_voltages     = ARRAY_SIZE(_t),		\
		.ops		= &tps65023_ldo_ops,		\
		.type		= REGULATOR_VOLTAGE,		\
		.owner		= THIS_MODULE,			\
		.volt_table	= _t,				\
		.vsel_reg	= TPS65023_REG_LDO_CTRL,	\
		.vsel_mask	= _vm,				\
		.enable_mask	= 1 << (_num),			\
		.enable_reg	= TPS65023_REG_REG_CTRL,	\
	}							\

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
static const unsigned int TPS65020_LDO_VSEL_table[] = {
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

/* PMIC details */
struct tps_pmic {
	struct regulator_dev *rdev[TPS65023_NUM_REGULATOR];
	const struct tps_driver_data *driver_data;
	struct regmap *regmap;
};

/* Struct passed as driver data */
struct tps_driver_data {
	const struct regulator_desc *desc;
	u8 core_regulator;
};

static int tps65023_dcdc_get_voltage_sel(struct regulator_dev *dev)
{
	struct tps_pmic *tps = rdev_get_drvdata(dev);
	int dcdc = rdev_get_id(dev);

	if (dcdc < TPS65023_DCDC_1 || dcdc > TPS65023_DCDC_3)
		return -EINVAL;

	if (dcdc != tps->driver_data->core_regulator)
		return 0;

	return regulator_get_voltage_sel_regmap(dev);
}

static int tps65023_dcdc_set_voltage_sel(struct regulator_dev *dev,
					 unsigned selector)
{
	struct tps_pmic *tps = rdev_get_drvdata(dev);
	int dcdc = rdev_get_id(dev);

	if (dcdc != tps->driver_data->core_regulator)
		return -EINVAL;

	return regulator_set_voltage_sel_regmap(dev, selector);
}

/* Operations permitted on VDCDCx */
static const struct regulator_ops tps65023_dcdc_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.get_voltage_sel = tps65023_dcdc_get_voltage_sel,
	.set_voltage_sel = tps65023_dcdc_set_voltage_sel,
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
};

/* Operations permitted on LDOx */
static const struct regulator_ops tps65023_ldo_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
};

static const struct regmap_config tps65023_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static const struct regulator_desc tps65020_regulators[] = {
	TPS65023_REGULATOR_DCDC(1, DCDC_FIXED_3300000_VSEL_table, 0x20),
	TPS65023_REGULATOR_DCDC(2, DCDC_FIXED_1800000_VSEL_table, 0x10),
	TPS65023_REGULATOR_DCDC(3, VCORE_VSEL_table, 0x08),
	TPS65023_REGULATOR_LDO(1, TPS65020_LDO_VSEL_table, 0x07),
	TPS65023_REGULATOR_LDO(2, TPS65020_LDO_VSEL_table, 0x70),
};

static const struct regulator_desc tps65021_regulators[] = {
	TPS65023_REGULATOR_DCDC(1, DCDC_FIXED_3300000_VSEL_table, 0x20),
	TPS65023_REGULATOR_DCDC(2, DCDC_FIXED_1800000_VSEL_table, 0x10),
	TPS65023_REGULATOR_DCDC(3, VCORE_VSEL_table, 0x08),
	TPS65023_REGULATOR_LDO(1, TPS65023_LDO1_VSEL_table, 0x07),
	TPS65023_REGULATOR_LDO(2, TPS65023_LDO2_VSEL_table, 0x70),
};

static const struct regulator_desc tps65023_regulators[] = {
	TPS65023_REGULATOR_DCDC(1, VCORE_VSEL_table, 0x20),
	TPS65023_REGULATOR_DCDC(2, DCDC_FIXED_3300000_VSEL_table, 0x10),
	TPS65023_REGULATOR_DCDC(3, DCDC_FIXED_1800000_VSEL_table, 0x08),
	TPS65023_REGULATOR_LDO(1, TPS65023_LDO1_VSEL_table, 0x07),
	TPS65023_REGULATOR_LDO(2, TPS65023_LDO2_VSEL_table, 0x70),
};

static struct tps_driver_data tps65020_drv_data = {
	.desc = tps65020_regulators,
	.core_regulator = TPS65023_DCDC_3,
};

static struct tps_driver_data tps65021_drv_data = {
	.desc = tps65021_regulators,
	.core_regulator = TPS65023_DCDC_3,
};

static struct tps_driver_data tps65023_drv_data = {
	.desc = tps65023_regulators,
	.core_regulator = TPS65023_DCDC_1,
};

static int tps_65023_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct regulator_init_data *init_data = dev_get_platdata(&client->dev);
	struct regulator_config config = { };
	struct tps_pmic *tps;
	int i;
	int error;

	tps = devm_kzalloc(&client->dev, sizeof(*tps), GFP_KERNEL);
	if (!tps)
		return -ENOMEM;

	tps->driver_data = (struct tps_driver_data *)id->driver_data;

	tps->regmap = devm_regmap_init_i2c(client, &tps65023_regmap_config);
	if (IS_ERR(tps->regmap)) {
		error = PTR_ERR(tps->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			error);
		return error;
	}

	/* common for all regulators */
	config.dev = &client->dev;
	config.driver_data = tps;
	config.regmap = tps->regmap;

	for (i = 0; i < TPS65023_NUM_REGULATOR; i++) {
		if (init_data)
			config.init_data = &init_data[i];

		/* Register the regulators */
		tps->rdev[i] = devm_regulator_register(&client->dev,
					&tps->driver_data->desc[i], &config);
		if (IS_ERR(tps->rdev[i])) {
			dev_err(&client->dev, "failed to register %s\n",
				id->name);
			return PTR_ERR(tps->rdev[i]);
		}
	}

	i2c_set_clientdata(client, tps);

	/* Enable setting output voltage by I2C */
	regmap_update_bits(tps->regmap, TPS65023_REG_CON_CTRL2,
			   TPS65023_REG_CTRL2_CORE_ADJ, 0);

	return 0;
}

static const struct of_device_id __maybe_unused tps65023_of_match[] = {
	{ .compatible = "ti,tps65020", .data = &tps65020_drv_data},
	{ .compatible = "ti,tps65021", .data = &tps65021_drv_data},
	{ .compatible = "ti,tps65023", .data = &tps65023_drv_data},
	{},
};
MODULE_DEVICE_TABLE(of, tps65023_of_match);

static const struct i2c_device_id tps_65023_id[] = {
	{
		.name = "tps65023",
		.driver_data = (kernel_ulong_t)&tps65023_drv_data
	}, {
		.name = "tps65021",
		.driver_data = (kernel_ulong_t)&tps65021_drv_data
	}, {
		.name = "tps65020",
		.driver_data = (kernel_ulong_t)&tps65020_drv_data
	},
	{ },
};
MODULE_DEVICE_TABLE(i2c, tps_65023_id);

static struct i2c_driver tps_65023_i2c_driver = {
	.driver = {
		.name = "tps65023",
		.of_match_table = of_match_ptr(tps65023_of_match),
	},
	.probe_new = tps_65023_probe,
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

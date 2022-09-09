// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI LP8788 MFD - ldo regulator driver
 *
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/lp8788.h>

/* register address */
#define LP8788_EN_LDO_A			0x0D	/* DLDO 1 ~ 8 */
#define LP8788_EN_LDO_B			0x0E	/* DLDO 9 ~ 12, ALDO 1 ~ 4 */
#define LP8788_EN_LDO_C			0x0F	/* ALDO 5 ~ 10 */
#define LP8788_EN_SEL			0x10
#define LP8788_DLDO1_VOUT		0x2E
#define LP8788_DLDO2_VOUT		0x2F
#define LP8788_DLDO3_VOUT		0x30
#define LP8788_DLDO4_VOUT		0x31
#define LP8788_DLDO5_VOUT		0x32
#define LP8788_DLDO6_VOUT		0x33
#define LP8788_DLDO7_VOUT		0x34
#define LP8788_DLDO8_VOUT		0x35
#define LP8788_DLDO9_VOUT		0x36
#define LP8788_DLDO10_VOUT		0x37
#define LP8788_DLDO11_VOUT		0x38
#define LP8788_DLDO12_VOUT		0x39
#define LP8788_ALDO1_VOUT		0x3A
#define LP8788_ALDO2_VOUT		0x3B
#define LP8788_ALDO3_VOUT		0x3C
#define LP8788_ALDO4_VOUT		0x3D
#define LP8788_ALDO5_VOUT		0x3E
#define LP8788_ALDO6_VOUT		0x3F
#define LP8788_ALDO7_VOUT		0x40
#define LP8788_ALDO8_VOUT		0x41
#define LP8788_ALDO9_VOUT		0x42
#define LP8788_ALDO10_VOUT		0x43
#define LP8788_DLDO1_TIMESTEP		0x44

/* mask/shift bits */
#define LP8788_EN_DLDO1_M		BIT(0)	/* Addr 0Dh ~ 0Fh */
#define LP8788_EN_DLDO2_M		BIT(1)
#define LP8788_EN_DLDO3_M		BIT(2)
#define LP8788_EN_DLDO4_M		BIT(3)
#define LP8788_EN_DLDO5_M		BIT(4)
#define LP8788_EN_DLDO6_M		BIT(5)
#define LP8788_EN_DLDO7_M		BIT(6)
#define LP8788_EN_DLDO8_M		BIT(7)
#define LP8788_EN_DLDO9_M		BIT(0)
#define LP8788_EN_DLDO10_M		BIT(1)
#define LP8788_EN_DLDO11_M		BIT(2)
#define LP8788_EN_DLDO12_M		BIT(3)
#define LP8788_EN_ALDO1_M		BIT(4)
#define LP8788_EN_ALDO2_M		BIT(5)
#define LP8788_EN_ALDO3_M		BIT(6)
#define LP8788_EN_ALDO4_M		BIT(7)
#define LP8788_EN_ALDO5_M		BIT(0)
#define LP8788_EN_ALDO6_M		BIT(1)
#define LP8788_EN_ALDO7_M		BIT(2)
#define LP8788_EN_ALDO8_M		BIT(3)
#define LP8788_EN_ALDO9_M		BIT(4)
#define LP8788_EN_ALDO10_M		BIT(5)
#define LP8788_EN_SEL_DLDO911_M		BIT(0)	/* Addr 10h */
#define LP8788_EN_SEL_DLDO7_M		BIT(1)
#define LP8788_EN_SEL_ALDO7_M		BIT(2)
#define LP8788_EN_SEL_ALDO5_M		BIT(3)
#define LP8788_EN_SEL_ALDO234_M		BIT(4)
#define LP8788_EN_SEL_ALDO1_M		BIT(5)
#define LP8788_VOUT_5BIT_M		0x1F	/* Addr 2Eh ~ 43h */
#define LP8788_VOUT_4BIT_M		0x0F
#define LP8788_VOUT_3BIT_M		0x07
#define LP8788_VOUT_1BIT_M		0x01
#define LP8788_STARTUP_TIME_M		0xF8	/* Addr 44h ~ 59h */
#define LP8788_STARTUP_TIME_S		3

#define ENABLE_TIME_USEC		32

enum lp8788_ldo_id {
	DLDO1,
	DLDO2,
	DLDO3,
	DLDO4,
	DLDO5,
	DLDO6,
	DLDO7,
	DLDO8,
	DLDO9,
	DLDO10,
	DLDO11,
	DLDO12,
	ALDO1,
	ALDO2,
	ALDO3,
	ALDO4,
	ALDO5,
	ALDO6,
	ALDO7,
	ALDO8,
	ALDO9,
	ALDO10,
};

struct lp8788_ldo {
	struct lp8788 *lp;
	struct regulator_desc *desc;
	struct regulator_dev *regulator;
	struct gpio_desc *ena_gpiod;
};

/* DLDO 1, 2, 3, 9 voltage table */
static const int lp8788_dldo1239_vtbl[] = {
	1800000, 1900000, 2000000, 2100000, 2200000, 2300000, 2400000, 2500000,
	2600000, 2700000, 2800000, 2900000, 3000000, 2850000, 2850000, 2850000,
	2850000, 2850000, 2850000, 2850000, 2850000, 2850000, 2850000, 2850000,
	2850000, 2850000, 2850000, 2850000, 2850000, 2850000, 2850000, 2850000,
};

/* DLDO 4 voltage table */
static const int lp8788_dldo4_vtbl[] = { 1800000, 3000000 };

/* DLDO 5, 7, 8 and ALDO 6 voltage table */
static const int lp8788_dldo578_aldo6_vtbl[] = {
	1800000, 1900000, 2000000, 2100000, 2200000, 2300000, 2400000, 2500000,
	2600000, 2700000, 2800000, 2900000, 3000000, 3000000, 3000000, 3000000,
};

/* DLDO 6 voltage table */
static const int lp8788_dldo6_vtbl[] = {
	3000000, 3100000, 3200000, 3300000, 3400000, 3500000, 3600000, 3600000,
};

/* DLDO 10, 11 voltage table */
static const int lp8788_dldo1011_vtbl[] = {
	1100000, 1150000, 1200000, 1250000, 1300000, 1350000, 1400000, 1450000,
	1500000, 1500000, 1500000, 1500000, 1500000, 1500000, 1500000, 1500000,
};

/* ALDO 1 voltage table */
static const int lp8788_aldo1_vtbl[] = { 1800000, 2850000 };

/* ALDO 7 voltage table */
static const int lp8788_aldo7_vtbl[] = {
	1200000, 1300000, 1400000, 1500000, 1600000, 1700000, 1800000, 1800000,
};

static int lp8788_ldo_enable_time(struct regulator_dev *rdev)
{
	struct lp8788_ldo *ldo = rdev_get_drvdata(rdev);
	enum lp8788_ldo_id id = rdev_get_id(rdev);
	u8 val, addr = LP8788_DLDO1_TIMESTEP + id;

	if (lp8788_read_byte(ldo->lp, addr, &val))
		return -EINVAL;

	val = (val & LP8788_STARTUP_TIME_M) >> LP8788_STARTUP_TIME_S;

	return ENABLE_TIME_USEC * val;
}

static const struct regulator_ops lp8788_ldo_voltage_table_ops = {
	.list_voltage = regulator_list_voltage_table,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.enable_time = lp8788_ldo_enable_time,
};

static const struct regulator_ops lp8788_ldo_voltage_fixed_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.enable_time = lp8788_ldo_enable_time,
};

static const struct regulator_desc lp8788_dldo_desc[] = {
	{
		.name = "dldo1",
		.id = DLDO1,
		.ops = &lp8788_ldo_voltage_table_ops,
		.n_voltages = ARRAY_SIZE(lp8788_dldo1239_vtbl),
		.volt_table = lp8788_dldo1239_vtbl,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.vsel_reg = LP8788_DLDO1_VOUT,
		.vsel_mask = LP8788_VOUT_5BIT_M,
		.enable_reg = LP8788_EN_LDO_A,
		.enable_mask = LP8788_EN_DLDO1_M,
	},
	{
		.name = "dldo2",
		.id = DLDO2,
		.ops = &lp8788_ldo_voltage_table_ops,
		.n_voltages = ARRAY_SIZE(lp8788_dldo1239_vtbl),
		.volt_table = lp8788_dldo1239_vtbl,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.vsel_reg = LP8788_DLDO2_VOUT,
		.vsel_mask = LP8788_VOUT_5BIT_M,
		.enable_reg = LP8788_EN_LDO_A,
		.enable_mask = LP8788_EN_DLDO2_M,
	},
	{
		.name = "dldo3",
		.id = DLDO3,
		.ops = &lp8788_ldo_voltage_table_ops,
		.n_voltages = ARRAY_SIZE(lp8788_dldo1239_vtbl),
		.volt_table = lp8788_dldo1239_vtbl,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.vsel_reg = LP8788_DLDO3_VOUT,
		.vsel_mask = LP8788_VOUT_5BIT_M,
		.enable_reg = LP8788_EN_LDO_A,
		.enable_mask = LP8788_EN_DLDO3_M,
	},
	{
		.name = "dldo4",
		.id = DLDO4,
		.ops = &lp8788_ldo_voltage_table_ops,
		.n_voltages = ARRAY_SIZE(lp8788_dldo4_vtbl),
		.volt_table = lp8788_dldo4_vtbl,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.vsel_reg = LP8788_DLDO4_VOUT,
		.vsel_mask = LP8788_VOUT_1BIT_M,
		.enable_reg = LP8788_EN_LDO_A,
		.enable_mask = LP8788_EN_DLDO4_M,
	},
	{
		.name = "dldo5",
		.id = DLDO5,
		.ops = &lp8788_ldo_voltage_table_ops,
		.n_voltages = ARRAY_SIZE(lp8788_dldo578_aldo6_vtbl),
		.volt_table = lp8788_dldo578_aldo6_vtbl,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.vsel_reg = LP8788_DLDO5_VOUT,
		.vsel_mask = LP8788_VOUT_4BIT_M,
		.enable_reg = LP8788_EN_LDO_A,
		.enable_mask = LP8788_EN_DLDO5_M,
	},
	{
		.name = "dldo6",
		.id = DLDO6,
		.ops = &lp8788_ldo_voltage_table_ops,
		.n_voltages = ARRAY_SIZE(lp8788_dldo6_vtbl),
		.volt_table = lp8788_dldo6_vtbl,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.vsel_reg = LP8788_DLDO6_VOUT,
		.vsel_mask = LP8788_VOUT_3BIT_M,
		.enable_reg = LP8788_EN_LDO_A,
		.enable_mask = LP8788_EN_DLDO6_M,
	},
	{
		.name = "dldo7",
		.id = DLDO7,
		.ops = &lp8788_ldo_voltage_table_ops,
		.n_voltages = ARRAY_SIZE(lp8788_dldo578_aldo6_vtbl),
		.volt_table = lp8788_dldo578_aldo6_vtbl,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.vsel_reg = LP8788_DLDO7_VOUT,
		.vsel_mask = LP8788_VOUT_4BIT_M,
		.enable_reg = LP8788_EN_LDO_A,
		.enable_mask = LP8788_EN_DLDO7_M,
	},
	{
		.name = "dldo8",
		.id = DLDO8,
		.ops = &lp8788_ldo_voltage_table_ops,
		.n_voltages = ARRAY_SIZE(lp8788_dldo578_aldo6_vtbl),
		.volt_table = lp8788_dldo578_aldo6_vtbl,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.vsel_reg = LP8788_DLDO8_VOUT,
		.vsel_mask = LP8788_VOUT_4BIT_M,
		.enable_reg = LP8788_EN_LDO_A,
		.enable_mask = LP8788_EN_DLDO8_M,
	},
	{
		.name = "dldo9",
		.id = DLDO9,
		.ops = &lp8788_ldo_voltage_table_ops,
		.n_voltages = ARRAY_SIZE(lp8788_dldo1239_vtbl),
		.volt_table = lp8788_dldo1239_vtbl,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.vsel_reg = LP8788_DLDO9_VOUT,
		.vsel_mask = LP8788_VOUT_5BIT_M,
		.enable_reg = LP8788_EN_LDO_B,
		.enable_mask = LP8788_EN_DLDO9_M,
	},
	{
		.name = "dldo10",
		.id = DLDO10,
		.ops = &lp8788_ldo_voltage_table_ops,
		.n_voltages = ARRAY_SIZE(lp8788_dldo1011_vtbl),
		.volt_table = lp8788_dldo1011_vtbl,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.vsel_reg = LP8788_DLDO10_VOUT,
		.vsel_mask = LP8788_VOUT_4BIT_M,
		.enable_reg = LP8788_EN_LDO_B,
		.enable_mask = LP8788_EN_DLDO10_M,
	},
	{
		.name = "dldo11",
		.id = DLDO11,
		.ops = &lp8788_ldo_voltage_table_ops,
		.n_voltages = ARRAY_SIZE(lp8788_dldo1011_vtbl),
		.volt_table = lp8788_dldo1011_vtbl,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.vsel_reg = LP8788_DLDO11_VOUT,
		.vsel_mask = LP8788_VOUT_4BIT_M,
		.enable_reg = LP8788_EN_LDO_B,
		.enable_mask = LP8788_EN_DLDO11_M,
	},
	{
		.name = "dldo12",
		.id = DLDO12,
		.ops = &lp8788_ldo_voltage_fixed_ops,
		.n_voltages = 1,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.enable_reg = LP8788_EN_LDO_B,
		.enable_mask = LP8788_EN_DLDO12_M,
		.min_uV = 2500000,
	},
};

static const struct regulator_desc lp8788_aldo_desc[] = {
	{
		.name = "aldo1",
		.id = ALDO1,
		.ops = &lp8788_ldo_voltage_table_ops,
		.n_voltages = ARRAY_SIZE(lp8788_aldo1_vtbl),
		.volt_table = lp8788_aldo1_vtbl,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.vsel_reg = LP8788_ALDO1_VOUT,
		.vsel_mask = LP8788_VOUT_1BIT_M,
		.enable_reg = LP8788_EN_LDO_B,
		.enable_mask = LP8788_EN_ALDO1_M,
	},
	{
		.name = "aldo2",
		.id = ALDO2,
		.ops = &lp8788_ldo_voltage_fixed_ops,
		.n_voltages = 1,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.enable_reg = LP8788_EN_LDO_B,
		.enable_mask = LP8788_EN_ALDO2_M,
		.min_uV = 2850000,
	},
	{
		.name = "aldo3",
		.id = ALDO3,
		.ops = &lp8788_ldo_voltage_fixed_ops,
		.n_voltages = 1,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.enable_reg = LP8788_EN_LDO_B,
		.enable_mask = LP8788_EN_ALDO3_M,
		.min_uV = 2850000,
	},
	{
		.name = "aldo4",
		.id = ALDO4,
		.ops = &lp8788_ldo_voltage_fixed_ops,
		.n_voltages = 1,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.enable_reg = LP8788_EN_LDO_B,
		.enable_mask = LP8788_EN_ALDO4_M,
		.min_uV = 2850000,
	},
	{
		.name = "aldo5",
		.id = ALDO5,
		.ops = &lp8788_ldo_voltage_fixed_ops,
		.n_voltages = 1,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.enable_reg = LP8788_EN_LDO_C,
		.enable_mask = LP8788_EN_ALDO5_M,
		.min_uV = 2850000,
	},
	{
		.name = "aldo6",
		.id = ALDO6,
		.ops = &lp8788_ldo_voltage_table_ops,
		.n_voltages = ARRAY_SIZE(lp8788_dldo578_aldo6_vtbl),
		.volt_table = lp8788_dldo578_aldo6_vtbl,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.vsel_reg = LP8788_ALDO6_VOUT,
		.vsel_mask = LP8788_VOUT_4BIT_M,
		.enable_reg = LP8788_EN_LDO_C,
		.enable_mask = LP8788_EN_ALDO6_M,
	},
	{
		.name = "aldo7",
		.id = ALDO7,
		.ops = &lp8788_ldo_voltage_table_ops,
		.n_voltages = ARRAY_SIZE(lp8788_aldo7_vtbl),
		.volt_table = lp8788_aldo7_vtbl,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.vsel_reg = LP8788_ALDO7_VOUT,
		.vsel_mask = LP8788_VOUT_3BIT_M,
		.enable_reg = LP8788_EN_LDO_C,
		.enable_mask = LP8788_EN_ALDO7_M,
	},
	{
		.name = "aldo8",
		.id = ALDO8,
		.ops = &lp8788_ldo_voltage_fixed_ops,
		.n_voltages = 1,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.enable_reg = LP8788_EN_LDO_C,
		.enable_mask = LP8788_EN_ALDO8_M,
		.min_uV = 2500000,
	},
	{
		.name = "aldo9",
		.id = ALDO9,
		.ops = &lp8788_ldo_voltage_fixed_ops,
		.n_voltages = 1,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.enable_reg = LP8788_EN_LDO_C,
		.enable_mask = LP8788_EN_ALDO9_M,
		.min_uV = 2500000,
	},
	{
		.name = "aldo10",
		.id = ALDO10,
		.ops = &lp8788_ldo_voltage_fixed_ops,
		.n_voltages = 1,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.enable_reg = LP8788_EN_LDO_C,
		.enable_mask = LP8788_EN_ALDO10_M,
		.min_uV = 1100000,
	},
};

static int lp8788_config_ldo_enable_mode(struct platform_device *pdev,
					struct lp8788_ldo *ldo,
					enum lp8788_ldo_id id)
{
	struct lp8788 *lp = ldo->lp;
	enum lp8788_ext_ldo_en_id enable_id;
	static const u8 en_mask[] = {
		[EN_ALDO1]   = LP8788_EN_SEL_ALDO1_M,
		[EN_ALDO234] = LP8788_EN_SEL_ALDO234_M,
		[EN_ALDO5]   = LP8788_EN_SEL_ALDO5_M,
		[EN_ALDO7]   = LP8788_EN_SEL_ALDO7_M,
		[EN_DLDO7]   = LP8788_EN_SEL_DLDO7_M,
		[EN_DLDO911] = LP8788_EN_SEL_DLDO911_M,
	};

	switch (id) {
	case DLDO7:
		enable_id = EN_DLDO7;
		break;
	case DLDO9:
	case DLDO11:
		enable_id = EN_DLDO911;
		break;
	case ALDO1:
		enable_id = EN_ALDO1;
		break;
	case ALDO2 ... ALDO4:
		enable_id = EN_ALDO234;
		break;
	case ALDO5:
		enable_id = EN_ALDO5;
		break;
	case ALDO7:
		enable_id = EN_ALDO7;
		break;
	default:
		return 0;
	}

	/*
	 * Do not use devm* here: the regulator core takes over the
	 * lifecycle management of the GPIO descriptor.
	 * FIXME: check default mode for GPIO here: high or low?
	 */
	ldo->ena_gpiod = gpiod_get_index_optional(&pdev->dev,
					       "enable",
					       enable_id,
					       GPIOD_OUT_HIGH |
					       GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(ldo->ena_gpiod))
		return PTR_ERR(ldo->ena_gpiod);

	/* if no GPIO for ldo pin, then set default enable mode */
	if (!ldo->ena_gpiod)
		goto set_default_ldo_enable_mode;

	return 0;

set_default_ldo_enable_mode:
	return lp8788_update_bits(lp, LP8788_EN_SEL, en_mask[enable_id], 0);
}

static int lp8788_dldo_probe(struct platform_device *pdev)
{
	struct lp8788 *lp = dev_get_drvdata(pdev->dev.parent);
	int id = pdev->id;
	struct lp8788_ldo *ldo;
	struct regulator_config cfg = { };
	struct regulator_dev *rdev;
	int ret;

	ldo = devm_kzalloc(&pdev->dev, sizeof(struct lp8788_ldo), GFP_KERNEL);
	if (!ldo)
		return -ENOMEM;

	ldo->lp = lp;
	ret = lp8788_config_ldo_enable_mode(pdev, ldo, id);
	if (ret)
		return ret;

	if (ldo->ena_gpiod)
		cfg.ena_gpiod = ldo->ena_gpiod;

	cfg.dev = pdev->dev.parent;
	cfg.init_data = lp->pdata ? lp->pdata->dldo_data[id] : NULL;
	cfg.driver_data = ldo;
	cfg.regmap = lp->regmap;

	rdev = devm_regulator_register(&pdev->dev, &lp8788_dldo_desc[id], &cfg);
	if (IS_ERR(rdev)) {
		ret = PTR_ERR(rdev);
		dev_err(&pdev->dev, "DLDO%d regulator register err = %d\n",
				id + 1, ret);
		return ret;
	}

	ldo->regulator = rdev;
	platform_set_drvdata(pdev, ldo);

	return 0;
}

static struct platform_driver lp8788_dldo_driver = {
	.probe = lp8788_dldo_probe,
	.driver = {
		.name = LP8788_DEV_DLDO,
	},
};

static int lp8788_aldo_probe(struct platform_device *pdev)
{
	struct lp8788 *lp = dev_get_drvdata(pdev->dev.parent);
	int id = pdev->id;
	struct lp8788_ldo *ldo;
	struct regulator_config cfg = { };
	struct regulator_dev *rdev;
	int ret;

	ldo = devm_kzalloc(&pdev->dev, sizeof(struct lp8788_ldo), GFP_KERNEL);
	if (!ldo)
		return -ENOMEM;

	ldo->lp = lp;
	ret = lp8788_config_ldo_enable_mode(pdev, ldo, id + ALDO1);
	if (ret)
		return ret;

	if (ldo->ena_gpiod)
		cfg.ena_gpiod = ldo->ena_gpiod;

	cfg.dev = pdev->dev.parent;
	cfg.init_data = lp->pdata ? lp->pdata->aldo_data[id] : NULL;
	cfg.driver_data = ldo;
	cfg.regmap = lp->regmap;

	rdev = devm_regulator_register(&pdev->dev, &lp8788_aldo_desc[id], &cfg);
	if (IS_ERR(rdev)) {
		ret = PTR_ERR(rdev);
		dev_err(&pdev->dev, "ALDO%d regulator register err = %d\n",
				id + 1, ret);
		return ret;
	}

	ldo->regulator = rdev;
	platform_set_drvdata(pdev, ldo);

	return 0;
}

static struct platform_driver lp8788_aldo_driver = {
	.probe = lp8788_aldo_probe,
	.driver = {
		.name = LP8788_DEV_ALDO,
	},
};

static struct platform_driver * const drivers[] = {
	&lp8788_dldo_driver,
	&lp8788_aldo_driver,
};

static int __init lp8788_ldo_init(void)
{
	return platform_register_drivers(drivers, ARRAY_SIZE(drivers));
}
subsys_initcall(lp8788_ldo_init);

static void __exit lp8788_ldo_exit(void)
{
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
}
module_exit(lp8788_ldo_exit);

MODULE_DESCRIPTION("TI LP8788 LDO Driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:lp8788-dldo");
MODULE_ALIAS("platform:lp8788-aldo");

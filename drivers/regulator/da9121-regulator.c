// SPDX-License-Identifier: GPL-2.0-only
//
// DA9121 Single-channel dual-phase 10A buck converter
//
// Copyright (C) 2020 Axis Communications AB
//
// DA9130 Single-channel dual-phase 10A buck converter (Automotive)
// DA9217 Single-channel dual-phase  6A buck converter
// DA9122 Dual-channel single-phase  5A buck converter
// DA9131 Dual-channel single-phase  5A buck converter (Automotive)
// DA9220 Dual-channel single-phase  3A buck converter
// DA9132 Dual-channel single-phase  3A buck converter (Automotive)
//
// Copyright (C) 2020 Dialog Semiconductor

#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/regulator/da9121.h>

#include "da9121-regulator.h"

/* Chip data */
struct da9121 {
	struct device *dev;
	struct regmap *regmap;
	int variant_id;
};

static const struct regulator_ops da9121_buck_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
};

#define DA9121_MIN_MV		300
#define DA9121_MAX_MV		1900
#define DA9121_STEP_MV		10
#define DA9121_MIN_SEL		(DA9121_MIN_MV / DA9121_STEP_MV)
#define DA9121_N_VOLTAGES	(((DA9121_MAX_MV - DA9121_MIN_MV) / DA9121_STEP_MV) \
				 + 1 + DA9121_MIN_SEL)

static const struct regulator_desc da9121_reg = {
	.name = "da9121",
	.of_match = "buck1",
	.owner = THIS_MODULE,
	.ops = &da9121_buck_ops,
	.type = REGULATOR_VOLTAGE,
	.n_voltages = DA9121_N_VOLTAGES,
	.min_uV = DA9121_MIN_MV * 1000,
	.uV_step = DA9121_STEP_MV * 1000,
	.linear_min_sel = DA9121_MIN_SEL,
	.vsel_reg = DA9121_REG_BUCK_BUCK1_5,
	.vsel_mask = DA9121_MASK_BUCK_BUCKx_5_CHx_A_VOUT,
	.enable_reg = DA9121_REG_BUCK_BUCK1_0,
	.enable_mask = DA9121_MASK_BUCK_BUCKx_0_CHx_EN,
	/* Default value of BUCK_BUCK1_0.CH1_SRC_DVC_UP */
	.ramp_delay = 20000,
	/* tBUCK_EN */
	.enable_time = 20,
};

/* DA9121 chip register model */
static const struct regmap_range da9121_1ch_readable_ranges[] = {
	regmap_reg_range(DA9121_REG_SYS_STATUS_0, DA9121_REG_SYS_MASK_3),
	regmap_reg_range(DA9121_REG_SYS_CONFIG_2, DA9121_REG_SYS_CONFIG_3),
	regmap_reg_range(DA9121_REG_SYS_GPIO0_0, DA9121_REG_SYS_GPIO2_1),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_0, DA9121_REG_BUCK_BUCK1_6),
	regmap_reg_range(DA9121_REG_OTP_DEVICE_ID, DA9121_REG_OTP_CONFIG_ID),
};

static const struct regmap_access_table da9121_1ch_readable_table = {
	.yes_ranges = da9121_1ch_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9121_1ch_readable_ranges),
};

static const struct regmap_range da9121_2ch_readable_ranges[] = {
	regmap_reg_range(DA9121_REG_SYS_STATUS_0, DA9121_REG_SYS_MASK_3),
	regmap_reg_range(DA9121_REG_SYS_CONFIG_2, DA9121_REG_SYS_CONFIG_3),
	regmap_reg_range(DA9121_REG_SYS_GPIO0_0, DA9121_REG_SYS_GPIO2_1),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_0, DA9121_REG_BUCK_BUCK1_7),
	regmap_reg_range(DA9xxx_REG_BUCK_BUCK2_0, DA9xxx_REG_BUCK_BUCK2_7),
	regmap_reg_range(DA9121_REG_OTP_DEVICE_ID, DA9121_REG_OTP_CONFIG_ID),
};

static const struct regmap_access_table da9121_2ch_readable_table = {
	.yes_ranges = da9121_2ch_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9121_2ch_readable_ranges),
};

static const struct regmap_range da9121_1ch_writeable_ranges[] = {
	regmap_reg_range(DA9121_REG_SYS_EVENT_0, DA9121_REG_SYS_MASK_3),
	regmap_reg_range(DA9121_REG_SYS_CONFIG_2, DA9121_REG_SYS_CONFIG_3),
	regmap_reg_range(DA9121_REG_SYS_GPIO0_0, DA9121_REG_SYS_GPIO2_1),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_0, DA9121_REG_BUCK_BUCK1_2),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_4, DA9121_REG_BUCK_BUCK1_6),
};

static const struct regmap_access_table da9121_1ch_writeable_table = {
	.yes_ranges = da9121_1ch_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9121_1ch_writeable_ranges),
};

static const struct regmap_range da9121_2ch_writeable_ranges[] = {
	regmap_reg_range(DA9121_REG_SYS_EVENT_0, DA9121_REG_SYS_MASK_3),
	regmap_reg_range(DA9121_REG_SYS_CONFIG_2, DA9121_REG_SYS_CONFIG_3),
	regmap_reg_range(DA9121_REG_SYS_GPIO0_0, DA9121_REG_SYS_GPIO2_1),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_0, DA9121_REG_BUCK_BUCK1_2),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_4, DA9121_REG_BUCK_BUCK1_7),
	regmap_reg_range(DA9xxx_REG_BUCK_BUCK2_0, DA9xxx_REG_BUCK_BUCK2_2),
	regmap_reg_range(DA9xxx_REG_BUCK_BUCK2_4, DA9xxx_REG_BUCK_BUCK2_7),
};

static const struct regmap_access_table da9121_2ch_writeable_table = {
	.yes_ranges = da9121_2ch_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9121_2ch_writeable_ranges),
};


static const struct regmap_range da9121_volatile_ranges[] = {
	regmap_reg_range(DA9121_REG_SYS_STATUS_0, DA9121_REG_SYS_EVENT_2),
	regmap_reg_range(DA9121_REG_SYS_GPIO0_0, DA9121_REG_SYS_GPIO2_1),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_0, DA9121_REG_BUCK_BUCK1_6),
};

static const struct regmap_access_table da9121_volatile_table = {
	.yes_ranges = da9121_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9121_volatile_ranges),
};

/* DA9121 regmap config for 1 channel variants */
static struct regmap_config da9121_1ch_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = DA9121_REG_OTP_CONFIG_ID,
	.rd_table = &da9121_1ch_readable_table,
	.wr_table = &da9121_1ch_writeable_table,
	.volatile_table = &da9121_volatile_table,
	.cache_type = REGCACHE_RBTREE,
};

/* DA9121 regmap config for 2 channel variants */
static struct regmap_config da9121_2ch_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = DA9121_REG_OTP_CONFIG_ID,
	.rd_table = &da9121_2ch_readable_table,
	.wr_table = &da9121_2ch_writeable_table,
	.volatile_table = &da9121_volatile_table,
	.cache_type = REGCACHE_RBTREE,
};

static int da9121_check_device_type(struct i2c_client *i2c, struct da9121 *chip)
{
	u32 device_id;
	u8 chip_id = chip->variant_id;
	u32 variant_id;
	u8 variant_mrc, variant_vrc;
	char *type;
	const char *name;
	bool config_match = false;
	int ret = 0;

	ret = regmap_read(chip->regmap, DA9121_REG_OTP_DEVICE_ID, &device_id);
	if (ret < 0) {
		dev_err(chip->dev, "Cannot read device ID: %d\n", ret);
		goto error;
	}

	ret = regmap_read(chip->regmap, DA9121_REG_OTP_VARIANT_ID, &variant_id);
	if (ret < 0) {
		dev_err(chip->dev, "Cannot read variant ID: %d\n", ret);
		goto error;
	}

	if (device_id != DA9121_DEVICE_ID) {
		dev_err(chip->dev, "Invalid device ID: 0x%02x\n", device_id);
		ret = -ENODEV;
		goto error;
	}

	variant_vrc = variant_id & DA9121_MASK_OTP_VARIANT_ID_VRC;

	switch (variant_vrc) {
	case DA9121_VARIANT_VRC:
		type = "DA9121/DA9130";
		config_match = (chip_id == DA9121_TYPE_DA9121_DA9130);
		break;
	case DA9220_VARIANT_VRC:
		type = "DA9220/DA9132";
		config_match = (chip_id == DA9121_TYPE_DA9220_DA9132);
		break;
	case DA9122_VARIANT_VRC:
		type = "DA9122/DA9131";
		config_match = (chip_id == DA9121_TYPE_DA9122_DA9131);
		break;
	case DA9217_VARIANT_VRC:
		type = "DA9217";
		config_match = (chip_id == DA9121_TYPE_DA9217);
		break;
	default:
		type = "Unknown";
		break;
	}

	dev_info(chip->dev,
		 "Device detected (device-ID: 0x%02X, var-ID: 0x%02X, %s)\n",
		 device_id, variant_id, type);

	if (!config_match) {
		dev_err(chip->dev, "Device tree configuration '%s' does not match detected device.\n", name);
		ret = -EINVAL;
		goto error;
	}

	variant_mrc = (variant_id & DA9121_MASK_OTP_VARIANT_ID_MRC)
			>> DA9121_SHIFT_OTP_VARIANT_ID_MRC;

	if ((device_id == DA9121_DEVICE_ID) &&
	    (variant_mrc < DA9121_VARIANT_MRC_BASE)) {
		dev_err(chip->dev,
			"Cannot support variant MRC: 0x%02X\n", variant_mrc);
		ret = -EINVAL;
	}
error:
	return ret;
}

static int da9121_assign_chip_model(struct i2c_client *i2c,
			struct da9121 *chip)
{
	struct regmap_config *regmap;
	int ret = 0;

	chip->dev = &i2c->dev;

	switch (chip->variant_id) {
	case DA9121_TYPE_DA9121_DA9130:
		fallthrough;
	case DA9121_TYPE_DA9217:
		regmap = &da9121_1ch_regmap_config;
		break;
	case DA9121_TYPE_DA9122_DA9131:
		fallthrough;
	case DA9121_TYPE_DA9220_DA9132:
		regmap = &da9121_2ch_regmap_config;
		break;
	}

	chip->regmap = devm_regmap_init_i2c(i2c, regmap);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		dev_err(chip->dev, "Failed to configure a register map: %d\n",
			ret);
	}

	ret = da9121_check_device_type(i2c, chip);

	return ret;
}

static const struct of_device_id da9121_dt_ids[] = {
	{ .compatible = "dlg,da9121", .data = (void *) DA9121_TYPE_DA9121_DA9130 },
	{ .compatible = "dlg,da9130", .data = (void *) DA9121_TYPE_DA9121_DA9130 },
	{ .compatible = "dlg,da9217", .data = (void *) DA9121_TYPE_DA9217 },
	{ .compatible = "dlg,da9122", .data = (void *) DA9121_TYPE_DA9122_DA9131 },
	{ .compatible = "dlg,da9131", .data = (void *) DA9121_TYPE_DA9122_DA9131 },
	{ .compatible = "dlg,da9220", .data = (void *) DA9121_TYPE_DA9220_DA9132 },
	{ .compatible = "dlg,da9132", .data = (void *) DA9121_TYPE_DA9220_DA9132 },
	{ }
};
MODULE_DEVICE_TABLE(of, da9121_dt_ids);

static inline int da9121_of_get_id(struct device *dev)
{
	const struct of_device_id *id = of_match_device(da9121_dt_ids, dev);

	if (!id) {
		dev_err(dev, "%s: Failed\n", __func__);
		return -EINVAL;
	}
	return (uintptr_t)id->data;
}

static int da9121_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct da9121 *chip;
	int ret = 0;
	struct device *dev = &i2c->dev;
	struct regulator_config config = {};
	struct regulator_dev *rdev;

	chip = devm_kzalloc(&i2c->dev, sizeof(struct da9121), GFP_KERNEL);
	if (!chip) {
		ret = -ENOMEM;
		goto error;
	}

	chip->variant_id = da9121_of_get_id(&i2c->dev);

	ret = da9121_assign_chip_model(i2c, chip);
	if (ret < 0)
		goto error;

	config.dev = &i2c->dev;
	config.of_node = dev->of_node;
	config.regmap = chip->regmap;

	rdev = devm_regulator_register(&i2c->dev, &da9121_reg, &config);
	if (IS_ERR(rdev)) {
		dev_err(&i2c->dev, "Failed to register da9121 regulator\n");
		return PTR_ERR(rdev);
	}

error:
	return ret;
}

static const struct i2c_device_id da9121_i2c_id[] = {
	{"da9121", DA9121_TYPE_DA9121_DA9130},
	{"da9130", DA9121_TYPE_DA9121_DA9130},
	{"da9217", DA9121_TYPE_DA9217},
	{"da9122", DA9121_TYPE_DA9122_DA9131},
	{"da9131", DA9121_TYPE_DA9122_DA9131},
	{"da9220", DA9121_TYPE_DA9220_DA9132},
	{"da9132", DA9121_TYPE_DA9220_DA9132},
	{},
};
MODULE_DEVICE_TABLE(i2c, da9121_i2c_id);

static struct i2c_driver da9121_regulator_driver = {
	.driver = {
		.name = "da9121",
		.of_match_table = of_match_ptr(da9121_dt_ids),
	},
	.probe = da9121_i2c_probe,
	.id_table = da9121_i2c_id,
};

module_i2c_driver(da9121_regulator_driver);

MODULE_LICENSE("GPL v2");

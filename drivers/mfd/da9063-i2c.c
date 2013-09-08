/* da9063-i2c.c: Interrupt support for Dialog DA9063
 *
 * Copyright 2012 Dialog Semiconductor Ltd.
 * Copyright 2013 Philipp Zabel, Pengutronix
 *
 * Author: Krystian Garbaciak <krystian.garbaciak@diasemi.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/err.h>

#include <linux/mfd/core.h>
#include <linux/mfd/da9063/core.h>
#include <linux/mfd/da9063/pdata.h>
#include <linux/mfd/da9063/registers.h>

static const struct regmap_range da9063_readable_ranges[] = {
	{
		.range_min = DA9063_REG_PAGE_CON,
		.range_max = DA9063_REG_SECOND_D,
	}, {
		.range_min = DA9063_REG_SEQ,
		.range_max = DA9063_REG_ID_32_31,
	}, {
		.range_min = DA9063_REG_SEQ_A,
		.range_max = DA9063_REG_AUTO3_LOW,
	}, {
		.range_min = DA9063_REG_T_OFFSET,
		.range_max = DA9063_REG_GP_ID_19,
	}, {
		.range_min = DA9063_REG_CHIP_ID,
		.range_max = DA9063_REG_CHIP_VARIANT,
	},
};

static const struct regmap_range da9063_writeable_ranges[] = {
	{
		.range_min = DA9063_REG_PAGE_CON,
		.range_max = DA9063_REG_PAGE_CON,
	}, {
		.range_min = DA9063_REG_FAULT_LOG,
		.range_max = DA9063_REG_VSYS_MON,
	}, {
		.range_min = DA9063_REG_COUNT_S,
		.range_max = DA9063_REG_ALARM_Y,
	}, {
		.range_min = DA9063_REG_SEQ,
		.range_max = DA9063_REG_ID_32_31,
	}, {
		.range_min = DA9063_REG_SEQ_A,
		.range_max = DA9063_REG_AUTO3_LOW,
	}, {
		.range_min = DA9063_REG_CONFIG_I,
		.range_max = DA9063_REG_MON_REG_4,
	}, {
		.range_min = DA9063_REG_GP_ID_0,
		.range_max = DA9063_REG_GP_ID_19,
	},
};

static const struct regmap_range da9063_volatile_ranges[] = {
	{
		.range_min = DA9063_REG_STATUS_A,
		.range_max = DA9063_REG_EVENT_D,
	}, {
		.range_min = DA9063_REG_CONTROL_F,
		.range_max = DA9063_REG_CONTROL_F,
	}, {
		.range_min = DA9063_REG_ADC_MAN,
		.range_max = DA9063_REG_ADC_MAN,
	}, {
		.range_min = DA9063_REG_ADC_RES_L,
		.range_max = DA9063_REG_SECOND_D,
	}, {
		.range_min = DA9063_REG_MON_REG_5,
		.range_max = DA9063_REG_MON_REG_6,
	},
};

static const struct regmap_access_table da9063_readable_table = {
	.yes_ranges = da9063_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063_readable_ranges),
};

static const struct regmap_access_table da9063_writeable_table = {
	.yes_ranges = da9063_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063_writeable_ranges),
};

static const struct regmap_access_table da9063_volatile_table = {
	.yes_ranges = da9063_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063_volatile_ranges),
};

static const struct regmap_range_cfg da9063_range_cfg[] = {
	{
		.range_min = DA9063_REG_PAGE_CON,
		.range_max = DA9063_REG_CHIP_VARIANT,
		.selector_reg = DA9063_REG_PAGE_CON,
		.selector_mask = 1 << DA9063_I2C_PAGE_SEL_SHIFT,
		.selector_shift = DA9063_I2C_PAGE_SEL_SHIFT,
		.window_start = 0,
		.window_len = 256,
	}
};

static struct regmap_config da9063_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.ranges = da9063_range_cfg,
	.num_ranges = ARRAY_SIZE(da9063_range_cfg),
	.max_register = DA9063_REG_CHIP_VARIANT,

	.cache_type = REGCACHE_RBTREE,

	.rd_table = &da9063_readable_table,
	.wr_table = &da9063_writeable_table,
	.volatile_table = &da9063_volatile_table,
};

static int da9063_i2c_probe(struct i2c_client *i2c,
	const struct i2c_device_id *id)
{
	struct da9063 *da9063;
	int ret;

	da9063 = devm_kzalloc(&i2c->dev, sizeof(struct da9063), GFP_KERNEL);
	if (da9063 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, da9063);
	da9063->dev = &i2c->dev;
	da9063->chip_irq = i2c->irq;

	da9063->regmap = devm_regmap_init_i2c(i2c, &da9063_regmap_config);
	if (IS_ERR(da9063->regmap)) {
		ret = PTR_ERR(da9063->regmap);
		dev_err(da9063->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	return da9063_device_init(da9063, i2c->irq);
}

static int da9063_i2c_remove(struct i2c_client *i2c)
{
	struct da9063 *da9063 = i2c_get_clientdata(i2c);

	da9063_device_exit(da9063);

	return 0;
}

static const struct i2c_device_id da9063_i2c_id[] = {
	{"da9063", PMIC_DA9063},
	{},
};
MODULE_DEVICE_TABLE(i2c, da9063_i2c_id);

static struct i2c_driver da9063_i2c_driver = {
	.driver = {
		.name = "da9063",
		.owner = THIS_MODULE,
	},
	.probe    = da9063_i2c_probe,
	.remove   = da9063_i2c_remove,
	.id_table = da9063_i2c_id,
};

module_i2c_driver(da9063_i2c_driver);

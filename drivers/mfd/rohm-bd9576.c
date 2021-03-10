// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021 ROHM Semiconductors
 *
 * ROHM BD9576MUF and BD9573MUF PMIC driver
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rohm-bd957x.h>
#include <linux/mfd/rohm-generic.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

static struct mfd_cell bd9573_mfd_cells[] = {
	{ .name = "bd9573-regulator", },
	{ .name = "bd9576-wdt", },
};

static struct mfd_cell bd9576_mfd_cells[] = {
	{ .name = "bd9576-regulator", },
	{ .name = "bd9576-wdt", },
};

static const struct regmap_range volatile_ranges[] = {
	regmap_reg_range(BD957X_REG_SMRB_ASSERT, BD957X_REG_SMRB_ASSERT),
	regmap_reg_range(BD957X_REG_PMIC_INTERNAL_STAT,
			 BD957X_REG_PMIC_INTERNAL_STAT),
	regmap_reg_range(BD957X_REG_INT_THERM_STAT, BD957X_REG_INT_THERM_STAT),
	regmap_reg_range(BD957X_REG_INT_OVP_STAT, BD957X_REG_INT_SYS_STAT),
	regmap_reg_range(BD957X_REG_INT_MAIN_STAT, BD957X_REG_INT_MAIN_STAT),
};

static const struct regmap_access_table volatile_regs = {
	.yes_ranges = &volatile_ranges[0],
	.n_yes_ranges = ARRAY_SIZE(volatile_ranges),
};

static struct regmap_config bd957x_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &volatile_regs,
	.max_register = BD957X_MAX_REGISTER,
	.cache_type = REGCACHE_RBTREE,
};

static int bd957x_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	int ret;
	struct regmap *regmap;
	struct mfd_cell *cells;
	int num_cells;
	unsigned long chip_type;

	chip_type = (unsigned long)of_device_get_match_data(&i2c->dev);

	switch (chip_type) {
	case ROHM_CHIP_TYPE_BD9576:
		cells = bd9576_mfd_cells;
		num_cells = ARRAY_SIZE(bd9576_mfd_cells);
		break;
	case ROHM_CHIP_TYPE_BD9573:
		cells = bd9573_mfd_cells;
		num_cells = ARRAY_SIZE(bd9573_mfd_cells);
		break;
	default:
		dev_err(&i2c->dev, "Unknown device type");
		return -EINVAL;
	}

	regmap = devm_regmap_init_i2c(i2c, &bd957x_regmap);
	if (IS_ERR(regmap)) {
		dev_err(&i2c->dev, "Failed to initialize Regmap\n");
		return PTR_ERR(regmap);
	}

	ret = devm_mfd_add_devices(&i2c->dev, PLATFORM_DEVID_AUTO, cells,
				   num_cells, NULL, 0, NULL);
	if (ret)
		dev_err(&i2c->dev, "Failed to create subdevices\n");

	return ret;
}

static const struct of_device_id bd957x_of_match[] = {
	{ .compatible = "rohm,bd9576", .data = (void *)ROHM_CHIP_TYPE_BD9576, },
	{ .compatible = "rohm,bd9573", .data = (void *)ROHM_CHIP_TYPE_BD9573, },
	{ },
};
MODULE_DEVICE_TABLE(of, bd957x_of_match);

static struct i2c_driver bd957x_drv = {
	.driver = {
		.name = "rohm-bd957x",
		.of_match_table = bd957x_of_match,
	},
	.probe = &bd957x_i2c_probe,
};
module_i2c_driver(bd957x_drv);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("ROHM BD9576MUF and BD9573MUF Power Management IC driver");
MODULE_LICENSE("GPL");

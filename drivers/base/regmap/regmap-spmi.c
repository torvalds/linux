/*
 * Register map access API - SPMI support
 *
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * Based on regmap-i2c.c:
 * Copyright 2011 Wolfson Microelectronics plc
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/regmap.h>
#include <linux/spmi.h>
#include <linux/module.h>
#include <linux/init.h>

static int regmap_spmi_read(void *context,
			    const void *reg, size_t reg_size,
			    void *val, size_t val_size)
{
	BUG_ON(reg_size != 2);
	return spmi_ext_register_readl(context, *(u16 *)reg,
				       val, val_size);
}

static int regmap_spmi_gather_write(void *context,
				    const void *reg, size_t reg_size,
				    const void *val, size_t val_size)
{
	BUG_ON(reg_size != 2);
	return spmi_ext_register_writel(context, *(u16 *)reg, val, val_size);
}

static int regmap_spmi_write(void *context, const void *data,
			     size_t count)
{
	BUG_ON(count < 2);
	return regmap_spmi_gather_write(context, data, 2, data + 2, count - 2);
}

static struct regmap_bus regmap_spmi = {
	.read				= regmap_spmi_read,
	.write				= regmap_spmi_write,
	.gather_write			= regmap_spmi_gather_write,
	.reg_format_endian_default	= REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default	= REGMAP_ENDIAN_NATIVE,
};

/**
 * regmap_init_spmi(): Initialize register map
 *
 * @sdev: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
struct regmap *regmap_init_spmi(struct spmi_device *sdev,
				const struct regmap_config *config)
{
	return regmap_init(&sdev->dev, &regmap_spmi, sdev, config);
}
EXPORT_SYMBOL_GPL(regmap_init_spmi);

/**
 * devm_regmap_init_spmi(): Initialise managed register map
 *
 * @sdev: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  The regmap will be automatically freed by the
 * device management code.
 */
struct regmap *devm_regmap_init_spmi(struct spmi_device *sdev,
				     const struct regmap_config *config)
{
	return devm_regmap_init(&sdev->dev, &regmap_spmi, sdev, config);
}
EXPORT_SYMBOL_GPL(devm_regmap_init_spmi);

MODULE_LICENSE("GPL");

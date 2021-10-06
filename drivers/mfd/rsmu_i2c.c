// SPDX-License-Identifier: GPL-2.0+
/*
 * I2C driver for Renesas Synchronization Management Unit (SMU) devices.
 *
 * Copyright (C) 2021 Integrated Device Technology, Inc., a Renesas Company.
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rsmu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "rsmu.h"

/*
 * 16-bit register address: the lower 8 bits of the register address come
 * from the offset addr byte and the upper 8 bits come from the page register.
 */
#define	RSMU_CM_PAGE_ADDR		0xFD
#define	RSMU_CM_PAGE_WINDOW		256

/*
 * 15-bit register address: the lower 7 bits of the register address come
 * from the offset addr byte and the upper 8 bits come from the page register.
 */
#define	RSMU_SABRE_PAGE_ADDR		0x7F
#define	RSMU_SABRE_PAGE_WINDOW		128

static const struct regmap_range_cfg rsmu_cm_range_cfg[] = {
	{
		.range_min = 0,
		.range_max = 0xD000,
		.selector_reg = RSMU_CM_PAGE_ADDR,
		.selector_mask = 0xFF,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = RSMU_CM_PAGE_WINDOW,
	}
};

static const struct regmap_range_cfg rsmu_sabre_range_cfg[] = {
	{
		.range_min = 0,
		.range_max = 0x400,
		.selector_reg = RSMU_SABRE_PAGE_ADDR,
		.selector_mask = 0xFF,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = RSMU_SABRE_PAGE_WINDOW,
	}
};

static bool rsmu_cm_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RSMU_CM_PAGE_ADDR:
		return false;
	default:
		return true;
	}
}

static bool rsmu_sabre_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RSMU_SABRE_PAGE_ADDR:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config rsmu_cm_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xD000,
	.ranges = rsmu_cm_range_cfg,
	.num_ranges = ARRAY_SIZE(rsmu_cm_range_cfg),
	.volatile_reg = rsmu_cm_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
	.can_multi_write = true,
};

static const struct regmap_config rsmu_sabre_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x400,
	.ranges = rsmu_sabre_range_cfg,
	.num_ranges = ARRAY_SIZE(rsmu_sabre_range_cfg),
	.volatile_reg = rsmu_sabre_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
	.can_multi_write = true,
};

static const struct regmap_config rsmu_sl_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = 0x339,
	.cache_type = REGCACHE_NONE,
	.can_multi_write = true,
};

static int rsmu_i2c_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	const struct regmap_config *cfg;
	struct rsmu_ddata *rsmu;
	int ret;

	rsmu = devm_kzalloc(&client->dev, sizeof(*rsmu), GFP_KERNEL);
	if (!rsmu)
		return -ENOMEM;

	i2c_set_clientdata(client, rsmu);

	rsmu->dev = &client->dev;
	rsmu->type = (enum rsmu_type)id->driver_data;

	switch (rsmu->type) {
	case RSMU_CM:
		cfg = &rsmu_cm_regmap_config;
		break;
	case RSMU_SABRE:
		cfg = &rsmu_sabre_regmap_config;
		break;
	case RSMU_SL:
		cfg = &rsmu_sl_regmap_config;
		break;
	default:
		dev_err(rsmu->dev, "Unsupported RSMU device type: %d\n", rsmu->type);
		return -ENODEV;
	}
	rsmu->regmap = devm_regmap_init_i2c(client, cfg);
	if (IS_ERR(rsmu->regmap)) {
		ret = PTR_ERR(rsmu->regmap);
		dev_err(rsmu->dev, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	return rsmu_core_init(rsmu);
}

static int rsmu_i2c_remove(struct i2c_client *client)
{
	struct rsmu_ddata *rsmu = i2c_get_clientdata(client);

	rsmu_core_exit(rsmu);

	return 0;
}

static const struct i2c_device_id rsmu_i2c_id[] = {
	{ "8a34000",  RSMU_CM },
	{ "8a34001",  RSMU_CM },
	{ "82p33810", RSMU_SABRE },
	{ "82p33811", RSMU_SABRE },
	{ "8v19n850", RSMU_SL },
	{ "8v19n851", RSMU_SL },
	{}
};
MODULE_DEVICE_TABLE(i2c, rsmu_i2c_id);

static const struct of_device_id rsmu_i2c_of_match[] = {
	{ .compatible = "idt,8a34000",  .data = (void *)RSMU_CM },
	{ .compatible = "idt,8a34001",  .data = (void *)RSMU_CM },
	{ .compatible = "idt,82p33810", .data = (void *)RSMU_SABRE },
	{ .compatible = "idt,82p33811", .data = (void *)RSMU_SABRE },
	{ .compatible = "idt,8v19n850", .data = (void *)RSMU_SL },
	{ .compatible = "idt,8v19n851", .data = (void *)RSMU_SL },
	{}
};
MODULE_DEVICE_TABLE(of, rsmu_i2c_of_match);

static struct i2c_driver rsmu_i2c_driver = {
	.driver = {
		.name = "rsmu-i2c",
		.of_match_table = of_match_ptr(rsmu_i2c_of_match),
	},
	.probe = rsmu_i2c_probe,
	.remove	= rsmu_i2c_remove,
	.id_table = rsmu_i2c_id,
};

static int __init rsmu_i2c_init(void)
{
	return i2c_add_driver(&rsmu_i2c_driver);
}
subsys_initcall(rsmu_i2c_init);

static void __exit rsmu_i2c_exit(void)
{
	i2c_del_driver(&rsmu_i2c_driver);
}
module_exit(rsmu_i2c_exit);

MODULE_DESCRIPTION("Renesas SMU I2C driver");
MODULE_LICENSE("GPL");

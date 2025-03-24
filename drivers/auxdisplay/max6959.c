// SPDX-License-Identifier: GPL-2.0
/*
 * MAX6958/6959 7-segment LED display controller
 * Datasheet:
 * https://www.analog.com/media/en/technical-documentation/data-sheets/MAX6958-MAX6959.pdf
 *
 * Copyright (c) 2024, Intel Corporation.
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */
#include <linux/array_size.h>
#include <linux/bitrev.h>
#include <linux/bits.h>
#include <linux/container_of.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <linux/map_to_7segment.h>

#include "line-display.h"

/* Registers */
#define REG_DECODE_MODE			0x01
#define REG_INTENSITY			0x02
#define REG_SCAN_LIMIT			0x03
#define REG_CONFIGURATION		0x04
#define REG_CONFIGURATION_S_BIT		BIT(0)

#define REG_DIGIT(x)			(0x20 + (x))
#define REG_DIGIT0			0x20
#define REG_DIGIT1			0x21
#define REG_DIGIT2			0x22
#define REG_DIGIT3			0x23

#define REG_SEGMENTS			0x24
#define REG_MAX				REG_SEGMENTS

struct max6959_priv {
	struct linedisp linedisp;
	struct delayed_work work;
	struct regmap *regmap;
};

static void max6959_disp_update(struct work_struct *work)
{
	struct max6959_priv *priv = container_of(work, struct max6959_priv, work.work);
	struct linedisp *linedisp = &priv->linedisp;
	struct linedisp_map *map = linedisp->map;
	char *s = linedisp->buf;
	u8 buf[4];

	/* Map segments according to datasheet */
	buf[0] = bitrev8(map_to_seg7(&map->map.seg7, *s++)) >> 1;
	buf[1] = bitrev8(map_to_seg7(&map->map.seg7, *s++)) >> 1;
	buf[2] = bitrev8(map_to_seg7(&map->map.seg7, *s++)) >> 1;
	buf[3] = bitrev8(map_to_seg7(&map->map.seg7, *s++)) >> 1;

	regmap_bulk_write(priv->regmap, REG_DIGIT(0), buf, ARRAY_SIZE(buf));
}

static int max6959_linedisp_get_map_type(struct linedisp *linedisp)
{
	struct max6959_priv *priv = container_of(linedisp, struct max6959_priv, linedisp);

	INIT_DELAYED_WORK(&priv->work, max6959_disp_update);
	return LINEDISP_MAP_SEG7;
}

static void max6959_linedisp_update(struct linedisp *linedisp)
{
	struct max6959_priv *priv = container_of(linedisp, struct max6959_priv, linedisp);

	schedule_delayed_work(&priv->work, 0);
}

static const struct linedisp_ops max6959_linedisp_ops = {
	.get_map_type = max6959_linedisp_get_map_type,
	.update = max6959_linedisp_update,
};

static int max6959_enable(struct max6959_priv *priv, bool enable)
{
	u8 mask = REG_CONFIGURATION_S_BIT;
	u8 value = enable ? mask : 0;

	return regmap_update_bits(priv->regmap, REG_CONFIGURATION, mask, value);
}

static void max6959_power_off(void *priv)
{
	max6959_enable(priv, false);
}

static int max6959_power_on(struct max6959_priv *priv)
{
	struct device *dev = regmap_get_device(priv->regmap);
	int ret;

	ret = max6959_enable(priv, true);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, max6959_power_off, priv);
}

static const struct regmap_config max6959_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = REG_MAX,
	.cache_type = REGCACHE_MAPLE,
};

static int max6959_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct max6959_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = devm_regmap_init_i2c(client, &max6959_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	ret = max6959_power_on(priv);
	if (ret)
		return ret;

	ret = linedisp_register(&priv->linedisp, dev, 4, &max6959_linedisp_ops);
	if (ret)
		return ret;

	i2c_set_clientdata(client, priv);

	return 0;
}

static void max6959_i2c_remove(struct i2c_client *client)
{
	struct max6959_priv *priv = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&priv->work);
	linedisp_unregister(&priv->linedisp);
}

static int max6959_suspend(struct device *dev)
{
	return max6959_enable(dev_get_drvdata(dev), false);
}

static int max6959_resume(struct device *dev)
{
	return max6959_enable(dev_get_drvdata(dev), true);
}

static DEFINE_SIMPLE_DEV_PM_OPS(max6959_pm_ops, max6959_suspend, max6959_resume);

static const struct i2c_device_id max6959_i2c_id[] = {
	{ "max6959" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max6959_i2c_id);

static const struct of_device_id max6959_of_table[] = {
	{ .compatible = "maxim,max6959" },
	{ }
};
MODULE_DEVICE_TABLE(of, max6959_of_table);

static struct i2c_driver max6959_i2c_driver = {
	.driver = {
		.name = "max6959",
		.pm = pm_sleep_ptr(&max6959_pm_ops),
		.of_match_table = max6959_of_table,
	},
	.probe = max6959_i2c_probe,
	.remove = max6959_i2c_remove,
	.id_table = max6959_i2c_id,
};
module_i2c_driver(max6959_i2c_driver);

MODULE_DESCRIPTION("MAX6958/6959 7-segment LED controller");
MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("LINEDISP");

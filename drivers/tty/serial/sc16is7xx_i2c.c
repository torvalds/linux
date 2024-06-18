// SPDX-License-Identifier: GPL-2.0+
/* SC16IS7xx I2C interface driver */

#include <linux/dev_printk.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/string.h>

#include "sc16is7xx.h"

static int sc16is7xx_i2c_probe(struct i2c_client *i2c)
{
	const struct sc16is7xx_devtype *devtype;
	struct regmap *regmaps[SC16IS7XX_MAX_PORTS];
	struct regmap_config regcfg;
	unsigned int i;

	devtype = i2c_get_match_data(i2c);
	if (!devtype)
		return dev_err_probe(&i2c->dev, -ENODEV, "Failed to match device\n");

	memcpy(&regcfg, &sc16is7xx_regcfg, sizeof(struct regmap_config));

	for (i = 0; i < devtype->nr_uart; i++) {
		regcfg.name = sc16is7xx_regmap_name(i);
		regcfg.read_flag_mask = sc16is7xx_regmap_port_mask(i);
		regcfg.write_flag_mask = sc16is7xx_regmap_port_mask(i);
		regmaps[i] = devm_regmap_init_i2c(i2c, &regcfg);
	}

	return sc16is7xx_probe(&i2c->dev, devtype, regmaps, i2c->irq);
}

static void sc16is7xx_i2c_remove(struct i2c_client *client)
{
	sc16is7xx_remove(&client->dev);
}

static const struct i2c_device_id sc16is7xx_i2c_id_table[] = {
	{ "sc16is74x",	(kernel_ulong_t)&sc16is74x_devtype, },
	{ "sc16is740",	(kernel_ulong_t)&sc16is74x_devtype, },
	{ "sc16is741",	(kernel_ulong_t)&sc16is74x_devtype, },
	{ "sc16is750",	(kernel_ulong_t)&sc16is750_devtype, },
	{ "sc16is752",	(kernel_ulong_t)&sc16is752_devtype, },
	{ "sc16is760",	(kernel_ulong_t)&sc16is760_devtype, },
	{ "sc16is762",	(kernel_ulong_t)&sc16is762_devtype, },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sc16is7xx_i2c_id_table);

static struct i2c_driver sc16is7xx_i2c_driver = {
	.driver = {
		.name		= SC16IS7XX_NAME,
		.of_match_table	= sc16is7xx_dt_ids,
	},
	.probe		= sc16is7xx_i2c_probe,
	.remove		= sc16is7xx_i2c_remove,
	.id_table	= sc16is7xx_i2c_id_table,
};

module_i2c_driver(sc16is7xx_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SC16IS7xx I2C interface driver");
MODULE_IMPORT_NS(SERIAL_NXP_SC16IS7XX);

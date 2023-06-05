// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Pengutronix, Juergen Borleis <kernel@pengutronix.de>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>

#include "lan9303.h"

struct lan9303_i2c {
	struct i2c_client *device;
	struct lan9303 chip;
};

static const struct regmap_config lan9303_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 32,
	.reg_stride = 1,
	.can_multi_write = true,
	.max_register = 0x0ff, /* address bits 0..1 are not used */
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,

	.volatile_table = &lan9303_register_set,
	.wr_table = &lan9303_register_set,
	.rd_table = &lan9303_register_set,

	.cache_type = REGCACHE_NONE,
};

static int lan9303_i2c_probe(struct i2c_client *client)
{
	struct lan9303_i2c *sw_dev;
	int ret;

	sw_dev = devm_kzalloc(&client->dev, sizeof(struct lan9303_i2c),
			      GFP_KERNEL);
	if (!sw_dev)
		return -ENOMEM;

	sw_dev->chip.regmap = devm_regmap_init_i2c(client,
						   &lan9303_i2c_regmap_config);
	if (IS_ERR(sw_dev->chip.regmap)) {
		ret = PTR_ERR(sw_dev->chip.regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	/* link forward and backward */
	sw_dev->device = client;
	i2c_set_clientdata(client, sw_dev);
	sw_dev->chip.dev = &client->dev;

	sw_dev->chip.ops = &lan9303_indirect_phy_ops;

	ret = lan9303_probe(&sw_dev->chip, client->dev.of_node);
	if (ret != 0)
		return ret;

	dev_info(&client->dev, "LAN9303 I2C driver loaded successfully\n");

	return 0;
}

static void lan9303_i2c_remove(struct i2c_client *client)
{
	struct lan9303_i2c *sw_dev = i2c_get_clientdata(client);

	if (!sw_dev)
		return;

	lan9303_remove(&sw_dev->chip);
}

static void lan9303_i2c_shutdown(struct i2c_client *client)
{
	struct lan9303_i2c *sw_dev = i2c_get_clientdata(client);

	if (!sw_dev)
		return;

	lan9303_shutdown(&sw_dev->chip);

	i2c_set_clientdata(client, NULL);
}

/*-------------------------------------------------------------------------*/

static const struct i2c_device_id lan9303_i2c_id[] = {
	{ "lan9303", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, lan9303_i2c_id);

static const struct of_device_id lan9303_i2c_of_match[] = {
	{ .compatible = "smsc,lan9303-i2c", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, lan9303_i2c_of_match);

static struct i2c_driver lan9303_i2c_driver = {
	.driver = {
		.name = "LAN9303_I2C",
		.of_match_table = lan9303_i2c_of_match,
	},
	.probe_new = lan9303_i2c_probe,
	.remove = lan9303_i2c_remove,
	.shutdown = lan9303_i2c_shutdown,
	.id_table = lan9303_i2c_id,
};
module_i2c_driver(lan9303_i2c_driver);

MODULE_AUTHOR("Juergen Borleis <kernel@pengutronix.de>");
MODULE_DESCRIPTION("Driver for SMSC/Microchip LAN9303 three port ethernet switch in I2C managed mode");
MODULE_LICENSE("GPL v2");

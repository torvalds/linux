// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Sitronix ST7571 connected via I2C bus.
 *
 * Copyright (C) 2025 Marcus Folkesson <marcus.folkesson@gmail.com>
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "st7571.h"

struct st7571_i2c_transport {
	struct i2c_client *client;

	/*
	 * Depending on the hardware design, the acknowledge signal may be hard to
	 * recognize as a valid logic "0" level.
	 * Therefor, ignore NAK if possible to stay compatible with most hardware designs
	 * and off-the-shelf panels out there.
	 *
	 * From section 6.4 MICROPOCESSOR INTERFACE section in the datasheet:
	 *
	 * "By connecting SDA_OUT to SDA_IN externally, the SDA line becomes fully
	 * I2C interface compatible.
	 * Separating acknowledge-output from serial data
	 * input is advantageous for chip-on-glass (COG) applications. In COG
	 * applications, the ITO resistance and the pull-up resistor will form a
	 * voltage  divider, which affects acknowledge-signal level. Larger ITO
	 * resistance will raise the acknowledged-signal level and system cannot
	 * recognize this level as a valid logic “0” level. By separating SDA_IN from
	 * SDA_OUT, the IC can be used in a mode that ignores the acknowledge-bit.
	 * For applications which check acknowledge-bit, it is necessary to minimize
	 * the ITO resistance of the SDA_OUT trace to guarantee a valid low level."
	 *
	 */
	bool ignore_nak;
};

static int st7571_i2c_regmap_write(void *context, const void *data, size_t count)
{
	struct st7571_i2c_transport *t = context;
	int ret;

	struct i2c_msg msg = {
		.addr = t->client->addr,
		.flags = t->ignore_nak ? I2C_M_IGNORE_NAK : 0,
		.len = count,
		.buf = (u8 *)data
	};

	ret = i2c_transfer(t->client->adapter, &msg, 1);

	/*
	 * Unfortunately, there is no way to check if the transfer failed because of
	 * a NAK or something else as I2C bus drivers use different return values for NAK.
	 *
	 * However, if the transfer fails and ignore_nak is set, we know it is an error.
	 */
	if (ret < 0 && t->ignore_nak)
		return ret;

	return 0;
}

/* The st7571 driver does not read registers but regmap expects a .read */
static int st7571_i2c_regmap_read(void *context, const void *reg_buf,
				  size_t reg_size, void *val_buf, size_t val_size)
{
	return -EOPNOTSUPP;
}

static const struct regmap_bus st7571_i2c_regmap_bus = {
	.read = st7571_i2c_regmap_read,
	.write = st7571_i2c_regmap_write,
};

static const struct regmap_config st7571_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.use_single_write = true,
};

static int st7571_i2c_probe(struct i2c_client *client)
{
	struct st7571_device *st7571;
	struct st7571_i2c_transport *t;
	struct regmap *regmap;

	t = devm_kzalloc(&client->dev, sizeof(*t), GFP_KERNEL);
	if (!t)
		return -ENOMEM;

	t->client = client;

	/*
	 * The hardware design could make it hard to detect a NAK on the I2C bus.
	 * If the adapter does not support protocol mangling do
	 * not set the I2C_M_IGNORE_NAK flag at the expense * of possible
	 * cruft in the logs.
	 */
	if (i2c_check_functionality(client->adapter, I2C_FUNC_PROTOCOL_MANGLING))
		t->ignore_nak = true;

	regmap = devm_regmap_init(&client->dev, &st7571_i2c_regmap_bus,
				  t, &st7571_i2c_regmap_config);
	if (IS_ERR(regmap)) {
		return dev_err_probe(&client->dev, PTR_ERR(regmap),
				     "Failed to initialize regmap\n");
	}

	st7571 = st7571_probe(&client->dev, regmap);
	if (IS_ERR(st7571))
		return dev_err_probe(&client->dev, PTR_ERR(st7571),
				     "Failed to initialize regmap\n");

	i2c_set_clientdata(client, st7571);
	return 0;
}

static void st7571_i2c_remove(struct i2c_client *client)
{
	struct st7571_device *st7571 = i2c_get_clientdata(client);

	st7571_remove(st7571);
}

static const struct of_device_id st7571_of_match[] = {
	{ .compatible = "sitronix,st7567", .data = &st7567_config },
	{ .compatible = "sitronix,st7571", .data = &st7571_config },
	{},
};
MODULE_DEVICE_TABLE(of, st7571_of_match);

static const struct i2c_device_id st7571_id[] = {
	{ "st7567", 0 },
	{ "st7571", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, st7571_id);

static struct i2c_driver st7571_i2c_driver = {
	.driver = {
		.name = "st7571-i2c",
		.of_match_table = st7571_of_match,
	},
	.probe = st7571_i2c_probe,
	.remove = st7571_i2c_remove,
	.id_table = st7571_id,
};

module_i2c_driver(st7571_i2c_driver);

MODULE_AUTHOR("Marcus Folkesson <marcus.folkesson@gmail.com>");
MODULE_DESCRIPTION("DRM Driver for Sitronix ST7571 LCD controller (I2C)");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("DRM_ST7571");

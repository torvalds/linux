// SPDX-License-Identifier: GPL-2.0+
//
// Gateworks I2C PLD GPIO expander
//
// Copyright (C) 2019 Linus Walleij <linus.walleij@linaro.org>
//
// Based on code and know-how from the OpenWrt driver:
// Copyright (C) 2009 Gateworks Corporation
// Authors: Chris Lang, Imre Kaloz

#include <linux/bits.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/module.h>

/**
 * struct gw_pld - State container for Gateworks PLD
 * @chip: GPIO chip instance
 * @client: I2C client
 * @out: shadow register for the output bute
 */
struct gw_pld {
	struct gpio_chip chip;
	struct i2c_client *client;
	u8 out;
};

/*
 * The Gateworks I2C PLD chip only expose one read and one write register.
 * Writing a "one" bit (to match the reset state) lets that pin be used as an
 * input. It is an open-drain model.
 */
static int gw_pld_input8(struct gpio_chip *gc, unsigned offset)
{
	struct gw_pld *gw = gpiochip_get_data(gc);

	gw->out |= BIT(offset);
	return i2c_smbus_write_byte(gw->client, gw->out);
}

static int gw_pld_get8(struct gpio_chip *gc, unsigned offset)
{
	struct gw_pld *gw = gpiochip_get_data(gc);
	s32 val;

	val = i2c_smbus_read_byte(gw->client);

	return (val < 0) ? 0 : !!(val & BIT(offset));
}

static int gw_pld_output8(struct gpio_chip *gc, unsigned offset, int value)
{
	struct gw_pld *gw = gpiochip_get_data(gc);

	if (value)
		gw->out |= BIT(offset);
	else
		gw->out &= ~BIT(offset);

	return i2c_smbus_write_byte(gw->client, gw->out);
}

static void gw_pld_set8(struct gpio_chip *gc, unsigned offset, int value)
{
	gw_pld_output8(gc, offset, value);
}

static int gw_pld_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct gw_pld *gw;
	int ret;

	gw = devm_kzalloc(dev, sizeof(*gw), GFP_KERNEL);
	if (!gw)
		return -ENOMEM;

	gw->chip.base = -1;
	gw->chip.can_sleep = true;
	gw->chip.parent = dev;
	gw->chip.owner = THIS_MODULE;
	gw->chip.label = dev_name(dev);
	gw->chip.ngpio = 8;
	gw->chip.direction_input = gw_pld_input8;
	gw->chip.get = gw_pld_get8;
	gw->chip.direction_output = gw_pld_output8;
	gw->chip.set = gw_pld_set8;
	gw->client = client;

	/*
	 * The Gateworks I2C PLD chip does not properly send the acknowledge
	 * bit at all times, but we can still use the standard i2c_smbus
	 * functions by simply ignoring this bit.
	 */
	client->flags |= I2C_M_IGNORE_NAK;
	gw->out = 0xFF;

	i2c_set_clientdata(client, gw);

	ret = devm_gpiochip_add_data(dev, &gw->chip, gw);
	if (ret)
		return ret;

	dev_info(dev, "registered Gateworks PLD GPIO device\n");

	return 0;
}

static const struct i2c_device_id gw_pld_id[] = {
	{ "gw-pld", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, gw_pld_id);

static const struct of_device_id gw_pld_dt_ids[] = {
	{ .compatible = "gateworks,pld-gpio", },
	{ },
};
MODULE_DEVICE_TABLE(of, gw_pld_dt_ids);

static struct i2c_driver gw_pld_driver = {
	.driver = {
		.name = "gw_pld",
		.of_match_table = gw_pld_dt_ids,
	},
	.probe = gw_pld_probe,
	.id_table = gw_pld_id,
};
module_i2c_driver(gw_pld_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");

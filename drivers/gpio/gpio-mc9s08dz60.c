/*
 * Copyright 2009-2012 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * Author: Wu Guoxing <b39297@freescale.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/gpio/driver.h>

#define GPIO_GROUP_NUM 2
#define GPIO_NUM_PER_GROUP 8
#define GPIO_NUM (GPIO_GROUP_NUM*GPIO_NUM_PER_GROUP)

struct mc9s08dz60 {
	struct i2c_client *client;
	struct gpio_chip chip;
};

static void mc9s_gpio_to_reg_and_bit(int offset, u8 *reg, u8 *bit)
{
	*reg = 0x20 + offset / GPIO_NUM_PER_GROUP;
	*bit = offset % GPIO_NUM_PER_GROUP;
}

static int mc9s08dz60_get_value(struct gpio_chip *gc, unsigned offset)
{
	u8 reg, bit;
	s32 value;
	struct mc9s08dz60 *mc9s = gpiochip_get_data(gc);

	mc9s_gpio_to_reg_and_bit(offset, &reg, &bit);
	value = i2c_smbus_read_byte_data(mc9s->client, reg);

	return (value >= 0) ? (value >> bit) & 0x1 : 0;
}

static int mc9s08dz60_set(struct mc9s08dz60 *mc9s, unsigned offset, int val)
{
	u8 reg, bit;
	s32 value;

	mc9s_gpio_to_reg_and_bit(offset, &reg, &bit);
	value = i2c_smbus_read_byte_data(mc9s->client, reg);
	if (value >= 0) {
		if (val)
			value |= 1 << bit;
		else
			value &= ~(1 << bit);

		return i2c_smbus_write_byte_data(mc9s->client, reg, value);
	} else
		return value;

}


static void mc9s08dz60_set_value(struct gpio_chip *gc, unsigned offset, int val)
{
	struct mc9s08dz60 *mc9s = gpiochip_get_data(gc);

	mc9s08dz60_set(mc9s, offset, val);
}

static int mc9s08dz60_direction_output(struct gpio_chip *gc,
				       unsigned offset, int val)
{
	struct mc9s08dz60 *mc9s = gpiochip_get_data(gc);

	return mc9s08dz60_set(mc9s, offset, val);
}

static int mc9s08dz60_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct mc9s08dz60 *mc9s;

	mc9s = devm_kzalloc(&client->dev, sizeof(*mc9s), GFP_KERNEL);
	if (!mc9s)
		return -ENOMEM;

	mc9s->chip.label = client->name;
	mc9s->chip.base = -1;
	mc9s->chip.parent = &client->dev;
	mc9s->chip.owner = THIS_MODULE;
	mc9s->chip.ngpio = GPIO_NUM;
	mc9s->chip.can_sleep = true;
	mc9s->chip.get = mc9s08dz60_get_value;
	mc9s->chip.set = mc9s08dz60_set_value;
	mc9s->chip.direction_output = mc9s08dz60_direction_output;
	mc9s->client = client;
	i2c_set_clientdata(client, mc9s);

	return devm_gpiochip_add_data(&client->dev, &mc9s->chip, mc9s);
}

static const struct i2c_device_id mc9s08dz60_id[] = {
	{"mc9s08dz60", 0},
	{},
};

static struct i2c_driver mc9s08dz60_i2c_driver = {
	.driver = {
		.name = "mc9s08dz60",
	},
	.probe = mc9s08dz60_probe,
	.id_table = mc9s08dz60_id,
};
builtin_i2c_driver(mc9s08dz60_i2c_driver);

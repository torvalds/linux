/*
 * Digital I/O driver for Technologic Systems I2C FPGA Core
 *
 * Copyright (C) 2015 Technologic Systems
 * Copyright (C) 2016 Savoir-Faire Linux
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether expressed or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.
 */

#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/regmap.h>

#define DEFAULT_PIN_NUMBER	32
/*
 * Register bits used by the GPIO device
 * Some boards, such as TS-7970 do not have a separate input bit
 */
#define TS4900_GPIO_OE		0x01
#define TS4900_GPIO_OUT		0x02
#define TS4900_GPIO_IN		0x04
#define TS7970_GPIO_IN		0x02

struct ts4900_gpio_priv {
	struct regmap *regmap;
	struct gpio_chip gpio_chip;
	unsigned int input_bit;
};

static int ts4900_gpio_get_direction(struct gpio_chip *chip,
				     unsigned int offset)
{
	struct ts4900_gpio_priv *priv = gpiochip_get_data(chip);
	unsigned int reg;

	regmap_read(priv->regmap, offset, &reg);

	return !(reg & TS4900_GPIO_OE);
}

static int ts4900_gpio_direction_input(struct gpio_chip *chip,
				       unsigned int offset)
{
	struct ts4900_gpio_priv *priv = gpiochip_get_data(chip);

	/*
	 * This will clear the output enable bit, the other bits are
	 * dontcare when this is cleared
	 */
	return regmap_write(priv->regmap, offset, 0);
}

static int ts4900_gpio_direction_output(struct gpio_chip *chip,
					unsigned int offset, int value)
{
	struct ts4900_gpio_priv *priv = gpiochip_get_data(chip);
	int ret;

	if (value)
		ret = regmap_write(priv->regmap, offset, TS4900_GPIO_OE |
							 TS4900_GPIO_OUT);
	else
		ret = regmap_write(priv->regmap, offset, TS4900_GPIO_OE);

	return ret;
}

static int ts4900_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct ts4900_gpio_priv *priv = gpiochip_get_data(chip);
	unsigned int reg;

	regmap_read(priv->regmap, offset, &reg);

	return !!(reg & priv->input_bit);
}

static void ts4900_gpio_set(struct gpio_chip *chip, unsigned int offset,
			    int value)
{
	struct ts4900_gpio_priv *priv = gpiochip_get_data(chip);

	if (value)
		regmap_update_bits(priv->regmap, offset, TS4900_GPIO_OUT,
				   TS4900_GPIO_OUT);
	else
		regmap_update_bits(priv->regmap, offset, TS4900_GPIO_OUT, 0);
}

static const struct regmap_config ts4900_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
};

static const struct gpio_chip template_chip = {
	.label			= "ts4900-gpio",
	.owner			= THIS_MODULE,
	.get_direction		= ts4900_gpio_get_direction,
	.direction_input	= ts4900_gpio_direction_input,
	.direction_output	= ts4900_gpio_direction_output,
	.get			= ts4900_gpio_get,
	.set			= ts4900_gpio_set,
	.base			= -1,
	.can_sleep		= true,
};

static const struct of_device_id ts4900_gpio_of_match_table[] = {
	{
		.compatible = "technologic,ts4900-gpio",
		.data = (void *)TS4900_GPIO_IN,
	}, {
		.compatible = "technologic,ts7970-gpio",
		.data = (void *)TS7970_GPIO_IN,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ts4900_gpio_of_match_table);

static int ts4900_gpio_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ts4900_gpio_priv *priv;
	u32 ngpio;
	int ret;

	if (of_property_read_u32(client->dev.of_node, "ngpios", &ngpio))
		ngpio = DEFAULT_PIN_NUMBER;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->gpio_chip = template_chip;
	priv->gpio_chip.label = "ts4900-gpio";
	priv->gpio_chip.ngpio = ngpio;
	priv->gpio_chip.parent = &client->dev;
	priv->input_bit = (uintptr_t)of_device_get_match_data(&client->dev);

	priv->regmap = devm_regmap_init_i2c(client, &ts4900_regmap_config);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = devm_gpiochip_add_data(&client->dev, &priv->gpio_chip, priv);
	if (ret < 0) {
		dev_err(&client->dev, "Unable to register gpiochip\n");
		return ret;
	}

	i2c_set_clientdata(client, priv);

	return 0;
}

static const struct i2c_device_id ts4900_gpio_id_table[] = {
	{ "ts4900-gpio", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, ts4900_gpio_id_table);

static struct i2c_driver ts4900_gpio_driver = {
	.driver = {
		.name = "ts4900-gpio",
		.of_match_table = ts4900_gpio_of_match_table,
	},
	.probe = ts4900_gpio_probe,
	.id_table = ts4900_gpio_id_table,
};
module_i2c_driver(ts4900_gpio_driver);

MODULE_AUTHOR("Technologic Systems");
MODULE_DESCRIPTION("GPIO interface for Technologic Systems I2C-FPGA core");
MODULE_LICENSE("GPL");

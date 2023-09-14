// SPDX-License-Identifier: GPL-2.0-only
/*
 *  NCA9539 I2C Port Expander I/O
 *
 *  Copyright (C) 2023 Cody Xie <cody.xie@rock-chips.com>
 *
 */

#include <linux/compiler_types.h>
#include <linux/bitfield.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/string.h>
#include <linux/types.h>

#define NCA9539_REG_INPUT_PORT_BASE 0x00
#define NCA9539_REG_INPUT_PORT0 (NCA9539_REG_INPUT_PORT_BASE + 0x0)
#define NCA9539_REG_INPUT_PORT1 (NCA9539_REG_INPUT_PORT_BASE + 0x1)
#define NCA9539_REG_OUTPUT_PORT_BASE 0x02
#define NCA9539_REG_OUTPUT_PORT0 (NCA9539_REG_OUTPUT_PORT_BASE + 0x0)
#define NCA9539_REG_OUTPUT_PORT1 (NCA9539_REG_OUTPUT_PORT_BASE + 0x1)
#define NCA9539_REG_POLARITY_BASE 0x04
#define NCA9539_REG_POLARITY_PORT0 (NCA9539_REG_POLARITY_BASE + 0x0)
#define NCA9539_REG_POLARITY_PORT1 (NCA9539_REG_POLARITY_BASE + 0x1)
#define NCA9539_REG_CONFIG_BASE 0x06
#define NCA9539_REG_CONFIG_PORT0 (NCA9539_REG_CONFIG_BASE + 0x0)
#define NCA9539_REG_CONFIG_PORT1 (NCA9539_REG_CONFIG_BASE + 0x1)

struct nca9539_chip {
	struct gpio_chip gpio_chip;
	struct regmap *regmap;
	struct regulator *regulator;
	unsigned int ngpio;
};

static int nca9539_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct nca9539_chip *priv = gpiochip_get_data(gc);
	unsigned int port = offset / 8;
	unsigned int pin = offset % 8;
	unsigned int value;
	int ret;

	dev_dbg(gc->parent, "%s offset(%d)", __func__, offset);
	ret = regmap_read(priv->regmap, NCA9539_REG_CONFIG_BASE + port, &value);
	if (ret < 0) {
		dev_err(gc->parent, "%s offset(%d) read config failed",
			__func__, offset);
		return ret;
	}

	if (value & BIT(pin))
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int nca9539_gpio_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	struct nca9539_chip *priv = gpiochip_get_data(gc);
	unsigned int port = offset / 8;
	unsigned int pin = offset % 8;
	int ret;

	dev_dbg(gc->parent, "%s offset(%d)", __func__, offset);
	ret = regmap_update_bits(priv->regmap, NCA9539_REG_CONFIG_BASE + port,
				 BIT(pin), BIT(pin));
	if (ret < 0) {
		dev_err(gc->parent, "%s offset(%d) read config failed",
			__func__, offset);
	}

	return ret;
}

static int nca9539_gpio_direction_output(struct gpio_chip *gc, unsigned int offset,
					 int val)
{
	struct nca9539_chip *priv = gpiochip_get_data(gc);
	unsigned int port = offset / 8;
	unsigned int pin = offset % 8;
	int ret;

	dev_dbg(gc->parent, "%s offset(%d) val(%d)", __func__, offset, val);
	ret = regmap_update_bits(priv->regmap, NCA9539_REG_CONFIG_BASE + port,
				 BIT(pin), 0);
	if (ret < 0) {
		dev_err(gc->parent,
			"%s offset(%d) val(%d) update config failed", __func__,
			offset, val);
		return ret;
	}

	ret = regmap_update_bits(priv->regmap,
				 NCA9539_REG_OUTPUT_PORT_BASE + port, BIT(pin),
				 val ? BIT(pin) : 0);
	if (ret < 0) {
		dev_err(gc->parent,
			"%s offset(%d) val(%d) update output failed", __func__,
			offset, val);
		return ret;
	}

	return ret;
}

static int nca9539_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct nca9539_chip *priv = gpiochip_get_data(gc);
	unsigned int port = offset / 8;
	unsigned int pin = offset % 8;
	unsigned int reg;
	unsigned int value;
	int ret;

	dev_dbg(gc->parent, "%s offset(%d)", __func__, offset);
	ret = regmap_read(priv->regmap, NCA9539_REG_CONFIG_BASE + port, &value);
	if (ret < 0) {
		dev_err(gc->parent, "%s offset(%d) check config failed",
			__func__, offset);
		return ret;
	}
	if (!(BIT(pin) & value))
		reg = NCA9539_REG_OUTPUT_PORT_BASE + port;
	else
		reg = NCA9539_REG_INPUT_PORT_BASE + port;
	ret = regmap_read(priv->regmap, reg, &value);
	if (ret < 0) {
		dev_err(gc->parent, "%s offset(%d) read value failed", __func__,
			offset);
		return -EIO;
	}

	return !!(BIT(pin) & value);
}

static void nca9539_gpio_set(struct gpio_chip *gc, unsigned int offset, int val)
{
	struct nca9539_chip *priv = gpiochip_get_data(gc);
	unsigned int port = offset / 8;
	unsigned int pin = offset % 8;
	unsigned int value;
	int ret;

	dev_dbg(gc->parent, "%s offset(%d) val(%d)", __func__, offset, val);
	ret = regmap_read(priv->regmap, NCA9539_REG_CONFIG_BASE + port, &value);
	if (ret < 0 || !!(BIT(pin) & value)) {
		dev_err(gc->parent, "%s offset(%d) val(%d) check config failed",
			__func__, offset, val);
	}

	ret = regmap_update_bits(priv->regmap,
				 NCA9539_REG_OUTPUT_PORT_BASE + port, BIT(pin),
				 val ? BIT(pin) : 0);
	if (ret < 0) {
		dev_err(gc->parent, "%s offset(%d) val(%d) read input failed",
			__func__, offset, val);
	}
}

static bool nca9539_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NCA9539_REG_OUTPUT_PORT0:
	case NCA9539_REG_OUTPUT_PORT1:
	case NCA9539_REG_POLARITY_PORT0:
	case NCA9539_REG_POLARITY_PORT1:
	case NCA9539_REG_CONFIG_PORT0:
	case NCA9539_REG_CONFIG_PORT1:
		return true;
	}
	return false;
}

static bool nca9539_is_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NCA9539_REG_INPUT_PORT0:
	case NCA9539_REG_INPUT_PORT1:
	case NCA9539_REG_OUTPUT_PORT0:
	case NCA9539_REG_OUTPUT_PORT1:
	case NCA9539_REG_POLARITY_PORT0:
	case NCA9539_REG_POLARITY_PORT1:
	case NCA9539_REG_CONFIG_PORT0:
	case NCA9539_REG_CONFIG_PORT1:
		return true;
	}
	return false;
}

static bool nca9539_is_volatile_reg(struct device *dev, unsigned int reg)
{
	return true;
}

static const struct reg_default nca9539_regmap_default[] = {
	{ NCA9539_REG_INPUT_PORT0, 0xFF },
	{ NCA9539_REG_INPUT_PORT1, 0xFF },
	{ NCA9539_REG_OUTPUT_PORT0, 0xFF },
	{ NCA9539_REG_OUTPUT_PORT1, 0xFF },
	{ NCA9539_REG_POLARITY_PORT0, 0x00 },
	{ NCA9539_REG_POLARITY_PORT1, 0x00 },
	{ NCA9539_REG_CONFIG_PORT0, 0xFF },
	{ NCA9539_REG_CONFIG_PORT1, 0xFF },
};

static const struct regmap_config nca9539_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 7,
	.writeable_reg = nca9539_is_writeable_reg,
	.readable_reg = nca9539_is_readable_reg,
	.volatile_reg = nca9539_is_volatile_reg,
	.reg_defaults = nca9539_regmap_default,
	.num_reg_defaults = ARRAY_SIZE(nca9539_regmap_default),
	.cache_type = REGCACHE_FLAT,
};

static const struct gpio_chip template_chip = {
	.label = "nca9539-gpio",
	.owner = THIS_MODULE,
	.get_direction = nca9539_gpio_get_direction,
	.direction_input = nca9539_gpio_direction_input,
	.direction_output = nca9539_gpio_direction_output,
	.get = nca9539_gpio_get,
	.set = nca9539_gpio_set,
	.base = -1,
	.can_sleep = true,
};

static int nca9539_probe(struct i2c_client *client)
{
	struct nca9539_chip *chip;
	struct regulator *reg;
	int ret;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->gpio_chip = template_chip;
	chip->gpio_chip.label = "nca9539-gpio";
	chip->gpio_chip.parent = &client->dev;
	chip->ngpio = (uintptr_t)of_device_get_match_data(&client->dev);
	chip->gpio_chip.ngpio = chip->ngpio;

	reg = devm_regulator_get(&client->dev, "vdd");
	if (IS_ERR(reg))
		return dev_err_probe(&client->dev, PTR_ERR(reg),
				     "reg get err\n");

	ret = regulator_enable(reg);
	if (ret) {
		dev_err(&client->dev, "reg en err: %d\n", ret);
		return ret;
	}
	chip->regulator = reg;

	chip->regmap = devm_regmap_init_i2c(client, &nca9539_regmap_config);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		goto err_exit;
	}
	regcache_mark_dirty(chip->regmap);
	ret = regcache_sync(chip->regmap);
	if (ret) {
		dev_err(&client->dev, "Failed to sync register map: %d\n", ret);
		goto err_exit;
	}

	// TODO(Cody): irq_chip setup

	ret = devm_gpiochip_add_data(&client->dev, &chip->gpio_chip, chip);
	if (ret < 0) {
		dev_err(&client->dev, "Unable to register gpiochip\n");
		goto err_exit;
	}

	i2c_set_clientdata(client, chip);

	return 0;

err_exit:
	regulator_disable(chip->regulator);
	return ret;
}

static int nca9539_remove(struct i2c_client *client)
{
	struct nca9539_chip *chip = i2c_get_clientdata(client);

	regulator_disable(chip->regulator);

	return 0;
}

static const struct of_device_id nca9539_gpio_of_match_table[] = {
	{
		.compatible = "novo,nca9539-gpio",
		.data = (void *)16,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, nca9539_gpio_of_match_table);

static const struct i2c_device_id nca9539_gpio_id_table[] = {
	{ "nca9539-gpio" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i2c, nca9539_gpio_id_table);

static struct i2c_driver nca9539_driver = {
	.driver = {
		.name		= "nca9539-gpio",
		.of_match_table	= nca9539_gpio_of_match_table,
	},
	.probe_new	= nca9539_probe,
	.remove	= nca9539_remove,
	.id_table	= nca9539_gpio_id_table,
};
module_i2c_driver(nca9539_driver);

MODULE_AUTHOR("Cody Xie <cody.xie@rock-chips.com>");
MODULE_DESCRIPTION("GPIO expander driver for Novosense nca9539");
MODULE_LICENSE("GPL");

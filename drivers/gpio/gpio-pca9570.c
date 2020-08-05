// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for PCA9570 I2C GPO expander
 *
 * Copyright (C) 2020 Sungbo Eo <mans0n@gorani.run>
 *
 * Based on gpio-tpic2810.c
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 */

#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>

/**
 * struct pca9570 - GPIO driver data
 * @chip: GPIO controller chip
 * @lock: Protects write sequences
 * @out: Buffer for device register
 */
struct pca9570 {
	struct gpio_chip chip;
	struct mutex lock;
	u8 out;
};

static int pca9570_read(struct pca9570 *gpio, u8 *value)
{
	struct i2c_client *client = to_i2c_client(gpio->chip.parent);
	int ret;

	ret = i2c_smbus_read_byte(client);
	if (ret < 0)
		return ret;

	*value = ret;
	return 0;
}

static int pca9570_write(struct pca9570 *gpio, u8 value)
{
	struct i2c_client *client = to_i2c_client(gpio->chip.parent);

	return i2c_smbus_write_byte(client, value);
}

static int pca9570_get_direction(struct gpio_chip *chip,
				 unsigned offset)
{
	/* This device always output */
	return GPIO_LINE_DIRECTION_OUT;
}

static int pca9570_get(struct gpio_chip *chip, unsigned offset)
{
	struct pca9570 *gpio = gpiochip_get_data(chip);
	u8 buffer;
	int ret;

	ret = pca9570_read(gpio, &buffer);
	if (ret)
		return ret;

	return !!(buffer & BIT(offset));
}

static void pca9570_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct pca9570 *gpio = gpiochip_get_data(chip);
	u8 buffer;
	int ret;

	mutex_lock(&gpio->lock);

	buffer = gpio->out;
	if (value)
		buffer |= BIT(offset);
	else
		buffer &= ~BIT(offset);

	ret = pca9570_write(gpio, buffer);
	if (ret)
		goto out;

	gpio->out = buffer;

out:
	mutex_unlock(&gpio->lock);
}

static int pca9570_probe(struct i2c_client *client)
{
	struct pca9570 *gpio;

	gpio = devm_kzalloc(&client->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->chip.label = client->name;
	gpio->chip.parent = &client->dev;
	gpio->chip.owner = THIS_MODULE;
	gpio->chip.get_direction = pca9570_get_direction;
	gpio->chip.get = pca9570_get;
	gpio->chip.set = pca9570_set;
	gpio->chip.base = -1;
	gpio->chip.ngpio = (uintptr_t)device_get_match_data(&client->dev);
	gpio->chip.can_sleep = true;

	mutex_init(&gpio->lock);

	/* Read the current output level */
	pca9570_read(gpio, &gpio->out);

	i2c_set_clientdata(client, gpio);

	return devm_gpiochip_add_data(&client->dev, &gpio->chip, gpio);
}

static const struct i2c_device_id pca9570_id_table[] = {
	{ "pca9570", 4 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, pca9570_id_table);

static const struct of_device_id pca9570_of_match_table[] = {
	{ .compatible = "nxp,pca9570", .data = (void *)4 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pca9570_of_match_table);

static struct i2c_driver pca9570_driver = {
	.driver = {
		.name = "pca9570",
		.of_match_table = pca9570_of_match_table,
	},
	.probe_new = pca9570_probe,
	.id_table = pca9570_id_table,
};
module_i2c_driver(pca9570_driver);

MODULE_AUTHOR("Sungbo Eo <mans0n@gorani.run>");
MODULE_DESCRIPTION("GPIO expander driver for PCA9570");
MODULE_LICENSE("GPL v2");

/*
 *  74Hx164 - Generic serial-in/parallel-out 8-bits shift register GPIO driver
 *
 *  Copyright (C) 2010 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2010 Miguel Gaio <miguel.gaio@efixo.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>
#include <linux/spi/74x164.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/module.h>

#define GEN_74X164_NUMBER_GPIOS	8

struct gen_74x164_chip {
	struct spi_device	*spi;
	u8			*buffer;
	struct gpio_chip	gpio_chip;
	struct mutex		lock;
	u32			registers;
};

static struct gen_74x164_chip *gpio_to_74x164_chip(struct gpio_chip *gc)
{
	return container_of(gc, struct gen_74x164_chip, gpio_chip);
}

static int __gen_74x164_write_config(struct gen_74x164_chip *chip)
{
	struct spi_message message;
	struct spi_transfer *msg_buf;
	int i, ret = 0;

	msg_buf = kzalloc(chip->registers * sizeof(struct spi_transfer),
			GFP_KERNEL);
	if (!msg_buf)
		return -ENOMEM;

	spi_message_init(&message);

	/*
	 * Since the registers are chained, every byte sent will make
	 * the previous byte shift to the next register in the
	 * chain. Thus, the first byte send will end up in the last
	 * register at the end of the transfer. So, to have a logical
	 * numbering, send the bytes in reverse order so that the last
	 * byte of the buffer will end up in the last register.
	 */
	for (i = chip->registers - 1; i >= 0; i--) {
		msg_buf[i].tx_buf = chip->buffer +i;
		msg_buf[i].len = sizeof(u8);
		spi_message_add_tail(msg_buf + i, &message);
	}

	ret = spi_sync(chip->spi, &message);

	kfree(msg_buf);

	return ret;
}

static int gen_74x164_get_value(struct gpio_chip *gc, unsigned offset)
{
	struct gen_74x164_chip *chip = gpio_to_74x164_chip(gc);
	u8 bank = offset / 8;
	u8 pin = offset % 8;
	int ret;

	mutex_lock(&chip->lock);
	ret = (chip->buffer[bank] >> pin) & 0x1;
	mutex_unlock(&chip->lock);

	return ret;
}

static void gen_74x164_set_value(struct gpio_chip *gc,
		unsigned offset, int val)
{
	struct gen_74x164_chip *chip = gpio_to_74x164_chip(gc);
	u8 bank = offset / 8;
	u8 pin = offset % 8;

	mutex_lock(&chip->lock);
	if (val)
		chip->buffer[bank] |= (1 << pin);
	else
		chip->buffer[bank] &= ~(1 << pin);

	__gen_74x164_write_config(chip);
	mutex_unlock(&chip->lock);
}

static int gen_74x164_direction_output(struct gpio_chip *gc,
		unsigned offset, int val)
{
	gen_74x164_set_value(gc, offset, val);
	return 0;
}

static int gen_74x164_probe(struct spi_device *spi)
{
	struct gen_74x164_chip *chip;
	struct gen_74x164_chip_platform_data *pdata;
	int ret;

	if (!spi->dev.of_node) {
		dev_err(&spi->dev, "No device tree data available.\n");
		return -EINVAL;
	}

	/*
	 * bits_per_word cannot be configured in platform data
	 */
	spi->bits_per_word = 8;

	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	chip = devm_kzalloc(&spi->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	pdata = spi->dev.platform_data;
	if (pdata && pdata->base)
		chip->gpio_chip.base = pdata->base;
	else
		chip->gpio_chip.base = -1;

	mutex_init(&chip->lock);

	dev_set_drvdata(&spi->dev, chip);

	chip->spi = spi;

	chip->gpio_chip.label = spi->modalias;
	chip->gpio_chip.direction_output = gen_74x164_direction_output;
	chip->gpio_chip.get = gen_74x164_get_value;
	chip->gpio_chip.set = gen_74x164_set_value;

	if (of_property_read_u32(spi->dev.of_node, "registers-number", &chip->registers)) {
		dev_err(&spi->dev, "Missing registers-number property in the DT.\n");
		ret = -EINVAL;
		goto exit_destroy;
	}

	chip->gpio_chip.ngpio = GEN_74X164_NUMBER_GPIOS * chip->registers;
	chip->buffer = devm_kzalloc(&spi->dev, chip->registers, GFP_KERNEL);
	if (!chip->buffer) {
		ret = -ENOMEM;
		goto exit_destroy;
	}

	chip->gpio_chip.can_sleep = 1;
	chip->gpio_chip.dev = &spi->dev;
	chip->gpio_chip.owner = THIS_MODULE;

	ret = __gen_74x164_write_config(chip);
	if (ret) {
		dev_err(&spi->dev, "Failed writing: %d\n", ret);
		goto exit_destroy;
	}

	ret = gpiochip_add(&chip->gpio_chip);
	if (ret)
		goto exit_destroy;

	return ret;

exit_destroy:
	dev_set_drvdata(&spi->dev, NULL);
	mutex_destroy(&chip->lock);
	return ret;
}

static int gen_74x164_remove(struct spi_device *spi)
{
	struct gen_74x164_chip *chip;
	int ret;

	chip = dev_get_drvdata(&spi->dev);
	if (chip == NULL)
		return -ENODEV;

	dev_set_drvdata(&spi->dev, NULL);

	ret = gpiochip_remove(&chip->gpio_chip);
	if (!ret)
		mutex_destroy(&chip->lock);
	else
		dev_err(&spi->dev, "Failed to remove the GPIO controller: %d\n",
				ret);

	return ret;
}

static const struct of_device_id gen_74x164_dt_ids[] = {
	{ .compatible = "fairchild,74hc595" },
	{},
};
MODULE_DEVICE_TABLE(of, gen_74x164_dt_ids);

static struct spi_driver gen_74x164_driver = {
	.driver = {
		.name		= "74x164",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(gen_74x164_dt_ids),
	},
	.probe		= gen_74x164_probe,
	.remove		= gen_74x164_remove,
};
module_spi_driver(gen_74x164_driver);

MODULE_AUTHOR("Gabor Juhos <juhosg@openwrt.org>");
MODULE_AUTHOR("Miguel Gaio <miguel.gaio@efixo.com>");
MODULE_DESCRIPTION("GPIO expander driver for 74X164 8-bits shift register");
MODULE_LICENSE("GPL v2");

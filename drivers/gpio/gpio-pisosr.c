/*
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
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

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>

#define DEFAULT_NGPIO 8

/**
 * struct pisosr_gpio - GPIO driver data
 * @chip: GPIO controller chip
 * @spi: SPI device pointer
 * @buffer: Buffer for device reads
 * @buffer_size: Size of buffer
 * @load_gpio: GPIO pin used to load input into device
 * @lock: Protects read sequences
 */
struct pisosr_gpio {
	struct gpio_chip chip;
	struct spi_device *spi;
	u8 *buffer;
	size_t buffer_size;
	struct gpio_desc *load_gpio;
	struct mutex lock;
};

static int pisosr_gpio_refresh(struct pisosr_gpio *gpio)
{
	int ret;

	mutex_lock(&gpio->lock);

	if (gpio->load_gpio) {
		gpiod_set_value_cansleep(gpio->load_gpio, 1);
		udelay(1); /* registers load time (~10ns) */
		gpiod_set_value_cansleep(gpio->load_gpio, 0);
		udelay(1); /* registers recovery time (~5ns) */
	}

	ret = spi_read(gpio->spi, gpio->buffer, gpio->buffer_size);

	mutex_unlock(&gpio->lock);

	return ret;
}

static int pisosr_gpio_get_direction(struct gpio_chip *chip,
				     unsigned offset)
{
	/* This device always input */
	return GPIO_LINE_DIRECTION_IN;
}

static int pisosr_gpio_direction_input(struct gpio_chip *chip,
				       unsigned offset)
{
	/* This device always input */
	return 0;
}

static int pisosr_gpio_direction_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	/* This device is input only */
	return -EINVAL;
}

static int pisosr_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct pisosr_gpio *gpio = gpiochip_get_data(chip);

	/* Refresh may not always be needed */
	pisosr_gpio_refresh(gpio);

	return (gpio->buffer[offset / 8] >> (offset % 8)) & 0x1;
}

static int pisosr_gpio_get_multiple(struct gpio_chip *chip,
				    unsigned long *mask, unsigned long *bits)
{
	struct pisosr_gpio *gpio = gpiochip_get_data(chip);
	unsigned long offset;
	unsigned long gpio_mask;
	unsigned long buffer_state;

	pisosr_gpio_refresh(gpio);

	bitmap_zero(bits, chip->ngpio);
	for_each_set_clump8(offset, gpio_mask, mask, chip->ngpio) {
		buffer_state = gpio->buffer[offset / 8] & gpio_mask;
		bitmap_set_value8(bits, buffer_state, offset);
	}

	return 0;
}

static const struct gpio_chip template_chip = {
	.label			= "pisosr-gpio",
	.owner			= THIS_MODULE,
	.get_direction		= pisosr_gpio_get_direction,
	.direction_input	= pisosr_gpio_direction_input,
	.direction_output	= pisosr_gpio_direction_output,
	.get			= pisosr_gpio_get,
	.get_multiple		= pisosr_gpio_get_multiple,
	.base			= -1,
	.ngpio			= DEFAULT_NGPIO,
	.can_sleep		= true,
};

static int pisosr_gpio_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct pisosr_gpio *gpio;
	int ret;

	gpio = devm_kzalloc(dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	spi_set_drvdata(spi, gpio);

	gpio->chip = template_chip;
	gpio->chip.parent = dev;
	of_property_read_u16(dev->of_node, "ngpios", &gpio->chip.ngpio);

	gpio->spi = spi;

	gpio->buffer_size = DIV_ROUND_UP(gpio->chip.ngpio, 8);
	gpio->buffer = devm_kzalloc(dev, gpio->buffer_size, GFP_KERNEL);
	if (!gpio->buffer)
		return -ENOMEM;

	gpio->load_gpio = devm_gpiod_get_optional(dev, "load", GPIOD_OUT_LOW);
	if (IS_ERR(gpio->load_gpio)) {
		ret = PTR_ERR(gpio->load_gpio);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Unable to allocate load GPIO\n");
		return ret;
	}

	mutex_init(&gpio->lock);

	ret = gpiochip_add_data(&gpio->chip, gpio);
	if (ret < 0) {
		dev_err(dev, "Unable to register gpiochip\n");
		return ret;
	}

	return 0;
}

static int pisosr_gpio_remove(struct spi_device *spi)
{
	struct pisosr_gpio *gpio = spi_get_drvdata(spi);

	gpiochip_remove(&gpio->chip);

	mutex_destroy(&gpio->lock);

	return 0;
}

static const struct spi_device_id pisosr_gpio_id_table[] = {
	{ "pisosr-gpio", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, pisosr_gpio_id_table);

static const struct of_device_id pisosr_gpio_of_match_table[] = {
	{ .compatible = "pisosr-gpio", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pisosr_gpio_of_match_table);

static struct spi_driver pisosr_gpio_driver = {
	.driver = {
		.name = "pisosr-gpio",
		.of_match_table = pisosr_gpio_of_match_table,
	},
	.probe = pisosr_gpio_probe,
	.remove = pisosr_gpio_remove,
	.id_table = pisosr_gpio_id_table,
};
module_spi_driver(pisosr_gpio_driver);

MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("SPI Compatible PISO Shift Register GPIO Driver");
MODULE_LICENSE("GPL v2");

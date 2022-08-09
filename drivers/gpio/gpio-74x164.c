// SPDX-License-Identifier: GPL-2.0-only
/*
 *  74Hx164 - Generic serial-in/parallel-out 8-bits shift register GPIO driver
 *
 *  Copyright (C) 2010 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2010 Miguel Gaio <miguel.gaio@efixo.com>
 */

#include <linux/bitops.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#define GEN_74X164_NUMBER_GPIOS	8

struct gen_74x164_chip {
	struct gpio_chip	gpio_chip;
	struct mutex		lock;
	struct gpio_desc	*gpiod_oe;
	u32			registers;
	/*
	 * Since the registers are chained, every byte sent will make
	 * the previous byte shift to the next register in the
	 * chain. Thus, the first byte sent will end up in the last
	 * register at the end of the transfer. So, to have a logical
	 * numbering, store the bytes in reverse order.
	 */
	u8			buffer[];
};

static int __gen_74x164_write_config(struct gen_74x164_chip *chip)
{
	return spi_write(to_spi_device(chip->gpio_chip.parent), chip->buffer,
			 chip->registers);
}

static int gen_74x164_get_value(struct gpio_chip *gc, unsigned offset)
{
	struct gen_74x164_chip *chip = gpiochip_get_data(gc);
	u8 bank = chip->registers - 1 - offset / 8;
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
	struct gen_74x164_chip *chip = gpiochip_get_data(gc);
	u8 bank = chip->registers - 1 - offset / 8;
	u8 pin = offset % 8;

	mutex_lock(&chip->lock);
	if (val)
		chip->buffer[bank] |= (1 << pin);
	else
		chip->buffer[bank] &= ~(1 << pin);

	__gen_74x164_write_config(chip);
	mutex_unlock(&chip->lock);
}

static void gen_74x164_set_multiple(struct gpio_chip *gc, unsigned long *mask,
				    unsigned long *bits)
{
	struct gen_74x164_chip *chip = gpiochip_get_data(gc);
	unsigned long offset;
	unsigned long bankmask;
	size_t bank;
	unsigned long bitmask;

	mutex_lock(&chip->lock);
	for_each_set_clump8(offset, bankmask, mask, chip->registers * 8) {
		bank = chip->registers - 1 - offset / 8;
		bitmask = bitmap_get_value8(bits, offset) & bankmask;

		chip->buffer[bank] &= ~bankmask;
		chip->buffer[bank] |= bitmask;
	}
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
	u32 nregs;
	int ret;

	/*
	 * bits_per_word cannot be configured in platform data
	 */
	spi->bits_per_word = 8;

	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	ret = device_property_read_u32(&spi->dev, "registers-number", &nregs);
	if (ret) {
		dev_err(&spi->dev, "Missing 'registers-number' property.\n");
		return -EINVAL;
	}

	chip = devm_kzalloc(&spi->dev, sizeof(*chip) + nregs, GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->gpiod_oe = devm_gpiod_get_optional(&spi->dev, "enable",
						 GPIOD_OUT_LOW);
	if (IS_ERR(chip->gpiod_oe))
		return PTR_ERR(chip->gpiod_oe);

	gpiod_set_value_cansleep(chip->gpiod_oe, 1);

	spi_set_drvdata(spi, chip);

	chip->gpio_chip.label = spi->modalias;
	chip->gpio_chip.direction_output = gen_74x164_direction_output;
	chip->gpio_chip.get = gen_74x164_get_value;
	chip->gpio_chip.set = gen_74x164_set_value;
	chip->gpio_chip.set_multiple = gen_74x164_set_multiple;
	chip->gpio_chip.base = -1;

	chip->registers = nregs;
	chip->gpio_chip.ngpio = GEN_74X164_NUMBER_GPIOS * chip->registers;

	chip->gpio_chip.can_sleep = true;
	chip->gpio_chip.parent = &spi->dev;
	chip->gpio_chip.owner = THIS_MODULE;

	mutex_init(&chip->lock);

	ret = __gen_74x164_write_config(chip);
	if (ret) {
		dev_err(&spi->dev, "Failed writing: %d\n", ret);
		goto exit_destroy;
	}

	ret = gpiochip_add_data(&chip->gpio_chip, chip);
	if (!ret)
		return 0;

exit_destroy:
	mutex_destroy(&chip->lock);

	return ret;
}

static int gen_74x164_remove(struct spi_device *spi)
{
	struct gen_74x164_chip *chip = spi_get_drvdata(spi);

	gpiod_set_value_cansleep(chip->gpiod_oe, 0);
	gpiochip_remove(&chip->gpio_chip);
	mutex_destroy(&chip->lock);

	return 0;
}

static const struct spi_device_id gen_74x164_spi_ids[] = {
	{ .name = "74hc595" },
	{ .name = "74lvc594" },
	{},
};
MODULE_DEVICE_TABLE(spi, gen_74x164_spi_ids);

static const struct of_device_id gen_74x164_dt_ids[] = {
	{ .compatible = "fairchild,74hc595" },
	{ .compatible = "nxp,74lvc594" },
	{},
};
MODULE_DEVICE_TABLE(of, gen_74x164_dt_ids);

static struct spi_driver gen_74x164_driver = {
	.driver = {
		.name		= "74x164",
		.of_match_table	= gen_74x164_dt_ids,
	},
	.probe		= gen_74x164_probe,
	.remove		= gen_74x164_remove,
	.id_table	= gen_74x164_spi_ids,
};
module_spi_driver(gen_74x164_driver);

MODULE_AUTHOR("Gabor Juhos <juhosg@openwrt.org>");
MODULE_AUTHOR("Miguel Gaio <miguel.gaio@efixo.com>");
MODULE_DESCRIPTION("GPIO expander driver for 74X164 8-bits shift register");
MODULE_LICENSE("GPL v2");

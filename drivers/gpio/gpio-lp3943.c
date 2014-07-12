/*
 * TI/National Semiconductor LP3943 GPIO driver
 *
 * Copyright 2013 Texas Instruments
 *
 * Author: Milo Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/mfd/lp3943.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

enum lp3943_gpios {
	LP3943_GPIO1,
	LP3943_GPIO2,
	LP3943_GPIO3,
	LP3943_GPIO4,
	LP3943_GPIO5,
	LP3943_GPIO6,
	LP3943_GPIO7,
	LP3943_GPIO8,
	LP3943_GPIO9,
	LP3943_GPIO10,
	LP3943_GPIO11,
	LP3943_GPIO12,
	LP3943_GPIO13,
	LP3943_GPIO14,
	LP3943_GPIO15,
	LP3943_GPIO16,
	LP3943_MAX_GPIO,
};

struct lp3943_gpio {
	struct gpio_chip chip;
	struct lp3943 *lp3943;
	u16 input_mask;		/* 1 = GPIO is input direction, 0 = output */
};

static inline struct lp3943_gpio *to_lp3943_gpio(struct gpio_chip *_chip)
{
	return container_of(_chip, struct lp3943_gpio, chip);
}

static int lp3943_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct lp3943_gpio *lp3943_gpio = to_lp3943_gpio(chip);
	struct lp3943 *lp3943 = lp3943_gpio->lp3943;

	/* Return an error if the pin is already assigned */
	if (test_and_set_bit(offset, &lp3943->pin_used))
		return -EBUSY;

	return 0;
}

static void lp3943_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct lp3943_gpio *lp3943_gpio = to_lp3943_gpio(chip);
	struct lp3943 *lp3943 = lp3943_gpio->lp3943;

	clear_bit(offset, &lp3943->pin_used);
}

static int lp3943_gpio_set_mode(struct lp3943_gpio *lp3943_gpio, u8 offset,
				u8 val)
{
	struct lp3943 *lp3943 = lp3943_gpio->lp3943;
	const struct lp3943_reg_cfg *mux = lp3943->mux_cfg;

	return lp3943_update_bits(lp3943, mux[offset].reg, mux[offset].mask,
				  val << mux[offset].shift);
}

static int lp3943_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct lp3943_gpio *lp3943_gpio = to_lp3943_gpio(chip);

	lp3943_gpio->input_mask |= BIT(offset);

	return lp3943_gpio_set_mode(lp3943_gpio, offset, LP3943_GPIO_IN);
}

static int lp3943_get_gpio_in_status(struct lp3943_gpio *lp3943_gpio,
				     struct gpio_chip *chip, unsigned offset)
{
	u8 addr, read;
	int err;

	switch (offset) {
	case LP3943_GPIO1 ... LP3943_GPIO8:
		addr = LP3943_REG_GPIO_A;
		break;
	case LP3943_GPIO9 ... LP3943_GPIO16:
		addr = LP3943_REG_GPIO_B;
		offset = offset - 8;
		break;
	default:
		return -EINVAL;
	}

	err = lp3943_read_byte(lp3943_gpio->lp3943, addr, &read);
	if (err)
		return err;

	return !!(read & BIT(offset));
}

static int lp3943_get_gpio_out_status(struct lp3943_gpio *lp3943_gpio,
				      struct gpio_chip *chip, unsigned offset)
{
	struct lp3943 *lp3943 = lp3943_gpio->lp3943;
	const struct lp3943_reg_cfg *mux = lp3943->mux_cfg;
	u8 read;
	int err;

	err = lp3943_read_byte(lp3943, mux[offset].reg, &read);
	if (err)
		return err;

	read = (read & mux[offset].mask) >> mux[offset].shift;

	if (read == LP3943_GPIO_OUT_HIGH)
		return 1;
	else if (read == LP3943_GPIO_OUT_LOW)
		return 0;
	else
		return -EINVAL;
}

static int lp3943_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct lp3943_gpio *lp3943_gpio = to_lp3943_gpio(chip);

	/*
	 * Limitation:
	 *   LP3943 doesn't have the GPIO direction register. It provides
	 *   only input and output status registers.
	 *   So, direction info is required to handle the 'get' operation.
	 *   This variable is updated whenever the direction is changed and
	 *   it is used here.
	 */

	if (lp3943_gpio->input_mask & BIT(offset))
		return lp3943_get_gpio_in_status(lp3943_gpio, chip, offset);
	else
		return lp3943_get_gpio_out_status(lp3943_gpio, chip, offset);
}

static void lp3943_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct lp3943_gpio *lp3943_gpio = to_lp3943_gpio(chip);
	u8 data;

	if (value)
		data = LP3943_GPIO_OUT_HIGH;
	else
		data = LP3943_GPIO_OUT_LOW;

	lp3943_gpio_set_mode(lp3943_gpio, offset, data);
}

static int lp3943_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
					int value)
{
	struct lp3943_gpio *lp3943_gpio = to_lp3943_gpio(chip);

	lp3943_gpio_set(chip, offset, value);
	lp3943_gpio->input_mask &= ~BIT(offset);

	return 0;
}

static const struct gpio_chip lp3943_gpio_chip = {
	.label			= "lp3943",
	.owner			= THIS_MODULE,
	.request		= lp3943_gpio_request,
	.free			= lp3943_gpio_free,
	.direction_input	= lp3943_gpio_direction_input,
	.get			= lp3943_gpio_get,
	.direction_output	= lp3943_gpio_direction_output,
	.set			= lp3943_gpio_set,
	.base			= -1,
	.ngpio			= LP3943_MAX_GPIO,
	.can_sleep		= 1,
};

static int lp3943_gpio_probe(struct platform_device *pdev)
{
	struct lp3943 *lp3943 = dev_get_drvdata(pdev->dev.parent);
	struct lp3943_gpio *lp3943_gpio;

	lp3943_gpio = devm_kzalloc(&pdev->dev, sizeof(*lp3943_gpio),
				GFP_KERNEL);
	if (!lp3943_gpio)
		return -ENOMEM;

	lp3943_gpio->lp3943 = lp3943;
	lp3943_gpio->chip = lp3943_gpio_chip;
	lp3943_gpio->chip.dev = &pdev->dev;

	platform_set_drvdata(pdev, lp3943_gpio);

	return gpiochip_add(&lp3943_gpio->chip);
}

static int lp3943_gpio_remove(struct platform_device *pdev)
{
	struct lp3943_gpio *lp3943_gpio = platform_get_drvdata(pdev);

	gpiochip_remove(&lp3943_gpio->chip);
	return 0;
}

static const struct of_device_id lp3943_gpio_of_match[] = {
	{ .compatible = "ti,lp3943-gpio", },
	{ }
};
MODULE_DEVICE_TABLE(of, lp3943_gpio_of_match);

static struct platform_driver lp3943_gpio_driver = {
	.probe = lp3943_gpio_probe,
	.remove = lp3943_gpio_remove,
	.driver = {
		.name = "lp3943-gpio",
		.owner = THIS_MODULE,
		.of_match_table = lp3943_gpio_of_match,
	},
};
module_platform_driver(lp3943_gpio_driver);

MODULE_DESCRIPTION("LP3943 GPIO driver");
MODULE_ALIAS("platform:lp3943-gpio");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL");

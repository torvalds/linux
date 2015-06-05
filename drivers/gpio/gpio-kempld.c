/*
 * Kontron PLD GPIO driver
 *
 * Copyright (c) 2010-2013 Kontron Europe GmbH
 * Author: Michael Brunner <michael.brunner@kontron.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/mfd/kempld.h>

#define KEMPLD_GPIO_MAX_NUM		16
#define KEMPLD_GPIO_MASK(x)		(BIT((x) % 8))
#define KEMPLD_GPIO_DIR_NUM(x)		(0x40 + (x) / 8)
#define KEMPLD_GPIO_LVL_NUM(x)		(0x42 + (x) / 8)
#define KEMPLD_GPIO_EVT_LVL_EDGE	0x46
#define KEMPLD_GPIO_IEN			0x4A

struct kempld_gpio_data {
	struct gpio_chip		chip;
	struct kempld_device_data	*pld;
};

/*
 * Set or clear GPIO bit
 * kempld_get_mutex must be called prior to calling this function.
 */
static void kempld_gpio_bitop(struct kempld_device_data *pld,
			      u8 reg, u8 bit, u8 val)
{
	u8 status;

	status = kempld_read8(pld, reg);
	if (val)
		status |= KEMPLD_GPIO_MASK(bit);
	else
		status &= ~KEMPLD_GPIO_MASK(bit);
	kempld_write8(pld, reg, status);
}

static int kempld_gpio_get_bit(struct kempld_device_data *pld, u8 reg, u8 bit)
{
	u8 status;

	kempld_get_mutex(pld);
	status = kempld_read8(pld, reg);
	kempld_release_mutex(pld);

	return !!(status & KEMPLD_GPIO_MASK(bit));
}

static int kempld_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct kempld_gpio_data *gpio
		= container_of(chip, struct kempld_gpio_data, chip);
	struct kempld_device_data *pld = gpio->pld;

	return kempld_gpio_get_bit(pld, KEMPLD_GPIO_LVL_NUM(offset), offset);
}

static void kempld_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct kempld_gpio_data *gpio
		= container_of(chip, struct kempld_gpio_data, chip);
	struct kempld_device_data *pld = gpio->pld;

	kempld_get_mutex(pld);
	kempld_gpio_bitop(pld, KEMPLD_GPIO_LVL_NUM(offset), offset, value);
	kempld_release_mutex(pld);
}

static int kempld_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct kempld_gpio_data *gpio
		= container_of(chip, struct kempld_gpio_data, chip);
	struct kempld_device_data *pld = gpio->pld;

	kempld_get_mutex(pld);
	kempld_gpio_bitop(pld, KEMPLD_GPIO_DIR_NUM(offset), offset, 0);
	kempld_release_mutex(pld);

	return 0;
}

static int kempld_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
					int value)
{
	struct kempld_gpio_data *gpio
		= container_of(chip, struct kempld_gpio_data, chip);
	struct kempld_device_data *pld = gpio->pld;

	kempld_get_mutex(pld);
	kempld_gpio_bitop(pld, KEMPLD_GPIO_LVL_NUM(offset), offset, value);
	kempld_gpio_bitop(pld, KEMPLD_GPIO_DIR_NUM(offset), offset, 1);
	kempld_release_mutex(pld);

	return 0;
}

static int kempld_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct kempld_gpio_data *gpio
		= container_of(chip, struct kempld_gpio_data, chip);
	struct kempld_device_data *pld = gpio->pld;

	return !kempld_gpio_get_bit(pld, KEMPLD_GPIO_DIR_NUM(offset), offset);
}

static int kempld_gpio_pincount(struct kempld_device_data *pld)
{
	u16 evt, evt_back;

	kempld_get_mutex(pld);

	/* Backup event register as it might be already initialized */
	evt_back = kempld_read16(pld, KEMPLD_GPIO_EVT_LVL_EDGE);
	/* Clear event register */
	kempld_write16(pld, KEMPLD_GPIO_EVT_LVL_EDGE, 0x0000);
	/* Read back event register */
	evt = kempld_read16(pld, KEMPLD_GPIO_EVT_LVL_EDGE);
	/* Restore event register */
	kempld_write16(pld, KEMPLD_GPIO_EVT_LVL_EDGE, evt_back);

	kempld_release_mutex(pld);

	return evt ? __ffs(evt) : 16;
}

static int kempld_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct kempld_device_data *pld = dev_get_drvdata(dev->parent);
	struct kempld_platform_data *pdata = dev_get_platdata(pld->dev);
	struct kempld_gpio_data *gpio;
	struct gpio_chip *chip;
	int ret;

	if (pld->info.spec_major < 2) {
		dev_err(dev,
			"Driver only supports GPIO devices compatible to PLD spec. rev. 2.0 or higher\n");
		return -ENODEV;
	}

	gpio = devm_kzalloc(dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->pld = pld;

	platform_set_drvdata(pdev, gpio);

	chip = &gpio->chip;
	chip->label = "gpio-kempld";
	chip->owner = THIS_MODULE;
	chip->dev = dev;
	chip->can_sleep = true;
	if (pdata && pdata->gpio_base)
		chip->base = pdata->gpio_base;
	else
		chip->base = -1;
	chip->direction_input = kempld_gpio_direction_input;
	chip->direction_output = kempld_gpio_direction_output;
	chip->get_direction = kempld_gpio_get_direction;
	chip->get = kempld_gpio_get;
	chip->set = kempld_gpio_set;
	chip->ngpio = kempld_gpio_pincount(pld);
	if (chip->ngpio == 0) {
		dev_err(dev, "No GPIO pins detected\n");
		return -ENODEV;
	}

	ret = gpiochip_add(chip);
	if (ret) {
		dev_err(dev, "Could not register GPIO chip\n");
		return ret;
	}

	dev_info(dev, "GPIO functionality initialized with %d pins\n",
		 chip->ngpio);

	return 0;
}

static int kempld_gpio_remove(struct platform_device *pdev)
{
	struct kempld_gpio_data *gpio = platform_get_drvdata(pdev);

	gpiochip_remove(&gpio->chip);
	return 0;
}

static struct platform_driver kempld_gpio_driver = {
	.driver = {
		.name = "kempld-gpio",
	},
	.probe		= kempld_gpio_probe,
	.remove		= kempld_gpio_remove,
};

module_platform_driver(kempld_gpio_driver);

MODULE_DESCRIPTION("KEM PLD GPIO Driver");
MODULE_AUTHOR("Michael Brunner <michael.brunner@kontron.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:kempld-gpio");

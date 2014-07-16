/*
 * GPIO driver for RICOH583 power management chip.
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 * Author: Laxman dewangan <ldewangan@nvidia.com>
 *
 * Based on code
 *	Copyright (C) 2011 RICOH COMPANY,LTD
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/mfd/rc5t583.h>

struct rc5t583_gpio {
	struct gpio_chip gpio_chip;
	struct rc5t583 *rc5t583;
};

static inline struct rc5t583_gpio *to_rc5t583_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct rc5t583_gpio, gpio_chip);
}

static int rc5t583_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct rc5t583_gpio *rc5t583_gpio = to_rc5t583_gpio(gc);
	struct device *parent = rc5t583_gpio->rc5t583->dev;
	uint8_t val = 0;
	int ret;

	ret = rc5t583_read(parent, RC5T583_GPIO_MON_IOIN, &val);
	if (ret < 0)
		return ret;

	return !!(val & BIT(offset));
}

static void rc5t583_gpio_set(struct gpio_chip *gc, unsigned int offset, int val)
{
	struct rc5t583_gpio *rc5t583_gpio = to_rc5t583_gpio(gc);
	struct device *parent = rc5t583_gpio->rc5t583->dev;
	if (val)
		rc5t583_set_bits(parent, RC5T583_GPIO_IOOUT, BIT(offset));
	else
		rc5t583_clear_bits(parent, RC5T583_GPIO_IOOUT, BIT(offset));
}

static int rc5t583_gpio_dir_input(struct gpio_chip *gc, unsigned int offset)
{
	struct rc5t583_gpio *rc5t583_gpio = to_rc5t583_gpio(gc);
	struct device *parent = rc5t583_gpio->rc5t583->dev;
	int ret;

	ret = rc5t583_clear_bits(parent, RC5T583_GPIO_IOSEL, BIT(offset));
	if (ret < 0)
		return ret;

	/* Set pin to gpio mode */
	return rc5t583_clear_bits(parent, RC5T583_GPIO_PGSEL, BIT(offset));
}

static int rc5t583_gpio_dir_output(struct gpio_chip *gc, unsigned offset,
			int value)
{
	struct rc5t583_gpio *rc5t583_gpio = to_rc5t583_gpio(gc);
	struct device *parent = rc5t583_gpio->rc5t583->dev;
	int ret;

	rc5t583_gpio_set(gc, offset, value);
	ret = rc5t583_set_bits(parent, RC5T583_GPIO_IOSEL, BIT(offset));
	if (ret < 0)
		return ret;

	/* Set pin to gpio mode */
	return rc5t583_clear_bits(parent, RC5T583_GPIO_PGSEL, BIT(offset));
}

static int rc5t583_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct rc5t583_gpio *rc5t583_gpio = to_rc5t583_gpio(gc);

	if (offset < RC5T583_MAX_GPIO)
		return rc5t583_gpio->rc5t583->irq_base +
				RC5T583_IRQ_GPIO0 + offset;
	return -EINVAL;
}

static void rc5t583_gpio_free(struct gpio_chip *gc, unsigned offset)
{
	struct rc5t583_gpio *rc5t583_gpio = to_rc5t583_gpio(gc);
	struct device *parent = rc5t583_gpio->rc5t583->dev;

	rc5t583_set_bits(parent, RC5T583_GPIO_PGSEL, BIT(offset));
}

static int rc5t583_gpio_probe(struct platform_device *pdev)
{
	struct rc5t583 *rc5t583 = dev_get_drvdata(pdev->dev.parent);
	struct rc5t583_platform_data *pdata = dev_get_platdata(rc5t583->dev);
	struct rc5t583_gpio *rc5t583_gpio;

	rc5t583_gpio = devm_kzalloc(&pdev->dev, sizeof(*rc5t583_gpio),
					GFP_KERNEL);
	if (!rc5t583_gpio)
		return -ENOMEM;

	rc5t583_gpio->gpio_chip.label = "gpio-rc5t583",
	rc5t583_gpio->gpio_chip.owner = THIS_MODULE,
	rc5t583_gpio->gpio_chip.free = rc5t583_gpio_free,
	rc5t583_gpio->gpio_chip.direction_input = rc5t583_gpio_dir_input,
	rc5t583_gpio->gpio_chip.direction_output = rc5t583_gpio_dir_output,
	rc5t583_gpio->gpio_chip.set = rc5t583_gpio_set,
	rc5t583_gpio->gpio_chip.get = rc5t583_gpio_get,
	rc5t583_gpio->gpio_chip.to_irq = rc5t583_gpio_to_irq,
	rc5t583_gpio->gpio_chip.ngpio = RC5T583_MAX_GPIO,
	rc5t583_gpio->gpio_chip.can_sleep = true,
	rc5t583_gpio->gpio_chip.dev = &pdev->dev;
	rc5t583_gpio->gpio_chip.base = -1;
	rc5t583_gpio->rc5t583 = rc5t583;

	if (pdata && pdata->gpio_base)
		rc5t583_gpio->gpio_chip.base = pdata->gpio_base;

	platform_set_drvdata(pdev, rc5t583_gpio);

	return gpiochip_add(&rc5t583_gpio->gpio_chip);
}

static int rc5t583_gpio_remove(struct platform_device *pdev)
{
	struct rc5t583_gpio *rc5t583_gpio = platform_get_drvdata(pdev);

	return gpiochip_remove(&rc5t583_gpio->gpio_chip);
}

static struct platform_driver rc5t583_gpio_driver = {
	.driver = {
		.name    = "rc5t583-gpio",
		.owner   = THIS_MODULE,
	},
	.probe		= rc5t583_gpio_probe,
	.remove		= rc5t583_gpio_remove,
};

static int __init rc5t583_gpio_init(void)
{
	return platform_driver_register(&rc5t583_gpio_driver);
}
subsys_initcall(rc5t583_gpio_init);

static void __exit rc5t583_gpio_exit(void)
{
	platform_driver_unregister(&rc5t583_gpio_driver);
}
module_exit(rc5t583_gpio_exit);

MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_DESCRIPTION("GPIO interface for RC5T583");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:rc5t583-gpio");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Philips UCB1400 GPIO driver
 *
 * Author: Marek Vasut <marek.vasut@gmail.com>
 */

#include <linux/module.h>
#include <linux/ucb1400.h>
#include <linux/gpio/driver.h>

static int ucb1400_gpio_dir_in(struct gpio_chip *gc, unsigned off)
{
	struct ucb1400_gpio *gpio;
	gpio = gpiochip_get_data(gc);
	ucb1400_gpio_set_direction(gpio->ac97, off, 0);
	return 0;
}

static int ucb1400_gpio_dir_out(struct gpio_chip *gc, unsigned off, int val)
{
	struct ucb1400_gpio *gpio;
	gpio = gpiochip_get_data(gc);
	ucb1400_gpio_set_direction(gpio->ac97, off, 1);
	ucb1400_gpio_set_value(gpio->ac97, off, val);
	return 0;
}

static int ucb1400_gpio_get(struct gpio_chip *gc, unsigned off)
{
	struct ucb1400_gpio *gpio;

	gpio = gpiochip_get_data(gc);
	return !!ucb1400_gpio_get_value(gpio->ac97, off);
}

static void ucb1400_gpio_set(struct gpio_chip *gc, unsigned off, int val)
{
	struct ucb1400_gpio *gpio;
	gpio = gpiochip_get_data(gc);
	ucb1400_gpio_set_value(gpio->ac97, off, val);
}

static int ucb1400_gpio_probe(struct platform_device *dev)
{
	struct ucb1400_gpio *ucb = dev_get_platdata(&dev->dev);
	int err = 0;

	if (!(ucb && ucb->gpio_offset)) {
		err = -EINVAL;
		goto err;
	}

	platform_set_drvdata(dev, ucb);

	ucb->gc.label = "ucb1400_gpio";
	ucb->gc.base = ucb->gpio_offset;
	ucb->gc.ngpio = 10;
	ucb->gc.owner = THIS_MODULE;

	ucb->gc.direction_input = ucb1400_gpio_dir_in;
	ucb->gc.direction_output = ucb1400_gpio_dir_out;
	ucb->gc.get = ucb1400_gpio_get;
	ucb->gc.set = ucb1400_gpio_set;
	ucb->gc.can_sleep = true;

	err = devm_gpiochip_add_data(&dev->dev, &ucb->gc, ucb);

err:
	return err;

}

static struct platform_driver ucb1400_gpio_driver = {
	.probe	= ucb1400_gpio_probe,
	.driver	= {
		.name	= "ucb1400_gpio"
	},
};

module_platform_driver(ucb1400_gpio_driver);

MODULE_DESCRIPTION("Philips UCB1400 GPIO driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ucb1400_gpio");

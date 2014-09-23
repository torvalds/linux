/*
 * GPIO Greybus driver.
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include "greybus.h"

struct gb_gpio_device {
	struct gpio_chip chip;
	struct greybus_module *gmod;
	struct gpio_chip *gpio;
	// FIXME - some lock?
};

static const struct greybus_module_id id_table[] = {
	{ GREYBUS_DEVICE(0x44, 0x44) },	/* make shit up */
	{ },	/* terminating NULL entry */
};

static int direction_input(struct gpio_chip *gpio, unsigned nr)
{
	//struct gb_gpio_device *gb_gpio_dev = container_of(gpio, struct gb_gpio_device, chip);

	// FIXME - do something there
	return 0;
}

static int direction_output(struct gpio_chip *gpio, unsigned nr, int val)
{
	// FIXME - do something there
	return 0;
}

static int gpio_get(struct gpio_chip *gpio, unsigned nr)
{
	// FIXME - do something there
	return 0;
}

static void gpio_set(struct gpio_chip *gpio, unsigned nr, int val)
{
	// FIXME - do something there
}

int gb_gpio_probe(struct greybus_module *gmod,
		  const struct greybus_module_id *id)
{
	struct gb_gpio_device *gb_gpio;
	struct gpio_chip *gpio;
	struct device *dev = &gmod->dev;
	int retval;

	gb_gpio = kzalloc(sizeof(*gb_gpio), GFP_KERNEL);
	if (!gb_gpio)
		return -ENOMEM;
	gb_gpio->gmod = gmod;

	gpio = &gb_gpio->chip;

	gpio->label = "greybus_gpio";
	gpio->owner = THIS_MODULE;
	gpio->direction_input = direction_input;
	gpio->direction_output = direction_output;
	gpio->get = gpio_get;
	gpio->set = gpio_set;
	gpio->dbg_show = NULL;
	gpio->base = 0;			// FIXME!!!
	gpio->ngpio = 42;		// FIXME!!!
	gpio->can_sleep = false;	// FIXME!!!

	gmod->gb_gpio_dev = gb_gpio;

	retval = gpiochip_add(gpio);
	if (retval) {
		dev_err(dev, "Failed to register GPIO\n");
		return retval;
	}

	return 0;
}

void gb_gpio_disconnect(struct greybus_module *gmod)
{
	struct gb_gpio_device *gb_gpio_dev;
	int retval;

	gb_gpio_dev = gmod->gb_gpio_dev;

	retval = gpiochip_remove(&gb_gpio_dev->chip);
	kfree(gb_gpio_dev);
}

#if 0
static struct greybus_driver gpio_gb_driver = {
	.probe =	gb_gpio_probe,
	.disconnect =	gb_gpio_disconnect,
	.id_table =	id_table,
};

module_greybus_driver(gpio_gb_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Greybus GPIO driver");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
#endif

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

struct gb_gpio {
	struct gpio_chip chip;
	struct greybus_device *gdev;
	// FIXME - some lock?
};

static const struct greybus_device_id id_table[] = {
	{ GREYBUS_DEVICE(0x44, 0x44) },	/* make shit up */
	{ },	/* terminating NULL entry */
};

static int direction_input(struct gpio_chip *gpio, unsigned nr)
{
	struct gp_gpio *gp_gpio = container_of(gpio, struct gp_gpio, chip);

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

static int gpio_set(struct gpio_chip *gpio, unsigned nr, int val)
{
	// FIXME - do something there
	return 0;
}

static int gpio_gb_probe(struct greybus_device *gdev, const struct greybus_device_id *id)
{
	struct gp_gpio *gp_gpio;
	struct gpio_chip *gpio;
	struct device *dev = &gdev->dev;

	gp_gpio = devm_kzalloc(dev, sizeof(*gp_gpio), GFP_KERNEL);
	if (!gp_gpio)
		return -ENOMEM;
	gp_gpio->gdev = gdev;

	gpio = &gp_gpio->gpio;

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

	greybus_set_drvdata(gdev, gp_gpio);

	retval = gpio_chip_add(gpio);
	if (retval) {
		dev_err(dev, "Failed to register GPIO\n");
		return retval;
	}

	return 0;
}

static void gpio_gb_disconnect(struct greybus_device *gdev)
{
	struct mmc_host *mmc;
	struct sd_gb_host *host;

	host = greybus_get_drvdata(gdev);
	mmc = host->mmc;

	mmc_remove_host(mmc);
	mmc_free_host(mmc);
}

static struct greybus_driver gpio_gb_driver = {
	.probe =	gpio_gb_probe,
	.disconnect =	gpio_gb_disconnect,
	.id_table =	id_table,
};

module_greybus_driver(gpio_gb_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Greybus GPIO driver");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");

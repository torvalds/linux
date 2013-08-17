/*
 * drivers/gpio/devres.c - managed gpio resources
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This file is based on kernel/irq/devres.c
 *
 * Copyright (c) 2011 John Crispin <blogic@openwrt.org>
 */

#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/gfp.h>

static void devm_gpio_release(struct device *dev, void *res)
{
	unsigned *gpio = res;

	gpio_free(*gpio);
}

static int devm_gpio_match(struct device *dev, void *res, void *data)
{
	unsigned *this = res, *gpio = data;

	return *this == *gpio;
}

/**
 *      devm_gpio_request - request a gpio for a managed device
 *      @dev: device to request the gpio for
 *      @gpio: gpio to allocate
 *      @label: the name of the requested gpio
 *
 *      Except for the extra @dev argument, this function takes the
 *      same arguments and performs the same function as
 *      gpio_request().  GPIOs requested with this function will be
 *      automatically freed on driver detach.
 *
 *      If an GPIO allocated with this function needs to be freed
 *      separately, devm_gpio_free() must be used.
 */

int devm_gpio_request(struct device *dev, unsigned gpio, const char *label)
{
	unsigned *dr;
	int rc;

	dr = devres_alloc(devm_gpio_release, sizeof(unsigned), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	rc = gpio_request(gpio, label);
	if (rc) {
		devres_free(dr);
		return rc;
	}

	*dr = gpio;
	devres_add(dev, dr);

	return 0;
}
EXPORT_SYMBOL(devm_gpio_request);

/**
 *      devm_gpio_free - free an interrupt
 *      @dev: device to free gpio for
 *      @gpio: gpio to free
 *
 *      Except for the extra @dev argument, this function takes the
 *      same arguments and performs the same function as gpio_free().
 *      This function instead of gpio_free() should be used to manually
 *      free GPIOs allocated with devm_gpio_request().
 */
void devm_gpio_free(struct device *dev, unsigned int gpio)
{

	WARN_ON(devres_destroy(dev, devm_gpio_release, devm_gpio_match,
		&gpio));
	gpio_free(gpio);
}
EXPORT_SYMBOL(devm_gpio_free);

// SPDX-License-Identifier: GPL-2.0+
/* Framework for MDIO devices, other than PHYs.
 *
 * Copyright (c) 2016 Andrew Lunn <andrew@lunn.ch>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mdio.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/delay.h>

void mdio_device_free(struct mdio_device *mdiodev)
{
	put_device(&mdiodev->dev);
}
EXPORT_SYMBOL(mdio_device_free);

static void mdio_device_release(struct device *dev)
{
	kfree(to_mdio_device(dev));
}

int mdio_device_bus_match(struct device *dev, struct device_driver *drv)
{
	struct mdio_device *mdiodev = to_mdio_device(dev);
	struct mdio_driver *mdiodrv = to_mdio_driver(drv);

	if (mdiodrv->mdiodrv.flags & MDIO_DEVICE_IS_PHY)
		return 0;

	return strcmp(mdiodev->modalias, drv->name) == 0;
}

struct mdio_device *mdio_device_create(struct mii_bus *bus, int addr)
{
	struct mdio_device *mdiodev;

	/* We allocate the device, and initialize the default values */
	mdiodev = kzalloc(sizeof(*mdiodev), GFP_KERNEL);
	if (!mdiodev)
		return ERR_PTR(-ENOMEM);

	mdiodev->dev.release = mdio_device_release;
	mdiodev->dev.parent = &bus->dev;
	mdiodev->dev.bus = &mdio_bus_type;
	mdiodev->device_free = mdio_device_free;
	mdiodev->device_remove = mdio_device_remove;
	mdiodev->bus = bus;
	mdiodev->addr = addr;

	dev_set_name(&mdiodev->dev, PHY_ID_FMT, bus->id, addr);

	device_initialize(&mdiodev->dev);

	return mdiodev;
}
EXPORT_SYMBOL(mdio_device_create);

/**
 * mdio_device_register - Register the mdio device on the MDIO bus
 * @mdiodev: mdio_device structure to be added to the MDIO bus
 */
int mdio_device_register(struct mdio_device *mdiodev)
{
	int err;

	dev_dbg(&mdiodev->dev, "mdio_device_register\n");

	err = mdiobus_register_device(mdiodev);
	if (err)
		return err;

	err = device_add(&mdiodev->dev);
	if (err) {
		pr_err("MDIO %d failed to add\n", mdiodev->addr);
		goto out;
	}

	return 0;

 out:
	mdiobus_unregister_device(mdiodev);
	return err;
}
EXPORT_SYMBOL(mdio_device_register);

/**
 * mdio_device_remove - Remove a previously registered mdio device from the
 *			MDIO bus
 * @mdiodev: mdio_device structure to remove
 *
 * This doesn't free the mdio_device itself, it merely reverses the effects
 * of mdio_device_register(). Use mdio_device_free() to free the device
 * after calling this function.
 */
void mdio_device_remove(struct mdio_device *mdiodev)
{
	device_del(&mdiodev->dev);
	mdiobus_unregister_device(mdiodev);
}
EXPORT_SYMBOL(mdio_device_remove);

void mdio_device_reset(struct mdio_device *mdiodev, int value)
{
	unsigned int d;

	if (!mdiodev->reset)
		return;

	gpiod_set_value(mdiodev->reset, value);

	d = value ? mdiodev->reset_assert_delay : mdiodev->reset_deassert_delay;
	if (d)
		usleep_range(d, d + max_t(unsigned int, d / 10, 100));
}
EXPORT_SYMBOL(mdio_device_reset);

/**
 * mdio_probe - probe an MDIO device
 * @dev: device to probe
 *
 * Description: Take care of setting up the mdio_device structure
 * and calling the driver to probe the device.
 */
static int mdio_probe(struct device *dev)
{
	struct mdio_device *mdiodev = to_mdio_device(dev);
	struct device_driver *drv = mdiodev->dev.driver;
	struct mdio_driver *mdiodrv = to_mdio_driver(drv);
	int err = 0;

	if (mdiodrv->probe) {
		/* Deassert the reset signal */
		mdio_device_reset(mdiodev, 0);

		err = mdiodrv->probe(mdiodev);
		if (err) {
			/* Assert the reset signal */
			mdio_device_reset(mdiodev, 1);
		}
	}

	return err;
}

static int mdio_remove(struct device *dev)
{
	struct mdio_device *mdiodev = to_mdio_device(dev);
	struct device_driver *drv = mdiodev->dev.driver;
	struct mdio_driver *mdiodrv = to_mdio_driver(drv);

	if (mdiodrv->remove) {
		mdiodrv->remove(mdiodev);

		/* Assert the reset signal */
		mdio_device_reset(mdiodev, 1);
	}

	return 0;
}

/**
 * mdio_driver_register - register an mdio_driver with the MDIO layer
 * @new_driver: new mdio_driver to register
 */
int mdio_driver_register(struct mdio_driver *drv)
{
	struct mdio_driver_common *mdiodrv = &drv->mdiodrv;
	int retval;

	pr_debug("mdio_driver_register: %s\n", mdiodrv->driver.name);

	mdiodrv->driver.bus = &mdio_bus_type;
	mdiodrv->driver.probe = mdio_probe;
	mdiodrv->driver.remove = mdio_remove;

	retval = driver_register(&mdiodrv->driver);
	if (retval) {
		pr_err("%s: Error %d in registering driver\n",
		       mdiodrv->driver.name, retval);

		return retval;
	}

	return 0;
}
EXPORT_SYMBOL(mdio_driver_register);

void mdio_driver_unregister(struct mdio_driver *drv)
{
	struct mdio_driver_common *mdiodrv = &drv->mdiodrv;

	driver_unregister(&mdiodrv->driver);
}
EXPORT_SYMBOL(mdio_driver_unregister);

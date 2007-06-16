/*
 *  linux/drivers/mmc/core/sdio_bus.c
 *
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * SDIO function driver model
 */

#include <linux/device.h>
#include <linux/err.h>

#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>

#include "sdio_cis.h"
#include "sdio_bus.h"

#define dev_to_sdio_func(d)	container_of(d, struct sdio_func, dev)

/*
 * This currently matches any SDIO function to any driver in order
 * to help initial development and testing.
 */
static int sdio_bus_match(struct device *dev, struct device_driver *drv)
{
	return 1;
}

static int
sdio_bus_uevent(struct device *dev, char **envp, int num_envp, char *buf,
		int buf_size)
{
	envp[0] = NULL;

	return 0;
}

static int sdio_bus_probe(struct device *dev)
{
	return -ENODEV;
}

static int sdio_bus_remove(struct device *dev)
{
	return 0;
}

static struct bus_type sdio_bus_type = {
	.name		= "sdio",
	.match		= sdio_bus_match,
	.uevent		= sdio_bus_uevent,
	.probe		= sdio_bus_probe,
	.remove		= sdio_bus_remove,
};

int sdio_register_bus(void)
{
	return bus_register(&sdio_bus_type);
}

void sdio_unregister_bus(void)
{
	bus_unregister(&sdio_bus_type);
}

/**
 *	sdio_register_driver - register a function driver
 *	@drv: SDIO function driver
 */
int sdio_register_driver(struct sdio_driver *drv)
{
	drv->drv.name = drv->name;
	drv->drv.bus = &sdio_bus_type;
	return driver_register(&drv->drv);
}
EXPORT_SYMBOL_GPL(sdio_register_driver);

/**
 *	sdio_unregister_driver - unregister a function driver
 *	@drv: SDIO function driver
 */
void sdio_unregister_driver(struct sdio_driver *drv)
{
	drv->drv.bus = &sdio_bus_type;
	driver_unregister(&drv->drv);
}
EXPORT_SYMBOL_GPL(sdio_unregister_driver);

static void sdio_release_func(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);

	sdio_free_cis(func);

	kfree(func);
}

/*
 * Allocate and initialise a new SDIO function structure.
 */
struct sdio_func *sdio_alloc_func(struct mmc_card *card)
{
	struct sdio_func *func;

	func = kmalloc(sizeof(struct sdio_func), GFP_KERNEL);
	if (!func)
		return ERR_PTR(-ENOMEM);

	memset(func, 0, sizeof(struct sdio_func));

	func->card = card;

	device_initialize(&func->dev);

	func->dev.parent = &card->dev;
	func->dev.bus = &sdio_bus_type;
	func->dev.release = sdio_release_func;

	return func;
}

/*
 * Register a new SDIO function with the driver model.
 */
int sdio_add_func(struct sdio_func *func)
{
	int ret;

	snprintf(func->dev.bus_id, sizeof(func->dev.bus_id),
		 "%s:%d", mmc_card_id(func->card), func->num);

	ret = device_add(&func->dev);
	if (ret == 0)
		sdio_func_set_present(func);

	return ret;
}

/*
 * Unregister a SDIO function with the driver model, and
 * (eventually) free it.
 */
void sdio_remove_func(struct sdio_func *func)
{
	if (sdio_func_present(func))
		device_del(&func->dev);

	put_device(&func->dev);
}


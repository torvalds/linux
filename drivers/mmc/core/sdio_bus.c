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
#define to_sdio_driver(d)      container_of(d, struct sdio_driver, drv)

static const struct sdio_device_id *sdio_match_one(struct sdio_func *func,
	const struct sdio_device_id *id)
{
	if (id->class != (__u8)SDIO_ANY_ID && id->class != func->class)
		return NULL;
	if (id->vendor != (__u16)SDIO_ANY_ID && id->vendor != func->vendor)
		return NULL;
	if (id->device != (__u16)SDIO_ANY_ID && id->device != func->device)
		return NULL;
	return id;
}

static const struct sdio_device_id *sdio_match_device(struct sdio_func *func,
	struct sdio_driver *sdrv)
{
	const struct sdio_device_id *ids;

	ids = sdrv->id_table;

	if (ids) {
		while (ids->class || ids->vendor || ids->device) {
			if (sdio_match_one(func, ids))
				return ids;
			ids++;
		}
	}

	return NULL;
}

static int sdio_bus_match(struct device *dev, struct device_driver *drv)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct sdio_driver *sdrv = to_sdio_driver(drv);

	if (sdio_match_device(func, sdrv))
		return 1;

	return 0;
}

static int
sdio_bus_uevent(struct device *dev, char **envp, int num_envp, char *buf,
		int buf_size)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	int i = 0, length = 0;

	if (add_uevent_var(envp, num_envp, &i,
			buf, buf_size, &length,
			"SDIO_CLASS=%02X", func->class))
		return -ENOMEM;

	if (add_uevent_var(envp, num_envp, &i,
			buf, buf_size, &length,
			"SDIO_ID=%04X:%04X", func->vendor, func->device))
		return -ENOMEM;

	if (add_uevent_var(envp, num_envp, &i,
			buf, buf_size, &length,
			"MODALIAS=sdio:c%02Xv%04Xd%04X",
			func->class, func->vendor, func->device))
		return -ENOMEM;

	envp[i] = NULL;

	return 0;
}

static int sdio_bus_probe(struct device *dev)
{
	struct sdio_driver *drv = to_sdio_driver(dev->driver);
	struct sdio_func *func = dev_to_sdio_func(dev);
	const struct sdio_device_id *id;

	id = sdio_match_device(func, drv);
	if (!id)
		return -ENODEV;

	return drv->probe(func, id);
}

static int sdio_bus_remove(struct device *dev)
{
	struct sdio_driver *drv = to_sdio_driver(dev->driver);
	struct sdio_func *func = dev_to_sdio_func(dev);

	drv->remove(func);

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

	sdio_free_func_cis(func);

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


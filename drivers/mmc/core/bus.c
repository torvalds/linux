/*
 *  linux/drivers/mmc/core/bus.c
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright (C) 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  MMC card bus driver model
 */

#include <linux/device.h>
#include <linux/err.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>

#include "sysfs.h"
#include "core.h"
#include "bus.h"

#define dev_to_mmc_card(d)	container_of(d, struct mmc_card, dev)
#define to_mmc_driver(d)	container_of(d, struct mmc_driver, drv)

static ssize_t mmc_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mmc_card *card = dev_to_mmc_card(dev);

	switch (card->type) {
	case MMC_TYPE_MMC:
		return sprintf(buf, "MMC\n");
	case MMC_TYPE_SD:
		return sprintf(buf, "SD\n");
	default:
		return -EFAULT;
	}
}

static struct device_attribute mmc_dev_attrs[] = {
	MMC_ATTR_RO(type),
	__ATTR_NULL,
};

/*
 * This currently matches any MMC driver to any MMC card - drivers
 * themselves make the decision whether to drive this card in their
 * probe method.
 */
static int mmc_bus_match(struct device *dev, struct device_driver *drv)
{
	return 1;
}

static int
mmc_bus_uevent(struct device *dev, char **envp, int num_envp, char *buf,
		int buf_size)
{
	struct mmc_card *card = dev_to_mmc_card(dev);
	int retval = 0, i = 0, length = 0;

#define add_env(fmt,val) do {					\
	retval = add_uevent_var(envp, num_envp, &i,		\
				buf, buf_size, &length,		\
				fmt, val);			\
	if (retval)						\
		return retval;					\
} while (0);

	switch (card->type) {
	case MMC_TYPE_MMC:
		add_env("MMC_TYPE=%s", "MMC");
		break;
	case MMC_TYPE_SD:
		add_env("MMC_TYPE=%s", "SD");
		break;
	}

	add_env("MMC_NAME=%s", mmc_card_name(card));

#undef add_env

	envp[i] = NULL;

	return 0;
}

static int mmc_bus_probe(struct device *dev)
{
	struct mmc_driver *drv = to_mmc_driver(dev->driver);
	struct mmc_card *card = dev_to_mmc_card(dev);

	return drv->probe(card);
}

static int mmc_bus_remove(struct device *dev)
{
	struct mmc_driver *drv = to_mmc_driver(dev->driver);
	struct mmc_card *card = dev_to_mmc_card(dev);

	drv->remove(card);

	return 0;
}

static int mmc_bus_suspend(struct device *dev, pm_message_t state)
{
	struct mmc_driver *drv = to_mmc_driver(dev->driver);
	struct mmc_card *card = dev_to_mmc_card(dev);
	int ret = 0;

	if (dev->driver && drv->suspend)
		ret = drv->suspend(card, state);
	return ret;
}

static int mmc_bus_resume(struct device *dev)
{
	struct mmc_driver *drv = to_mmc_driver(dev->driver);
	struct mmc_card *card = dev_to_mmc_card(dev);
	int ret = 0;

	if (dev->driver && drv->resume)
		ret = drv->resume(card);
	return ret;
}

static struct bus_type mmc_bus_type = {
	.name		= "mmc",
	.dev_attrs	= mmc_dev_attrs,
	.match		= mmc_bus_match,
	.uevent		= mmc_bus_uevent,
	.probe		= mmc_bus_probe,
	.remove		= mmc_bus_remove,
	.suspend	= mmc_bus_suspend,
	.resume		= mmc_bus_resume,
};

int mmc_register_bus(void)
{
	return bus_register(&mmc_bus_type);
}

void mmc_unregister_bus(void)
{
	bus_unregister(&mmc_bus_type);
}

/**
 *	mmc_register_driver - register a media driver
 *	@drv: MMC media driver
 */
int mmc_register_driver(struct mmc_driver *drv)
{
	drv->drv.bus = &mmc_bus_type;
	return driver_register(&drv->drv);
}

EXPORT_SYMBOL(mmc_register_driver);

/**
 *	mmc_unregister_driver - unregister a media driver
 *	@drv: MMC media driver
 */
void mmc_unregister_driver(struct mmc_driver *drv)
{
	drv->drv.bus = &mmc_bus_type;
	driver_unregister(&drv->drv);
}

EXPORT_SYMBOL(mmc_unregister_driver);

static void mmc_release_card(struct device *dev)
{
	struct mmc_card *card = dev_to_mmc_card(dev);

	kfree(card);
}

/*
 * Allocate and initialise a new MMC card structure.
 */
struct mmc_card *mmc_alloc_card(struct mmc_host *host)
{
	struct mmc_card *card;

	card = kmalloc(sizeof(struct mmc_card), GFP_KERNEL);
	if (!card)
		return ERR_PTR(-ENOMEM);

	memset(card, 0, sizeof(struct mmc_card));

	card->host = host;

	device_initialize(&card->dev);

	card->dev.parent = mmc_classdev(host);
	card->dev.bus = &mmc_bus_type;
	card->dev.release = mmc_release_card;

	return card;
}

/*
 * Register a new MMC card with the driver model.
 */
int mmc_add_card(struct mmc_card *card)
{
	int ret;

	snprintf(card->dev.bus_id, sizeof(card->dev.bus_id),
		 "%s:%04x", mmc_hostname(card->host), card->rca);

	card->dev.uevent_suppress = 1;

	ret = device_add(&card->dev);
	if (ret)
		return ret;

	if (card->host->bus_ops->sysfs_add) {
		ret = card->host->bus_ops->sysfs_add(card->host, card);
		if (ret) {
			device_del(&card->dev);
			return ret;
		 }
	}

	card->dev.uevent_suppress = 0;

	kobject_uevent(&card->dev.kobj, KOBJ_ADD);

	mmc_card_set_present(card);

	return 0;
}

/*
 * Unregister a new MMC card with the driver model, and
 * (eventually) free it.
 */
void mmc_remove_card(struct mmc_card *card)
{
	if (mmc_card_present(card)) {
		if (card->host->bus_ops->sysfs_remove)
			card->host->bus_ops->sysfs_remove(card->host, card);
		device_del(&card->dev);
	}

	put_device(&card->dev);
}


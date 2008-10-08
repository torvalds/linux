/*
 * drivers/net/phy/mdio_bus.c
 *
 * MDIO Bus interface
 *
 * Author: Andy Fleming
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

/**
 * mdiobus_alloc - allocate a mii_bus structure
 *
 * Description: called by a bus driver to allocate an mii_bus
 * structure to fill in.
 */
struct mii_bus *mdiobus_alloc(void)
{
	struct mii_bus *bus;

	bus = kzalloc(sizeof(*bus), GFP_KERNEL);
	if (bus != NULL)
		bus->state = MDIOBUS_ALLOCATED;

	return bus;
}
EXPORT_SYMBOL(mdiobus_alloc);

/**
 * mdiobus_release - mii_bus device release callback
 *
 * Description: called when the last reference to an mii_bus is
 * dropped, to free the underlying memory.
 */
static void mdiobus_release(struct device *d)
{
	struct mii_bus *bus = to_mii_bus(d);
	BUG_ON(bus->state != MDIOBUS_RELEASED);
	kfree(bus);
}

static struct class mdio_bus_class = {
	.name		= "mdio_bus",
	.dev_release	= mdiobus_release,
};

/**
 * mdiobus_register - bring up all the PHYs on a given bus and attach them to bus
 * @bus: target mii_bus
 *
 * Description: Called by a bus driver to bring up all the PHYs
 *   on a given bus, and attach them to the bus.
 *
 * Returns 0 on success or < 0 on error.
 */
int mdiobus_register(struct mii_bus *bus)
{
	int i;
	int err = 0;

	if (NULL == bus || NULL == bus->name ||
			NULL == bus->read ||
			NULL == bus->write)
		return -EINVAL;

	BUG_ON(bus->state != MDIOBUS_ALLOCATED &&
	       bus->state != MDIOBUS_UNREGISTERED);

	bus->dev.parent = bus->parent;
	bus->dev.class = &mdio_bus_class;
	bus->dev.groups = NULL;
	memcpy(bus->dev.bus_id, bus->id, MII_BUS_ID_SIZE);

	err = device_register(&bus->dev);
	if (err) {
		printk(KERN_ERR "mii_bus %s failed to register\n", bus->id);
		return -EINVAL;
	}

	bus->state = MDIOBUS_REGISTERED;

	mutex_init(&bus->mdio_lock);

	if (bus->reset)
		bus->reset(bus);

	for (i = 0; i < PHY_MAX_ADDR; i++) {
		bus->phy_map[i] = NULL;
		if ((bus->phy_mask & (1 << i)) == 0) {
			struct phy_device *phydev;

			phydev = mdiobus_scan(bus, i);
			if (IS_ERR(phydev))
				err = PTR_ERR(phydev);
		}
	}

	pr_info("%s: probed\n", bus->name);

	return err;
}
EXPORT_SYMBOL(mdiobus_register);

void mdiobus_unregister(struct mii_bus *bus)
{
	int i;

	BUG_ON(bus->state != MDIOBUS_REGISTERED);
	bus->state = MDIOBUS_UNREGISTERED;

	device_unregister(&bus->dev);
	for (i = 0; i < PHY_MAX_ADDR; i++) {
		if (bus->phy_map[i])
			device_unregister(&bus->phy_map[i]->dev);
	}
}
EXPORT_SYMBOL(mdiobus_unregister);

/**
 * mdiobus_free - free a struct mii_bus
 * @bus: mii_bus to free
 *
 * This function releases the reference to the underlying device
 * object in the mii_bus.  If this is the last reference, the mii_bus
 * will be freed.
 */
void mdiobus_free(struct mii_bus *bus)
{
	/*
	 * For compatibility with error handling in drivers.
	 */
	if (bus->state == MDIOBUS_ALLOCATED) {
		kfree(bus);
		return;
	}

	BUG_ON(bus->state != MDIOBUS_UNREGISTERED);
	bus->state = MDIOBUS_RELEASED;

	put_device(&bus->dev);
}
EXPORT_SYMBOL(mdiobus_free);

struct phy_device *mdiobus_scan(struct mii_bus *bus, int addr)
{
	struct phy_device *phydev;
	int err;

	phydev = get_phy_device(bus, addr);
	if (IS_ERR(phydev) || phydev == NULL)
		return phydev;

	/* There's a PHY at this address
	 * We need to set:
	 * 1) IRQ
	 * 2) bus_id
	 * 3) parent
	 * 4) bus
	 * 5) mii_bus
	 * And, we need to register it */

	phydev->irq = bus->irq != NULL ? bus->irq[addr] : PHY_POLL;

	phydev->dev.parent = bus->parent;
	phydev->dev.bus = &mdio_bus_type;
	snprintf(phydev->dev.bus_id, BUS_ID_SIZE, PHY_ID_FMT, bus->id, addr);

	phydev->bus = bus;

	/* Run all of the fixups for this PHY */
	phy_scan_fixups(phydev);

	err = device_register(&phydev->dev);
	if (err) {
		printk(KERN_ERR "phy %d failed to register\n", addr);
		phy_device_free(phydev);
		phydev = NULL;
	}

	bus->phy_map[addr] = phydev;

	return phydev;
}
EXPORT_SYMBOL(mdiobus_scan);

/**
 * mdio_bus_match - determine if given PHY driver supports the given PHY device
 * @dev: target PHY device
 * @drv: given PHY driver
 *
 * Description: Given a PHY device, and a PHY driver, return 1 if
 *   the driver supports the device.  Otherwise, return 0.
 */
static int mdio_bus_match(struct device *dev, struct device_driver *drv)
{
	struct phy_device *phydev = to_phy_device(dev);
	struct phy_driver *phydrv = to_phy_driver(drv);

	return ((phydrv->phy_id & phydrv->phy_id_mask) ==
		(phydev->phy_id & phydrv->phy_id_mask));
}

/* Suspend and resume.  Copied from platform_suspend and
 * platform_resume
 */
static int mdio_bus_suspend(struct device * dev, pm_message_t state)
{
	int ret = 0;
	struct device_driver *drv = dev->driver;

	if (drv && drv->suspend)
		ret = drv->suspend(dev, state);

	return ret;
}

static int mdio_bus_resume(struct device * dev)
{
	int ret = 0;
	struct device_driver *drv = dev->driver;

	if (drv && drv->resume)
		ret = drv->resume(dev);

	return ret;
}

struct bus_type mdio_bus_type = {
	.name		= "mdio_bus",
	.match		= mdio_bus_match,
	.suspend	= mdio_bus_suspend,
	.resume		= mdio_bus_resume,
};
EXPORT_SYMBOL(mdio_bus_type);

int __init mdio_bus_init(void)
{
	int ret;

	ret = class_register(&mdio_bus_class);
	if (!ret) {
		ret = bus_register(&mdio_bus_type);
		if (ret)
			class_unregister(&mdio_bus_class);
	}

	return ret;
}

void mdio_bus_exit(void)
{
	class_unregister(&mdio_bus_class);
	bus_unregister(&mdio_bus_type);
}

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
	return kzalloc(sizeof(struct mii_bus), GFP_KERNEL);
}
EXPORT_SYMBOL(mdiobus_alloc);

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
 * This function frees the mii_bus.
 */
void mdiobus_free(struct mii_bus *bus)
{
	kfree(bus);
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
	return bus_register(&mdio_bus_type);
}

void mdio_bus_exit(void)
{
	bus_unregister(&mdio_bus_type);
}

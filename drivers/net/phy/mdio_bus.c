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
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
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
#include <linux/version.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

/* mdiobus_register 
 *
 * description: Called by a bus driver to bring up all the PHYs
 *   on a given bus, and attach them to the bus
 */
int mdiobus_register(struct mii_bus *bus)
{
	int i;
	int err = 0;

	spin_lock_init(&bus->mdio_lock);

	if (NULL == bus || NULL == bus->name ||
			NULL == bus->read ||
			NULL == bus->write)
		return -EINVAL;

	if (bus->reset)
		bus->reset(bus);

	for (i = 0; i < PHY_MAX_ADDR; i++) {
		struct phy_device *phydev;

		phydev = get_phy_device(bus, i);

		if (IS_ERR(phydev))
			return PTR_ERR(phydev);

		/* There's a PHY at this address
		 * We need to set:
		 * 1) IRQ
		 * 2) bus_id
		 * 3) parent
		 * 4) bus
		 * 5) mii_bus
		 * And, we need to register it */
		if (phydev) {
			phydev->irq = bus->irq[i];

			phydev->dev.parent = bus->dev;
			phydev->dev.bus = &mdio_bus_type;
			sprintf(phydev->dev.bus_id, "phy%d:%d", bus->id, i);

			phydev->bus = bus;

			err = device_register(&phydev->dev);

			if (err)
				printk(KERN_ERR "phy %d failed to register\n",
						i);
		}

		bus->phy_map[i] = phydev;
	}

	pr_info("%s: probed\n", bus->name);

	return err;
}
EXPORT_SYMBOL(mdiobus_register);

void mdiobus_unregister(struct mii_bus *bus)
{
	int i;

	for (i = 0; i < PHY_MAX_ADDR; i++) {
		if (bus->phy_map[i]) {
			device_unregister(&bus->phy_map[i]->dev);
			kfree(bus->phy_map[i]);
		}
	}
}
EXPORT_SYMBOL(mdiobus_unregister);

/* mdio_bus_match
 *
 * description: Given a PHY device, and a PHY driver, return 1 if
 *   the driver supports the device.  Otherwise, return 0
 */
static int mdio_bus_match(struct device *dev, struct device_driver *drv)
{
	struct phy_device *phydev = to_phy_device(dev);
	struct phy_driver *phydrv = to_phy_driver(drv);

	return (phydrv->phy_id == (phydev->phy_id & phydrv->phy_id_mask));
}

/* Suspend and resume.  Copied from platform_suspend and
 * platform_resume
 */
static int mdio_bus_suspend(struct device * dev, pm_message_t state)
{
	int ret = 0;
	struct device_driver *drv = dev->driver;

	if (drv && drv->suspend) {
		ret = drv->suspend(dev, state, SUSPEND_DISABLE);
		if (ret == 0)
			ret = drv->suspend(dev, state, SUSPEND_SAVE_STATE);
		if (ret == 0)
			ret = drv->suspend(dev, state, SUSPEND_POWER_DOWN);
	}
	return ret;
}

static int mdio_bus_resume(struct device * dev)
{
	int ret = 0;
	struct device_driver *drv = dev->driver;

	if (drv && drv->resume) {
		ret = drv->resume(dev, RESUME_POWER_ON);
		if (ret == 0)
			ret = drv->resume(dev, RESUME_RESTORE_STATE);
		if (ret == 0)
			ret = drv->resume(dev, RESUME_ENABLE);
	}
	return ret;
}

struct bus_type mdio_bus_type = {
	.name		= "mdio_bus",
	.match		= mdio_bus_match,
	.suspend	= mdio_bus_suspend,
	.resume		= mdio_bus_resume,
};

int __init mdio_bus_init(void)
{
	return bus_register(&mdio_bus_type);
}

void mdio_bus_exit(void)
{
	bus_unregister(&mdio_bus_type);
}

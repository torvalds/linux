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
#include <linux/device.h>
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
 * @d: the target struct device that contains the mii_bus
 *
 * Description: called when the last reference to an mii_bus is
 * dropped, to free the underlying memory.
 */
static void mdiobus_release(struct device *d)
{
	struct mii_bus *bus = to_mii_bus(d);
	BUG_ON(bus->state != MDIOBUS_RELEASED &&
	       /* for compatibility with error handling in drivers */
	       bus->state != MDIOBUS_ALLOCATED);
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
	int i, err;

	if (NULL == bus || NULL == bus->name ||
			NULL == bus->read ||
			NULL == bus->write)
		return -EINVAL;

	BUG_ON(bus->state != MDIOBUS_ALLOCATED &&
	       bus->state != MDIOBUS_UNREGISTERED);

	bus->dev.parent = bus->parent;
	bus->dev.class = &mdio_bus_class;
	bus->dev.groups = NULL;
	dev_set_name(&bus->dev, "%s", bus->id);

	err = device_register(&bus->dev);
	if (err) {
		printk(KERN_ERR "mii_bus %s failed to register\n", bus->id);
		return -EINVAL;
	}

	mutex_init(&bus->mdio_lock);

	if (bus->reset)
		bus->reset(bus);

	for (i = 0; i < PHY_MAX_ADDR; i++) {
		if ((bus->phy_mask & (1 << i)) == 0) {
			struct phy_device *phydev;

			phydev = mdiobus_scan(bus, i);
			if (IS_ERR(phydev)) {
				err = PTR_ERR(phydev);
				goto error;
			}
		}
	}

	bus->state = MDIOBUS_REGISTERED;
	pr_info("%s: probed\n", bus->name);
	return 0;

error:
	while (--i >= 0) {
		if (bus->phy_map[i])
			device_unregister(&bus->phy_map[i]->dev);
	}
	device_del(&bus->dev);
	return err;
}
EXPORT_SYMBOL(mdiobus_register);

void mdiobus_unregister(struct mii_bus *bus)
{
	int i;

	BUG_ON(bus->state != MDIOBUS_REGISTERED);
	bus->state = MDIOBUS_UNREGISTERED;

	device_del(&bus->dev);
	for (i = 0; i < PHY_MAX_ADDR; i++) {
		if (bus->phy_map[i])
			device_unregister(&bus->phy_map[i]->dev);
		bus->phy_map[i] = NULL;
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

	err = phy_device_register(phydev);
	if (err) {
		phy_device_free(phydev);
		return NULL;
	}

	return phydev;
}
EXPORT_SYMBOL(mdiobus_scan);

/**
 * mdiobus_read - Convenience function for reading a given MII mgmt register
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to read
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
int mdiobus_read(struct mii_bus *bus, int addr, u16 regnum)
{
	int retval;

	BUG_ON(in_interrupt());

	mutex_lock(&bus->mdio_lock);
	retval = bus->read(bus, addr, regnum);
	mutex_unlock(&bus->mdio_lock);

	return retval;
}
EXPORT_SYMBOL(mdiobus_read);

/**
 * mdiobus_write - Convenience function for writing a given MII mgmt register
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
int mdiobus_write(struct mii_bus *bus, int addr, u16 regnum, u16 val)
{
	int err;

	BUG_ON(in_interrupt());

	mutex_lock(&bus->mdio_lock);
	err = bus->write(bus, addr, regnum, val);
	mutex_unlock(&bus->mdio_lock);

	return err;
}
EXPORT_SYMBOL(mdiobus_write);

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

#ifdef CONFIG_PM

static bool mdio_bus_phy_may_suspend(struct phy_device *phydev)
{
	struct device_driver *drv = phydev->dev.driver;
	struct phy_driver *phydrv = to_phy_driver(drv);
	struct net_device *netdev = phydev->attached_dev;

	if (!drv || !phydrv->suspend)
		return false;

	/* PHY not attached? May suspend. */
	if (!netdev)
		return true;

	/*
	 * Don't suspend PHY if the attched netdev parent may wakeup.
	 * The parent may point to a PCI device, as in tg3 driver.
	 */
	if (netdev->dev.parent && device_may_wakeup(netdev->dev.parent))
		return false;

	/*
	 * Also don't suspend PHY if the netdev itself may wakeup. This
	 * is the case for devices w/o underlaying pwr. mgmt. aware bus,
	 * e.g. SoC devices.
	 */
	if (device_may_wakeup(&netdev->dev))
		return false;

	return true;
}

static int mdio_bus_suspend(struct device *dev)
{
	struct phy_driver *phydrv = to_phy_driver(dev->driver);
	struct phy_device *phydev = to_phy_device(dev);

	/*
	 * We must stop the state machine manually, otherwise it stops out of
	 * control, possibly with the phydev->lock held. Upon resume, netdev
	 * may call phy routines that try to grab the same lock, and that may
	 * lead to a deadlock.
	 */
	if (phydev->attached_dev)
		phy_stop_machine(phydev);

	if (!mdio_bus_phy_may_suspend(phydev))
		return 0;

	return phydrv->suspend(phydev);
}

static int mdio_bus_resume(struct device *dev)
{
	struct phy_driver *phydrv = to_phy_driver(dev->driver);
	struct phy_device *phydev = to_phy_device(dev);
	int ret;

	if (!mdio_bus_phy_may_suspend(phydev))
		goto no_resume;

	ret = phydrv->resume(phydev);
	if (ret < 0)
		return ret;

no_resume:
	if (phydev->attached_dev)
		phy_start_machine(phydev, NULL);

	return 0;
}

static int mdio_bus_restore(struct device *dev)
{
	struct phy_device *phydev = to_phy_device(dev);
	struct net_device *netdev = phydev->attached_dev;
	int ret;

	if (!netdev)
		return 0;

	ret = phy_init_hw(phydev);
	if (ret < 0)
		return ret;

	/* The PHY needs to renegotiate. */
	phydev->link = 0;
	phydev->state = PHY_UP;

	phy_start_machine(phydev, NULL);

	return 0;
}

static struct dev_pm_ops mdio_bus_pm_ops = {
	.suspend = mdio_bus_suspend,
	.resume = mdio_bus_resume,
	.freeze = mdio_bus_suspend,
	.thaw = mdio_bus_resume,
	.restore = mdio_bus_restore,
};

#define MDIO_BUS_PM_OPS (&mdio_bus_pm_ops)

#else

#define MDIO_BUS_PM_OPS NULL

#endif /* CONFIG_PM */

struct bus_type mdio_bus_type = {
	.name		= "mdio_bus",
	.match		= mdio_bus_match,
	.pm		= MDIO_BUS_PM_OPS,
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

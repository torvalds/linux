// SPDX-License-Identifier: GPL-2.0+
/* MDIO Bus provider interface
 *
 * Author: Andy Fleming
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/micrel_phy.h>
#include <linux/mii.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>

/**
 * mdiobus_alloc_size - allocate a mii_bus structure
 * @size: extra amount of memory to allocate for private storage.
 * If non-zero, then bus->priv is points to that memory.
 *
 * Description: called by a bus driver to allocate an mii_bus
 * structure to fill in.
 */
struct mii_bus *mdiobus_alloc_size(size_t size)
{
	struct mii_bus *bus;
	size_t aligned_size = ALIGN(sizeof(*bus), NETDEV_ALIGN);
	size_t alloc_size;
	int i;

	/* If we alloc extra space, it should be aligned */
	if (size)
		alloc_size = aligned_size + size;
	else
		alloc_size = sizeof(*bus);

	bus = kzalloc(alloc_size, GFP_KERNEL);
	if (!bus)
		return NULL;

	bus->state = MDIOBUS_ALLOCATED;
	if (size)
		bus->priv = (void *)bus + aligned_size;

	/* Initialise the interrupts to polling and 64-bit seqcounts */
	for (i = 0; i < PHY_MAX_ADDR; i++) {
		bus->irq[i] = PHY_POLL;
		u64_stats_init(&bus->stats[i].syncp);
	}

	return bus;
}
EXPORT_SYMBOL(mdiobus_alloc_size);

#if IS_ENABLED(CONFIG_OF_MDIO)
/* Walk the list of subnodes of a mdio bus and look for a node that
 * matches the mdio device's address with its 'reg' property. If
 * found, set the of_node pointer for the mdio device. This allows
 * auto-probed phy devices to be supplied with information passed in
 * via DT.
 * If a PHY package is found, PHY is searched also there.
 */
static int of_mdiobus_find_phy(struct device *dev, struct mdio_device *mdiodev,
			       struct device_node *np)
{
	struct device_node *child;

	for_each_available_child_of_node(np, child) {
		int addr;

		if (of_node_name_eq(child, "ethernet-phy-package")) {
			/* Validate PHY package reg presence */
			if (!of_property_present(child, "reg")) {
				of_node_put(child);
				return -EINVAL;
			}

			if (!of_mdiobus_find_phy(dev, mdiodev, child)) {
				/* The refcount for the PHY package will be
				 * incremented later when PHY join the Package.
				 */
				of_node_put(child);
				return 0;
			}

			continue;
		}

		addr = of_mdio_parse_addr(dev, child);
		if (addr < 0)
			continue;

		if (addr == mdiodev->addr) {
			device_set_node(dev, of_fwnode_handle(child));
			/* The refcount on "child" is passed to the mdio
			 * device. Do _not_ use of_node_put(child) here.
			 */
			return 0;
		}
	}

	return -ENODEV;
}

static void of_mdiobus_link_mdiodev(struct mii_bus *bus,
				    struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;

	if (dev->of_node || !bus->dev.of_node)
		return;

	of_mdiobus_find_phy(dev, mdiodev, bus->dev.of_node);
}
#endif

static struct phy_device *mdiobus_scan(struct mii_bus *bus, int addr, bool c45)
{
	struct phy_device *phydev = ERR_PTR(-ENODEV);
	struct fwnode_handle *fwnode;
	char node_name[16];
	int err;

	phydev = get_phy_device(bus, addr, c45);
	if (IS_ERR(phydev))
		return phydev;

#if IS_ENABLED(CONFIG_OF_MDIO)
	/* For DT, see if the auto-probed phy has a corresponding child
	 * in the bus node, and set the of_node pointer in this case.
	 */
	of_mdiobus_link_mdiodev(bus, &phydev->mdio);
#endif

	/* Search for a swnode for the phy in the swnode hierarchy of the bus.
	 * If there is no swnode for the phy provided, just ignore it.
	 */
	if (dev_fwnode(&bus->dev) && !dev_fwnode(&phydev->mdio.dev)) {
		snprintf(node_name, sizeof(node_name), "ethernet-phy@%d",
			 addr);
		fwnode = fwnode_get_named_child_node(dev_fwnode(&bus->dev),
						     node_name);
		if (fwnode)
			device_set_node(&phydev->mdio.dev, fwnode);
	}

	err = phy_device_register(phydev);
	if (err) {
		phy_device_free(phydev);
		return ERR_PTR(-ENODEV);
	}

	return phydev;
}

/**
 * mdiobus_scan_c22 - scan one address on a bus for C22 MDIO devices.
 * @bus: mii_bus to scan
 * @addr: address on bus to scan
 *
 * This function scans one address on the MDIO bus, looking for
 * devices which can be identified using a vendor/product ID in
 * registers 2 and 3. Not all MDIO devices have such registers, but
 * PHY devices typically do. Hence this function assumes anything
 * found is a PHY, or can be treated as a PHY. Other MDIO devices,
 * such as switches, will probably not be found during the scan.
 */
struct phy_device *mdiobus_scan_c22(struct mii_bus *bus, int addr)
{
	return mdiobus_scan(bus, addr, false);
}
EXPORT_SYMBOL(mdiobus_scan_c22);

/**
 * mdiobus_scan_c45 - scan one address on a bus for C45 MDIO devices.
 * @bus: mii_bus to scan
 * @addr: address on bus to scan
 *
 * This function scans one address on the MDIO bus, looking for
 * devices which can be identified using a vendor/product ID in
 * registers 2 and 3. Not all MDIO devices have such registers, but
 * PHY devices typically do. Hence this function assumes anything
 * found is a PHY, or can be treated as a PHY. Other MDIO devices,
 * such as switches, will probably not be found during the scan.
 */
static struct phy_device *mdiobus_scan_c45(struct mii_bus *bus, int addr)
{
	return mdiobus_scan(bus, addr, true);
}

static int mdiobus_scan_bus_c22(struct mii_bus *bus)
{
	int i;

	for (i = 0; i < PHY_MAX_ADDR; i++) {
		if ((bus->phy_mask & BIT(i)) == 0) {
			struct phy_device *phydev;

			phydev = mdiobus_scan_c22(bus, i);
			if (IS_ERR(phydev) && (PTR_ERR(phydev) != -ENODEV))
				return PTR_ERR(phydev);
		}
	}
	return 0;
}

static int mdiobus_scan_bus_c45(struct mii_bus *bus)
{
	int i;

	for (i = 0; i < PHY_MAX_ADDR; i++) {
		if ((bus->phy_mask & BIT(i)) == 0) {
			struct phy_device *phydev;

			/* Don't scan C45 if we already have a C22 device */
			if (bus->mdio_map[i])
				continue;

			phydev = mdiobus_scan_c45(bus, i);
			if (IS_ERR(phydev) && (PTR_ERR(phydev) != -ENODEV))
				return PTR_ERR(phydev);
		}
	}
	return 0;
}

/* There are some C22 PHYs which do bad things when where is a C45
 * transaction on the bus, like accepting a read themselves, and
 * stomping over the true devices reply, to performing a write to
 * themselves which was intended for another device. Now that C22
 * devices have been found, see if any of them are bad for C45, and if we
 * should skip the C45 scan.
 */
static bool mdiobus_prevent_c45_scan(struct mii_bus *bus)
{
	int i;

	for (i = 0; i < PHY_MAX_ADDR; i++) {
		struct phy_device *phydev;
		u32 oui;

		phydev = mdiobus_get_phy(bus, i);
		if (!phydev)
			continue;
		oui = phydev->phy_id >> 10;

		if (oui == MICREL_OUI)
			return true;
	}
	return false;
}

/**
 * __mdiobus_register - bring up all the PHYs on a given bus and attach them to bus
 * @bus: target mii_bus
 * @owner: module containing bus accessor functions
 *
 * Description: Called by a bus driver to bring up all the PHYs
 *   on a given bus, and attach them to the bus. Drivers should use
 *   mdiobus_register() rather than __mdiobus_register() unless they
 *   need to pass a specific owner module. MDIO devices which are not
 *   PHYs will not be brought up by this function. They are expected
 *   to be explicitly listed in DT and instantiated by of_mdiobus_register().
 *
 * Returns 0 on success or < 0 on error.
 */
int __mdiobus_register(struct mii_bus *bus, struct module *owner)
{
	struct mdio_device *mdiodev;
	struct gpio_desc *gpiod;
	bool prevent_c45_scan;
	int i, err;

	if (!bus || !bus->name)
		return -EINVAL;

	/* An access method always needs both read and write operations */
	if (!!bus->read != !!bus->write || !!bus->read_c45 != !!bus->write_c45)
		return -EINVAL;

	/* At least one method is mandatory */
	if (!bus->read && !bus->read_c45)
		return -EINVAL;

	if (bus->parent && bus->parent->of_node)
		bus->parent->of_node->fwnode.flags |=
					FWNODE_FLAG_NEEDS_CHILD_BOUND_ON_ADD;

	WARN(bus->state != MDIOBUS_ALLOCATED &&
	     bus->state != MDIOBUS_UNREGISTERED,
	     "%s: not in ALLOCATED or UNREGISTERED state\n", bus->id);

	bus->owner = owner;
	bus->dev.parent = bus->parent;
	bus->dev.class = &mdio_bus_class;
	bus->dev.groups = NULL;
	dev_set_name(&bus->dev, "%s", bus->id);

	/* If the bus state is allocated, we're registering a fresh bus
	 * that may have a fwnode associated with it. Grab a reference
	 * to the fwnode. This will be dropped when the bus is released.
	 * If the bus was set to unregistered, it means that the bus was
	 * previously registered, and we've already grabbed a reference.
	 */
	if (bus->state == MDIOBUS_ALLOCATED)
		fwnode_handle_get(dev_fwnode(&bus->dev));

	/* We need to set state to MDIOBUS_UNREGISTERED to correctly release
	 * the device in mdiobus_free()
	 *
	 * State will be updated later in this function in case of success
	 */
	bus->state = MDIOBUS_UNREGISTERED;

	err = device_register(&bus->dev);
	if (err) {
		pr_err("mii_bus %s failed to register\n", bus->id);
		return -EINVAL;
	}

	mutex_init(&bus->mdio_lock);
	mutex_init(&bus->shared_lock);

	/* assert bus level PHY GPIO reset */
	gpiod = devm_gpiod_get_optional(&bus->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(gpiod)) {
		err = dev_err_probe(&bus->dev, PTR_ERR(gpiod),
				    "mii_bus %s couldn't get reset GPIO\n",
				    bus->id);
		device_del(&bus->dev);
		return err;
	} else	if (gpiod) {
		bus->reset_gpiod = gpiod;
		fsleep(bus->reset_delay_us);
		gpiod_set_value_cansleep(gpiod, 0);
		if (bus->reset_post_delay_us > 0)
			fsleep(bus->reset_post_delay_us);
	}

	if (bus->reset) {
		err = bus->reset(bus);
		if (err)
			goto error_reset_gpiod;
	}

	if (bus->read) {
		err = mdiobus_scan_bus_c22(bus);
		if (err)
			goto error;
	}

	prevent_c45_scan = mdiobus_prevent_c45_scan(bus);

	if (!prevent_c45_scan && bus->read_c45) {
		err = mdiobus_scan_bus_c45(bus);
		if (err)
			goto error;
	}

	bus->state = MDIOBUS_REGISTERED;
	dev_dbg(&bus->dev, "probed\n");
	return 0;

error:
	for (i = 0; i < PHY_MAX_ADDR; i++) {
		mdiodev = bus->mdio_map[i];
		if (!mdiodev)
			continue;

		mdiodev->device_remove(mdiodev);
		mdiodev->device_free(mdiodev);
	}
error_reset_gpiod:
	/* Put PHYs in RESET to save power */
	if (bus->reset_gpiod)
		gpiod_set_value_cansleep(bus->reset_gpiod, 1);

	device_del(&bus->dev);
	return err;
}
EXPORT_SYMBOL(__mdiobus_register);

void mdiobus_unregister(struct mii_bus *bus)
{
	struct mdio_device *mdiodev;
	int i;

	if (WARN_ON_ONCE(bus->state != MDIOBUS_REGISTERED))
		return;
	bus->state = MDIOBUS_UNREGISTERED;

	for (i = 0; i < PHY_MAX_ADDR; i++) {
		mdiodev = bus->mdio_map[i];
		if (!mdiodev)
			continue;

		mdiodev->device_remove(mdiodev);
		mdiodev->device_free(mdiodev);
	}

	/* Put PHYs in RESET to save power */
	if (bus->reset_gpiod)
		gpiod_set_value_cansleep(bus->reset_gpiod, 1);

	device_del(&bus->dev);
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
	/* For compatibility with error handling in drivers. */
	if (bus->state == MDIOBUS_ALLOCATED) {
		kfree(bus);
		return;
	}

	WARN(bus->state != MDIOBUS_UNREGISTERED,
	     "%s: not in UNREGISTERED state\n", bus->id);
	bus->state = MDIOBUS_RELEASED;

	put_device(&bus->dev);
}
EXPORT_SYMBOL(mdiobus_free);

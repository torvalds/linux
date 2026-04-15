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
#include "phylib-internal.h"

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

	WARN(bus->state != MDIOBUS_RELEASED &&
	     /* for compatibility with error handling in drivers */
	     bus->state != MDIOBUS_ALLOCATED,
	     "%s: not in RELEASED or ALLOCATED state\n",
	     bus->id);

	if (bus->state == MDIOBUS_RELEASED)
		fwnode_handle_put(dev_fwnode(d));

	kfree(bus);
}

struct mdio_bus_stat_attr {
	struct device_attribute attr;
	int address;
	unsigned int field_offset;
};

static struct mdio_bus_stat_attr *to_sattr(struct device_attribute *attr)
{
	return container_of(attr, struct mdio_bus_stat_attr, attr);
}

static u64 mdio_bus_get_stat(struct mdio_bus_stats *s, unsigned int offset)
{
	const u64_stats_t *stats = (const void *)s + offset;
	unsigned int start;
	u64 val = 0;

	do {
		start = u64_stats_fetch_begin(&s->syncp);
		val = u64_stats_read(stats);
	} while (u64_stats_fetch_retry(&s->syncp, start));

	return val;
}

static ssize_t mdio_bus_stat_field_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct mdio_bus_stat_attr *sattr = to_sattr(attr);
	struct mii_bus *bus = to_mii_bus(dev);
	u64 val = 0;

	if (sattr->address < 0) {
		/* get global stats */
		for (int i = 0; i < PHY_MAX_ADDR; i++)
			val += mdio_bus_get_stat(&bus->stats[i],
						 sattr->field_offset);
	} else {
		val = mdio_bus_get_stat(&bus->stats[sattr->address],
					sattr->field_offset);
	}

	return sysfs_emit(buf, "%llu\n", val);
}

static ssize_t mdio_bus_device_stat_field_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct mdio_bus_stat_attr *sattr = to_sattr(attr);
	struct mdio_device *mdiodev = to_mdio_device(dev);
	struct mii_bus *bus = mdiodev->bus;
	int addr = mdiodev->addr;
	u64 val;

	val = mdio_bus_get_stat(&bus->stats[addr], sattr->field_offset);

	return sysfs_emit(buf, "%llu\n", val);
}

#define MDIO_BUS_STATS_ATTR(field)					\
static const struct mdio_bus_stat_attr dev_attr_mdio_bus_##field = {	\
	.attr = __ATTR(field, 0444, mdio_bus_stat_field_show, NULL),	\
	.address = -1,							\
	.field_offset = offsetof(struct mdio_bus_stats, field),		\
};									\
static const struct mdio_bus_stat_attr dev_attr_mdio_bus_device_##field = { \
	.attr = __ATTR(field, 0444, mdio_bus_device_stat_field_show, NULL), \
	.field_offset = offsetof(struct mdio_bus_stats, field),		\
}

MDIO_BUS_STATS_ATTR(transfers);
MDIO_BUS_STATS_ATTR(errors);
MDIO_BUS_STATS_ATTR(writes);
MDIO_BUS_STATS_ATTR(reads);

#define MDIO_BUS_STATS_ADDR_ATTR_DECL(field, addr, file)		\
static const struct mdio_bus_stat_attr					\
dev_attr_mdio_bus_addr_##field##_##addr = {				\
	.attr = { .attr = { .name = file, .mode = 0444 },		\
		     .show = mdio_bus_stat_field_show,			\
	},								\
	.address = addr,						\
	.field_offset = offsetof(struct mdio_bus_stats, field),		\
}

#define MDIO_BUS_STATS_ADDR_ATTR(field, addr)				\
	MDIO_BUS_STATS_ADDR_ATTR_DECL(field, addr,			\
				 __stringify(field) "_" __stringify(addr))

#define MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(addr)			\
	MDIO_BUS_STATS_ADDR_ATTR(transfers, addr);			\
	MDIO_BUS_STATS_ADDR_ATTR(errors, addr);				\
	MDIO_BUS_STATS_ADDR_ATTR(writes, addr);				\
	MDIO_BUS_STATS_ADDR_ATTR(reads, addr)				\

MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(0);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(1);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(2);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(3);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(4);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(5);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(6);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(7);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(8);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(9);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(10);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(11);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(12);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(13);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(14);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(15);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(16);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(17);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(18);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(19);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(20);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(21);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(22);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(23);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(24);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(25);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(26);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(27);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(28);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(29);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(30);
MDIO_BUS_STATS_ADDR_ATTR_GROUP_DECL(31);

#define MDIO_BUS_STATS_ADDR_ATTR_GROUP(addr)				\
	&(dev_attr_mdio_bus_addr_transfers_##addr).attr.attr,		\
	&(dev_attr_mdio_bus_addr_errors_##addr).attr.attr,		\
	&(dev_attr_mdio_bus_addr_writes_##addr).attr.attr,		\
	&(dev_attr_mdio_bus_addr_reads_##addr).attr.attr			\

static const struct attribute *const mdio_bus_statistics_attrs[] = {
	&dev_attr_mdio_bus_transfers.attr.attr,
	&dev_attr_mdio_bus_errors.attr.attr,
	&dev_attr_mdio_bus_writes.attr.attr,
	&dev_attr_mdio_bus_reads.attr.attr,
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(0),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(1),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(2),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(3),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(4),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(5),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(6),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(7),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(8),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(9),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(10),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(11),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(12),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(13),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(14),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(15),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(16),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(17),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(18),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(19),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(20),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(21),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(22),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(23),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(24),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(25),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(26),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(27),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(28),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(29),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(30),
	MDIO_BUS_STATS_ADDR_ATTR_GROUP(31),
	NULL,
};

static const struct attribute_group mdio_bus_statistics_group = {
	.name		= "statistics",
	.attrs_const	= mdio_bus_statistics_attrs,
};
__ATTRIBUTE_GROUPS(mdio_bus_statistics);

const struct class mdio_bus_class = {
	.name		= "mdio_bus",
	.dev_release	= mdiobus_release,
	.dev_groups	= mdio_bus_statistics_groups,
};

/**
 * mdio_bus_match - determine if given MDIO driver supports the given
 *		    MDIO device
 * @dev: target MDIO device
 * @drv: given MDIO driver
 *
 * Return: 1 if the driver supports the device, 0 otherwise
 *
 * Description: This may require calling the devices own match function,
 *   since different classes of MDIO devices have different match criteria.
 */
static int mdio_bus_match(struct device *dev, const struct device_driver *drv)
{
	const struct mdio_driver *mdiodrv = to_mdio_driver(drv);
	struct mdio_device *mdio = to_mdio_device(dev);

	/* Both the driver and device must type-match */
	if (!(mdiodrv->mdiodrv.flags & MDIO_DEVICE_IS_PHY) !=
	    !(mdio->flags & MDIO_DEVICE_FLAG_PHY))
		return 0;

	if (of_driver_match_device(dev, drv))
		return 1;

	if (mdio->bus_match)
		return mdio->bus_match(dev, drv);

	return 0;
}

static int mdio_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	int rc;

	/* Some devices have extra OF data and an OF-style MODALIAS */
	rc = of_device_uevent_modalias(dev, env);
	if (rc != -ENODEV)
		return rc;

	return 0;
}

static const struct attribute *const mdio_bus_device_statistics_attrs[] = {
	&dev_attr_mdio_bus_device_transfers.attr.attr,
	&dev_attr_mdio_bus_device_errors.attr.attr,
	&dev_attr_mdio_bus_device_writes.attr.attr,
	&dev_attr_mdio_bus_device_reads.attr.attr,
	NULL,
};

static const struct attribute_group mdio_bus_device_statistics_group = {
	.name		= "statistics",
	.attrs_const	= mdio_bus_device_statistics_attrs,
};
__ATTRIBUTE_GROUPS(mdio_bus_device_statistics);

const struct bus_type mdio_bus_type = {
	.name		= "mdio_bus",
	.dev_groups	= mdio_bus_device_statistics_groups,
	.match		= mdio_bus_match,
	.uevent		= mdio_uevent,
};

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
	struct phy_device *phydev;

	mdiobus_for_each_phy(bus, phydev) {
		u32 oui = phydev->phy_id >> 10;

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
		fwnode_set_flag(&bus->parent->of_node->fwnode,
				FWNODE_FLAG_NEEDS_CHILD_BOUND_ON_ADD);

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

/**
 * mdio_find_bus - Given the name of a mdiobus, find the mii_bus.
 * @mdio_name: The name of a mdiobus.
 *
 * Return: a reference to the mii_bus, or NULL if none found. The
 * embedded struct device will have its reference count incremented,
 * and this must be put_deviced'ed once the bus is finished with.
 */
struct mii_bus *mdio_find_bus(const char *mdio_name)
{
	struct device *d;

	d = class_find_device_by_name(&mdio_bus_class, mdio_name);
	return d ? to_mii_bus(d) : NULL;
}
EXPORT_SYMBOL(mdio_find_bus);

#if IS_ENABLED(CONFIG_OF_MDIO)
/**
 * of_mdio_find_bus - Given an mii_bus node, find the mii_bus.
 * @mdio_bus_np: Pointer to the mii_bus.
 *
 * Return: a reference to the mii_bus, or NULL if none found. The
 * embedded struct device will have its reference count incremented,
 * and this must be put once the bus is finished with.
 *
 * Because the association of a device_node and mii_bus is made via
 * of_mdiobus_register(), the mii_bus cannot be found before it is
 * registered with of_mdiobus_register().
 *
 */
struct mii_bus *of_mdio_find_bus(struct device_node *mdio_bus_np)
{
	struct device *d;

	if (!mdio_bus_np)
		return NULL;

	d = class_find_device_by_of_node(&mdio_bus_class, mdio_bus_np);
	return d ? to_mii_bus(d) : NULL;
}
EXPORT_SYMBOL(of_mdio_find_bus);
#endif

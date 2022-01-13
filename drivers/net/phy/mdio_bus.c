// SPDX-License-Identifier: GPL-2.0+
/* MDIO Bus interface
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
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/reset.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>

#define CREATE_TRACE_POINTS
#include <trace/events/mdio.h>

#include "mdio-boardinfo.h"

static int mdiobus_register_gpiod(struct mdio_device *mdiodev)
{
	/* Deassert the optional reset signal */
	mdiodev->reset_gpio = gpiod_get_optional(&mdiodev->dev,
						 "reset", GPIOD_OUT_LOW);
	if (IS_ERR(mdiodev->reset_gpio))
		return PTR_ERR(mdiodev->reset_gpio);

	if (mdiodev->reset_gpio)
		gpiod_set_consumer_name(mdiodev->reset_gpio, "PHY reset");

	return 0;
}

static int mdiobus_register_reset(struct mdio_device *mdiodev)
{
	struct reset_control *reset;

	reset = reset_control_get_optional_exclusive(&mdiodev->dev, "phy");
	if (IS_ERR(reset))
		return PTR_ERR(reset);

	mdiodev->reset_ctrl = reset;

	return 0;
}

int mdiobus_register_device(struct mdio_device *mdiodev)
{
	int err;

	if (mdiodev->bus->mdio_map[mdiodev->addr])
		return -EBUSY;

	if (mdiodev->flags & MDIO_DEVICE_FLAG_PHY) {
		err = mdiobus_register_gpiod(mdiodev);
		if (err)
			return err;

		err = mdiobus_register_reset(mdiodev);
		if (err)
			return err;

		/* Assert the reset signal */
		mdio_device_reset(mdiodev, 1);
	}

	mdiodev->bus->mdio_map[mdiodev->addr] = mdiodev;

	return 0;
}
EXPORT_SYMBOL(mdiobus_register_device);

int mdiobus_unregister_device(struct mdio_device *mdiodev)
{
	if (mdiodev->bus->mdio_map[mdiodev->addr] != mdiodev)
		return -EINVAL;

	reset_control_put(mdiodev->reset_ctrl);

	mdiodev->bus->mdio_map[mdiodev->addr] = NULL;

	return 0;
}
EXPORT_SYMBOL(mdiobus_unregister_device);

struct phy_device *mdiobus_get_phy(struct mii_bus *bus, int addr)
{
	struct mdio_device *mdiodev = bus->mdio_map[addr];

	if (!mdiodev)
		return NULL;

	if (!(mdiodev->flags & MDIO_DEVICE_FLAG_PHY))
		return NULL;

	return container_of(mdiodev, struct phy_device, mdio);
}
EXPORT_SYMBOL(mdiobus_get_phy);

bool mdiobus_is_registered_device(struct mii_bus *bus, int addr)
{
	return bus->mdio_map[addr];
}
EXPORT_SYMBOL(mdiobus_is_registered_device);

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

struct mdio_bus_stat_attr {
	int addr;
	unsigned int field_offset;
};

static u64 mdio_bus_get_stat(struct mdio_bus_stats *s, unsigned int offset)
{
	const char *p = (const char *)s + offset;
	unsigned int start;
	u64 val = 0;

	do {
		start = u64_stats_fetch_begin(&s->syncp);
		val = u64_stats_read((const u64_stats_t *)p);
	} while (u64_stats_fetch_retry(&s->syncp, start));

	return val;
}

static u64 mdio_bus_get_global_stat(struct mii_bus *bus, unsigned int offset)
{
	unsigned int i;
	u64 val = 0;

	for (i = 0; i < PHY_MAX_ADDR; i++)
		val += mdio_bus_get_stat(&bus->stats[i], offset);

	return val;
}

static ssize_t mdio_bus_stat_field_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct mii_bus *bus = to_mii_bus(dev);
	struct mdio_bus_stat_attr *sattr;
	struct dev_ext_attribute *eattr;
	u64 val;

	eattr = container_of(attr, struct dev_ext_attribute, attr);
	sattr = eattr->var;

	if (sattr->addr < 0)
		val = mdio_bus_get_global_stat(bus, sattr->field_offset);
	else
		val = mdio_bus_get_stat(&bus->stats[sattr->addr],
					sattr->field_offset);

	return sprintf(buf, "%llu\n", val);
}

static ssize_t mdio_bus_device_stat_field_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct mdio_device *mdiodev = to_mdio_device(dev);
	struct mii_bus *bus = mdiodev->bus;
	struct mdio_bus_stat_attr *sattr;
	struct dev_ext_attribute *eattr;
	int addr = mdiodev->addr;
	u64 val;

	eattr = container_of(attr, struct dev_ext_attribute, attr);
	sattr = eattr->var;

	val = mdio_bus_get_stat(&bus->stats[addr], sattr->field_offset);

	return sprintf(buf, "%llu\n", val);
}

#define MDIO_BUS_STATS_ATTR_DECL(field, file)				\
static struct dev_ext_attribute dev_attr_mdio_bus_##field = {		\
	.attr = { .attr = { .name = file, .mode = 0444 },		\
		     .show = mdio_bus_stat_field_show,			\
	},								\
	.var = &((struct mdio_bus_stat_attr) {				\
		-1, offsetof(struct mdio_bus_stats, field)		\
	}),								\
};									\
static struct dev_ext_attribute dev_attr_mdio_bus_device_##field = {	\
	.attr = { .attr = { .name = file, .mode = 0444 },		\
		     .show = mdio_bus_device_stat_field_show,		\
	},								\
	.var = &((struct mdio_bus_stat_attr) {				\
		-1, offsetof(struct mdio_bus_stats, field)		\
	}),								\
};

#define MDIO_BUS_STATS_ATTR(field)					\
	MDIO_BUS_STATS_ATTR_DECL(field, __stringify(field))

MDIO_BUS_STATS_ATTR(transfers);
MDIO_BUS_STATS_ATTR(errors);
MDIO_BUS_STATS_ATTR(writes);
MDIO_BUS_STATS_ATTR(reads);

#define MDIO_BUS_STATS_ADDR_ATTR_DECL(field, addr, file)		\
static struct dev_ext_attribute dev_attr_mdio_bus_addr_##field##_##addr = { \
	.attr = { .attr = { .name = file, .mode = 0444 },		\
		     .show = mdio_bus_stat_field_show,			\
	},								\
	.var = &((struct mdio_bus_stat_attr) {				\
		addr, offsetof(struct mdio_bus_stats, field)		\
	}),								\
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
	&dev_attr_mdio_bus_addr_transfers_##addr.attr.attr,		\
	&dev_attr_mdio_bus_addr_errors_##addr.attr.attr,		\
	&dev_attr_mdio_bus_addr_writes_##addr.attr.attr,		\
	&dev_attr_mdio_bus_addr_reads_##addr.attr.attr			\

static struct attribute *mdio_bus_statistics_attrs[] = {
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
	.name	= "statistics",
	.attrs	= mdio_bus_statistics_attrs,
};

static const struct attribute_group *mdio_bus_groups[] = {
	&mdio_bus_statistics_group,
	NULL,
};

static struct class mdio_bus_class = {
	.name		= "mdio_bus",
	.dev_release	= mdiobus_release,
	.dev_groups	= mdio_bus_groups,
};

/**
 * mdio_find_bus - Given the name of a mdiobus, find the mii_bus.
 * @mdio_name: The name of a mdiobus.
 *
 * Returns a reference to the mii_bus, or NULL if none found.  The
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
 * Returns a reference to the mii_bus, or NULL if none found.  The
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

/* Walk the list of subnodes of a mdio bus and look for a node that
 * matches the mdio device's address with its 'reg' property. If
 * found, set the of_node pointer for the mdio device. This allows
 * auto-probed phy devices to be supplied with information passed in
 * via DT.
 */
static void of_mdiobus_link_mdiodev(struct mii_bus *bus,
				    struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct device_node *child;

	if (dev->of_node || !bus->dev.of_node)
		return;

	for_each_available_child_of_node(bus->dev.of_node, child) {
		int addr;

		addr = of_mdio_parse_addr(dev, child);
		if (addr < 0)
			continue;

		if (addr == mdiodev->addr) {
			device_set_node(dev, of_fwnode_handle(child));
			/* The refcount on "child" is passed to the mdio
			 * device. Do _not_ use of_node_put(child) here.
			 */
			return;
		}
	}
}
#else /* !IS_ENABLED(CONFIG_OF_MDIO) */
static inline void of_mdiobus_link_mdiodev(struct mii_bus *mdio,
					   struct mdio_device *mdiodev)
{
}
#endif

/**
 * mdiobus_create_device - create a full MDIO device given
 * a mdio_board_info structure
 * @bus: MDIO bus to create the devices on
 * @bi: mdio_board_info structure describing the devices
 *
 * Returns 0 on success or < 0 on error.
 */
static int mdiobus_create_device(struct mii_bus *bus,
				 struct mdio_board_info *bi)
{
	struct mdio_device *mdiodev;
	int ret = 0;

	mdiodev = mdio_device_create(bus, bi->mdio_addr);
	if (IS_ERR(mdiodev))
		return -ENODEV;

	strncpy(mdiodev->modalias, bi->modalias,
		sizeof(mdiodev->modalias));
	mdiodev->bus_match = mdio_device_bus_match;
	mdiodev->dev.platform_data = (void *)bi->platform_data;

	ret = mdio_device_register(mdiodev);
	if (ret)
		mdio_device_free(mdiodev);

	return ret;
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
	int i, err;
	struct gpio_desc *gpiod;

	if (NULL == bus || NULL == bus->name ||
	    NULL == bus->read || NULL == bus->write)
		return -EINVAL;

	if (bus->parent && bus->parent->of_node)
		bus->parent->of_node->fwnode.flags |=
					FWNODE_FLAG_NEEDS_CHILD_BOUND_ON_ADD;

	BUG_ON(bus->state != MDIOBUS_ALLOCATED &&
	       bus->state != MDIOBUS_UNREGISTERED);

	bus->owner = owner;
	bus->dev.parent = bus->parent;
	bus->dev.class = &mdio_bus_class;
	bus->dev.groups = NULL;
	dev_set_name(&bus->dev, "%s", bus->id);

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

	for (i = 0; i < PHY_MAX_ADDR; i++) {
		if ((bus->phy_mask & (1 << i)) == 0) {
			struct phy_device *phydev;

			phydev = mdiobus_scan(bus, i);
			if (IS_ERR(phydev) && (PTR_ERR(phydev) != -ENODEV)) {
				err = PTR_ERR(phydev);
				goto error;
			}
		}
	}

	mdiobus_setup_mdiodev_from_board_info(bus, mdiobus_create_device);

	bus->state = MDIOBUS_REGISTERED;
	pr_info("%s: probed\n", bus->name);
	return 0;

error:
	while (--i >= 0) {
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

		if (mdiodev->reset_gpio)
			gpiod_put(mdiodev->reset_gpio);

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

	BUG_ON(bus->state != MDIOBUS_UNREGISTERED);
	bus->state = MDIOBUS_RELEASED;

	put_device(&bus->dev);
}
EXPORT_SYMBOL(mdiobus_free);

/**
 * mdiobus_scan - scan a bus for MDIO devices.
 * @bus: mii_bus to scan
 * @addr: address on bus to scan
 *
 * This function scans the MDIO bus, looking for devices which can be
 * identified using a vendor/product ID in registers 2 and 3. Not all
 * MDIO devices have such registers, but PHY devices typically
 * do. Hence this function assumes anything found is a PHY, or can be
 * treated as a PHY. Other MDIO devices, such as switches, will
 * probably not be found during the scan.
 */
struct phy_device *mdiobus_scan(struct mii_bus *bus, int addr)
{
	struct phy_device *phydev = ERR_PTR(-ENODEV);
	int err;

	switch (bus->probe_capabilities) {
	case MDIOBUS_NO_CAP:
	case MDIOBUS_C22:
		phydev = get_phy_device(bus, addr, false);
		break;
	case MDIOBUS_C45:
		phydev = get_phy_device(bus, addr, true);
		break;
	case MDIOBUS_C22_C45:
		phydev = get_phy_device(bus, addr, false);
		if (IS_ERR(phydev))
			phydev = get_phy_device(bus, addr, true);
		break;
	}

	if (IS_ERR(phydev))
		return phydev;

	/*
	 * For DT, see if the auto-probed phy has a correspoding child
	 * in the bus node, and set the of_node pointer in this case.
	 */
	of_mdiobus_link_mdiodev(bus, &phydev->mdio);

	err = phy_device_register(phydev);
	if (err) {
		phy_device_free(phydev);
		return ERR_PTR(-ENODEV);
	}

	return phydev;
}
EXPORT_SYMBOL(mdiobus_scan);

static void mdiobus_stats_acct(struct mdio_bus_stats *stats, bool op, int ret)
{
	preempt_disable();
	u64_stats_update_begin(&stats->syncp);

	u64_stats_inc(&stats->transfers);
	if (ret < 0) {
		u64_stats_inc(&stats->errors);
		goto out;
	}

	if (op)
		u64_stats_inc(&stats->reads);
	else
		u64_stats_inc(&stats->writes);
out:
	u64_stats_update_end(&stats->syncp);
	preempt_enable();
}

/**
 * __mdiobus_read - Unlocked version of the mdiobus_read function
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to read
 *
 * Read a MDIO bus register. Caller must hold the mdio bus lock.
 *
 * NOTE: MUST NOT be called from interrupt context.
 */
int __mdiobus_read(struct mii_bus *bus, int addr, u32 regnum)
{
	int retval;

	lockdep_assert_held_once(&bus->mdio_lock);

	retval = bus->read(bus, addr, regnum);

	trace_mdio_access(bus, 1, addr, regnum, retval, retval);
	mdiobus_stats_acct(&bus->stats[addr], true, retval);

	return retval;
}
EXPORT_SYMBOL(__mdiobus_read);

/**
 * __mdiobus_write - Unlocked version of the mdiobus_write function
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * Write a MDIO bus register. Caller must hold the mdio bus lock.
 *
 * NOTE: MUST NOT be called from interrupt context.
 */
int __mdiobus_write(struct mii_bus *bus, int addr, u32 regnum, u16 val)
{
	int err;

	lockdep_assert_held_once(&bus->mdio_lock);

	err = bus->write(bus, addr, regnum, val);

	trace_mdio_access(bus, 0, addr, regnum, val, err);
	mdiobus_stats_acct(&bus->stats[addr], false, err);

	return err;
}
EXPORT_SYMBOL(__mdiobus_write);

/**
 * __mdiobus_modify_changed - Unlocked version of the mdiobus_modify function
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to modify
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * Read, modify, and if any change, write the register value back to the
 * device. Any error returns a negative number.
 *
 * NOTE: MUST NOT be called from interrupt context.
 */
int __mdiobus_modify_changed(struct mii_bus *bus, int addr, u32 regnum,
			     u16 mask, u16 set)
{
	int new, ret;

	ret = __mdiobus_read(bus, addr, regnum);
	if (ret < 0)
		return ret;

	new = (ret & ~mask) | set;
	if (new == ret)
		return 0;

	ret = __mdiobus_write(bus, addr, regnum, new);

	return ret < 0 ? ret : 1;
}
EXPORT_SYMBOL_GPL(__mdiobus_modify_changed);

/**
 * mdiobus_read_nested - Nested version of the mdiobus_read function
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to read
 *
 * In case of nested MDIO bus access avoid lockdep false positives by
 * using mutex_lock_nested().
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
int mdiobus_read_nested(struct mii_bus *bus, int addr, u32 regnum)
{
	int retval;

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);
	retval = __mdiobus_read(bus, addr, regnum);
	mutex_unlock(&bus->mdio_lock);

	return retval;
}
EXPORT_SYMBOL(mdiobus_read_nested);

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
int mdiobus_read(struct mii_bus *bus, int addr, u32 regnum)
{
	int retval;

	mutex_lock(&bus->mdio_lock);
	retval = __mdiobus_read(bus, addr, regnum);
	mutex_unlock(&bus->mdio_lock);

	return retval;
}
EXPORT_SYMBOL(mdiobus_read);

/**
 * mdiobus_write_nested - Nested version of the mdiobus_write function
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * In case of nested MDIO bus access avoid lockdep false positives by
 * using mutex_lock_nested().
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
int mdiobus_write_nested(struct mii_bus *bus, int addr, u32 regnum, u16 val)
{
	int err;

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);
	err = __mdiobus_write(bus, addr, regnum, val);
	mutex_unlock(&bus->mdio_lock);

	return err;
}
EXPORT_SYMBOL(mdiobus_write_nested);

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
int mdiobus_write(struct mii_bus *bus, int addr, u32 regnum, u16 val)
{
	int err;

	mutex_lock(&bus->mdio_lock);
	err = __mdiobus_write(bus, addr, regnum, val);
	mutex_unlock(&bus->mdio_lock);

	return err;
}
EXPORT_SYMBOL(mdiobus_write);

/**
 * mdiobus_modify - Convenience function for modifying a given mdio device
 *	register
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to write
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 */
int mdiobus_modify(struct mii_bus *bus, int addr, u32 regnum, u16 mask, u16 set)
{
	int err;

	mutex_lock(&bus->mdio_lock);
	err = __mdiobus_modify_changed(bus, addr, regnum, mask, set);
	mutex_unlock(&bus->mdio_lock);

	return err < 0 ? err : 0;
}
EXPORT_SYMBOL_GPL(mdiobus_modify);

/**
 * mdiobus_modify_changed - Convenience function for modifying a given mdio
 *	device register and returning if it changed
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to write
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 */
int mdiobus_modify_changed(struct mii_bus *bus, int addr, u32 regnum,
			   u16 mask, u16 set)
{
	int err;

	mutex_lock(&bus->mdio_lock);
	err = __mdiobus_modify_changed(bus, addr, regnum, mask, set);
	mutex_unlock(&bus->mdio_lock);

	return err;
}
EXPORT_SYMBOL_GPL(mdiobus_modify_changed);

/**
 * mdio_bus_match - determine if given MDIO driver supports the given
 *		    MDIO device
 * @dev: target MDIO device
 * @drv: given MDIO driver
 *
 * Description: Given a MDIO device, and a MDIO driver, return 1 if
 *   the driver supports the device.  Otherwise, return 0. This may
 *   require calling the devices own match function, since different classes
 *   of MDIO devices have different match criteria.
 */
static int mdio_bus_match(struct device *dev, struct device_driver *drv)
{
	struct mdio_driver *mdiodrv = to_mdio_driver(drv);
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

static int mdio_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	int rc;

	/* Some devices have extra OF data and an OF-style MODALIAS */
	rc = of_device_uevent_modalias(dev, env);
	if (rc != -ENODEV)
		return rc;

	return 0;
}

static struct attribute *mdio_bus_device_statistics_attrs[] = {
	&dev_attr_mdio_bus_device_transfers.attr.attr,
	&dev_attr_mdio_bus_device_errors.attr.attr,
	&dev_attr_mdio_bus_device_writes.attr.attr,
	&dev_attr_mdio_bus_device_reads.attr.attr,
	NULL,
};

static const struct attribute_group mdio_bus_device_statistics_group = {
	.name	= "statistics",
	.attrs	= mdio_bus_device_statistics_attrs,
};

static const struct attribute_group *mdio_bus_dev_groups[] = {
	&mdio_bus_device_statistics_group,
	NULL,
};

struct bus_type mdio_bus_type = {
	.name		= "mdio_bus",
	.dev_groups	= mdio_bus_dev_groups,
	.match		= mdio_bus_match,
	.uevent		= mdio_uevent,
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
EXPORT_SYMBOL_GPL(mdio_bus_init);

#if IS_ENABLED(CONFIG_PHYLIB)
void mdio_bus_exit(void)
{
	class_unregister(&mdio_bus_class);
	bus_unregister(&mdio_bus_type);
}
EXPORT_SYMBOL_GPL(mdio_bus_exit);
#else
module_init(mdio_bus_init);
/* no module_exit, intentional */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MDIO bus/device layer");
#endif

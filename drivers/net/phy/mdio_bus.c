// SPDX-License-Identifier: GPL-2.0+
/* MDIO Bus interface
 *
 * Author: Andy Fleming
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/ethtool.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>

#define CREATE_TRACE_POINTS
#include <trace/events/mdio.h>

static struct mdio_device *mdiobus_find_device(struct mii_bus *bus, int addr)
{
	bool addr_valid = addr >= 0 && addr < ARRAY_SIZE(bus->mdio_map);

	if (WARN_ONCE(!addr_valid, "addr %d out of range\n", addr))
		return NULL;

	return bus->mdio_map[addr];
}

struct phy_device *mdiobus_get_phy(struct mii_bus *bus, int addr)
{
	struct mdio_device *mdiodev;

	mdiodev = mdiobus_find_device(bus, addr);
	if (!mdiodev)
		return NULL;

	if (!(mdiodev->flags & MDIO_DEVICE_FLAG_PHY))
		return NULL;

	return container_of(mdiodev, struct phy_device, mdio);
}
EXPORT_SYMBOL(mdiobus_get_phy);

bool mdiobus_is_registered_device(struct mii_bus *bus, int addr)
{
	return mdiobus_find_device(bus, addr) != NULL;
}
EXPORT_SYMBOL(mdiobus_is_registered_device);

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
 * Return: The register value if successful, negative error code on failure
 *
 * Read a MDIO bus register. Caller must hold the mdio bus lock.
 *
 * NOTE: MUST NOT be called from interrupt context.
 */
int __mdiobus_read(struct mii_bus *bus, int addr, u32 regnum)
{
	int retval;

	lockdep_assert_held_once(&bus->mdio_lock);

	if (addr >= PHY_MAX_ADDR)
		return -ENXIO;

	if (bus->read)
		retval = bus->read(bus, addr, regnum);
	else
		retval = -EOPNOTSUPP;

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
 * Return: Zero if successful, negative error code on failure
 *
 * Write a MDIO bus register. Caller must hold the mdio bus lock.
 *
 * NOTE: MUST NOT be called from interrupt context.
 */
int __mdiobus_write(struct mii_bus *bus, int addr, u32 regnum, u16 val)
{
	int err;

	lockdep_assert_held_once(&bus->mdio_lock);

	if (addr >= PHY_MAX_ADDR)
		return -ENXIO;

	if (bus->write)
		err = bus->write(bus, addr, regnum, val);
	else
		err = -EOPNOTSUPP;

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
 * Return: 1 if the register was modified, 0 if no change was needed,
 *	   negative on any error condition
 *
 * Read, modify, and if any change, write the register value back to the
 * device.
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
 * __mdiobus_c45_read - Unlocked version of the mdiobus_c45_read function
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @devad: device address to read
 * @regnum: register number to read
 *
 * Return: The register value if successful, negative error code on failure
 *
 * Read a MDIO bus register. Caller must hold the mdio bus lock.
 *
 * NOTE: MUST NOT be called from interrupt context.
 */
int __mdiobus_c45_read(struct mii_bus *bus, int addr, int devad, u32 regnum)
{
	int retval;

	lockdep_assert_held_once(&bus->mdio_lock);

	if (addr >= PHY_MAX_ADDR)
		return -ENXIO;

	if (bus->read_c45)
		retval = bus->read_c45(bus, addr, devad, regnum);
	else
		retval = -EOPNOTSUPP;

	trace_mdio_access(bus, 1, addr, regnum, retval, retval);
	mdiobus_stats_acct(&bus->stats[addr], true, retval);

	return retval;
}
EXPORT_SYMBOL(__mdiobus_c45_read);

/**
 * __mdiobus_c45_write - Unlocked version of the mdiobus_write function
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @devad: device address to read
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * Return: Zero if successful, negative error code on failure
 *
 * Write a MDIO bus register. Caller must hold the mdio bus lock.
 *
 * NOTE: MUST NOT be called from interrupt context.
 */
int __mdiobus_c45_write(struct mii_bus *bus, int addr, int devad, u32 regnum,
			u16 val)
{
	int err;

	lockdep_assert_held_once(&bus->mdio_lock);

	if (addr >= PHY_MAX_ADDR)
		return -ENXIO;

	if (bus->write_c45)
		err = bus->write_c45(bus, addr, devad, regnum, val);
	else
		err = -EOPNOTSUPP;

	trace_mdio_access(bus, 0, addr, regnum, val, err);
	mdiobus_stats_acct(&bus->stats[addr], false, err);

	return err;
}
EXPORT_SYMBOL(__mdiobus_c45_write);

/**
 * __mdiobus_c45_modify_changed - Unlocked version of the mdiobus_modify function
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @devad: device address to read
 * @regnum: register number to modify
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * Return: 1 if the register was modified, 0 if no change was needed,
 *	   negative on any error condition
 *
 * Read, modify, and if any change, write the register value back to the
 * device. Any error returns a negative number.
 *
 * NOTE: MUST NOT be called from interrupt context.
 */
static int __mdiobus_c45_modify_changed(struct mii_bus *bus, int addr,
					int devad, u32 regnum, u16 mask,
					u16 set)
{
	int new, ret;

	ret = __mdiobus_c45_read(bus, addr, devad, regnum);
	if (ret < 0)
		return ret;

	new = (ret & ~mask) | set;
	if (new == ret)
		return 0;

	ret = __mdiobus_c45_write(bus, addr, devad, regnum, new);

	return ret < 0 ? ret : 1;
}

/**
 * mdiobus_read_nested - Nested version of the mdiobus_read function
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to read
 *
 * Return: The register value if successful, negative error code on failure
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
 * Return: The register value if successful, negative error code on failure
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
 * mdiobus_c45_read - Convenience function for reading a given MII mgmt register
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @devad: device address to read
 * @regnum: register number to read
 *
 * Return: The register value if successful, negative error code on failure
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
int mdiobus_c45_read(struct mii_bus *bus, int addr, int devad, u32 regnum)
{
	int retval;

	mutex_lock(&bus->mdio_lock);
	retval = __mdiobus_c45_read(bus, addr, devad, regnum);
	mutex_unlock(&bus->mdio_lock);

	return retval;
}
EXPORT_SYMBOL(mdiobus_c45_read);

/**
 * mdiobus_c45_read_nested - Nested version of the mdiobus_c45_read function
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @devad: device address to read
 * @regnum: register number to read
 *
 * Return: The register value if successful, negative error code on failure
 *
 * In case of nested MDIO bus access avoid lockdep false positives by
 * using mutex_lock_nested().
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
int mdiobus_c45_read_nested(struct mii_bus *bus, int addr, int devad,
			    u32 regnum)
{
	int retval;

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);
	retval = __mdiobus_c45_read(bus, addr, devad, regnum);
	mutex_unlock(&bus->mdio_lock);

	return retval;
}
EXPORT_SYMBOL(mdiobus_c45_read_nested);

/**
 * mdiobus_write_nested - Nested version of the mdiobus_write function
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * Return: Zero if successful, negative error code on failure
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
 * Return: Zero if successful, negative error code on failure
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
 * mdiobus_c45_write - Convenience function for writing a given MII mgmt register
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @devad: device address to read
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * Return: Zero if successful, negative error code on failure
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
int mdiobus_c45_write(struct mii_bus *bus, int addr, int devad, u32 regnum,
		      u16 val)
{
	int err;

	mutex_lock(&bus->mdio_lock);
	err = __mdiobus_c45_write(bus, addr, devad, regnum, val);
	mutex_unlock(&bus->mdio_lock);

	return err;
}
EXPORT_SYMBOL(mdiobus_c45_write);

/**
 * mdiobus_c45_write_nested - Nested version of the mdiobus_c45_write function
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @devad: device address to read
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * Return: Zero if successful, negative error code on failure
 *
 * In case of nested MDIO bus access avoid lockdep false positives by
 * using mutex_lock_nested().
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
int mdiobus_c45_write_nested(struct mii_bus *bus, int addr, int devad,
			     u32 regnum, u16 val)
{
	int err;

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);
	err = __mdiobus_c45_write(bus, addr, devad, regnum, val);
	mutex_unlock(&bus->mdio_lock);

	return err;
}
EXPORT_SYMBOL(mdiobus_c45_write_nested);

/*
 * __mdiobus_modify - Convenience function for modifying a given mdio device
 *	register
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to write
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * Return: 0 on success, negative on any error condition
 */
int __mdiobus_modify(struct mii_bus *bus, int addr, u32 regnum, u16 mask,
		     u16 set)
{
	int err;

	err = __mdiobus_modify_changed(bus, addr, regnum, mask, set);

	return err < 0 ? err : 0;
}
EXPORT_SYMBOL_GPL(__mdiobus_modify);

/**
 * mdiobus_modify - Convenience function for modifying a given mdio device
 *	register
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to write
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * Return: 0 on success, negative on any error condition
 */
int mdiobus_modify(struct mii_bus *bus, int addr, u32 regnum, u16 mask, u16 set)
{
	int err;

	mutex_lock(&bus->mdio_lock);
	err = __mdiobus_modify(bus, addr, regnum, mask, set);
	mutex_unlock(&bus->mdio_lock);

	return err;
}
EXPORT_SYMBOL_GPL(mdiobus_modify);

/**
 * mdiobus_c45_modify - Convenience function for modifying a given mdio device
 *	register
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @devad: device address to read
 * @regnum: register number to write
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * Return: 0 on success, negative on any error condition
 */
int mdiobus_c45_modify(struct mii_bus *bus, int addr, int devad, u32 regnum,
		       u16 mask, u16 set)
{
	int err;

	mutex_lock(&bus->mdio_lock);
	err = __mdiobus_c45_modify_changed(bus, addr, devad, regnum,
					   mask, set);
	mutex_unlock(&bus->mdio_lock);

	return err < 0 ? err : 0;
}
EXPORT_SYMBOL_GPL(mdiobus_c45_modify);

/**
 * mdiobus_modify_changed - Convenience function for modifying a given mdio
 *	device register and returning if it changed
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to write
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * Return: 1 if the register was modified, 0 if no change was needed,
 *	   negative on any error condition
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
 * mdiobus_c45_modify_changed - Convenience function for modifying a given mdio
 *	device register and returning if it changed
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @devad: device address to read
 * @regnum: register number to write
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * Return: 1 if the register was modified, 0 if no change was needed,
 *	   negative on any error condition
 */
int mdiobus_c45_modify_changed(struct mii_bus *bus, int addr, int devad,
			       u32 regnum, u16 mask, u16 set)
{
	int err;

	mutex_lock(&bus->mdio_lock);
	err = __mdiobus_c45_modify_changed(bus, addr, devad, regnum, mask, set);
	mutex_unlock(&bus->mdio_lock);

	return err;
}
EXPORT_SYMBOL_GPL(mdiobus_c45_modify_changed);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MDIO bus/device layer");

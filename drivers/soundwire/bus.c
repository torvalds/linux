// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

#include <linux/acpi.h>
#include <linux/mod_devicetable.h>
#include <linux/soundwire/sdw.h>
#include "bus.h"

/**
 * sdw_add_bus_master() - add a bus Master instance
 * @bus: bus instance
 *
 * Initializes the bus instance, read properties and create child
 * devices.
 */
int sdw_add_bus_master(struct sdw_bus *bus)
{
	int ret;

	if (!bus->dev) {
		pr_err("SoundWire bus has no device");
		return -ENODEV;
	}

	mutex_init(&bus->bus_lock);
	INIT_LIST_HEAD(&bus->slaves);

	if (bus->ops->read_prop) {
		ret = bus->ops->read_prop(bus);
		if (ret < 0) {
			dev_err(bus->dev, "Bus read properties failed:%d", ret);
			return ret;
		}
	}

	/*
	 * Device numbers in SoundWire are 0 thru 15. Enumeration device
	 * number (0), Broadcast device number (15), Group numbers (12 and
	 * 13) and Master device number (14) are not used for assignment so
	 * mask these and other higher bits.
	 */

	/* Set higher order bits */
	*bus->assigned = ~GENMASK(SDW_BROADCAST_DEV_NUM, SDW_ENUM_DEV_NUM);

	/* Set enumuration device number and broadcast device number */
	set_bit(SDW_ENUM_DEV_NUM, bus->assigned);
	set_bit(SDW_BROADCAST_DEV_NUM, bus->assigned);

	/* Set group device numbers and master device number */
	set_bit(SDW_GROUP12_DEV_NUM, bus->assigned);
	set_bit(SDW_GROUP13_DEV_NUM, bus->assigned);
	set_bit(SDW_MASTER_DEV_NUM, bus->assigned);

	/*
	 * SDW is an enumerable bus, but devices can be powered off. So,
	 * they won't be able to report as present.
	 *
	 * Create Slave devices based on Slaves described in
	 * the respective firmware (ACPI/DT)
	 */
	if (IS_ENABLED(CONFIG_ACPI) && ACPI_HANDLE(bus->dev))
		ret = sdw_acpi_find_slaves(bus);
	else
		ret = -ENOTSUPP; /* No ACPI/DT so error out */

	if (ret) {
		dev_err(bus->dev, "Finding slaves failed:%d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(sdw_add_bus_master);

static int sdw_delete_slave(struct device *dev, void *data)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct sdw_bus *bus = slave->bus;

	mutex_lock(&bus->bus_lock);

	if (slave->dev_num) /* clear dev_num if assigned */
		clear_bit(slave->dev_num, bus->assigned);

	list_del_init(&slave->node);
	mutex_unlock(&bus->bus_lock);

	device_unregister(dev);
	return 0;
}

/**
 * sdw_delete_bus_master() - delete the bus master instance
 * @bus: bus to be deleted
 *
 * Remove the instance, delete the child devices.
 */
void sdw_delete_bus_master(struct sdw_bus *bus)
{
	device_for_each_child(bus->dev, NULL, sdw_delete_slave);
}
EXPORT_SYMBOL(sdw_delete_bus_master);

void sdw_extract_slave_id(struct sdw_bus *bus,
			u64 addr, struct sdw_slave_id *id)
{
	dev_dbg(bus->dev, "SDW Slave Addr: %llx", addr);

	/*
	 * Spec definition
	 *   Register		Bit	Contents
	 *   DevId_0 [7:4]	47:44	sdw_version
	 *   DevId_0 [3:0]	43:40	unique_id
	 *   DevId_1		39:32	mfg_id [15:8]
	 *   DevId_2		31:24	mfg_id [7:0]
	 *   DevId_3		23:16	part_id [15:8]
	 *   DevId_4		15:08	part_id [7:0]
	 *   DevId_5		07:00	class_id
	 */
	id->sdw_version = (addr >> 44) & GENMASK(3, 0);
	id->unique_id = (addr >> 40) & GENMASK(3, 0);
	id->mfg_id = (addr >> 24) & GENMASK(15, 0);
	id->part_id = (addr >> 8) & GENMASK(15, 0);
	id->class_id = addr & GENMASK(7, 0);

	dev_dbg(bus->dev,
		"SDW Slave class_id %x, part_id %x, mfg_id %x, unique_id %x, version %x",
				id->class_id, id->part_id, id->mfg_id,
				id->unique_id, id->sdw_version);

}

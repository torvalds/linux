// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include "bus.h"

static void sdw_slave_release(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);

	kfree(slave);
}

static int sdw_slave_add(struct sdw_bus *bus,
			 struct sdw_slave_id *id, struct fwyesde_handle *fwyesde)
{
	struct sdw_slave *slave;
	int ret;

	slave = kzalloc(sizeof(*slave), GFP_KERNEL);
	if (!slave)
		return -ENOMEM;

	/* Initialize data structure */
	memcpy(&slave->id, id, sizeof(*id));
	slave->dev.parent = bus->dev;
	slave->dev.fwyesde = fwyesde;

	if (id->unique_id == SDW_IGNORED_UNIQUE_ID) {
		/* name shall be sdw:link:mfg:part:class */
		dev_set_name(&slave->dev, "sdw:%x:%x:%x:%x",
			     bus->link_id, id->mfg_id, id->part_id,
			     id->class_id);
	} else {
		/* name shall be sdw:link:mfg:part:class:unique */
		dev_set_name(&slave->dev, "sdw:%x:%x:%x:%x:%x",
			     bus->link_id, id->mfg_id, id->part_id,
			     id->class_id, id->unique_id);
	}

	slave->dev.release = sdw_slave_release;
	slave->dev.bus = &sdw_bus_type;
	slave->dev.of_yesde = of_yesde_get(to_of_yesde(fwyesde));
	slave->bus = bus;
	slave->status = SDW_SLAVE_UNATTACHED;
	slave->dev_num = 0;

	mutex_lock(&bus->bus_lock);
	list_add_tail(&slave->yesde, &bus->slaves);
	mutex_unlock(&bus->bus_lock);

	ret = device_register(&slave->dev);
	if (ret) {
		dev_err(bus->dev, "Failed to add slave: ret %d\n", ret);

		/*
		 * On err, don't free but drop ref as this will be freed
		 * when release method is invoked.
		 */
		mutex_lock(&bus->bus_lock);
		list_del(&slave->yesde);
		mutex_unlock(&bus->bus_lock);
		put_device(&slave->dev);
	}
	sdw_slave_debugfs_init(slave);

	return ret;
}

#if IS_ENABLED(CONFIG_ACPI)

static bool find_slave(struct sdw_bus *bus,
		       struct acpi_device *adev,
		       struct sdw_slave_id *id)
{
	unsigned long long addr;
	unsigned int link_id;
	acpi_status status;

	status = acpi_evaluate_integer(adev->handle,
				       METHOD_NAME__ADR, NULL, &addr);

	if (ACPI_FAILURE(status)) {
		dev_err(bus->dev, "_ADR resolution failed: %x\n",
			status);
		return false;
	}

	/* Extract link id from ADR, Bit 51 to 48 (included) */
	link_id = (addr >> 48) & GENMASK(3, 0);

	/* Check for link_id match */
	if (link_id != bus->link_id)
		return false;

	sdw_extract_slave_id(bus, addr, id);

	return true;
}

/*
 * sdw_acpi_find_slaves() - Find Slave devices in Master ACPI yesde
 * @bus: SDW bus instance
 *
 * Scans Master ACPI yesde for SDW child Slave devices and registers it.
 */
int sdw_acpi_find_slaves(struct sdw_bus *bus)
{
	struct acpi_device *adev, *parent;
	struct acpi_device *adev2, *parent2;

	parent = ACPI_COMPANION(bus->dev);
	if (!parent) {
		dev_err(bus->dev, "Can't find parent for acpi bind\n");
		return -ENODEV;
	}

	list_for_each_entry(adev, &parent->children, yesde) {
		struct sdw_slave_id id;
		struct sdw_slave_id id2;
		bool igyesre_unique_id = true;

		if (!find_slave(bus, adev, &id))
			continue;

		/* brute-force O(N^2) search for duplicates */
		parent2 = parent;
		list_for_each_entry(adev2, &parent2->children, yesde) {

			if (adev == adev2)
				continue;

			if (!find_slave(bus, adev2, &id2))
				continue;

			if (id.sdw_version != id2.sdw_version ||
			    id.mfg_id != id2.mfg_id ||
			    id.part_id != id2.part_id ||
			    id.class_id != id2.class_id)
				continue;

			if (id.unique_id != id2.unique_id) {
				dev_dbg(bus->dev,
					"Valid unique IDs %x %x for Slave mfg %x part %d\n",
					id.unique_id, id2.unique_id,
					id.mfg_id, id.part_id);
				igyesre_unique_id = false;
			} else {
				dev_err(bus->dev,
					"Invalid unique IDs %x %x for Slave mfg %x part %d\n",
					id.unique_id, id2.unique_id,
					id.mfg_id, id.part_id);
				return -ENODEV;
			}
		}

		if (igyesre_unique_id)
			id.unique_id = SDW_IGNORED_UNIQUE_ID;

		/*
		 * don't error check for sdw_slave_add as we want to continue
		 * adding Slaves
		 */
		sdw_slave_add(bus, &id, acpi_fwyesde_handle(adev));
	}

	return 0;
}

#endif

/*
 * sdw_of_find_slaves() - Find Slave devices in master device tree yesde
 * @bus: SDW bus instance
 *
 * Scans Master DT yesde for SDW child Slave devices and registers it.
 */
int sdw_of_find_slaves(struct sdw_bus *bus)
{
	struct device *dev = bus->dev;
	struct device_yesde *yesde;

	for_each_child_of_yesde(bus->dev->of_yesde, yesde) {
		int link_id, ret, len;
		unsigned int sdw_version;
		const char *compat = NULL;
		struct sdw_slave_id id;
		const __be32 *addr;

		compat = of_get_property(yesde, "compatible", NULL);
		if (!compat)
			continue;

		ret = sscanf(compat, "sdw%01x%04hx%04hx%02hhx", &sdw_version,
			     &id.mfg_id, &id.part_id, &id.class_id);

		if (ret != 4) {
			dev_err(dev, "Invalid compatible string found %s\n",
				compat);
			continue;
		}

		addr = of_get_property(yesde, "reg", &len);
		if (!addr || (len < 2 * sizeof(u32))) {
			dev_err(dev, "Invalid Link and Instance ID\n");
			continue;
		}

		link_id = be32_to_cpup(addr++);
		id.unique_id = be32_to_cpup(addr);
		id.sdw_version = sdw_version;

		/* Check for link_id match */
		if (link_id != bus->link_id)
			continue;

		sdw_slave_add(bus, &id, of_fwyesde_handle(yesde));
	}

	return 0;
}

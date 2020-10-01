// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include "bus.h"
#include "sysfs_local.h"

static void sdw_slave_release(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);

	kfree(slave);
}

struct device_type sdw_slave_type = {
	.name =		"sdw_slave",
	.release =	sdw_slave_release,
	.uevent =	sdw_slave_uevent,
};

int sdw_slave_add(struct sdw_bus *bus,
		  struct sdw_slave_id *id, struct fwnode_handle *fwnode)
{
	struct sdw_slave *slave;
	int ret;
	int i;

	slave = kzalloc(sizeof(*slave), GFP_KERNEL);
	if (!slave)
		return -ENOMEM;

	/* Initialize data structure */
	memcpy(&slave->id, id, sizeof(*id));
	slave->dev.parent = bus->dev;
	slave->dev.fwnode = fwnode;

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

	slave->dev.bus = &sdw_bus_type;
	slave->dev.of_node = of_node_get(to_of_node(fwnode));
	slave->dev.type = &sdw_slave_type;
	slave->dev.groups = sdw_slave_status_attr_groups;
	slave->bus = bus;
	slave->status = SDW_SLAVE_UNATTACHED;
	init_completion(&slave->enumeration_complete);
	init_completion(&slave->initialization_complete);
	slave->dev_num = 0;
	init_completion(&slave->probe_complete);
	slave->probed = false;
	slave->first_interrupt_done = false;

	for (i = 0; i < SDW_MAX_PORTS; i++)
		init_completion(&slave->port_ready[i]);

	mutex_lock(&bus->bus_lock);
	list_add_tail(&slave->node, &bus->slaves);
	mutex_unlock(&bus->bus_lock);

	ret = device_register(&slave->dev);
	if (ret) {
		dev_err(bus->dev, "Failed to add slave: ret %d\n", ret);

		/*
		 * On err, don't free but drop ref as this will be freed
		 * when release method is invoked.
		 */
		mutex_lock(&bus->bus_lock);
		list_del(&slave->node);
		mutex_unlock(&bus->bus_lock);
		put_device(&slave->dev);

		return ret;
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
	link_id = SDW_DISCO_LINK_ID(addr);

	/* Check for link_id match */
	if (link_id != bus->link_id)
		return false;

	sdw_extract_slave_id(bus, addr, id);

	return true;
}

/*
 * sdw_acpi_find_slaves() - Find Slave devices in Master ACPI node
 * @bus: SDW bus instance
 *
 * Scans Master ACPI node for SDW child Slave devices and registers it.
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

	list_for_each_entry(adev, &parent->children, node) {
		struct sdw_slave_id id;
		struct sdw_slave_id id2;
		bool ignore_unique_id = true;

		if (!find_slave(bus, adev, &id))
			continue;

		/* brute-force O(N^2) search for duplicates */
		parent2 = parent;
		list_for_each_entry(adev2, &parent2->children, node) {

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
				ignore_unique_id = false;
			} else {
				dev_err(bus->dev,
					"Invalid unique IDs %x %x for Slave mfg %x part %d\n",
					id.unique_id, id2.unique_id,
					id.mfg_id, id.part_id);
				return -ENODEV;
			}
		}

		if (ignore_unique_id)
			id.unique_id = SDW_IGNORED_UNIQUE_ID;

		/*
		 * don't error check for sdw_slave_add as we want to continue
		 * adding Slaves
		 */
		sdw_slave_add(bus, &id, acpi_fwnode_handle(adev));
	}

	return 0;
}

#endif

/*
 * sdw_of_find_slaves() - Find Slave devices in master device tree node
 * @bus: SDW bus instance
 *
 * Scans Master DT node for SDW child Slave devices and registers it.
 */
int sdw_of_find_slaves(struct sdw_bus *bus)
{
	struct device *dev = bus->dev;
	struct device_node *node;

	for_each_child_of_node(bus->dev->of_node, node) {
		int link_id, ret, len;
		unsigned int sdw_version;
		const char *compat = NULL;
		struct sdw_slave_id id;
		const __be32 *addr;

		compat = of_get_property(node, "compatible", NULL);
		if (!compat)
			continue;

		ret = sscanf(compat, "sdw%01x%04hx%04hx%02hhx", &sdw_version,
			     &id.mfg_id, &id.part_id, &id.class_id);

		if (ret != 4) {
			dev_err(dev, "Invalid compatible string found %s\n",
				compat);
			continue;
		}

		addr = of_get_property(node, "reg", &len);
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

		sdw_slave_add(bus, &id, of_fwnode_handle(node));
	}

	return 0;
}

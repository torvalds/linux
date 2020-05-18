// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2019-2020 Intel Corporation.

#include <linux/device.h>
#include <linux/acpi.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include "bus.h"

static void sdw_master_device_release(struct device *dev)
{
	struct sdw_master_device *md = dev_to_sdw_master_device(dev);

	kfree(md);
}

struct device_type sdw_master_type = {
	.name =		"soundwire_master",
	.release =	sdw_master_device_release,
};

/**
 * sdw_master_device_add() - create a Linux Master Device representation.
 * @bus: SDW bus instance
 * @parent: parent device
 * @fwnode: firmware node handle
 */
int sdw_master_device_add(struct sdw_bus *bus, struct device *parent,
			  struct fwnode_handle *fwnode)
{
	struct sdw_master_device *md;
	int ret;

	if (!parent)
		return -EINVAL;

	md = kzalloc(sizeof(*md), GFP_KERNEL);
	if (!md)
		return -ENOMEM;

	md->dev.bus = &sdw_bus_type;
	md->dev.type = &sdw_master_type;
	md->dev.parent = parent;
	md->dev.of_node = parent->of_node;
	md->dev.fwnode = fwnode;
	md->dev.dma_mask = parent->dma_mask;

	dev_set_name(&md->dev, "sdw-master-%d", bus->id);

	ret = device_register(&md->dev);
	if (ret) {
		dev_err(parent, "Failed to add master: ret %d\n", ret);
		/*
		 * On err, don't free but drop ref as this will be freed
		 * when release method is invoked.
		 */
		put_device(&md->dev);
		goto device_register_err;
	}

	/* add shortcuts to improve code readability/compactness */
	md->bus = bus;
	bus->dev = &md->dev;
	bus->md = md;

device_register_err:
	return ret;
}

/**
 * sdw_master_device_del() - delete a Linux Master Device representation.
 * @bus: bus handle
 *
 * This function is the dual of sdw_master_device_add()
 */
int sdw_master_device_del(struct sdw_bus *bus)
{
	device_unregister(bus->dev);

	return 0;
}

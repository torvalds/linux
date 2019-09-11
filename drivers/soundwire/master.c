// SPDX-License-Identifier: (GPL-2.0)
// Copyright(c) 2019 Intel Corporation.

#include <linux/device.h>
#include <linux/acpi.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include "bus.h"

static void sdw_md_release(struct device *dev)
{
	struct sdw_master_device *md = to_sdw_master_device(dev);

	kfree(md);
}

struct device_type sdw_md_type = {
	.name =		"soundwire_master",
	.release =	sdw_md_release,
};

struct sdw_master_device *sdw_md_add(struct sdw_md_driver *driver,
				     struct device *parent,
				     struct fwnode_handle *fwnode,
				     int link_id)
{
	struct sdw_master_device *md;
	int ret;

	if (!driver->probe) {
		dev_err(parent, "mandatory probe callback missing\n");
		return ERR_PTR(-EINVAL);
	}

	md = kzalloc(sizeof(*md), GFP_KERNEL);
	if (!md)
		return ERR_PTR(-ENOMEM);

	md->link_id = link_id;

	md->driver = driver;

	md->dev.parent = parent;
	md->dev.fwnode = fwnode;
	md->dev.bus = &sdw_bus_type;
	md->dev.type = &sdw_md_type;
	md->dev.dma_mask = md->dev.parent->dma_mask;
	dev_set_name(&md->dev, "sdw-master-%d", md->link_id);

	ret = device_register(&md->dev);
	if (ret) {
		dev_err(parent, "Failed to add master: ret %d\n", ret);
		/*
		 * On err, don't free but drop ref as this will be freed
		 * when release method is invoked.
		 */
		put_device(&md->dev);
		return ERR_PTR(-ENOMEM);
	}

	return md;
}
EXPORT_SYMBOL_GPL(sdw_md_add);

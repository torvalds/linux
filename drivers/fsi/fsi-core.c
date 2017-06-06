/*
 * FSI core driver
 *
 * Copyright (C) IBM Corporation 2016
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/fsi.h>
#include <linux/idr.h>
#include <linux/module.h>

#include "fsi-master.h"

static DEFINE_IDA(master_ida);

struct fsi_slave {
	struct device		dev;
	struct fsi_master	*master;
	int			id;
	int			link;
	uint32_t		size;	/* size of slave address space */
};

#define to_fsi_slave(d) container_of(d, struct fsi_slave, dev)

/* FSI master support */
int fsi_master_register(struct fsi_master *master)
{
	int rc;

	if (!master)
		return -EINVAL;

	master->idx = ida_simple_get(&master_ida, 0, INT_MAX, GFP_KERNEL);
	dev_set_name(&master->dev, "fsi%d", master->idx);

	rc = device_register(&master->dev);
	if (rc)
		ida_simple_remove(&master_ida, master->idx);

	return rc;
}
EXPORT_SYMBOL_GPL(fsi_master_register);

void fsi_master_unregister(struct fsi_master *master)
{
	if (master->idx >= 0) {
		ida_simple_remove(&master_ida, master->idx);
		master->idx = -1;
	}

	device_unregister(&master->dev);
}
EXPORT_SYMBOL_GPL(fsi_master_unregister);

/* FSI core & Linux bus type definitions */

static int fsi_bus_match(struct device *dev, struct device_driver *drv)
{
	struct fsi_device *fsi_dev = to_fsi_dev(dev);
	struct fsi_driver *fsi_drv = to_fsi_drv(drv);
	const struct fsi_device_id *id;

	if (!fsi_drv->id_table)
		return 0;

	for (id = fsi_drv->id_table; id->engine_type; id++) {
		if (id->engine_type != fsi_dev->engine_type)
			continue;
		if (id->version == FSI_VERSION_ANY ||
				id->version == fsi_dev->version)
			return 1;
	}

	return 0;
}

struct bus_type fsi_bus_type = {
	.name		= "fsi",
	.match		= fsi_bus_match,
};
EXPORT_SYMBOL_GPL(fsi_bus_type);

static int fsi_init(void)
{
	return bus_register(&fsi_bus_type);
}

static void fsi_exit(void)
{
	bus_unregister(&fsi_bus_type);
}

module_init(fsi_init);
module_exit(fsi_exit);

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

static int fsi_master_read(struct fsi_master *master, int link,
		uint8_t slave_id, uint32_t addr, void *val, size_t size);
static int fsi_master_write(struct fsi_master *master, int link,
		uint8_t slave_id, uint32_t addr, const void *val, size_t size);

/* FSI slave support */
static int fsi_slave_calc_addr(struct fsi_slave *slave, uint32_t *addrp,
		uint8_t *idp)
{
	uint32_t addr = *addrp;
	uint8_t id = *idp;

	if (addr > slave->size)
		return -EINVAL;

	/* For 23 bit addressing, we encode the extra two bits in the slave
	 * id (and the slave's actual ID needs to be 0).
	 */
	if (addr > 0x1fffff) {
		if (slave->id != 0)
			return -EINVAL;
		id = (addr >> 21) & 0x3;
		addr &= 0x1fffff;
	}

	*addrp = addr;
	*idp = id;
	return 0;
}

static int fsi_slave_read(struct fsi_slave *slave, uint32_t addr,
			void *val, size_t size)
{
	uint8_t id = slave->id;
	int rc;

	rc = fsi_slave_calc_addr(slave, &addr, &id);
	if (rc)
		return rc;

	return fsi_master_read(slave->master, slave->link, id,
			addr, val, size);
}

static int fsi_slave_write(struct fsi_slave *slave, uint32_t addr,
			const void *val, size_t size)
{
	uint8_t id = slave->id;
	int rc;

	rc = fsi_slave_calc_addr(slave, &addr, &id);
	if (rc)
		return rc;

	return fsi_master_write(slave->master, slave->link, id,
			addr, val, size);
}

static int fsi_slave_init(struct fsi_master *master, int link, uint8_t id)
{
	/* todo: initialise slave device, perform engine scan */

	return -ENODEV;
}

/* FSI master support */
static int fsi_check_access(uint32_t addr, size_t size)
{
	if (size != 1 && size != 2 && size != 4)
		return -EINVAL;

	if ((addr & 0x3) != (size & 0x3))
		return -EINVAL;

	return 0;
}

static int fsi_master_read(struct fsi_master *master, int link,
		uint8_t slave_id, uint32_t addr, void *val, size_t size)
{
	int rc;

	rc = fsi_check_access(addr, size);
	if (rc)
		return rc;

	return master->read(master, link, slave_id, addr, val, size);
}

static int fsi_master_write(struct fsi_master *master, int link,
		uint8_t slave_id, uint32_t addr, const void *val, size_t size)
{
	int rc;

	rc = fsi_check_access(addr, size);
	if (rc)
		return rc;

	return master->write(master, link, slave_id, addr, val, size);
}

static int fsi_master_scan(struct fsi_master *master)
{
	int link;

	for (link = 0; link < master->n_links; link++)
		fsi_slave_init(master, link, 0);

	return 0;
}

int fsi_master_register(struct fsi_master *master)
{
	int rc;

	if (!master)
		return -EINVAL;

	master->idx = ida_simple_get(&master_ida, 0, INT_MAX, GFP_KERNEL);
	dev_set_name(&master->dev, "fsi%d", master->idx);

	rc = device_register(&master->dev);
	if (rc) {
		ida_simple_remove(&master_ida, master->idx);
		return rc;
	}

	fsi_master_scan(master);
	return 0;
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

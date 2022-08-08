// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Cadence Design Systems Inc.
 *
 * Author: Boris Brezillon <boris.brezillon@bootlin.com>
 */

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "internals.h"

static DEFINE_IDR(i3c_bus_idr);
static DEFINE_MUTEX(i3c_core_lock);

/**
 * i3c_bus_maintenance_lock - Lock the bus for a maintenance operation
 * @bus: I3C bus to take the lock on
 *
 * This function takes the bus lock so that no other operations can occur on
 * the bus. This is needed for all kind of bus maintenance operation, like
 * - enabling/disabling slave events
 * - re-triggering DAA
 * - changing the dynamic address of a device
 * - relinquishing mastership
 * - ...
 *
 * The reason for this kind of locking is that we don't want drivers and core
 * logic to rely on I3C device information that could be changed behind their
 * back.
 */
static void i3c_bus_maintenance_lock(struct i3c_bus *bus)
{
	down_write(&bus->lock);
}

/**
 * i3c_bus_maintenance_unlock - Release the bus lock after a maintenance
 *			      operation
 * @bus: I3C bus to release the lock on
 *
 * Should be called when the bus maintenance operation is done. See
 * i3c_bus_maintenance_lock() for more details on what these maintenance
 * operations are.
 */
static void i3c_bus_maintenance_unlock(struct i3c_bus *bus)
{
	up_write(&bus->lock);
}

/**
 * i3c_bus_normaluse_lock - Lock the bus for a normal operation
 * @bus: I3C bus to take the lock on
 *
 * This function takes the bus lock for any operation that is not a maintenance
 * operation (see i3c_bus_maintenance_lock() for a non-exhaustive list of
 * maintenance operations). Basically all communications with I3C devices are
 * normal operations (HDR, SDR transfers or CCC commands that do not change bus
 * state or I3C dynamic address).
 *
 * Note that this lock is not guaranteeing serialization of normal operations.
 * In other words, transfer requests passed to the I3C master can be submitted
 * in parallel and I3C master drivers have to use their own locking to make
 * sure two different communications are not inter-mixed, or access to the
 * output/input queue is not done while the engine is busy.
 */
void i3c_bus_normaluse_lock(struct i3c_bus *bus)
{
	down_read(&bus->lock);
}

/**
 * i3c_bus_normaluse_unlock - Release the bus lock after a normal operation
 * @bus: I3C bus to release the lock on
 *
 * Should be called when a normal operation is done. See
 * i3c_bus_normaluse_lock() for more details on what these normal operations
 * are.
 */
void i3c_bus_normaluse_unlock(struct i3c_bus *bus)
{
	up_read(&bus->lock);
}

static struct i3c_master_controller *
i3c_bus_to_i3c_master(struct i3c_bus *i3cbus)
{
	return container_of(i3cbus, struct i3c_master_controller, bus);
}

static struct i3c_master_controller *dev_to_i3cmaster(struct device *dev)
{
	return container_of(dev, struct i3c_master_controller, dev);
}

static const struct device_type i3c_device_type;

static struct i3c_bus *dev_to_i3cbus(struct device *dev)
{
	struct i3c_master_controller *master;

	if (dev->type == &i3c_device_type)
		return dev_to_i3cdev(dev)->bus;

	master = dev_to_i3cmaster(dev);

	return &master->bus;
}

static struct i3c_dev_desc *dev_to_i3cdesc(struct device *dev)
{
	struct i3c_master_controller *master;

	if (dev->type == &i3c_device_type)
		return dev_to_i3cdev(dev)->desc;

	master = dev_to_i3cmaster(dev);

	return master->this;
}

static ssize_t bcr_show(struct device *dev,
			struct device_attribute *da,
			char *buf)
{
	struct i3c_bus *bus = dev_to_i3cbus(dev);
	struct i3c_dev_desc *desc;
	ssize_t ret;

	i3c_bus_normaluse_lock(bus);
	desc = dev_to_i3cdesc(dev);
	ret = sprintf(buf, "%x\n", desc->info.bcr);
	i3c_bus_normaluse_unlock(bus);

	return ret;
}
static DEVICE_ATTR_RO(bcr);

static ssize_t dcr_show(struct device *dev,
			struct device_attribute *da,
			char *buf)
{
	struct i3c_bus *bus = dev_to_i3cbus(dev);
	struct i3c_dev_desc *desc;
	ssize_t ret;

	i3c_bus_normaluse_lock(bus);
	desc = dev_to_i3cdesc(dev);
	ret = sprintf(buf, "%x\n", desc->info.dcr);
	i3c_bus_normaluse_unlock(bus);

	return ret;
}
static DEVICE_ATTR_RO(dcr);

static ssize_t pid_show(struct device *dev,
			struct device_attribute *da,
			char *buf)
{
	struct i3c_bus *bus = dev_to_i3cbus(dev);
	struct i3c_dev_desc *desc;
	ssize_t ret;

	i3c_bus_normaluse_lock(bus);
	desc = dev_to_i3cdesc(dev);
	ret = sprintf(buf, "%llx\n", desc->info.pid);
	i3c_bus_normaluse_unlock(bus);

	return ret;
}
static DEVICE_ATTR_RO(pid);

static ssize_t dynamic_address_show(struct device *dev,
				    struct device_attribute *da,
				    char *buf)
{
	struct i3c_bus *bus = dev_to_i3cbus(dev);
	struct i3c_dev_desc *desc;
	ssize_t ret;

	i3c_bus_normaluse_lock(bus);
	desc = dev_to_i3cdesc(dev);
	ret = sprintf(buf, "%02x\n", desc->info.dyn_addr);
	i3c_bus_normaluse_unlock(bus);

	return ret;
}
static DEVICE_ATTR_RO(dynamic_address);

static const char * const hdrcap_strings[] = {
	"hdr-ddr", "hdr-tsp", "hdr-tsl",
};

static ssize_t hdrcap_show(struct device *dev,
			   struct device_attribute *da,
			   char *buf)
{
	struct i3c_bus *bus = dev_to_i3cbus(dev);
	struct i3c_dev_desc *desc;
	ssize_t offset = 0, ret;
	unsigned long caps;
	int mode;

	i3c_bus_normaluse_lock(bus);
	desc = dev_to_i3cdesc(dev);
	caps = desc->info.hdr_cap;
	for_each_set_bit(mode, &caps, 8) {
		if (mode >= ARRAY_SIZE(hdrcap_strings))
			break;

		if (!hdrcap_strings[mode])
			continue;

		ret = sprintf(buf + offset, offset ? " %s" : "%s",
			      hdrcap_strings[mode]);
		if (ret < 0)
			goto out;

		offset += ret;
	}

	ret = sprintf(buf + offset, "\n");
	if (ret < 0)
		goto out;

	ret = offset + ret;

out:
	i3c_bus_normaluse_unlock(bus);

	return ret;
}
static DEVICE_ATTR_RO(hdrcap);

static ssize_t modalias_show(struct device *dev,
			     struct device_attribute *da, char *buf)
{
	struct i3c_device *i3c = dev_to_i3cdev(dev);
	struct i3c_device_info devinfo;
	u16 manuf, part, ext;

	i3c_device_get_info(i3c, &devinfo);
	manuf = I3C_PID_MANUF_ID(devinfo.pid);
	part = I3C_PID_PART_ID(devinfo.pid);
	ext = I3C_PID_EXTRA_INFO(devinfo.pid);

	if (I3C_PID_RND_LOWER_32BITS(devinfo.pid))
		return sprintf(buf, "i3c:dcr%02Xmanuf%04X", devinfo.dcr,
			       manuf);

	return sprintf(buf, "i3c:dcr%02Xmanuf%04Xpart%04Xext%04X",
		       devinfo.dcr, manuf, part, ext);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *i3c_device_attrs[] = {
	&dev_attr_bcr.attr,
	&dev_attr_dcr.attr,
	&dev_attr_pid.attr,
	&dev_attr_dynamic_address.attr,
	&dev_attr_hdrcap.attr,
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(i3c_device);

static int i3c_device_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct i3c_device *i3cdev = dev_to_i3cdev(dev);
	struct i3c_device_info devinfo;
	u16 manuf, part, ext;

	i3c_device_get_info(i3cdev, &devinfo);
	manuf = I3C_PID_MANUF_ID(devinfo.pid);
	part = I3C_PID_PART_ID(devinfo.pid);
	ext = I3C_PID_EXTRA_INFO(devinfo.pid);

	if (I3C_PID_RND_LOWER_32BITS(devinfo.pid))
		return add_uevent_var(env, "MODALIAS=i3c:dcr%02Xmanuf%04X",
				      devinfo.dcr, manuf);

	return add_uevent_var(env,
			      "MODALIAS=i3c:dcr%02Xmanuf%04Xpart%04Xext%04X",
			      devinfo.dcr, manuf, part, ext);
}

static const struct device_type i3c_device_type = {
	.groups	= i3c_device_groups,
	.uevent = i3c_device_uevent,
};

static int i3c_device_match(struct device *dev, struct device_driver *drv)
{
	struct i3c_device *i3cdev;
	struct i3c_driver *i3cdrv;

	if (dev->type != &i3c_device_type)
		return 0;

	i3cdev = dev_to_i3cdev(dev);
	i3cdrv = drv_to_i3cdrv(drv);
	if (i3c_device_match_id(i3cdev, i3cdrv->id_table))
		return 1;

	return 0;
}

static int i3c_device_probe(struct device *dev)
{
	struct i3c_device *i3cdev = dev_to_i3cdev(dev);
	struct i3c_driver *driver = drv_to_i3cdrv(dev->driver);

	return driver->probe(i3cdev);
}

static int i3c_device_remove(struct device *dev)
{
	struct i3c_device *i3cdev = dev_to_i3cdev(dev);
	struct i3c_driver *driver = drv_to_i3cdrv(dev->driver);

	if (driver->remove)
		driver->remove(i3cdev);

	i3c_device_free_ibi(i3cdev);

	return 0;
}

struct bus_type i3c_bus_type = {
	.name = "i3c",
	.match = i3c_device_match,
	.probe = i3c_device_probe,
	.remove = i3c_device_remove,
};

static enum i3c_addr_slot_status
i3c_bus_get_addr_slot_status(struct i3c_bus *bus, u16 addr)
{
	int status, bitpos = addr * 2;

	if (addr > I2C_MAX_ADDR)
		return I3C_ADDR_SLOT_RSVD;

	status = bus->addrslots[bitpos / BITS_PER_LONG];
	status >>= bitpos % BITS_PER_LONG;

	return status & I3C_ADDR_SLOT_STATUS_MASK;
}

static void i3c_bus_set_addr_slot_status(struct i3c_bus *bus, u16 addr,
					 enum i3c_addr_slot_status status)
{
	int bitpos = addr * 2;
	unsigned long *ptr;

	if (addr > I2C_MAX_ADDR)
		return;

	ptr = bus->addrslots + (bitpos / BITS_PER_LONG);
	*ptr &= ~((unsigned long)I3C_ADDR_SLOT_STATUS_MASK <<
						(bitpos % BITS_PER_LONG));
	*ptr |= (unsigned long)status << (bitpos % BITS_PER_LONG);
}

static bool i3c_bus_dev_addr_is_avail(struct i3c_bus *bus, u8 addr)
{
	enum i3c_addr_slot_status status;

	status = i3c_bus_get_addr_slot_status(bus, addr);

	return status == I3C_ADDR_SLOT_FREE;
}

static int i3c_bus_get_free_addr(struct i3c_bus *bus, u8 start_addr)
{
	enum i3c_addr_slot_status status;
	u8 addr;

	for (addr = start_addr; addr < I3C_MAX_ADDR; addr++) {
		status = i3c_bus_get_addr_slot_status(bus, addr);
		if (status == I3C_ADDR_SLOT_FREE)
			return addr;
	}

	return -ENOMEM;
}

static void i3c_bus_init_addrslots(struct i3c_bus *bus)
{
	int i;

	/* Addresses 0 to 7 are reserved. */
	for (i = 0; i < 8; i++)
		i3c_bus_set_addr_slot_status(bus, i, I3C_ADDR_SLOT_RSVD);

	/*
	 * Reserve broadcast address and all addresses that might collide
	 * with the broadcast address when facing a single bit error.
	 */
	i3c_bus_set_addr_slot_status(bus, I3C_BROADCAST_ADDR,
				     I3C_ADDR_SLOT_RSVD);
	for (i = 0; i < 7; i++)
		i3c_bus_set_addr_slot_status(bus, I3C_BROADCAST_ADDR ^ BIT(i),
					     I3C_ADDR_SLOT_RSVD);
}

static void i3c_bus_cleanup(struct i3c_bus *i3cbus)
{
	mutex_lock(&i3c_core_lock);
	idr_remove(&i3c_bus_idr, i3cbus->id);
	mutex_unlock(&i3c_core_lock);
}

static int i3c_bus_init(struct i3c_bus *i3cbus)
{
	int ret;

	init_rwsem(&i3cbus->lock);
	INIT_LIST_HEAD(&i3cbus->devs.i2c);
	INIT_LIST_HEAD(&i3cbus->devs.i3c);
	i3c_bus_init_addrslots(i3cbus);
	i3cbus->mode = I3C_BUS_MODE_PURE;

	mutex_lock(&i3c_core_lock);
	ret = idr_alloc(&i3c_bus_idr, i3cbus, 0, 0, GFP_KERNEL);
	mutex_unlock(&i3c_core_lock);

	if (ret < 0)
		return ret;

	i3cbus->id = ret;

	return 0;
}

static const char * const i3c_bus_mode_strings[] = {
	[I3C_BUS_MODE_PURE] = "pure",
	[I3C_BUS_MODE_MIXED_FAST] = "mixed-fast",
	[I3C_BUS_MODE_MIXED_LIMITED] = "mixed-limited",
	[I3C_BUS_MODE_MIXED_SLOW] = "mixed-slow",
};

static ssize_t mode_show(struct device *dev,
			 struct device_attribute *da,
			 char *buf)
{
	struct i3c_bus *i3cbus = dev_to_i3cbus(dev);
	ssize_t ret;

	i3c_bus_normaluse_lock(i3cbus);
	if (i3cbus->mode < 0 ||
	    i3cbus->mode >= ARRAY_SIZE(i3c_bus_mode_strings) ||
	    !i3c_bus_mode_strings[i3cbus->mode])
		ret = sprintf(buf, "unknown\n");
	else
		ret = sprintf(buf, "%s\n", i3c_bus_mode_strings[i3cbus->mode]);
	i3c_bus_normaluse_unlock(i3cbus);

	return ret;
}
static DEVICE_ATTR_RO(mode);

static ssize_t current_master_show(struct device *dev,
				   struct device_attribute *da,
				   char *buf)
{
	struct i3c_bus *i3cbus = dev_to_i3cbus(dev);
	ssize_t ret;

	i3c_bus_normaluse_lock(i3cbus);
	ret = sprintf(buf, "%d-%llx\n", i3cbus->id,
		      i3cbus->cur_master->info.pid);
	i3c_bus_normaluse_unlock(i3cbus);

	return ret;
}
static DEVICE_ATTR_RO(current_master);

static ssize_t i3c_scl_frequency_show(struct device *dev,
				      struct device_attribute *da,
				      char *buf)
{
	struct i3c_bus *i3cbus = dev_to_i3cbus(dev);
	ssize_t ret;

	i3c_bus_normaluse_lock(i3cbus);
	ret = sprintf(buf, "%ld\n", i3cbus->scl_rate.i3c);
	i3c_bus_normaluse_unlock(i3cbus);

	return ret;
}
static DEVICE_ATTR_RO(i3c_scl_frequency);

static ssize_t i2c_scl_frequency_show(struct device *dev,
				      struct device_attribute *da,
				      char *buf)
{
	struct i3c_bus *i3cbus = dev_to_i3cbus(dev);
	ssize_t ret;

	i3c_bus_normaluse_lock(i3cbus);
	ret = sprintf(buf, "%ld\n", i3cbus->scl_rate.i2c);
	i3c_bus_normaluse_unlock(i3cbus);

	return ret;
}
static DEVICE_ATTR_RO(i2c_scl_frequency);

static struct attribute *i3c_masterdev_attrs[] = {
	&dev_attr_mode.attr,
	&dev_attr_current_master.attr,
	&dev_attr_i3c_scl_frequency.attr,
	&dev_attr_i2c_scl_frequency.attr,
	&dev_attr_bcr.attr,
	&dev_attr_dcr.attr,
	&dev_attr_pid.attr,
	&dev_attr_dynamic_address.attr,
	&dev_attr_hdrcap.attr,
	NULL,
};
ATTRIBUTE_GROUPS(i3c_masterdev);

static void i3c_masterdev_release(struct device *dev)
{
	struct i3c_master_controller *master = dev_to_i3cmaster(dev);
	struct i3c_bus *bus = dev_to_i3cbus(dev);

	if (master->wq)
		destroy_workqueue(master->wq);

	WARN_ON(!list_empty(&bus->devs.i2c) || !list_empty(&bus->devs.i3c));
	i3c_bus_cleanup(bus);

	of_node_put(dev->of_node);
}

static const struct device_type i3c_masterdev_type = {
	.groups	= i3c_masterdev_groups,
};

static int i3c_bus_set_mode(struct i3c_bus *i3cbus, enum i3c_bus_mode mode,
			    unsigned long max_i2c_scl_rate)
{
	struct i3c_master_controller *master = i3c_bus_to_i3c_master(i3cbus);

	i3cbus->mode = mode;

	switch (i3cbus->mode) {
	case I3C_BUS_MODE_PURE:
		if (!i3cbus->scl_rate.i3c)
			i3cbus->scl_rate.i3c = I3C_BUS_TYP_I3C_SCL_RATE;
		break;
	case I3C_BUS_MODE_MIXED_FAST:
	case I3C_BUS_MODE_MIXED_LIMITED:
		if (!i3cbus->scl_rate.i3c)
			i3cbus->scl_rate.i3c = I3C_BUS_TYP_I3C_SCL_RATE;
		if (!i3cbus->scl_rate.i2c)
			i3cbus->scl_rate.i2c = max_i2c_scl_rate;
		break;
	case I3C_BUS_MODE_MIXED_SLOW:
		if (!i3cbus->scl_rate.i2c)
			i3cbus->scl_rate.i2c = max_i2c_scl_rate;
		if (!i3cbus->scl_rate.i3c ||
		    i3cbus->scl_rate.i3c > i3cbus->scl_rate.i2c)
			i3cbus->scl_rate.i3c = i3cbus->scl_rate.i2c;
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(&master->dev, "i2c-scl = %ld Hz i3c-scl = %ld Hz\n",
		i3cbus->scl_rate.i2c, i3cbus->scl_rate.i3c);

	/*
	 * I3C/I2C frequency may have been overridden, check that user-provided
	 * values are not exceeding max possible frequency.
	 */
	if (i3cbus->scl_rate.i3c > I3C_BUS_MAX_I3C_SCL_RATE ||
	    i3cbus->scl_rate.i2c > I3C_BUS_I2C_FM_PLUS_SCL_RATE)
		return -EINVAL;

	return 0;
}

static struct i3c_master_controller *
i2c_adapter_to_i3c_master(struct i2c_adapter *adap)
{
	return container_of(adap, struct i3c_master_controller, i2c);
}

static struct i2c_adapter *
i3c_master_to_i2c_adapter(struct i3c_master_controller *master)
{
	return &master->i2c;
}

static void i3c_master_free_i2c_dev(struct i2c_dev_desc *dev)
{
	kfree(dev);
}

static struct i2c_dev_desc *
i3c_master_alloc_i2c_dev(struct i3c_master_controller *master,
			 const struct i2c_dev_boardinfo *boardinfo)
{
	struct i2c_dev_desc *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->common.master = master;
	dev->boardinfo = boardinfo;
	dev->addr = boardinfo->base.addr;
	dev->lvr = boardinfo->lvr;

	return dev;
}

static void *i3c_ccc_cmd_dest_init(struct i3c_ccc_cmd_dest *dest, u8 addr,
				   u16 payloadlen)
{
	dest->addr = addr;
	dest->payload.len = payloadlen;
	if (payloadlen)
		dest->payload.data = kzalloc(payloadlen, GFP_KERNEL);
	else
		dest->payload.data = NULL;

	return dest->payload.data;
}

static void i3c_ccc_cmd_dest_cleanup(struct i3c_ccc_cmd_dest *dest)
{
	kfree(dest->payload.data);
}

static void i3c_ccc_cmd_init(struct i3c_ccc_cmd *cmd, bool rnw, u8 id,
			     struct i3c_ccc_cmd_dest *dests,
			     unsigned int ndests)
{
	cmd->rnw = rnw ? 1 : 0;
	cmd->id = id;
	cmd->dests = dests;
	cmd->ndests = ndests;
	cmd->err = I3C_ERROR_UNKNOWN;
}

static int i3c_master_send_ccc_cmd_locked(struct i3c_master_controller *master,
					  struct i3c_ccc_cmd *cmd)
{
	int ret;

	if (!cmd || !master)
		return -EINVAL;

	if (WARN_ON(master->init_done &&
		    !rwsem_is_locked(&master->bus.lock)))
		return -EINVAL;

	if (!master->ops->send_ccc_cmd)
		return -ENOTSUPP;

	if ((cmd->id & I3C_CCC_DIRECT) && (!cmd->dests || !cmd->ndests))
		return -EINVAL;

	if (master->ops->supports_ccc_cmd &&
	    !master->ops->supports_ccc_cmd(master, cmd))
		return -ENOTSUPP;

	ret = master->ops->send_ccc_cmd(master, cmd);
	if (ret) {
		if (cmd->err != I3C_ERROR_UNKNOWN)
			return cmd->err;

		return ret;
	}

	return 0;
}

static struct i2c_dev_desc *
i3c_master_find_i2c_dev_by_addr(const struct i3c_master_controller *master,
				u16 addr)
{
	struct i2c_dev_desc *dev;

	i3c_bus_for_each_i2cdev(&master->bus, dev) {
		if (dev->boardinfo->base.addr == addr)
			return dev;
	}

	return NULL;
}

/**
 * i3c_master_get_free_addr() - get a free address on the bus
 * @master: I3C master object
 * @start_addr: where to start searching
 *
 * This function must be called with the bus lock held in write mode.
 *
 * Return: the first free address starting at @start_addr (included) or -ENOMEM
 * if there's no more address available.
 */
int i3c_master_get_free_addr(struct i3c_master_controller *master,
			     u8 start_addr)
{
	return i3c_bus_get_free_addr(&master->bus, start_addr);
}
EXPORT_SYMBOL_GPL(i3c_master_get_free_addr);

static void i3c_device_release(struct device *dev)
{
	struct i3c_device *i3cdev = dev_to_i3cdev(dev);

	WARN_ON(i3cdev->desc);

	of_node_put(i3cdev->dev.of_node);
	kfree(i3cdev);
}

static void i3c_master_free_i3c_dev(struct i3c_dev_desc *dev)
{
	kfree(dev);
}

static struct i3c_dev_desc *
i3c_master_alloc_i3c_dev(struct i3c_master_controller *master,
			 const struct i3c_device_info *info)
{
	struct i3c_dev_desc *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->common.master = master;
	dev->info = *info;
	mutex_init(&dev->ibi_lock);

	return dev;
}

static int i3c_master_rstdaa_locked(struct i3c_master_controller *master,
				    u8 addr)
{
	enum i3c_addr_slot_status addrstat;
	struct i3c_ccc_cmd_dest dest;
	struct i3c_ccc_cmd cmd;
	int ret;

	if (!master)
		return -EINVAL;

	addrstat = i3c_bus_get_addr_slot_status(&master->bus, addr);
	if (addr != I3C_BROADCAST_ADDR && addrstat != I3C_ADDR_SLOT_I3C_DEV)
		return -EINVAL;

	i3c_ccc_cmd_dest_init(&dest, addr, 0);
	i3c_ccc_cmd_init(&cmd, false,
			 I3C_CCC_RSTDAA(addr == I3C_BROADCAST_ADDR),
			 &dest, 1);
	ret = i3c_master_send_ccc_cmd_locked(master, &cmd);
	i3c_ccc_cmd_dest_cleanup(&dest);

	return ret;
}

/**
 * i3c_master_entdaa_locked() - start a DAA (Dynamic Address Assignment)
 *				procedure
 * @master: master used to send frames on the bus
 *
 * Send a ENTDAA CCC command to start a DAA procedure.
 *
 * Note that this function only sends the ENTDAA CCC command, all the logic
 * behind dynamic address assignment has to be handled in the I3C master
 * driver.
 *
 * This function must be called with the bus lock held in write mode.
 *
 * Return: 0 in case of success, a positive I3C error code if the error is
 * one of the official Mx error codes, and a negative error code otherwise.
 */
int i3c_master_entdaa_locked(struct i3c_master_controller *master)
{
	struct i3c_ccc_cmd_dest dest;
	struct i3c_ccc_cmd cmd;
	int ret;

	i3c_ccc_cmd_dest_init(&dest, I3C_BROADCAST_ADDR, 0);
	i3c_ccc_cmd_init(&cmd, false, I3C_CCC_ENTDAA, &dest, 1);
	ret = i3c_master_send_ccc_cmd_locked(master, &cmd);
	i3c_ccc_cmd_dest_cleanup(&dest);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_master_entdaa_locked);

static int i3c_master_enec_disec_locked(struct i3c_master_controller *master,
					u8 addr, bool enable, u8 evts)
{
	struct i3c_ccc_events *events;
	struct i3c_ccc_cmd_dest dest;
	struct i3c_ccc_cmd cmd;
	int ret;

	events = i3c_ccc_cmd_dest_init(&dest, addr, sizeof(*events));
	if (!events)
		return -ENOMEM;

	events->events = evts;
	i3c_ccc_cmd_init(&cmd, false,
			 enable ?
			 I3C_CCC_ENEC(addr == I3C_BROADCAST_ADDR) :
			 I3C_CCC_DISEC(addr == I3C_BROADCAST_ADDR),
			 &dest, 1);
	ret = i3c_master_send_ccc_cmd_locked(master, &cmd);
	i3c_ccc_cmd_dest_cleanup(&dest);

	return ret;
}

/**
 * i3c_master_disec_locked() - send a DISEC CCC command
 * @master: master used to send frames on the bus
 * @addr: a valid I3C slave address or %I3C_BROADCAST_ADDR
 * @evts: events to disable
 *
 * Send a DISEC CCC command to disable some or all events coming from a
 * specific slave, or all devices if @addr is %I3C_BROADCAST_ADDR.
 *
 * This function must be called with the bus lock held in write mode.
 *
 * Return: 0 in case of success, a positive I3C error code if the error is
 * one of the official Mx error codes, and a negative error code otherwise.
 */
int i3c_master_disec_locked(struct i3c_master_controller *master, u8 addr,
			    u8 evts)
{
	return i3c_master_enec_disec_locked(master, addr, false, evts);
}
EXPORT_SYMBOL_GPL(i3c_master_disec_locked);

/**
 * i3c_master_enec_locked() - send an ENEC CCC command
 * @master: master used to send frames on the bus
 * @addr: a valid I3C slave address or %I3C_BROADCAST_ADDR
 * @evts: events to disable
 *
 * Sends an ENEC CCC command to enable some or all events coming from a
 * specific slave, or all devices if @addr is %I3C_BROADCAST_ADDR.
 *
 * This function must be called with the bus lock held in write mode.
 *
 * Return: 0 in case of success, a positive I3C error code if the error is
 * one of the official Mx error codes, and a negative error code otherwise.
 */
int i3c_master_enec_locked(struct i3c_master_controller *master, u8 addr,
			   u8 evts)
{
	return i3c_master_enec_disec_locked(master, addr, true, evts);
}
EXPORT_SYMBOL_GPL(i3c_master_enec_locked);

/**
 * i3c_master_defslvs_locked() - send a DEFSLVS CCC command
 * @master: master used to send frames on the bus
 *
 * Send a DEFSLVS CCC command containing all the devices known to the @master.
 * This is useful when you have secondary masters on the bus to propagate
 * device information.
 *
 * This should be called after all I3C devices have been discovered (in other
 * words, after the DAA procedure has finished) and instantiated in
 * &i3c_master_controller_ops->bus_init().
 * It should also be called if a master ACKed an Hot-Join request and assigned
 * a dynamic address to the device joining the bus.
 *
 * This function must be called with the bus lock held in write mode.
 *
 * Return: 0 in case of success, a positive I3C error code if the error is
 * one of the official Mx error codes, and a negative error code otherwise.
 */
int i3c_master_defslvs_locked(struct i3c_master_controller *master)
{
	struct i3c_ccc_defslvs *defslvs;
	struct i3c_ccc_dev_desc *desc;
	struct i3c_ccc_cmd_dest dest;
	struct i3c_dev_desc *i3cdev;
	struct i2c_dev_desc *i2cdev;
	struct i3c_ccc_cmd cmd;
	struct i3c_bus *bus;
	bool send = false;
	int ndevs = 0, ret;

	if (!master)
		return -EINVAL;

	bus = i3c_master_get_bus(master);
	i3c_bus_for_each_i3cdev(bus, i3cdev) {
		ndevs++;

		if (i3cdev == master->this)
			continue;

		if (I3C_BCR_DEVICE_ROLE(i3cdev->info.bcr) ==
		    I3C_BCR_I3C_MASTER)
			send = true;
	}

	/* No other master on the bus, skip DEFSLVS. */
	if (!send)
		return 0;

	i3c_bus_for_each_i2cdev(bus, i2cdev)
		ndevs++;

	defslvs = i3c_ccc_cmd_dest_init(&dest, I3C_BROADCAST_ADDR,
					struct_size(defslvs, slaves,
						    ndevs - 1));
	if (!defslvs)
		return -ENOMEM;

	defslvs->count = ndevs;
	defslvs->master.bcr = master->this->info.bcr;
	defslvs->master.dcr = master->this->info.dcr;
	defslvs->master.dyn_addr = master->this->info.dyn_addr << 1;
	defslvs->master.static_addr = I3C_BROADCAST_ADDR << 1;

	desc = defslvs->slaves;
	i3c_bus_for_each_i2cdev(bus, i2cdev) {
		desc->lvr = i2cdev->lvr;
		desc->static_addr = i2cdev->addr << 1;
		desc++;
	}

	i3c_bus_for_each_i3cdev(bus, i3cdev) {
		/* Skip the I3C dev representing this master. */
		if (i3cdev == master->this)
			continue;

		desc->bcr = i3cdev->info.bcr;
		desc->dcr = i3cdev->info.dcr;
		desc->dyn_addr = i3cdev->info.dyn_addr << 1;
		desc->static_addr = i3cdev->info.static_addr << 1;
		desc++;
	}

	i3c_ccc_cmd_init(&cmd, false, I3C_CCC_DEFSLVS, &dest, 1);
	ret = i3c_master_send_ccc_cmd_locked(master, &cmd);
	i3c_ccc_cmd_dest_cleanup(&dest);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_master_defslvs_locked);

static int i3c_master_setda_locked(struct i3c_master_controller *master,
				   u8 oldaddr, u8 newaddr, bool setdasa)
{
	struct i3c_ccc_cmd_dest dest;
	struct i3c_ccc_setda *setda;
	struct i3c_ccc_cmd cmd;
	int ret;

	if (!oldaddr || !newaddr)
		return -EINVAL;

	setda = i3c_ccc_cmd_dest_init(&dest, oldaddr, sizeof(*setda));
	if (!setda)
		return -ENOMEM;

	setda->addr = newaddr << 1;
	i3c_ccc_cmd_init(&cmd, false,
			 setdasa ? I3C_CCC_SETDASA : I3C_CCC_SETNEWDA,
			 &dest, 1);
	ret = i3c_master_send_ccc_cmd_locked(master, &cmd);
	i3c_ccc_cmd_dest_cleanup(&dest);

	return ret;
}

static int i3c_master_setdasa_locked(struct i3c_master_controller *master,
				     u8 static_addr, u8 dyn_addr)
{
	return i3c_master_setda_locked(master, static_addr, dyn_addr, true);
}

static int i3c_master_setnewda_locked(struct i3c_master_controller *master,
				      u8 oldaddr, u8 newaddr)
{
	return i3c_master_setda_locked(master, oldaddr, newaddr, false);
}

static int i3c_master_getmrl_locked(struct i3c_master_controller *master,
				    struct i3c_device_info *info)
{
	struct i3c_ccc_cmd_dest dest;
	struct i3c_ccc_mrl *mrl;
	struct i3c_ccc_cmd cmd;
	int ret;

	mrl = i3c_ccc_cmd_dest_init(&dest, info->dyn_addr, sizeof(*mrl));
	if (!mrl)
		return -ENOMEM;

	/*
	 * When the device does not have IBI payload GETMRL only returns 2
	 * bytes of data.
	 */
	if (!(info->bcr & I3C_BCR_IBI_PAYLOAD))
		dest.payload.len -= 1;

	i3c_ccc_cmd_init(&cmd, true, I3C_CCC_GETMRL, &dest, 1);
	ret = i3c_master_send_ccc_cmd_locked(master, &cmd);
	if (ret)
		goto out;

	switch (dest.payload.len) {
	case 3:
		info->max_ibi_len = mrl->ibi_len;
		fallthrough;
	case 2:
		info->max_read_len = be16_to_cpu(mrl->read_len);
		break;
	default:
		ret = -EIO;
		goto out;
	}

out:
	i3c_ccc_cmd_dest_cleanup(&dest);

	return ret;
}

static int i3c_master_getmwl_locked(struct i3c_master_controller *master,
				    struct i3c_device_info *info)
{
	struct i3c_ccc_cmd_dest dest;
	struct i3c_ccc_mwl *mwl;
	struct i3c_ccc_cmd cmd;
	int ret;

	mwl = i3c_ccc_cmd_dest_init(&dest, info->dyn_addr, sizeof(*mwl));
	if (!mwl)
		return -ENOMEM;

	i3c_ccc_cmd_init(&cmd, true, I3C_CCC_GETMWL, &dest, 1);
	ret = i3c_master_send_ccc_cmd_locked(master, &cmd);
	if (ret)
		goto out;

	if (dest.payload.len != sizeof(*mwl)) {
		ret = -EIO;
		goto out;
	}

	info->max_write_len = be16_to_cpu(mwl->len);

out:
	i3c_ccc_cmd_dest_cleanup(&dest);

	return ret;
}

static int i3c_master_getmxds_locked(struct i3c_master_controller *master,
				     struct i3c_device_info *info)
{
	struct i3c_ccc_getmxds *getmaxds;
	struct i3c_ccc_cmd_dest dest;
	struct i3c_ccc_cmd cmd;
	int ret;

	getmaxds = i3c_ccc_cmd_dest_init(&dest, info->dyn_addr,
					 sizeof(*getmaxds));
	if (!getmaxds)
		return -ENOMEM;

	i3c_ccc_cmd_init(&cmd, true, I3C_CCC_GETMXDS, &dest, 1);
	ret = i3c_master_send_ccc_cmd_locked(master, &cmd);
	if (ret)
		goto out;

	if (dest.payload.len != 2 && dest.payload.len != 5) {
		ret = -EIO;
		goto out;
	}

	info->max_read_ds = getmaxds->maxrd;
	info->max_write_ds = getmaxds->maxwr;
	if (dest.payload.len == 5)
		info->max_read_turnaround = getmaxds->maxrdturn[0] |
					    ((u32)getmaxds->maxrdturn[1] << 8) |
					    ((u32)getmaxds->maxrdturn[2] << 16);

out:
	i3c_ccc_cmd_dest_cleanup(&dest);

	return ret;
}

static int i3c_master_gethdrcap_locked(struct i3c_master_controller *master,
				       struct i3c_device_info *info)
{
	struct i3c_ccc_gethdrcap *gethdrcap;
	struct i3c_ccc_cmd_dest dest;
	struct i3c_ccc_cmd cmd;
	int ret;

	gethdrcap = i3c_ccc_cmd_dest_init(&dest, info->dyn_addr,
					  sizeof(*gethdrcap));
	if (!gethdrcap)
		return -ENOMEM;

	i3c_ccc_cmd_init(&cmd, true, I3C_CCC_GETHDRCAP, &dest, 1);
	ret = i3c_master_send_ccc_cmd_locked(master, &cmd);
	if (ret)
		goto out;

	if (dest.payload.len != 1) {
		ret = -EIO;
		goto out;
	}

	info->hdr_cap = gethdrcap->modes;

out:
	i3c_ccc_cmd_dest_cleanup(&dest);

	return ret;
}

static int i3c_master_getpid_locked(struct i3c_master_controller *master,
				    struct i3c_device_info *info)
{
	struct i3c_ccc_getpid *getpid;
	struct i3c_ccc_cmd_dest dest;
	struct i3c_ccc_cmd cmd;
	int ret, i;

	getpid = i3c_ccc_cmd_dest_init(&dest, info->dyn_addr, sizeof(*getpid));
	if (!getpid)
		return -ENOMEM;

	i3c_ccc_cmd_init(&cmd, true, I3C_CCC_GETPID, &dest, 1);
	ret = i3c_master_send_ccc_cmd_locked(master, &cmd);
	if (ret)
		goto out;

	info->pid = 0;
	for (i = 0; i < sizeof(getpid->pid); i++) {
		int sft = (sizeof(getpid->pid) - i - 1) * 8;

		info->pid |= (u64)getpid->pid[i] << sft;
	}

out:
	i3c_ccc_cmd_dest_cleanup(&dest);

	return ret;
}

static int i3c_master_getbcr_locked(struct i3c_master_controller *master,
				    struct i3c_device_info *info)
{
	struct i3c_ccc_getbcr *getbcr;
	struct i3c_ccc_cmd_dest dest;
	struct i3c_ccc_cmd cmd;
	int ret;

	getbcr = i3c_ccc_cmd_dest_init(&dest, info->dyn_addr, sizeof(*getbcr));
	if (!getbcr)
		return -ENOMEM;

	i3c_ccc_cmd_init(&cmd, true, I3C_CCC_GETBCR, &dest, 1);
	ret = i3c_master_send_ccc_cmd_locked(master, &cmd);
	if (ret)
		goto out;

	info->bcr = getbcr->bcr;

out:
	i3c_ccc_cmd_dest_cleanup(&dest);

	return ret;
}

static int i3c_master_getdcr_locked(struct i3c_master_controller *master,
				    struct i3c_device_info *info)
{
	struct i3c_ccc_getdcr *getdcr;
	struct i3c_ccc_cmd_dest dest;
	struct i3c_ccc_cmd cmd;
	int ret;

	getdcr = i3c_ccc_cmd_dest_init(&dest, info->dyn_addr, sizeof(*getdcr));
	if (!getdcr)
		return -ENOMEM;

	i3c_ccc_cmd_init(&cmd, true, I3C_CCC_GETDCR, &dest, 1);
	ret = i3c_master_send_ccc_cmd_locked(master, &cmd);
	if (ret)
		goto out;

	info->dcr = getdcr->dcr;

out:
	i3c_ccc_cmd_dest_cleanup(&dest);

	return ret;
}

static int i3c_master_retrieve_dev_info(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *master = i3c_dev_get_master(dev);
	enum i3c_addr_slot_status slot_status;
	int ret;

	if (!dev->info.dyn_addr)
		return -EINVAL;

	slot_status = i3c_bus_get_addr_slot_status(&master->bus,
						   dev->info.dyn_addr);
	if (slot_status == I3C_ADDR_SLOT_RSVD ||
	    slot_status == I3C_ADDR_SLOT_I2C_DEV)
		return -EINVAL;

	ret = i3c_master_getpid_locked(master, &dev->info);
	if (ret)
		return ret;

	ret = i3c_master_getbcr_locked(master, &dev->info);
	if (ret)
		return ret;

	ret = i3c_master_getdcr_locked(master, &dev->info);
	if (ret)
		return ret;

	if (dev->info.bcr & I3C_BCR_MAX_DATA_SPEED_LIM) {
		ret = i3c_master_getmxds_locked(master, &dev->info);
		if (ret)
			return ret;
	}

	if (dev->info.bcr & I3C_BCR_IBI_PAYLOAD)
		dev->info.max_ibi_len = 1;

	i3c_master_getmrl_locked(master, &dev->info);
	i3c_master_getmwl_locked(master, &dev->info);

	if (dev->info.bcr & I3C_BCR_HDR_CAP) {
		ret = i3c_master_gethdrcap_locked(master, &dev->info);
		if (ret)
			return ret;
	}

	return 0;
}

static void i3c_master_put_i3c_addrs(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *master = i3c_dev_get_master(dev);

	if (dev->info.static_addr)
		i3c_bus_set_addr_slot_status(&master->bus,
					     dev->info.static_addr,
					     I3C_ADDR_SLOT_FREE);

	if (dev->info.dyn_addr)
		i3c_bus_set_addr_slot_status(&master->bus, dev->info.dyn_addr,
					     I3C_ADDR_SLOT_FREE);

	if (dev->boardinfo && dev->boardinfo->init_dyn_addr)
		i3c_bus_set_addr_slot_status(&master->bus, dev->info.dyn_addr,
					     I3C_ADDR_SLOT_FREE);
}

static int i3c_master_get_i3c_addrs(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *master = i3c_dev_get_master(dev);
	enum i3c_addr_slot_status status;

	if (!dev->info.static_addr && !dev->info.dyn_addr)
		return 0;

	if (dev->info.static_addr) {
		status = i3c_bus_get_addr_slot_status(&master->bus,
						      dev->info.static_addr);
		if (status != I3C_ADDR_SLOT_FREE)
			return -EBUSY;

		i3c_bus_set_addr_slot_status(&master->bus,
					     dev->info.static_addr,
					     I3C_ADDR_SLOT_I3C_DEV);
	}

	/*
	 * ->init_dyn_addr should have been reserved before that, so, if we're
	 * trying to apply a pre-reserved dynamic address, we should not try
	 * to reserve the address slot a second time.
	 */
	if (dev->info.dyn_addr &&
	    (!dev->boardinfo ||
	     dev->boardinfo->init_dyn_addr != dev->info.dyn_addr)) {
		status = i3c_bus_get_addr_slot_status(&master->bus,
						      dev->info.dyn_addr);
		if (status != I3C_ADDR_SLOT_FREE)
			goto err_release_static_addr;

		i3c_bus_set_addr_slot_status(&master->bus, dev->info.dyn_addr,
					     I3C_ADDR_SLOT_I3C_DEV);
	}

	return 0;

err_release_static_addr:
	if (dev->info.static_addr)
		i3c_bus_set_addr_slot_status(&master->bus,
					     dev->info.static_addr,
					     I3C_ADDR_SLOT_FREE);

	return -EBUSY;
}

static int i3c_master_attach_i3c_dev(struct i3c_master_controller *master,
				     struct i3c_dev_desc *dev)
{
	int ret;

	/*
	 * We don't attach devices to the controller until they are
	 * addressable on the bus.
	 */
	if (!dev->info.static_addr && !dev->info.dyn_addr)
		return 0;

	ret = i3c_master_get_i3c_addrs(dev);
	if (ret)
		return ret;

	/* Do not attach the master device itself. */
	if (master->this != dev && master->ops->attach_i3c_dev) {
		ret = master->ops->attach_i3c_dev(dev);
		if (ret) {
			i3c_master_put_i3c_addrs(dev);
			return ret;
		}
	}

	list_add_tail(&dev->common.node, &master->bus.devs.i3c);

	return 0;
}

static int i3c_master_reattach_i3c_dev(struct i3c_dev_desc *dev,
				       u8 old_dyn_addr)
{
	struct i3c_master_controller *master = i3c_dev_get_master(dev);
	enum i3c_addr_slot_status status;
	int ret;

	if (dev->info.dyn_addr != old_dyn_addr &&
	    (!dev->boardinfo ||
	     dev->info.dyn_addr != dev->boardinfo->init_dyn_addr)) {
		status = i3c_bus_get_addr_slot_status(&master->bus,
						      dev->info.dyn_addr);
		if (status != I3C_ADDR_SLOT_FREE)
			return -EBUSY;
		i3c_bus_set_addr_slot_status(&master->bus,
					     dev->info.dyn_addr,
					     I3C_ADDR_SLOT_I3C_DEV);
	}

	if (master->ops->reattach_i3c_dev) {
		ret = master->ops->reattach_i3c_dev(dev, old_dyn_addr);
		if (ret) {
			i3c_master_put_i3c_addrs(dev);
			return ret;
		}
	}

	return 0;
}

static void i3c_master_detach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *master = i3c_dev_get_master(dev);

	/* Do not detach the master device itself. */
	if (master->this != dev && master->ops->detach_i3c_dev)
		master->ops->detach_i3c_dev(dev);

	i3c_master_put_i3c_addrs(dev);
	list_del(&dev->common.node);
}

static int i3c_master_attach_i2c_dev(struct i3c_master_controller *master,
				     struct i2c_dev_desc *dev)
{
	int ret;

	if (master->ops->attach_i2c_dev) {
		ret = master->ops->attach_i2c_dev(dev);
		if (ret)
			return ret;
	}

	list_add_tail(&dev->common.node, &master->bus.devs.i2c);

	return 0;
}

static void i3c_master_detach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *master = i2c_dev_get_master(dev);

	list_del(&dev->common.node);

	if (master->ops->detach_i2c_dev)
		master->ops->detach_i2c_dev(dev);
}

static int i3c_master_early_i3c_dev_add(struct i3c_master_controller *master,
					  struct i3c_dev_boardinfo *boardinfo)
{
	struct i3c_device_info info = {
		.static_addr = boardinfo->static_addr,
	};
	struct i3c_dev_desc *i3cdev;
	int ret;

	i3cdev = i3c_master_alloc_i3c_dev(master, &info);
	if (IS_ERR(i3cdev))
		return -ENOMEM;

	i3cdev->boardinfo = boardinfo;

	ret = i3c_master_attach_i3c_dev(master, i3cdev);
	if (ret)
		goto err_free_dev;

	ret = i3c_master_setdasa_locked(master, i3cdev->info.static_addr,
					i3cdev->boardinfo->init_dyn_addr);
	if (ret)
		goto err_detach_dev;

	i3cdev->info.dyn_addr = i3cdev->boardinfo->init_dyn_addr;
	ret = i3c_master_reattach_i3c_dev(i3cdev, 0);
	if (ret)
		goto err_rstdaa;

	ret = i3c_master_retrieve_dev_info(i3cdev);
	if (ret)
		goto err_rstdaa;

	return 0;

err_rstdaa:
	i3c_master_rstdaa_locked(master, i3cdev->boardinfo->init_dyn_addr);
err_detach_dev:
	i3c_master_detach_i3c_dev(i3cdev);
err_free_dev:
	i3c_master_free_i3c_dev(i3cdev);

	return ret;
}

static void
i3c_master_register_new_i3c_devs(struct i3c_master_controller *master)
{
	struct i3c_dev_desc *desc;
	int ret;

	if (!master->init_done)
		return;

	i3c_bus_for_each_i3cdev(&master->bus, desc) {
		if (desc->dev || !desc->info.dyn_addr || desc == master->this)
			continue;

		desc->dev = kzalloc(sizeof(*desc->dev), GFP_KERNEL);
		if (!desc->dev)
			continue;

		desc->dev->bus = &master->bus;
		desc->dev->desc = desc;
		desc->dev->dev.parent = &master->dev;
		desc->dev->dev.type = &i3c_device_type;
		desc->dev->dev.bus = &i3c_bus_type;
		desc->dev->dev.release = i3c_device_release;
		dev_set_name(&desc->dev->dev, "%d-%llx", master->bus.id,
			     desc->info.pid);

		if (desc->boardinfo)
			desc->dev->dev.of_node = desc->boardinfo->of_node;

		ret = device_register(&desc->dev->dev);
		if (ret)
			dev_err(&master->dev,
				"Failed to add I3C device (err = %d)\n", ret);
	}
}

/**
 * i3c_master_do_daa() - do a DAA (Dynamic Address Assignment)
 * @master: master doing the DAA
 *
 * This function is instantiating an I3C device object and adding it to the
 * I3C device list. All device information are automatically retrieved using
 * standard CCC commands.
 *
 * The I3C device object is returned in case the master wants to attach
 * private data to it using i3c_dev_set_master_data().
 *
 * This function must be called with the bus lock held in write mode.
 *
 * Return: a 0 in case of success, an negative error code otherwise.
 */
int i3c_master_do_daa(struct i3c_master_controller *master)
{
	int ret;

	i3c_bus_maintenance_lock(&master->bus);
	ret = master->ops->do_daa(master);
	i3c_bus_maintenance_unlock(&master->bus);

	if (ret)
		return ret;

	i3c_bus_normaluse_lock(&master->bus);
	i3c_master_register_new_i3c_devs(master);
	i3c_bus_normaluse_unlock(&master->bus);

	return 0;
}
EXPORT_SYMBOL_GPL(i3c_master_do_daa);

/**
 * i3c_master_set_info() - set master device information
 * @master: master used to send frames on the bus
 * @info: I3C device information
 *
 * Set master device info. This should be called from
 * &i3c_master_controller_ops->bus_init().
 *
 * Not all &i3c_device_info fields are meaningful for a master device.
 * Here is a list of fields that should be properly filled:
 *
 * - &i3c_device_info->dyn_addr
 * - &i3c_device_info->bcr
 * - &i3c_device_info->dcr
 * - &i3c_device_info->pid
 * - &i3c_device_info->hdr_cap if %I3C_BCR_HDR_CAP bit is set in
 *   &i3c_device_info->bcr
 *
 * This function must be called with the bus lock held in maintenance mode.
 *
 * Return: 0 if @info contains valid information (not every piece of
 * information can be checked, but we can at least make sure @info->dyn_addr
 * and @info->bcr are correct), -EINVAL otherwise.
 */
int i3c_master_set_info(struct i3c_master_controller *master,
			const struct i3c_device_info *info)
{
	struct i3c_dev_desc *i3cdev;
	int ret;

	if (!i3c_bus_dev_addr_is_avail(&master->bus, info->dyn_addr))
		return -EINVAL;

	if (I3C_BCR_DEVICE_ROLE(info->bcr) == I3C_BCR_I3C_MASTER &&
	    master->secondary)
		return -EINVAL;

	if (master->this)
		return -EINVAL;

	i3cdev = i3c_master_alloc_i3c_dev(master, info);
	if (IS_ERR(i3cdev))
		return PTR_ERR(i3cdev);

	master->this = i3cdev;
	master->bus.cur_master = master->this;

	ret = i3c_master_attach_i3c_dev(master, i3cdev);
	if (ret)
		goto err_free_dev;

	return 0;

err_free_dev:
	i3c_master_free_i3c_dev(i3cdev);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_master_set_info);

static void i3c_master_detach_free_devs(struct i3c_master_controller *master)
{
	struct i3c_dev_desc *i3cdev, *i3ctmp;
	struct i2c_dev_desc *i2cdev, *i2ctmp;

	list_for_each_entry_safe(i3cdev, i3ctmp, &master->bus.devs.i3c,
				 common.node) {
		i3c_master_detach_i3c_dev(i3cdev);

		if (i3cdev->boardinfo && i3cdev->boardinfo->init_dyn_addr)
			i3c_bus_set_addr_slot_status(&master->bus,
					i3cdev->boardinfo->init_dyn_addr,
					I3C_ADDR_SLOT_FREE);

		i3c_master_free_i3c_dev(i3cdev);
	}

	list_for_each_entry_safe(i2cdev, i2ctmp, &master->bus.devs.i2c,
				 common.node) {
		i3c_master_detach_i2c_dev(i2cdev);
		i3c_bus_set_addr_slot_status(&master->bus,
					     i2cdev->addr,
					     I3C_ADDR_SLOT_FREE);
		i3c_master_free_i2c_dev(i2cdev);
	}
}

/**
 * i3c_master_bus_init() - initialize an I3C bus
 * @master: main master initializing the bus
 *
 * This function is following all initialisation steps described in the I3C
 * specification:
 *
 * 1. Attach I2C devs to the master so that the master can fill its internal
 *    device table appropriately
 *
 * 2. Call &i3c_master_controller_ops->bus_init() method to initialize
 *    the master controller. That's usually where the bus mode is selected
 *    (pure bus or mixed fast/slow bus)
 *
 * 3. Instruct all devices on the bus to drop their dynamic address. This is
 *    particularly important when the bus was previously configured by someone
 *    else (for example the bootloader)
 *
 * 4. Disable all slave events.
 *
 * 5. Reserve address slots for I3C devices with init_dyn_addr. And if devices
 *    also have static_addr, try to pre-assign dynamic addresses requested by
 *    the FW with SETDASA and attach corresponding statically defined I3C
 *    devices to the master.
 *
 * 6. Do a DAA (Dynamic Address Assignment) to assign dynamic addresses to all
 *    remaining I3C devices
 *
 * Once this is done, all I3C and I2C devices should be usable.
 *
 * Return: a 0 in case of success, an negative error code otherwise.
 */
static int i3c_master_bus_init(struct i3c_master_controller *master)
{
	enum i3c_addr_slot_status status;
	struct i2c_dev_boardinfo *i2cboardinfo;
	struct i3c_dev_boardinfo *i3cboardinfo;
	struct i2c_dev_desc *i2cdev;
	int ret;

	/*
	 * First attach all devices with static definitions provided by the
	 * FW.
	 */
	list_for_each_entry(i2cboardinfo, &master->boardinfo.i2c, node) {
		status = i3c_bus_get_addr_slot_status(&master->bus,
						      i2cboardinfo->base.addr);
		if (status != I3C_ADDR_SLOT_FREE) {
			ret = -EBUSY;
			goto err_detach_devs;
		}

		i3c_bus_set_addr_slot_status(&master->bus,
					     i2cboardinfo->base.addr,
					     I3C_ADDR_SLOT_I2C_DEV);

		i2cdev = i3c_master_alloc_i2c_dev(master, i2cboardinfo);
		if (IS_ERR(i2cdev)) {
			ret = PTR_ERR(i2cdev);
			goto err_detach_devs;
		}

		ret = i3c_master_attach_i2c_dev(master, i2cdev);
		if (ret) {
			i3c_master_free_i2c_dev(i2cdev);
			goto err_detach_devs;
		}
	}

	/*
	 * Now execute the controller specific ->bus_init() routine, which
	 * might configure its internal logic to match the bus limitations.
	 */
	ret = master->ops->bus_init(master);
	if (ret)
		goto err_detach_devs;

	/*
	 * The master device should have been instantiated in ->bus_init(),
	 * complain if this was not the case.
	 */
	if (!master->this) {
		dev_err(&master->dev,
			"master_set_info() was not called in ->bus_init()\n");
		ret = -EINVAL;
		goto err_bus_cleanup;
	}

	/*
	 * Reset all dynamic address that may have been assigned before
	 * (assigned by the bootloader for example).
	 */
	ret = i3c_master_rstdaa_locked(master, I3C_BROADCAST_ADDR);
	if (ret && ret != I3C_ERROR_M2)
		goto err_bus_cleanup;

	/* Disable all slave events before starting DAA. */
	ret = i3c_master_disec_locked(master, I3C_BROADCAST_ADDR,
				      I3C_CCC_EVENT_SIR | I3C_CCC_EVENT_MR |
				      I3C_CCC_EVENT_HJ);
	if (ret && ret != I3C_ERROR_M2)
		goto err_bus_cleanup;

	/*
	 * Reserve init_dyn_addr first, and then try to pre-assign dynamic
	 * address and retrieve device information if needed.
	 * In case pre-assign dynamic address fails, setting dynamic address to
	 * the requested init_dyn_addr is retried after DAA is done in
	 * i3c_master_add_i3c_dev_locked().
	 */
	list_for_each_entry(i3cboardinfo, &master->boardinfo.i3c, node) {

		/*
		 * We don't reserve a dynamic address for devices that
		 * don't explicitly request one.
		 */
		if (!i3cboardinfo->init_dyn_addr)
			continue;

		ret = i3c_bus_get_addr_slot_status(&master->bus,
						   i3cboardinfo->init_dyn_addr);
		if (ret != I3C_ADDR_SLOT_FREE) {
			ret = -EBUSY;
			goto err_rstdaa;
		}

		i3c_bus_set_addr_slot_status(&master->bus,
					     i3cboardinfo->init_dyn_addr,
					     I3C_ADDR_SLOT_I3C_DEV);

		/*
		 * Only try to create/attach devices that have a static
		 * address. Other devices will be created/attached when
		 * DAA happens, and the requested dynamic address will
		 * be set using SETNEWDA once those devices become
		 * addressable.
		 */

		if (i3cboardinfo->static_addr)
			i3c_master_early_i3c_dev_add(master, i3cboardinfo);
	}

	ret = i3c_master_do_daa(master);
	if (ret)
		goto err_rstdaa;

	return 0;

err_rstdaa:
	i3c_master_rstdaa_locked(master, I3C_BROADCAST_ADDR);

err_bus_cleanup:
	if (master->ops->bus_cleanup)
		master->ops->bus_cleanup(master);

err_detach_devs:
	i3c_master_detach_free_devs(master);

	return ret;
}

static void i3c_master_bus_cleanup(struct i3c_master_controller *master)
{
	if (master->ops->bus_cleanup)
		master->ops->bus_cleanup(master);

	i3c_master_detach_free_devs(master);
}

static void i3c_master_attach_boardinfo(struct i3c_dev_desc *i3cdev)
{
	struct i3c_master_controller *master = i3cdev->common.master;
	struct i3c_dev_boardinfo *i3cboardinfo;

	list_for_each_entry(i3cboardinfo, &master->boardinfo.i3c, node) {
		if (i3cdev->info.pid != i3cboardinfo->pid)
			continue;

		i3cdev->boardinfo = i3cboardinfo;
		i3cdev->info.static_addr = i3cboardinfo->static_addr;
		return;
	}
}

static struct i3c_dev_desc *
i3c_master_search_i3c_dev_duplicate(struct i3c_dev_desc *refdev)
{
	struct i3c_master_controller *master = i3c_dev_get_master(refdev);
	struct i3c_dev_desc *i3cdev;

	i3c_bus_for_each_i3cdev(&master->bus, i3cdev) {
		if (i3cdev != refdev && i3cdev->info.pid == refdev->info.pid)
			return i3cdev;
	}

	return NULL;
}

/**
 * i3c_master_add_i3c_dev_locked() - add an I3C slave to the bus
 * @master: master used to send frames on the bus
 * @addr: I3C slave dynamic address assigned to the device
 *
 * This function is instantiating an I3C device object and adding it to the
 * I3C device list. All device information are automatically retrieved using
 * standard CCC commands.
 *
 * The I3C device object is returned in case the master wants to attach
 * private data to it using i3c_dev_set_master_data().
 *
 * This function must be called with the bus lock held in write mode.
 *
 * Return: a 0 in case of success, an negative error code otherwise.
 */
int i3c_master_add_i3c_dev_locked(struct i3c_master_controller *master,
				  u8 addr)
{
	struct i3c_device_info info = { .dyn_addr = addr };
	struct i3c_dev_desc *newdev, *olddev;
	u8 old_dyn_addr = addr, expected_dyn_addr;
	struct i3c_ibi_setup ibireq = { };
	bool enable_ibi = false;
	int ret;

	if (!master)
		return -EINVAL;

	newdev = i3c_master_alloc_i3c_dev(master, &info);
	if (IS_ERR(newdev))
		return PTR_ERR(newdev);

	ret = i3c_master_attach_i3c_dev(master, newdev);
	if (ret)
		goto err_free_dev;

	ret = i3c_master_retrieve_dev_info(newdev);
	if (ret)
		goto err_detach_dev;

	i3c_master_attach_boardinfo(newdev);

	olddev = i3c_master_search_i3c_dev_duplicate(newdev);
	if (olddev) {
		newdev->dev = olddev->dev;
		if (newdev->dev)
			newdev->dev->desc = newdev;

		/*
		 * We need to restore the IBI state too, so let's save the
		 * IBI information and try to restore them after olddev has
		 * been detached+released and its IBI has been stopped and
		 * the associated resources have been freed.
		 */
		mutex_lock(&olddev->ibi_lock);
		if (olddev->ibi) {
			ibireq.handler = olddev->ibi->handler;
			ibireq.max_payload_len = olddev->ibi->max_payload_len;
			ibireq.num_slots = olddev->ibi->num_slots;

			if (olddev->ibi->enabled) {
				enable_ibi = true;
				i3c_dev_disable_ibi_locked(olddev);
			}

			i3c_dev_free_ibi_locked(olddev);
		}
		mutex_unlock(&olddev->ibi_lock);

		old_dyn_addr = olddev->info.dyn_addr;

		i3c_master_detach_i3c_dev(olddev);
		i3c_master_free_i3c_dev(olddev);
	}

	ret = i3c_master_reattach_i3c_dev(newdev, old_dyn_addr);
	if (ret)
		goto err_detach_dev;

	/*
	 * Depending on our previous state, the expected dynamic address might
	 * differ:
	 * - if the device already had a dynamic address assigned, let's try to
	 *   re-apply this one
	 * - if the device did not have a dynamic address and the firmware
	 *   requested a specific address, pick this one
	 * - in any other case, keep the address automatically assigned by the
	 *   master
	 */
	if (old_dyn_addr && old_dyn_addr != newdev->info.dyn_addr)
		expected_dyn_addr = old_dyn_addr;
	else if (newdev->boardinfo && newdev->boardinfo->init_dyn_addr)
		expected_dyn_addr = newdev->boardinfo->init_dyn_addr;
	else
		expected_dyn_addr = newdev->info.dyn_addr;

	if (newdev->info.dyn_addr != expected_dyn_addr) {
		/*
		 * Try to apply the expected dynamic address. If it fails, keep
		 * the address assigned by the master.
		 */
		ret = i3c_master_setnewda_locked(master,
						 newdev->info.dyn_addr,
						 expected_dyn_addr);
		if (!ret) {
			old_dyn_addr = newdev->info.dyn_addr;
			newdev->info.dyn_addr = expected_dyn_addr;
			i3c_master_reattach_i3c_dev(newdev, old_dyn_addr);
		} else {
			dev_err(&master->dev,
				"Failed to assign reserved/old address to device %d%llx",
				master->bus.id, newdev->info.pid);
		}
	}

	/*
	 * Now is time to try to restore the IBI setup. If we're lucky,
	 * everything works as before, otherwise, all we can do is complain.
	 * FIXME: maybe we should add callback to inform the driver that it
	 * should request the IBI again instead of trying to hide that from
	 * him.
	 */
	if (ibireq.handler) {
		mutex_lock(&newdev->ibi_lock);
		ret = i3c_dev_request_ibi_locked(newdev, &ibireq);
		if (ret) {
			dev_err(&master->dev,
				"Failed to request IBI on device %d-%llx",
				master->bus.id, newdev->info.pid);
		} else if (enable_ibi) {
			ret = i3c_dev_enable_ibi_locked(newdev);
			if (ret)
				dev_err(&master->dev,
					"Failed to re-enable IBI on device %d-%llx",
					master->bus.id, newdev->info.pid);
		}
		mutex_unlock(&newdev->ibi_lock);
	}

	return 0;

err_detach_dev:
	if (newdev->dev && newdev->dev->desc)
		newdev->dev->desc = NULL;

	i3c_master_detach_i3c_dev(newdev);

err_free_dev:
	i3c_master_free_i3c_dev(newdev);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_master_add_i3c_dev_locked);

#define OF_I3C_REG1_IS_I2C_DEV			BIT(31)

static int
of_i3c_master_add_i2c_boardinfo(struct i3c_master_controller *master,
				struct device_node *node, u32 *reg)
{
	struct i2c_dev_boardinfo *boardinfo;
	struct device *dev = &master->dev;
	int ret;

	boardinfo = devm_kzalloc(dev, sizeof(*boardinfo), GFP_KERNEL);
	if (!boardinfo)
		return -ENOMEM;

	ret = of_i2c_get_board_info(dev, node, &boardinfo->base);
	if (ret)
		return ret;

	/*
	 * The I3C Specification does not clearly say I2C devices with 10-bit
	 * address are supported. These devices can't be passed properly through
	 * DEFSLVS command.
	 */
	if (boardinfo->base.flags & I2C_CLIENT_TEN) {
		dev_err(dev, "I2C device with 10 bit address not supported.");
		return -ENOTSUPP;
	}

	/* LVR is encoded in reg[2]. */
	boardinfo->lvr = reg[2];

	list_add_tail(&boardinfo->node, &master->boardinfo.i2c);
	of_node_get(node);

	return 0;
}

static int
of_i3c_master_add_i3c_boardinfo(struct i3c_master_controller *master,
				struct device_node *node, u32 *reg)
{
	struct i3c_dev_boardinfo *boardinfo;
	struct device *dev = &master->dev;
	enum i3c_addr_slot_status addrstatus;
	u32 init_dyn_addr = 0;

	boardinfo = devm_kzalloc(dev, sizeof(*boardinfo), GFP_KERNEL);
	if (!boardinfo)
		return -ENOMEM;

	if (reg[0]) {
		if (reg[0] > I3C_MAX_ADDR)
			return -EINVAL;

		addrstatus = i3c_bus_get_addr_slot_status(&master->bus,
							  reg[0]);
		if (addrstatus != I3C_ADDR_SLOT_FREE)
			return -EINVAL;
	}

	boardinfo->static_addr = reg[0];

	if (!of_property_read_u32(node, "assigned-address", &init_dyn_addr)) {
		if (init_dyn_addr > I3C_MAX_ADDR)
			return -EINVAL;

		addrstatus = i3c_bus_get_addr_slot_status(&master->bus,
							  init_dyn_addr);
		if (addrstatus != I3C_ADDR_SLOT_FREE)
			return -EINVAL;
	}

	boardinfo->pid = ((u64)reg[1] << 32) | reg[2];

	if ((boardinfo->pid & GENMASK_ULL(63, 48)) ||
	    I3C_PID_RND_LOWER_32BITS(boardinfo->pid))
		return -EINVAL;

	boardinfo->init_dyn_addr = init_dyn_addr;
	boardinfo->of_node = of_node_get(node);
	list_add_tail(&boardinfo->node, &master->boardinfo.i3c);

	return 0;
}

static int of_i3c_master_add_dev(struct i3c_master_controller *master,
				 struct device_node *node)
{
	u32 reg[3];
	int ret;

	if (!master || !node)
		return -EINVAL;

	ret = of_property_read_u32_array(node, "reg", reg, ARRAY_SIZE(reg));
	if (ret)
		return ret;

	/*
	 * The manufacturer ID can't be 0. If reg[1] == 0 that means we're
	 * dealing with an I2C device.
	 */
	if (!reg[1])
		ret = of_i3c_master_add_i2c_boardinfo(master, node, reg);
	else
		ret = of_i3c_master_add_i3c_boardinfo(master, node, reg);

	return ret;
}

static int of_populate_i3c_bus(struct i3c_master_controller *master)
{
	struct device *dev = &master->dev;
	struct device_node *i3cbus_np = dev->of_node;
	struct device_node *node;
	int ret;
	u32 val;

	if (!i3cbus_np)
		return 0;

	for_each_available_child_of_node(i3cbus_np, node) {
		ret = of_i3c_master_add_dev(master, node);
		if (ret) {
			of_node_put(node);
			return ret;
		}
	}

	/*
	 * The user might want to limit I2C and I3C speed in case some devices
	 * on the bus are not supporting typical rates, or if the bus topology
	 * prevents it from using max possible rate.
	 */
	if (!of_property_read_u32(i3cbus_np, "i2c-scl-hz", &val))
		master->bus.scl_rate.i2c = val;

	if (!of_property_read_u32(i3cbus_np, "i3c-scl-hz", &val))
		master->bus.scl_rate.i3c = val;

	return 0;
}

static int i3c_master_i2c_adapter_xfer(struct i2c_adapter *adap,
				       struct i2c_msg *xfers, int nxfers)
{
	struct i3c_master_controller *master = i2c_adapter_to_i3c_master(adap);
	struct i2c_dev_desc *dev;
	int i, ret;
	u16 addr;

	if (!xfers || !master || nxfers <= 0)
		return -EINVAL;

	if (!master->ops->i2c_xfers)
		return -ENOTSUPP;

	/* Doing transfers to different devices is not supported. */
	addr = xfers[0].addr;
	for (i = 1; i < nxfers; i++) {
		if (addr != xfers[i].addr)
			return -ENOTSUPP;
	}

	i3c_bus_normaluse_lock(&master->bus);
	dev = i3c_master_find_i2c_dev_by_addr(master, addr);
	if (!dev)
		ret = -ENOENT;
	else
		ret = master->ops->i2c_xfers(dev, xfers, nxfers);
	i3c_bus_normaluse_unlock(&master->bus);

	return ret ? ret : nxfers;
}

static u32 i3c_master_i2c_funcs(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_EMUL | I2C_FUNC_I2C;
}

static const struct i2c_algorithm i3c_master_i2c_algo = {
	.master_xfer = i3c_master_i2c_adapter_xfer,
	.functionality = i3c_master_i2c_funcs,
};

static int i3c_master_i2c_adapter_init(struct i3c_master_controller *master)
{
	struct i2c_adapter *adap = i3c_master_to_i2c_adapter(master);
	struct i2c_dev_desc *i2cdev;
	int ret;

	adap->dev.parent = master->dev.parent;
	adap->owner = master->dev.parent->driver->owner;
	adap->algo = &i3c_master_i2c_algo;
	strncpy(adap->name, dev_name(master->dev.parent), sizeof(adap->name));

	/* FIXME: Should we allow i3c masters to override these values? */
	adap->timeout = 1000;
	adap->retries = 3;

	ret = i2c_add_adapter(adap);
	if (ret)
		return ret;

	/*
	 * We silently ignore failures here. The bus should keep working
	 * correctly even if one or more i2c devices are not registered.
	 */
	i3c_bus_for_each_i2cdev(&master->bus, i2cdev)
		i2cdev->dev = i2c_new_client_device(adap, &i2cdev->boardinfo->base);

	return 0;
}

static void i3c_master_i2c_adapter_cleanup(struct i3c_master_controller *master)
{
	struct i2c_dev_desc *i2cdev;

	i2c_del_adapter(&master->i2c);

	i3c_bus_for_each_i2cdev(&master->bus, i2cdev)
		i2cdev->dev = NULL;
}

static void i3c_master_unregister_i3c_devs(struct i3c_master_controller *master)
{
	struct i3c_dev_desc *i3cdev;

	i3c_bus_for_each_i3cdev(&master->bus, i3cdev) {
		if (!i3cdev->dev)
			continue;

		i3cdev->dev->desc = NULL;
		if (device_is_registered(&i3cdev->dev->dev))
			device_unregister(&i3cdev->dev->dev);
		else
			put_device(&i3cdev->dev->dev);
		i3cdev->dev = NULL;
	}
}

/**
 * i3c_master_queue_ibi() - Queue an IBI
 * @dev: the device this IBI is coming from
 * @slot: the IBI slot used to store the payload
 *
 * Queue an IBI to the controller workqueue. The IBI handler attached to
 * the dev will be called from a workqueue context.
 */
void i3c_master_queue_ibi(struct i3c_dev_desc *dev, struct i3c_ibi_slot *slot)
{
	atomic_inc(&dev->ibi->pending_ibis);
	queue_work(dev->common.master->wq, &slot->work);
}
EXPORT_SYMBOL_GPL(i3c_master_queue_ibi);

static void i3c_master_handle_ibi(struct work_struct *work)
{
	struct i3c_ibi_slot *slot = container_of(work, struct i3c_ibi_slot,
						 work);
	struct i3c_dev_desc *dev = slot->dev;
	struct i3c_master_controller *master = i3c_dev_get_master(dev);
	struct i3c_ibi_payload payload;

	payload.data = slot->data;
	payload.len = slot->len;

	if (dev->dev)
		dev->ibi->handler(dev->dev, &payload);

	master->ops->recycle_ibi_slot(dev, slot);
	if (atomic_dec_and_test(&dev->ibi->pending_ibis))
		complete(&dev->ibi->all_ibis_handled);
}

static void i3c_master_init_ibi_slot(struct i3c_dev_desc *dev,
				     struct i3c_ibi_slot *slot)
{
	slot->dev = dev;
	INIT_WORK(&slot->work, i3c_master_handle_ibi);
}

struct i3c_generic_ibi_slot {
	struct list_head node;
	struct i3c_ibi_slot base;
};

struct i3c_generic_ibi_pool {
	spinlock_t lock;
	unsigned int num_slots;
	struct i3c_generic_ibi_slot *slots;
	void *payload_buf;
	struct list_head free_slots;
	struct list_head pending;
};

/**
 * i3c_generic_ibi_free_pool() - Free a generic IBI pool
 * @pool: the IBI pool to free
 *
 * Free all IBI slots allated by a generic IBI pool.
 */
void i3c_generic_ibi_free_pool(struct i3c_generic_ibi_pool *pool)
{
	struct i3c_generic_ibi_slot *slot;
	unsigned int nslots = 0;

	while (!list_empty(&pool->free_slots)) {
		slot = list_first_entry(&pool->free_slots,
					struct i3c_generic_ibi_slot, node);
		list_del(&slot->node);
		nslots++;
	}

	/*
	 * If the number of freed slots is not equal to the number of allocated
	 * slots we have a leak somewhere.
	 */
	WARN_ON(nslots != pool->num_slots);

	kfree(pool->payload_buf);
	kfree(pool->slots);
	kfree(pool);
}
EXPORT_SYMBOL_GPL(i3c_generic_ibi_free_pool);

/**
 * i3c_generic_ibi_alloc_pool() - Create a generic IBI pool
 * @dev: the device this pool will be used for
 * @req: IBI setup request describing what the device driver expects
 *
 * Create a generic IBI pool based on the information provided in @req.
 *
 * Return: a valid IBI pool in case of success, an ERR_PTR() otherwise.
 */
struct i3c_generic_ibi_pool *
i3c_generic_ibi_alloc_pool(struct i3c_dev_desc *dev,
			   const struct i3c_ibi_setup *req)
{
	struct i3c_generic_ibi_pool *pool;
	struct i3c_generic_ibi_slot *slot;
	unsigned int i;
	int ret;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&pool->lock);
	INIT_LIST_HEAD(&pool->free_slots);
	INIT_LIST_HEAD(&pool->pending);

	pool->slots = kcalloc(req->num_slots, sizeof(*slot), GFP_KERNEL);
	if (!pool->slots) {
		ret = -ENOMEM;
		goto err_free_pool;
	}

	if (req->max_payload_len) {
		pool->payload_buf = kcalloc(req->num_slots,
					    req->max_payload_len, GFP_KERNEL);
		if (!pool->payload_buf) {
			ret = -ENOMEM;
			goto err_free_pool;
		}
	}

	for (i = 0; i < req->num_slots; i++) {
		slot = &pool->slots[i];
		i3c_master_init_ibi_slot(dev, &slot->base);

		if (req->max_payload_len)
			slot->base.data = pool->payload_buf +
					  (i * req->max_payload_len);

		list_add_tail(&slot->node, &pool->free_slots);
		pool->num_slots++;
	}

	return pool;

err_free_pool:
	i3c_generic_ibi_free_pool(pool);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(i3c_generic_ibi_alloc_pool);

/**
 * i3c_generic_ibi_get_free_slot() - Get a free slot from a generic IBI pool
 * @pool: the pool to query an IBI slot on
 *
 * Search for a free slot in a generic IBI pool.
 * The slot should be returned to the pool using i3c_generic_ibi_recycle_slot()
 * when it's no longer needed.
 *
 * Return: a pointer to a free slot, or NULL if there's no free slot available.
 */
struct i3c_ibi_slot *
i3c_generic_ibi_get_free_slot(struct i3c_generic_ibi_pool *pool)
{
	struct i3c_generic_ibi_slot *slot;
	unsigned long flags;

	spin_lock_irqsave(&pool->lock, flags);
	slot = list_first_entry_or_null(&pool->free_slots,
					struct i3c_generic_ibi_slot, node);
	if (slot)
		list_del(&slot->node);
	spin_unlock_irqrestore(&pool->lock, flags);

	return slot ? &slot->base : NULL;
}
EXPORT_SYMBOL_GPL(i3c_generic_ibi_get_free_slot);

/**
 * i3c_generic_ibi_recycle_slot() - Return a slot to a generic IBI pool
 * @pool: the pool to return the IBI slot to
 * @s: IBI slot to recycle
 *
 * Add an IBI slot back to its generic IBI pool. Should be called from the
 * master driver struct_master_controller_ops->recycle_ibi() method.
 */
void i3c_generic_ibi_recycle_slot(struct i3c_generic_ibi_pool *pool,
				  struct i3c_ibi_slot *s)
{
	struct i3c_generic_ibi_slot *slot;
	unsigned long flags;

	if (!s)
		return;

	slot = container_of(s, struct i3c_generic_ibi_slot, base);
	spin_lock_irqsave(&pool->lock, flags);
	list_add_tail(&slot->node, &pool->free_slots);
	spin_unlock_irqrestore(&pool->lock, flags);
}
EXPORT_SYMBOL_GPL(i3c_generic_ibi_recycle_slot);

static int i3c_master_check_ops(const struct i3c_master_controller_ops *ops)
{
	if (!ops || !ops->bus_init || !ops->priv_xfers ||
	    !ops->send_ccc_cmd || !ops->do_daa || !ops->i2c_xfers)
		return -EINVAL;

	if (ops->request_ibi &&
	    (!ops->enable_ibi || !ops->disable_ibi || !ops->free_ibi ||
	     !ops->recycle_ibi_slot))
		return -EINVAL;

	return 0;
}

/**
 * i3c_master_register() - register an I3C master
 * @master: master used to send frames on the bus
 * @parent: the parent device (the one that provides this I3C master
 *	    controller)
 * @ops: the master controller operations
 * @secondary: true if you are registering a secondary master. Will return
 *	       -ENOTSUPP if set to true since secondary masters are not yet
 *	       supported
 *
 * This function takes care of everything for you:
 *
 * - creates and initializes the I3C bus
 * - populates the bus with static I2C devs if @parent->of_node is not
 *   NULL
 * - registers all I3C devices added by the controller during bus
 *   initialization
 * - registers the I2C adapter and all I2C devices
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int i3c_master_register(struct i3c_master_controller *master,
			struct device *parent,
			const struct i3c_master_controller_ops *ops,
			bool secondary)
{
	unsigned long i2c_scl_rate = I3C_BUS_I2C_FM_PLUS_SCL_RATE;
	struct i3c_bus *i3cbus = i3c_master_get_bus(master);
	enum i3c_bus_mode mode = I3C_BUS_MODE_PURE;
	struct i2c_dev_boardinfo *i2cbi;
	int ret;

	/* We do not support secondary masters yet. */
	if (secondary)
		return -ENOTSUPP;

	ret = i3c_master_check_ops(ops);
	if (ret)
		return ret;

	master->dev.parent = parent;
	master->dev.of_node = of_node_get(parent->of_node);
	master->dev.bus = &i3c_bus_type;
	master->dev.type = &i3c_masterdev_type;
	master->dev.release = i3c_masterdev_release;
	master->ops = ops;
	master->secondary = secondary;
	INIT_LIST_HEAD(&master->boardinfo.i2c);
	INIT_LIST_HEAD(&master->boardinfo.i3c);

	ret = i3c_bus_init(i3cbus);
	if (ret)
		return ret;

	device_initialize(&master->dev);
	dev_set_name(&master->dev, "i3c-%d", i3cbus->id);

	ret = of_populate_i3c_bus(master);
	if (ret)
		goto err_put_dev;

	list_for_each_entry(i2cbi, &master->boardinfo.i2c, node) {
		switch (i2cbi->lvr & I3C_LVR_I2C_INDEX_MASK) {
		case I3C_LVR_I2C_INDEX(0):
			if (mode < I3C_BUS_MODE_MIXED_FAST)
				mode = I3C_BUS_MODE_MIXED_FAST;
			break;
		case I3C_LVR_I2C_INDEX(1):
			if (mode < I3C_BUS_MODE_MIXED_LIMITED)
				mode = I3C_BUS_MODE_MIXED_LIMITED;
			break;
		case I3C_LVR_I2C_INDEX(2):
			if (mode < I3C_BUS_MODE_MIXED_SLOW)
				mode = I3C_BUS_MODE_MIXED_SLOW;
			break;
		default:
			ret = -EINVAL;
			goto err_put_dev;
		}

		if (i2cbi->lvr & I3C_LVR_I2C_FM_MODE)
			i2c_scl_rate = I3C_BUS_I2C_FM_SCL_RATE;
	}

	ret = i3c_bus_set_mode(i3cbus, mode, i2c_scl_rate);
	if (ret)
		goto err_put_dev;

	master->wq = alloc_workqueue("%s", 0, 0, dev_name(parent));
	if (!master->wq) {
		ret = -ENOMEM;
		goto err_put_dev;
	}

	ret = i3c_master_bus_init(master);
	if (ret)
		goto err_put_dev;

	ret = device_add(&master->dev);
	if (ret)
		goto err_cleanup_bus;

	/*
	 * Expose our I3C bus as an I2C adapter so that I2C devices are exposed
	 * through the I2C subsystem.
	 */
	ret = i3c_master_i2c_adapter_init(master);
	if (ret)
		goto err_del_dev;

	/*
	 * We're done initializing the bus and the controller, we can now
	 * register I3C devices discovered during the initial DAA.
	 */
	master->init_done = true;
	i3c_bus_normaluse_lock(&master->bus);
	i3c_master_register_new_i3c_devs(master);
	i3c_bus_normaluse_unlock(&master->bus);

	return 0;

err_del_dev:
	device_del(&master->dev);

err_cleanup_bus:
	i3c_master_bus_cleanup(master);

err_put_dev:
	put_device(&master->dev);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_master_register);

/**
 * i3c_master_unregister() - unregister an I3C master
 * @master: master used to send frames on the bus
 *
 * Basically undo everything done in i3c_master_register().
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int i3c_master_unregister(struct i3c_master_controller *master)
{
	i3c_master_i2c_adapter_cleanup(master);
	i3c_master_unregister_i3c_devs(master);
	i3c_master_bus_cleanup(master);
	device_unregister(&master->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(i3c_master_unregister);

int i3c_dev_do_priv_xfers_locked(struct i3c_dev_desc *dev,
				 struct i3c_priv_xfer *xfers,
				 int nxfers)
{
	struct i3c_master_controller *master;

	if (!dev)
		return -ENOENT;

	master = i3c_dev_get_master(dev);
	if (!master || !xfers)
		return -EINVAL;

	if (!master->ops->priv_xfers)
		return -ENOTSUPP;

	return master->ops->priv_xfers(dev, xfers, nxfers);
}

int i3c_dev_disable_ibi_locked(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *master;
	int ret;

	if (!dev->ibi)
		return -EINVAL;

	master = i3c_dev_get_master(dev);
	ret = master->ops->disable_ibi(dev);
	if (ret)
		return ret;

	reinit_completion(&dev->ibi->all_ibis_handled);
	if (atomic_read(&dev->ibi->pending_ibis))
		wait_for_completion(&dev->ibi->all_ibis_handled);

	dev->ibi->enabled = false;

	return 0;
}

int i3c_dev_enable_ibi_locked(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *master = i3c_dev_get_master(dev);
	int ret;

	if (!dev->ibi)
		return -EINVAL;

	ret = master->ops->enable_ibi(dev);
	if (!ret)
		dev->ibi->enabled = true;

	return ret;
}

int i3c_dev_request_ibi_locked(struct i3c_dev_desc *dev,
			       const struct i3c_ibi_setup *req)
{
	struct i3c_master_controller *master = i3c_dev_get_master(dev);
	struct i3c_device_ibi_info *ibi;
	int ret;

	if (!master->ops->request_ibi)
		return -ENOTSUPP;

	if (dev->ibi)
		return -EBUSY;

	ibi = kzalloc(sizeof(*ibi), GFP_KERNEL);
	if (!ibi)
		return -ENOMEM;

	atomic_set(&ibi->pending_ibis, 0);
	init_completion(&ibi->all_ibis_handled);
	ibi->handler = req->handler;
	ibi->max_payload_len = req->max_payload_len;
	ibi->num_slots = req->num_slots;

	dev->ibi = ibi;
	ret = master->ops->request_ibi(dev, req);
	if (ret) {
		kfree(ibi);
		dev->ibi = NULL;
	}

	return ret;
}

void i3c_dev_free_ibi_locked(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *master = i3c_dev_get_master(dev);

	if (!dev->ibi)
		return;

	if (WARN_ON(dev->ibi->enabled))
		WARN_ON(i3c_dev_disable_ibi_locked(dev));

	master->ops->free_ibi(dev);
	kfree(dev->ibi);
	dev->ibi = NULL;
}

static int __init i3c_init(void)
{
	return bus_register(&i3c_bus_type);
}
subsys_initcall(i3c_init);

static void __exit i3c_exit(void)
{
	idr_destroy(&i3c_bus_idr);
	bus_unregister(&i3c_bus_type);
}
module_exit(i3c_exit);

MODULE_AUTHOR("Boris Brezillon <boris.brezillon@bootlin.com>");
MODULE_DESCRIPTION("I3C core");
MODULE_LICENSE("GPL v2");

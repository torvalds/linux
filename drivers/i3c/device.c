// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Cadence Design Systems Inc.
 *
 * Author: Boris Brezillon <boris.brezillon@bootlin.com>
 */

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include "internals.h"

/**
 * i3c_device_do_priv_xfers() - do I3C SDR private transfers directed to a
 *				specific device
 *
 * @dev: device with which the transfers should be done
 * @xfers: array of transfers
 * @nxfers: number of transfers
 *
 * Initiate one or several private SDR transfers with @dev.
 *
 * This function can sleep and thus cannot be called in atomic context.
 *
 * Return:
 * * 0 in case of success, a negative error core otherwise.
 * * -EAGAIN: controller lost address arbitration. Target (IBI, HJ or
 *   controller role request) win the bus. Client driver needs to resend the
 *   'xfers' some time later. See I3C spec ver 1.1.1 09-Jun-2021. Section:
 *   5.1.2.2.3.
 */
int i3c_device_do_priv_xfers(struct i3c_device *dev,
			     struct i3c_priv_xfer *xfers,
			     int nxfers)
{
	int ret, i;

	if (nxfers < 1)
		return 0;

	for (i = 0; i < nxfers; i++) {
		if (!xfers[i].len || !xfers[i].data.in)
			return -EINVAL;
	}

	i3c_bus_normaluse_lock(dev->bus);
	ret = i3c_dev_do_priv_xfers_locked(dev->desc, xfers, nxfers);
	i3c_bus_normaluse_unlock(dev->bus);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_device_do_priv_xfers);

/**
 * i3c_device_do_setdasa() - do I3C dynamic address assignement with
 *                           static address
 *
 * @dev: device with which the DAA should be done
 *
 * Return: 0 in case of success, a negative error core otherwise.
 */
int i3c_device_do_setdasa(struct i3c_device *dev)
{
	int ret;

	i3c_bus_normaluse_lock(dev->bus);
	ret = i3c_dev_setdasa_locked(dev->desc);
	i3c_bus_normaluse_unlock(dev->bus);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_device_do_setdasa);

/**
 * i3c_device_get_info() - get I3C device information
 *
 * @dev: device we want information on
 * @info: the information object to fill in
 *
 * Retrieve I3C dev info.
 */
void i3c_device_get_info(const struct i3c_device *dev,
			 struct i3c_device_info *info)
{
	if (!info)
		return;

	i3c_bus_normaluse_lock(dev->bus);
	if (dev->desc)
		*info = dev->desc->info;
	i3c_bus_normaluse_unlock(dev->bus);
}
EXPORT_SYMBOL_GPL(i3c_device_get_info);

/**
 * i3c_device_disable_ibi() - Disable IBIs coming from a specific device
 * @dev: device on which IBIs should be disabled
 *
 * This function disable IBIs coming from a specific device and wait for
 * all pending IBIs to be processed.
 *
 * Return: 0 in case of success, a negative error core otherwise.
 */
int i3c_device_disable_ibi(struct i3c_device *dev)
{
	int ret = -ENOENT;

	i3c_bus_normaluse_lock(dev->bus);
	if (dev->desc) {
		mutex_lock(&dev->desc->ibi_lock);
		ret = i3c_dev_disable_ibi_locked(dev->desc);
		mutex_unlock(&dev->desc->ibi_lock);
	}
	i3c_bus_normaluse_unlock(dev->bus);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_device_disable_ibi);

/**
 * i3c_device_enable_ibi() - Enable IBIs coming from a specific device
 * @dev: device on which IBIs should be enabled
 *
 * This function enable IBIs coming from a specific device and wait for
 * all pending IBIs to be processed. This should be called on a device
 * where i3c_device_request_ibi() has succeeded.
 *
 * Note that IBIs from this device might be received before this function
 * returns to its caller.
 *
 * Return: 0 in case of success, a negative error core otherwise.
 */
int i3c_device_enable_ibi(struct i3c_device *dev)
{
	int ret = -ENOENT;

	i3c_bus_normaluse_lock(dev->bus);
	if (dev->desc) {
		mutex_lock(&dev->desc->ibi_lock);
		ret = i3c_dev_enable_ibi_locked(dev->desc);
		mutex_unlock(&dev->desc->ibi_lock);
	}
	i3c_bus_normaluse_unlock(dev->bus);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_device_enable_ibi);

/**
 * i3c_device_request_ibi() - Request an IBI
 * @dev: device for which we should enable IBIs
 * @req: setup requested for this IBI
 *
 * This function is responsible for pre-allocating all resources needed to
 * process IBIs coming from @dev. When this function returns, the IBI is not
 * enabled until i3c_device_enable_ibi() is called.
 *
 * Return: 0 in case of success, a negative error core otherwise.
 */
int i3c_device_request_ibi(struct i3c_device *dev,
			   const struct i3c_ibi_setup *req)
{
	int ret = -ENOENT;

	if (!req->handler || !req->num_slots)
		return -EINVAL;

	i3c_bus_normaluse_lock(dev->bus);
	if (dev->desc) {
		mutex_lock(&dev->desc->ibi_lock);
		ret = i3c_dev_request_ibi_locked(dev->desc, req);
		mutex_unlock(&dev->desc->ibi_lock);
	}
	i3c_bus_normaluse_unlock(dev->bus);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_device_request_ibi);

/**
 * i3c_device_free_ibi() - Free all resources needed for IBI handling
 * @dev: device on which you want to release IBI resources
 *
 * This function is responsible for de-allocating resources previously
 * allocated by i3c_device_request_ibi(). It should be called after disabling
 * IBIs with i3c_device_disable_ibi().
 */
void i3c_device_free_ibi(struct i3c_device *dev)
{
	i3c_bus_normaluse_lock(dev->bus);
	if (dev->desc) {
		mutex_lock(&dev->desc->ibi_lock);
		i3c_dev_free_ibi_locked(dev->desc);
		mutex_unlock(&dev->desc->ibi_lock);
	}
	i3c_bus_normaluse_unlock(dev->bus);
}
EXPORT_SYMBOL_GPL(i3c_device_free_ibi);

/**
 * i3cdev_to_dev() - Returns the device embedded in @i3cdev
 * @i3cdev: I3C device
 *
 * Return: a pointer to a device object.
 */
struct device *i3cdev_to_dev(struct i3c_device *i3cdev)
{
	return &i3cdev->dev;
}
EXPORT_SYMBOL_GPL(i3cdev_to_dev);

/**
 * i3c_device_match_id() - Returns the i3c_device_id entry matching @i3cdev
 * @i3cdev: I3C device
 * @id_table: I3C device match table
 *
 * Return: a pointer to an i3c_device_id object or NULL if there's no match.
 */
const struct i3c_device_id *
i3c_device_match_id(struct i3c_device *i3cdev,
		    const struct i3c_device_id *id_table)
{
	struct i3c_device_info devinfo;
	const struct i3c_device_id *id;
	u16 manuf, part, ext_info;
	bool rndpid;

	i3c_device_get_info(i3cdev, &devinfo);

	manuf = I3C_PID_MANUF_ID(devinfo.pid);
	part = I3C_PID_PART_ID(devinfo.pid);
	ext_info = I3C_PID_EXTRA_INFO(devinfo.pid);
	rndpid = I3C_PID_RND_LOWER_32BITS(devinfo.pid);

	for (id = id_table; id->match_flags != 0; id++) {
		if ((id->match_flags & I3C_MATCH_DCR) &&
		    id->dcr != devinfo.dcr)
			continue;

		if ((id->match_flags & I3C_MATCH_MANUF) &&
		    id->manuf_id != manuf)
			continue;

		if ((id->match_flags & I3C_MATCH_PART) &&
		    (rndpid || id->part_id != part))
			continue;

		if ((id->match_flags & I3C_MATCH_EXTRA_INFO) &&
		    (rndpid || id->extra_info != ext_info))
			continue;

		return id;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(i3c_device_match_id);

/**
 * i3c_driver_register_with_owner() - register an I3C device driver
 *
 * @drv: driver to register
 * @owner: module that owns this driver
 *
 * Register @drv to the core.
 *
 * Return: 0 in case of success, a negative error core otherwise.
 */
int i3c_driver_register_with_owner(struct i3c_driver *drv, struct module *owner)
{
	drv->driver.owner = owner;
	drv->driver.bus = &i3c_bus_type;

	if (!drv->probe) {
		pr_err("Trying to register an i3c driver without probe callback\n");
		return -EINVAL;
	}

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(i3c_driver_register_with_owner);

/**
 * i3c_driver_unregister() - unregister an I3C device driver
 *
 * @drv: driver to unregister
 *
 * Unregister @drv.
 */
void i3c_driver_unregister(struct i3c_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(i3c_driver_unregister);

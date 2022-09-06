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
 * Return: 0 in case of success, a negative error core otherwise.
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
 * i3c_device_generate_ibi() - request In-Band Interrupt
 *
 * @dev: target device
 * @data: IBI payload
 * @len: payload length in bytes
 *
 * Request In-Band Interrupt with or without data payload.
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int i3c_device_generate_ibi(struct i3c_device *dev, const u8 *data, int len)
{
	int ret;

	i3c_bus_normaluse_lock(dev->bus);
	ret = i3c_dev_generate_ibi_locked(dev->desc, data, len);
	i3c_bus_normaluse_unlock(dev->bus);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_device_generate_ibi);

/**
 * i3c_device_pending_read_notify() - Notify the bus master about the
 *				      pending read data through IBI
 *
 * @dev: device with which the transfers should be done
 * @pending_read: the transfer that conveys the pending read data
 * @ibi_notify: the transfer that conveys the IBI with data (MDB)
 *
 * Initiate a private SDR transfer with @dev, then issue an IBI with
 * data to notify the bus master that there is a pending read transfer.
 *
 * This function can sleep and thus cannot be called in atomic context.
 *
 * Return: 0 in case of success, a negative error core otherwise.
 */
int i3c_device_pending_read_notify(struct i3c_device *dev,
				   struct i3c_priv_xfer *pending_read,
				   struct i3c_priv_xfer *ibi_notify)
{
	int ret;

	i3c_bus_normaluse_lock(dev->bus);
	ret = i3c_dev_pending_read_notify_locked(dev->desc, pending_read,
						 ibi_notify);
	i3c_bus_normaluse_unlock(dev->bus);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_device_pending_read_notify);

/**
 * i3c_device_is_ibi_enabled() - Query the In-Band Interrupt status
 *
 * @dev: target device
 *
 * Queries the device to check if In-Band Interrupt (IBI) is enabled by the bus
 * controller.
 *
 * Return: 1 if enabled, 0 if disabled.
 */
bool i3c_device_is_ibi_enabled(struct i3c_device *dev)
{
	bool ret;

	i3c_bus_normaluse_lock(dev->bus);
	ret = i3c_dev_is_ibi_enabled_locked(dev->desc);
	i3c_bus_normaluse_unlock(dev->bus);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_device_is_ibi_enabled);

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
 * i3c_device_getstatus_ccc() - receive device status
 *
 * @dev: I3C device to get the status for
 * @info: I3C device info to fill the status in
 *
 * Receive I3C device status from I3C master device via corresponding CCC
 * command
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int i3c_device_getstatus_ccc(struct i3c_device *dev, struct i3c_device_info *info)
{
	int ret = -EINVAL;

	i3c_bus_normaluse_lock(dev->bus);
	if (dev->desc)
		ret = i3c_dev_getstatus_locked(dev->desc, info);
	i3c_bus_normaluse_unlock(dev->bus);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_device_getstatus_ccc);

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

/**
 * i3c_device_control_pec() - enable or disable PEC support in HW
 *
 * @dev: I3C device to get the status for
 * @pec: flag telling whether PEC support shall be enabled or disabled
 *
 * Try to enable or disable HW support for PEC (Packet Error Check).
 * In case no HW support for PEC, software implementation could be used.
 *
 * Return: 0 in case of success, -EOPNOTSUPP in case PEC is not supported by HW,
 *         other negative error codes when PEC enabling failed.
 */
int i3c_device_control_pec(struct i3c_device *dev, bool pec)
{
	return i3c_dev_control_pec(dev->desc, pec);
}
EXPORT_SYMBOL_GPL(i3c_device_control_pec);

/**
 * i3c_device_setmrl_ccc() - set maximum read length
 *
 * @dev: I3C device to set the length for
 * @info: I3C device info to fill the length in
 * @read_len: maximum read length value to be set
 * @ibi_len: maximum ibi payload length to be set
 *
 * Set I3C device maximum read length from I3C master device via corresponding CCC command
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int i3c_device_setmrl_ccc(struct i3c_device *dev, struct i3c_device_info *info, u16 read_len,
			  u8 ibi_len)
{
	struct i3c_master_controller *master = i3c_dev_get_master(dev->desc);
	int ret = -EINVAL;

	i3c_bus_normaluse_lock(dev->bus);
	if (master)
		ret = i3c_master_setmrl_locked(master, info, read_len, ibi_len);
	i3c_bus_normaluse_unlock(dev->bus);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_device_setmrl_ccc);

/**
 * i3c_device_setmwl_ccc() - set maximum write length
 *
 * @dev: I3C device to set the length for
 * @info: I3C device info to fill the length in
 * @write_len: maximum write length value to be set
 *
 * Set I3C device maximum write length from I3C master device via corresponding CCC command
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int i3c_device_setmwl_ccc(struct i3c_device *dev, struct i3c_device_info *info, u16 write_len)
{
	struct i3c_master_controller *master = i3c_dev_get_master(dev->desc);
	int ret = -EINVAL;

	i3c_bus_normaluse_lock(dev->bus);
	if (master)
		ret = i3c_master_setmwl_locked(master, info, write_len);
	i3c_bus_normaluse_unlock(dev->bus);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_device_setmwl_ccc);

/**
 * i3c_device_getmrl_ccc() - get maximum read length
 *
 * @dev: I3C device to get the length for
 * @info: I3C device info to fill the length in
 *
 * Receive I3C device maximum read length from I3C master device via corresponding CCC command
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int i3c_device_getmrl_ccc(struct i3c_device *dev, struct i3c_device_info *info)
{
	struct i3c_master_controller *master = i3c_dev_get_master(dev->desc);
	int ret = -EINVAL;

	i3c_bus_normaluse_lock(dev->bus);
	if (master)
		ret = i3c_master_getmrl_locked(master, info);
	i3c_bus_normaluse_unlock(dev->bus);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_device_getmrl_ccc);

/**
 * i3c_device_getmwl_ccc() - get maximum write length
 *
 * @dev: I3C device to get the length for
 * @info: I3C device info to fill the length in
 *
 * Receive I3C device maximum write length from I3C master device via corresponding CCC command
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int i3c_device_getmwl_ccc(struct i3c_device *dev, struct i3c_device_info *info)
{
	struct i3c_master_controller *master = i3c_dev_get_master(dev->desc);
	int ret = -EINVAL;

	i3c_bus_normaluse_lock(dev->bus);
	if (master)
		ret = i3c_master_getmwl_locked(master, info);
	i3c_bus_normaluse_unlock(dev->bus);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_device_getmwl_ccc);

int i3c_device_setaasa_ccc(struct i3c_device *dev)
{
	struct i3c_master_controller *master = i3c_dev_get_master(dev->desc);
	int ret = -EINVAL;

	i3c_bus_normaluse_lock(dev->bus);
	if (master)
		ret = i3c_master_setaasa_locked(master);
	i3c_bus_normaluse_unlock(dev->bus);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_device_setaasa_ccc);

int i3c_device_sethid_ccc(struct i3c_device *dev)
{
	struct i3c_master_controller *master = i3c_dev_get_master(dev->desc);
	int ret = -EINVAL;

	i3c_bus_normaluse_lock(dev->bus);
	if (master)
		ret = i3c_master_sethid_locked(master);
	i3c_bus_normaluse_unlock(dev->bus);

	return ret;
}
EXPORT_SYMBOL_GPL(i3c_device_sethid_ccc);

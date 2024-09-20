// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RapidIO driver support
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/rio.h>
#include <linux/rio_ids.h>
#include <linux/rio_drv.h>

#include "rio.h"

/**
 *  rio_match_device - Tell if a RIO device has a matching RIO device id structure
 *  @id: the RIO device id structure to match against
 *  @rdev: the RIO device structure to match against
 *
 *  Used from driver probe and bus matching to check whether a RIO device
 *  matches a device id structure provided by a RIO driver. Returns the
 *  matching &struct rio_device_id or %NULL if there is no match.
 */
static const struct rio_device_id *rio_match_device(const struct rio_device_id
						    *id,
						    const struct rio_dev *rdev)
{
	while (id->vid || id->asm_vid) {
		if (((id->vid == RIO_ANY_ID) || (id->vid == rdev->vid)) &&
		    ((id->did == RIO_ANY_ID) || (id->did == rdev->did)) &&
		    ((id->asm_vid == RIO_ANY_ID)
		     || (id->asm_vid == rdev->asm_vid))
		    && ((id->asm_did == RIO_ANY_ID)
			|| (id->asm_did == rdev->asm_did)))
			return id;
		id++;
	}
	return NULL;
}

/**
 * rio_dev_get - Increments the reference count of the RIO device structure
 *
 * @rdev: RIO device being referenced
 *
 * Each live reference to a device should be refcounted.
 *
 * Drivers for RIO devices should normally record such references in
 * their probe() methods, when they bind to a device, and release
 * them by calling rio_dev_put(), in their disconnect() methods.
 */
struct rio_dev *rio_dev_get(struct rio_dev *rdev)
{
	if (rdev)
		get_device(&rdev->dev);

	return rdev;
}

/**
 * rio_dev_put - Release a use of the RIO device structure
 *
 * @rdev: RIO device being disconnected
 *
 * Must be called when a user of a device is finished with it.
 * When the last user of the device calls this function, the
 * memory of the device is freed.
 */
void rio_dev_put(struct rio_dev *rdev)
{
	if (rdev)
		put_device(&rdev->dev);
}

/**
 *  rio_device_probe - Tell if a RIO device structure has a matching RIO device id structure
 *  @dev: the RIO device structure to match against
 *
 * return 0 and set rio_dev->driver when drv claims rio_dev, else error
 */
static int rio_device_probe(struct device *dev)
{
	struct rio_driver *rdrv = to_rio_driver(dev->driver);
	struct rio_dev *rdev = to_rio_dev(dev);
	int error = -ENODEV;
	const struct rio_device_id *id;

	if (!rdev->driver && rdrv->probe) {
		if (!rdrv->id_table)
			return error;
		id = rio_match_device(rdrv->id_table, rdev);
		rio_dev_get(rdev);
		if (id)
			error = rdrv->probe(rdev, id);
		if (error >= 0) {
			rdev->driver = rdrv;
			error = 0;
		} else
			rio_dev_put(rdev);
	}
	return error;
}

/**
 *  rio_device_remove - Remove a RIO device from the system
 *
 *  @dev: the RIO device structure to match against
 *
 * Remove a RIO device from the system. If it has an associated
 * driver, then run the driver remove() method.  Then update
 * the reference count.
 */
static void rio_device_remove(struct device *dev)
{
	struct rio_dev *rdev = to_rio_dev(dev);
	struct rio_driver *rdrv = rdev->driver;

	if (rdrv) {
		if (rdrv->remove)
			rdrv->remove(rdev);
		rdev->driver = NULL;
	}

	rio_dev_put(rdev);
}

static void rio_device_shutdown(struct device *dev)
{
	struct rio_dev *rdev = to_rio_dev(dev);
	struct rio_driver *rdrv = rdev->driver;

	dev_dbg(dev, "RIO: %s\n", __func__);

	if (rdrv && rdrv->shutdown)
		rdrv->shutdown(rdev);
}

/**
 *  rio_register_driver - register a new RIO driver
 *  @rdrv: the RIO driver structure to register
 *
 *  Adds a &struct rio_driver to the list of registered drivers.
 *  Returns a negative value on error, otherwise 0. If no error
 *  occurred, the driver remains registered even if no device
 *  was claimed during registration.
 */
int rio_register_driver(struct rio_driver *rdrv)
{
	/* initialize common driver fields */
	rdrv->driver.name = rdrv->name;
	rdrv->driver.bus = &rio_bus_type;

	/* register with core */
	return driver_register(&rdrv->driver);
}

/**
 *  rio_unregister_driver - unregister a RIO driver
 *  @rdrv: the RIO driver structure to unregister
 *
 *  Deletes the &struct rio_driver from the list of registered RIO
 *  drivers, gives it a chance to clean up by calling its remove()
 *  function for each device it was responsible for, and marks those
 *  devices as driverless.
 */
void rio_unregister_driver(struct rio_driver *rdrv)
{
	driver_unregister(&rdrv->driver);
}

void rio_attach_device(struct rio_dev *rdev)
{
	rdev->dev.bus = &rio_bus_type;
}
EXPORT_SYMBOL_GPL(rio_attach_device);

/**
 *  rio_match_bus - Tell if a RIO device structure has a matching RIO driver device id structure
 *  @dev: the standard device structure to match against
 *  @drv: the standard driver structure containing the ids to match against
 *
 *  Used by a driver to check whether a RIO device present in the
 *  system is in its list of supported devices. Returns 1 if
 *  there is a matching &struct rio_device_id or 0 if there is
 *  no match.
 */
static int rio_match_bus(struct device *dev, const struct device_driver *drv)
{
	struct rio_dev *rdev = to_rio_dev(dev);
	const struct rio_driver *rdrv = to_rio_driver(drv);
	const struct rio_device_id *id = rdrv->id_table;
	const struct rio_device_id *found_id;

	if (!id)
		goto out;

	found_id = rio_match_device(id, rdev);

	if (found_id)
		return 1;

      out:return 0;
}

static int rio_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct rio_dev *rdev;

	if (!dev)
		return -ENODEV;

	rdev = to_rio_dev(dev);
	if (!rdev)
		return -ENODEV;

	if (add_uevent_var(env, "MODALIAS=rapidio:v%04Xd%04Xav%04Xad%04X",
			   rdev->vid, rdev->did, rdev->asm_vid, rdev->asm_did))
		return -ENOMEM;
	return 0;
}

struct class rio_mport_class = {
	.name		= "rapidio_port",
	.dev_groups	= rio_mport_groups,
};
EXPORT_SYMBOL_GPL(rio_mport_class);

struct bus_type rio_bus_type = {
	.name = "rapidio",
	.match = rio_match_bus,
	.dev_groups = rio_dev_groups,
	.bus_groups = rio_bus_groups,
	.probe = rio_device_probe,
	.remove = rio_device_remove,
	.shutdown = rio_device_shutdown,
	.uevent	= rio_uevent,
};

/**
 *  rio_bus_init - Register the RapidIO bus with the device model
 *
 *  Registers the RIO mport device class and RIO bus type with the Linux
 *  device model.
 */
static int __init rio_bus_init(void)
{
	int ret;

	ret = class_register(&rio_mport_class);
	if (!ret) {
		ret = bus_register(&rio_bus_type);
		if (ret)
			class_unregister(&rio_mport_class);
	}
	return ret;
}

postcore_initcall(rio_bus_init);

EXPORT_SYMBOL_GPL(rio_register_driver);
EXPORT_SYMBOL_GPL(rio_unregister_driver);
EXPORT_SYMBOL_GPL(rio_bus_type);
EXPORT_SYMBOL_GPL(rio_dev_get);
EXPORT_SYMBOL_GPL(rio_dev_put);

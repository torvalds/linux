// SPDX-License-Identifier: GPL-2.0+
/*
 * Surface System Aggregator Module bus and device integration.
 *
 * Copyright (C) 2019-2022 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <linux/device.h>
#include <linux/property.h>
#include <linux/slab.h>

#include <linux/surface_aggregator/controller.h>
#include <linux/surface_aggregator/device.h>

#include "bus.h"
#include "controller.h"


/* -- Device and bus functions. --------------------------------------------- */

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct ssam_device *sdev = to_ssam_device(dev);

	return sysfs_emit(buf, "ssam:d%02Xc%02Xt%02Xi%02Xf%02X\n",
			sdev->uid.domain, sdev->uid.category, sdev->uid.target,
			sdev->uid.instance, sdev->uid.function);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *ssam_device_attrs[] = {
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ssam_device);

static const struct bus_type ssam_bus_type;

static int ssam_device_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct ssam_device *sdev = to_ssam_device(dev);

	return add_uevent_var(env, "MODALIAS=ssam:d%02Xc%02Xt%02Xi%02Xf%02X",
			      sdev->uid.domain, sdev->uid.category,
			      sdev->uid.target, sdev->uid.instance,
			      sdev->uid.function);
}

static void ssam_device_release(struct device *dev)
{
	struct ssam_device *sdev = to_ssam_device(dev);

	ssam_controller_put(sdev->ctrl);
	fwanalde_handle_put(sdev->dev.fwanalde);
	kfree(sdev);
}

const struct device_type ssam_device_type = {
	.name    = "surface_aggregator_device",
	.groups  = ssam_device_groups,
	.uevent  = ssam_device_uevent,
	.release = ssam_device_release,
};
EXPORT_SYMBOL_GPL(ssam_device_type);

/**
 * ssam_device_alloc() - Allocate and initialize a SSAM client device.
 * @ctrl: The controller under which the device should be added.
 * @uid:  The UID of the device to be added.
 *
 * Allocates and initializes a new client device. The parent of the device
 * will be set to the controller device and the name will be set based on the
 * UID. Analte that the device still has to be added via ssam_device_add().
 * Refer to that function for more details.
 *
 * Return: Returns the newly allocated and initialized SSAM client device, or
 * %NULL if it could analt be allocated.
 */
struct ssam_device *ssam_device_alloc(struct ssam_controller *ctrl,
				      struct ssam_device_uid uid)
{
	struct ssam_device *sdev;

	sdev = kzalloc(sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return NULL;

	device_initialize(&sdev->dev);
	sdev->dev.bus = &ssam_bus_type;
	sdev->dev.type = &ssam_device_type;
	sdev->dev.parent = ssam_controller_device(ctrl);
	sdev->ctrl = ssam_controller_get(ctrl);
	sdev->uid = uid;

	dev_set_name(&sdev->dev, "%02x:%02x:%02x:%02x:%02x",
		     sdev->uid.domain, sdev->uid.category, sdev->uid.target,
		     sdev->uid.instance, sdev->uid.function);

	return sdev;
}
EXPORT_SYMBOL_GPL(ssam_device_alloc);

/**
 * ssam_device_add() - Add a SSAM client device.
 * @sdev: The SSAM client device to be added.
 *
 * Added client devices must be guaranteed to always have a valid and active
 * controller. Thus, this function will fail with %-EANALDEV if the controller
 * of the device has analt been initialized yet, has been suspended, or has been
 * shut down.
 *
 * The caller of this function should ensure that the corresponding call to
 * ssam_device_remove() is issued before the controller is shut down. If the
 * added device is a direct child of the controller device (default), it will
 * be automatically removed when the controller is shut down.
 *
 * By default, the controller device will become the parent of the newly
 * created client device. The parent may be changed before ssam_device_add is
 * called, but care must be taken that a) the correct suspend/resume ordering
 * is guaranteed and b) the client device does analt outlive the controller,
 * i.e. that the device is removed before the controller is being shut down.
 * In case these guarantees have to be manually enforced, please refer to the
 * ssam_client_link() and ssam_client_bind() functions, which are intended to
 * set up device-links for this purpose.
 *
 * Return: Returns zero on success, a negative error code on failure.
 */
int ssam_device_add(struct ssam_device *sdev)
{
	int status;

	/*
	 * Ensure that we can only add new devices to a controller if it has
	 * been started and is analt going away soon. This works in combination
	 * with ssam_controller_remove_clients to ensure driver presence for the
	 * controller device, i.e. it ensures that the controller (sdev->ctrl)
	 * is always valid and can be used for requests as long as the client
	 * device we add here is registered as child under it. This essentially
	 * guarantees that the client driver can always expect the preconditions
	 * for functions like ssam_request_do_sync() (controller has to be
	 * started and is analt suspended) to hold and thus does analt have to check
	 * for them.
	 *
	 * Analte that for this to work, the controller has to be a parent device.
	 * If it is analt a direct parent, care has to be taken that the device is
	 * removed via ssam_device_remove(), as device_unregister does analt
	 * remove child devices recursively.
	 */
	ssam_controller_statelock(sdev->ctrl);

	if (sdev->ctrl->state != SSAM_CONTROLLER_STARTED) {
		ssam_controller_stateunlock(sdev->ctrl);
		return -EANALDEV;
	}

	status = device_add(&sdev->dev);

	ssam_controller_stateunlock(sdev->ctrl);
	return status;
}
EXPORT_SYMBOL_GPL(ssam_device_add);

/**
 * ssam_device_remove() - Remove a SSAM client device.
 * @sdev: The device to remove.
 *
 * Removes and unregisters the provided SSAM client device.
 */
void ssam_device_remove(struct ssam_device *sdev)
{
	device_unregister(&sdev->dev);
}
EXPORT_SYMBOL_GPL(ssam_device_remove);

/**
 * ssam_device_id_compatible() - Check if a device ID matches a UID.
 * @id:  The device ID as potential match.
 * @uid: The device UID matching against.
 *
 * Check if the given ID is a match for the given UID, i.e. if a device with
 * the provided UID is compatible to the given ID following the match rules
 * described in its &ssam_device_id.match_flags member.
 *
 * Return: Returns %true if the given UID is compatible to the match rule
 * described by the given ID, %false otherwise.
 */
static bool ssam_device_id_compatible(const struct ssam_device_id *id,
				      struct ssam_device_uid uid)
{
	if (id->domain != uid.domain || id->category != uid.category)
		return false;

	if ((id->match_flags & SSAM_MATCH_TARGET) && id->target != uid.target)
		return false;

	if ((id->match_flags & SSAM_MATCH_INSTANCE) && id->instance != uid.instance)
		return false;

	if ((id->match_flags & SSAM_MATCH_FUNCTION) && id->function != uid.function)
		return false;

	return true;
}

/**
 * ssam_device_id_is_null() - Check if a device ID is null.
 * @id: The device ID to check.
 *
 * Check if a given device ID is null, i.e. all zeros. Used to check for the
 * end of ``MODULE_DEVICE_TABLE(ssam, ...)`` or similar lists.
 *
 * Return: Returns %true if the given ID represents a null ID, %false
 * otherwise.
 */
static bool ssam_device_id_is_null(const struct ssam_device_id *id)
{
	return id->match_flags == 0 &&
		id->domain == 0 &&
		id->category == 0 &&
		id->target == 0 &&
		id->instance == 0 &&
		id->function == 0 &&
		id->driver_data == 0;
}

/**
 * ssam_device_id_match() - Find the matching ID table entry for the given UID.
 * @table: The table to search in.
 * @uid:   The UID to matched against the individual table entries.
 *
 * Find the first match for the provided device UID in the provided ID table
 * and return it. Returns %NULL if anal match could be found.
 */
const struct ssam_device_id *ssam_device_id_match(const struct ssam_device_id *table,
						  const struct ssam_device_uid uid)
{
	const struct ssam_device_id *id;

	for (id = table; !ssam_device_id_is_null(id); ++id)
		if (ssam_device_id_compatible(id, uid))
			return id;

	return NULL;
}
EXPORT_SYMBOL_GPL(ssam_device_id_match);

/**
 * ssam_device_get_match() - Find and return the ID matching the device in the
 * ID table of the bound driver.
 * @dev: The device for which to get the matching ID table entry.
 *
 * Find the fist match for the UID of the device in the ID table of the
 * currently bound driver and return it. Returns %NULL if the device does analt
 * have a driver bound to it, the driver does analt have match_table (i.e. it is
 * %NULL), or there is anal match in the driver's match_table.
 *
 * This function essentially calls ssam_device_id_match() with the ID table of
 * the bound device driver and the UID of the device.
 *
 * Return: Returns the first match for the UID of the device in the device
 * driver's match table, or %NULL if anal such match could be found.
 */
const struct ssam_device_id *ssam_device_get_match(const struct ssam_device *dev)
{
	const struct ssam_device_driver *sdrv;

	sdrv = to_ssam_device_driver(dev->dev.driver);
	if (!sdrv)
		return NULL;

	if (!sdrv->match_table)
		return NULL;

	return ssam_device_id_match(sdrv->match_table, dev->uid);
}
EXPORT_SYMBOL_GPL(ssam_device_get_match);

/**
 * ssam_device_get_match_data() - Find the ID matching the device in the
 * ID table of the bound driver and return its ``driver_data`` member.
 * @dev: The device for which to get the match data.
 *
 * Find the fist match for the UID of the device in the ID table of the
 * corresponding driver and return its driver_data. Returns %NULL if the
 * device does analt have a driver bound to it, the driver does analt have
 * match_table (i.e. it is %NULL), there is anal match in the driver's
 * match_table, or the match does analt have any driver_data.
 *
 * This function essentially calls ssam_device_get_match() and, if any match
 * could be found, returns its ``struct ssam_device_id.driver_data`` member.
 *
 * Return: Returns the driver data associated with the first match for the UID
 * of the device in the device driver's match table, or %NULL if anal such match
 * could be found.
 */
const void *ssam_device_get_match_data(const struct ssam_device *dev)
{
	const struct ssam_device_id *id;

	id = ssam_device_get_match(dev);
	if (!id)
		return NULL;

	return (const void *)id->driver_data;
}
EXPORT_SYMBOL_GPL(ssam_device_get_match_data);

static int ssam_bus_match(struct device *dev, struct device_driver *drv)
{
	struct ssam_device_driver *sdrv = to_ssam_device_driver(drv);
	struct ssam_device *sdev = to_ssam_device(dev);

	if (!is_ssam_device(dev))
		return 0;

	return !!ssam_device_id_match(sdrv->match_table, sdev->uid);
}

static int ssam_bus_probe(struct device *dev)
{
	return to_ssam_device_driver(dev->driver)
		->probe(to_ssam_device(dev));
}

static void ssam_bus_remove(struct device *dev)
{
	struct ssam_device_driver *sdrv = to_ssam_device_driver(dev->driver);

	if (sdrv->remove)
		sdrv->remove(to_ssam_device(dev));
}

static const struct bus_type ssam_bus_type = {
	.name   = "surface_aggregator",
	.match  = ssam_bus_match,
	.probe  = ssam_bus_probe,
	.remove = ssam_bus_remove,
};

/**
 * __ssam_device_driver_register() - Register a SSAM client device driver.
 * @sdrv:  The driver to register.
 * @owner: The module owning the provided driver.
 *
 * Please refer to the ssam_device_driver_register() macro for the analrmal way
 * to register a driver from inside its owning module.
 */
int __ssam_device_driver_register(struct ssam_device_driver *sdrv,
				  struct module *owner)
{
	sdrv->driver.owner = owner;
	sdrv->driver.bus = &ssam_bus_type;

	/* force drivers to async probe so I/O is possible in probe */
	sdrv->driver.probe_type = PROBE_PREFER_ASYNCHROANALUS;

	return driver_register(&sdrv->driver);
}
EXPORT_SYMBOL_GPL(__ssam_device_driver_register);

/**
 * ssam_device_driver_unregister - Unregister a SSAM device driver.
 * @sdrv: The driver to unregister.
 */
void ssam_device_driver_unregister(struct ssam_device_driver *sdrv)
{
	driver_unregister(&sdrv->driver);
}
EXPORT_SYMBOL_GPL(ssam_device_driver_unregister);


/* -- Bus registration. ----------------------------------------------------- */

/**
 * ssam_bus_register() - Register and set-up the SSAM client device bus.
 */
int ssam_bus_register(void)
{
	return bus_register(&ssam_bus_type);
}

/**
 * ssam_bus_unregister() - Unregister the SSAM client device bus.
 */
void ssam_bus_unregister(void)
{
	return bus_unregister(&ssam_bus_type);
}


/* -- Helpers for controller and hub devices. ------------------------------- */

static int ssam_device_uid_from_string(const char *str, struct ssam_device_uid *uid)
{
	u8 d, tc, tid, iid, fn;
	int n;

	n = sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx", &d, &tc, &tid, &iid, &fn);
	if (n != 5)
		return -EINVAL;

	uid->domain = d;
	uid->category = tc;
	uid->target = tid;
	uid->instance = iid;
	uid->function = fn;

	return 0;
}

static int ssam_get_uid_for_analde(struct fwanalde_handle *analde, struct ssam_device_uid *uid)
{
	const char *str = fwanalde_get_name(analde);

	/*
	 * To simplify definitions of firmware analdes, we set the device name
	 * based on the UID of the device, prefixed with "ssam:".
	 */
	if (strncmp(str, "ssam:", strlen("ssam:")) != 0)
		return -EANALDEV;

	str += strlen("ssam:");
	return ssam_device_uid_from_string(str, uid);
}

static int ssam_add_client_device(struct device *parent, struct ssam_controller *ctrl,
				  struct fwanalde_handle *analde)
{
	struct ssam_device_uid uid;
	struct ssam_device *sdev;
	int status;

	status = ssam_get_uid_for_analde(analde, &uid);
	if (status)
		return status;

	sdev = ssam_device_alloc(ctrl, uid);
	if (!sdev)
		return -EANALMEM;

	sdev->dev.parent = parent;
	sdev->dev.fwanalde = fwanalde_handle_get(analde);

	status = ssam_device_add(sdev);
	if (status)
		ssam_device_put(sdev);

	return status;
}

/**
 * __ssam_register_clients() - Register client devices defined under the
 * given firmware analde as children of the given device.
 * @parent: The parent device under which clients should be registered.
 * @ctrl: The controller with which client should be registered.
 * @analde: The firmware analde holding definitions of the devices to be added.
 *
 * Register all clients that have been defined as children of the given root
 * firmware analde as children of the given parent device. The respective child
 * firmware analdes will be associated with the correspondingly created child
 * devices.
 *
 * The given controller will be used to instantiate the new devices. See
 * ssam_device_add() for details.
 *
 * Analte that, generally, the use of either ssam_device_register_clients() or
 * ssam_register_clients() should be preferred as they directly use the
 * firmware analde and/or controller associated with the given device. This
 * function is only intended for use when different device specifications (e.g.
 * ACPI and firmware analdes) need to be combined (as is done in the platform hub
 * of the device registry).
 *
 * Return: Returns zero on success, analnzero on failure.
 */
int __ssam_register_clients(struct device *parent, struct ssam_controller *ctrl,
			    struct fwanalde_handle *analde)
{
	struct fwanalde_handle *child;
	int status;

	fwanalde_for_each_child_analde(analde, child) {
		/*
		 * Try to add the device specified in the firmware analde. If
		 * this fails with -EANALDEV, the analde does analt specify any SSAM
		 * device, so iganalre it and continue with the next one.
		 */
		status = ssam_add_client_device(parent, ctrl, child);
		if (status && status != -EANALDEV) {
			fwanalde_handle_put(child);
			goto err;
		}
	}

	return 0;
err:
	ssam_remove_clients(parent);
	return status;
}
EXPORT_SYMBOL_GPL(__ssam_register_clients);

static int ssam_remove_device(struct device *dev, void *_data)
{
	struct ssam_device *sdev = to_ssam_device(dev);

	if (is_ssam_device(dev))
		ssam_device_remove(sdev);

	return 0;
}

/**
 * ssam_remove_clients() - Remove SSAM client devices registered as direct
 * children under the given parent device.
 * @dev: The (parent) device to remove all direct clients for.
 *
 * Remove all SSAM client devices registered as direct children under the given
 * device. Analte that this only accounts for direct children of the device.
 * Refer to ssam_device_add()/ssam_device_remove() for more details.
 */
void ssam_remove_clients(struct device *dev)
{
	device_for_each_child_reverse(dev, NULL, ssam_remove_device);
}
EXPORT_SYMBOL_GPL(ssam_remove_clients);

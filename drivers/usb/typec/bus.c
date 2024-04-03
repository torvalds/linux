// SPDX-License-Identifier: GPL-2.0
/*
 * Bus for USB Type-C Alternate Modes
 *
 * Copyright (C) 2018 Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/usb/pd_vdo.h>

#include "bus.h"
#include "class.h"
#include "mux.h"
#include "retimer.h"

static inline int
typec_altmode_set_retimer(struct altmode *alt, unsigned long conf, void *data)
{
	struct typec_retimer_state state;

	if (!alt->retimer)
		return 0;

	state.alt = &alt->adev;
	state.mode = conf;
	state.data = data;

	return typec_retimer_set(alt->retimer, &state);
}

static inline int
typec_altmode_set_mux(struct altmode *alt, unsigned long conf, void *data)
{
	struct typec_mux_state state;

	if (!alt->mux)
		return 0;

	state.alt = &alt->adev;
	state.mode = conf;
	state.data = data;

	return typec_mux_set(alt->mux, &state);
}

/* Wrapper to set various Type-C port switches together. */
static inline int
typec_altmode_set_switches(struct altmode *alt, unsigned long conf, void *data)
{
	int ret;

	ret = typec_altmode_set_retimer(alt, conf, data);
	if (ret)
		return ret;

	return typec_altmode_set_mux(alt, conf, data);
}

static int typec_altmode_set_state(struct typec_altmode *adev,
				   unsigned long conf, void *data)
{
	bool is_port = is_typec_port(adev->dev.parent);
	struct altmode *port_altmode;

	port_altmode = is_port ? to_altmode(adev) : to_altmode(adev)->partner;

	return typec_altmode_set_switches(port_altmode, conf, data);
}

/* -------------------------------------------------------------------------- */
/* Common API */

/**
 * typec_altmode_notify - Communication between the OS and alternate mode driver
 * @adev: Handle to the alternate mode
 * @conf: Alternate mode specific configuration value
 * @data: Alternate mode specific data
 *
 * The primary purpose for this function is to allow the alternate mode drivers
 * to tell which pin configuration has been negotiated with the partner. That
 * information will then be used for example to configure the muxes.
 * Communication to the other direction is also possible, and low level device
 * drivers can also send notifications to the alternate mode drivers. The actual
 * communication will be specific for every SVID.
 */
int typec_altmode_notify(struct typec_altmode *adev,
			 unsigned long conf, void *data)
{
	bool is_port;
	struct altmode *altmode;
	struct altmode *partner;
	int ret;

	if (!adev)
		return 0;

	altmode = to_altmode(adev);

	if (!altmode->partner)
		return -ENODEV;

	is_port = is_typec_port(adev->dev.parent);
	partner = altmode->partner;

	ret = typec_altmode_set_switches(is_port ? altmode : partner, conf, data);
	if (ret)
		return ret;

	if (partner->adev.ops && partner->adev.ops->notify)
		return partner->adev.ops->notify(&partner->adev, conf, data);

	return 0;
}
EXPORT_SYMBOL_GPL(typec_altmode_notify);

/**
 * typec_altmode_enter - Enter Mode
 * @adev: The alternate mode
 * @vdo: VDO for the Enter Mode command
 *
 * The alternate mode drivers use this function to enter mode. The port drivers
 * use this to inform the alternate mode drivers that the partner has initiated
 * Enter Mode command. If the alternate mode does not require VDO, @vdo must be
 * NULL.
 */
int typec_altmode_enter(struct typec_altmode *adev, u32 *vdo)
{
	struct altmode *partner = to_altmode(adev)->partner;
	struct typec_altmode *pdev = &partner->adev;
	int ret;

	if (!adev || adev->active)
		return 0;

	if (!pdev->ops || !pdev->ops->enter)
		return -EOPNOTSUPP;

	if (is_typec_port(pdev->dev.parent) && !pdev->active)
		return -EPERM;

	/* Moving to USB Safe State */
	ret = typec_altmode_set_state(adev, TYPEC_STATE_SAFE, NULL);
	if (ret)
		return ret;

	/* Enter Mode */
	return pdev->ops->enter(pdev, vdo);
}
EXPORT_SYMBOL_GPL(typec_altmode_enter);

/**
 * typec_altmode_exit - Exit Mode
 * @adev: The alternate mode
 *
 * The partner of @adev has initiated Exit Mode command.
 */
int typec_altmode_exit(struct typec_altmode *adev)
{
	struct altmode *partner = to_altmode(adev)->partner;
	struct typec_altmode *pdev = &partner->adev;
	int ret;

	if (!adev || !adev->active)
		return 0;

	if (!pdev->ops || !pdev->ops->exit)
		return -EOPNOTSUPP;

	/* Moving to USB Safe State */
	ret = typec_altmode_set_state(adev, TYPEC_STATE_SAFE, NULL);
	if (ret)
		return ret;

	/* Exit Mode command */
	return pdev->ops->exit(pdev);
}
EXPORT_SYMBOL_GPL(typec_altmode_exit);

/**
 * typec_altmode_attention - Attention command
 * @adev: The alternate mode
 * @vdo: VDO for the Attention command
 *
 * Notifies the partner of @adev about Attention command.
 */
int typec_altmode_attention(struct typec_altmode *adev, u32 vdo)
{
	struct altmode *partner = to_altmode(adev)->partner;
	struct typec_altmode *pdev;

	if (!partner)
		return -ENODEV;

	pdev = &partner->adev;

	if (pdev->ops && pdev->ops->attention)
		pdev->ops->attention(pdev, vdo);

	return 0;
}
EXPORT_SYMBOL_GPL(typec_altmode_attention);

/**
 * typec_altmode_vdm - Send Vendor Defined Messages (VDM) to the partner
 * @adev: Alternate mode handle
 * @header: VDM Header
 * @vdo: Array of Vendor Defined Data Objects
 * @count: Number of Data Objects
 *
 * The alternate mode drivers use this function for SVID specific communication
 * with the partner. The port drivers use it to deliver the Structured VDMs
 * received from the partners to the alternate mode drivers.
 */
int typec_altmode_vdm(struct typec_altmode *adev,
		      const u32 header, const u32 *vdo, int count)
{
	struct typec_altmode *pdev;
	struct altmode *altmode;

	if (!adev)
		return 0;

	altmode = to_altmode(adev);

	if (!altmode->partner)
		return -ENODEV;

	pdev = &altmode->partner->adev;

	if (!pdev->ops || !pdev->ops->vdm)
		return -EOPNOTSUPP;

	return pdev->ops->vdm(pdev, header, vdo, count);
}
EXPORT_SYMBOL_GPL(typec_altmode_vdm);

const struct typec_altmode *
typec_altmode_get_partner(struct typec_altmode *adev)
{
	if (!adev || !to_altmode(adev)->partner)
		return NULL;

	return &to_altmode(adev)->partner->adev;
}
EXPORT_SYMBOL_GPL(typec_altmode_get_partner);

/* -------------------------------------------------------------------------- */
/* API for cable alternate modes */

/**
 * typec_cable_altmode_enter - Enter Mode
 * @adev: The alternate mode
 * @sop: Cable plug target for Enter Mode command
 * @vdo: VDO for the Enter Mode command
 *
 * Alternate mode drivers use this function to enter mode on the cable plug.
 * If the alternate mode does not require VDO, @vdo must be NULL.
 */
int typec_cable_altmode_enter(struct typec_altmode *adev, enum typec_plug_index sop, u32 *vdo)
{
	struct altmode *partner = to_altmode(adev)->partner;
	struct typec_altmode *pdev;

	if (!adev || adev->active)
		return 0;

	if (!partner)
		return -ENODEV;

	pdev = &partner->adev;

	if (!pdev->active)
		return -EPERM;

	if (!pdev->cable_ops || !pdev->cable_ops->enter)
		return -EOPNOTSUPP;

	return pdev->cable_ops->enter(pdev, sop, vdo);
}
EXPORT_SYMBOL_GPL(typec_cable_altmode_enter);

/**
 * typec_cable_altmode_exit - Exit Mode
 * @adev: The alternate mode
 * @sop: Cable plug target for Exit Mode command
 *
 * The alternate mode drivers use this function to exit mode on the cable plug.
 */
int typec_cable_altmode_exit(struct typec_altmode *adev, enum typec_plug_index sop)
{
	struct altmode *partner = to_altmode(adev)->partner;
	struct typec_altmode *pdev;

	if (!adev || !adev->active)
		return 0;

	if (!partner)
		return -ENODEV;

	pdev = &partner->adev;

	if (!pdev->cable_ops || !pdev->cable_ops->exit)
		return -EOPNOTSUPP;

	return pdev->cable_ops->exit(pdev, sop);
}
EXPORT_SYMBOL_GPL(typec_cable_altmode_exit);

/**
 * typec_cable_altmode_vdm - Send Vendor Defined Messages (VDM) between the cable plug and port.
 * @adev: Alternate mode handle
 * @sop: Cable plug target for VDM
 * @header: VDM Header
 * @vdo: Array of Vendor Defined Data Objects
 * @count: Number of Data Objects
 *
 * The alternate mode drivers use this function for SVID specific communication
 * with the cable plugs. The port drivers use it to deliver the Structured VDMs
 * received from the cable plugs to the alternate mode drivers.
 */
int typec_cable_altmode_vdm(struct typec_altmode *adev, enum typec_plug_index sop,
			    const u32 header, const u32 *vdo, int count)
{
	struct altmode *altmode;
	struct typec_altmode *pdev;

	if (!adev)
		return 0;

	altmode = to_altmode(adev);

	if (is_typec_plug(adev->dev.parent)) {
		if (!altmode->partner)
			return -ENODEV;
		pdev = &altmode->partner->adev;
	} else {
		if (!altmode->plug[sop])
			return -ENODEV;
		pdev = &altmode->plug[sop]->adev;
	}

	if (!pdev->cable_ops || !pdev->cable_ops->vdm)
		return -EOPNOTSUPP;

	return pdev->cable_ops->vdm(pdev, sop, header, vdo, count);
}
EXPORT_SYMBOL_GPL(typec_cable_altmode_vdm);

/* -------------------------------------------------------------------------- */
/* API for the alternate mode drivers */

/**
 * typec_altmode_get_plug - Find cable plug alternate mode
 * @adev: Handle to partner alternate mode
 * @index: Cable plug index
 *
 * Increment reference count for cable plug alternate mode device. Returns
 * handle to the cable plug alternate mode, or NULL if none is found.
 */
struct typec_altmode *typec_altmode_get_plug(struct typec_altmode *adev,
					     enum typec_plug_index index)
{
	struct altmode *port = to_altmode(adev)->partner;

	if (port->plug[index]) {
		get_device(&port->plug[index]->adev.dev);
		return &port->plug[index]->adev;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(typec_altmode_get_plug);

/**
 * typec_altmode_put_plug - Decrement cable plug alternate mode reference count
 * @plug: Handle to the cable plug alternate mode
 */
void typec_altmode_put_plug(struct typec_altmode *plug)
{
	if (plug)
		put_device(&plug->dev);
}
EXPORT_SYMBOL_GPL(typec_altmode_put_plug);

int __typec_altmode_register_driver(struct typec_altmode_driver *drv,
				    struct module *module)
{
	if (!drv->probe)
		return -EINVAL;

	drv->driver.owner = module;
	drv->driver.bus = &typec_bus;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(__typec_altmode_register_driver);

void typec_altmode_unregister_driver(struct typec_altmode_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(typec_altmode_unregister_driver);

/* -------------------------------------------------------------------------- */
/* API for the port drivers */

/**
 * typec_match_altmode - Match SVID and mode to an array of alternate modes
 * @altmodes: Array of alternate modes
 * @n: Number of elements in the array, or -1 for NULL terminated arrays
 * @svid: Standard or Vendor ID to match with
 * @mode: Mode to match with
 *
 * Return pointer to an alternate mode with SVID matching @svid, or NULL when no
 * match is found.
 */
struct typec_altmode *typec_match_altmode(struct typec_altmode **altmodes,
					  size_t n, u16 svid, u8 mode)
{
	int i;

	for (i = 0; i < n; i++) {
		if (!altmodes[i])
			break;
		if (altmodes[i]->svid == svid && altmodes[i]->mode == mode)
			return altmodes[i];
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(typec_match_altmode);

/* -------------------------------------------------------------------------- */

static ssize_t
description_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct typec_altmode *alt = to_typec_altmode(dev);

	return sprintf(buf, "%s\n", alt->desc ? alt->desc : "");
}
static DEVICE_ATTR_RO(description);

static struct attribute *typec_attrs[] = {
	&dev_attr_description.attr,
	NULL
};
ATTRIBUTE_GROUPS(typec);

static int typec_match(struct device *dev, struct device_driver *driver)
{
	struct typec_altmode_driver *drv = to_altmode_driver(driver);
	struct typec_altmode *altmode = to_typec_altmode(dev);
	const struct typec_device_id *id;

	for (id = drv->id_table; id->svid; id++)
		if (id->svid == altmode->svid &&
		    (id->mode == TYPEC_ANY_MODE || id->mode == altmode->mode))
			return 1;
	return 0;
}

static int typec_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct typec_altmode *altmode = to_typec_altmode(dev);

	if (add_uevent_var(env, "SVID=%04X", altmode->svid))
		return -ENOMEM;

	if (add_uevent_var(env, "MODE=%u", altmode->mode))
		return -ENOMEM;

	return add_uevent_var(env, "MODALIAS=typec:id%04Xm%02X",
			      altmode->svid, altmode->mode);
}

static int typec_altmode_create_links(struct altmode *alt)
{
	struct device *port_dev = &alt->partner->adev.dev;
	struct device *dev = &alt->adev.dev;
	int err;

	err = sysfs_create_link(&dev->kobj, &port_dev->kobj, "port");
	if (err)
		return err;

	err = sysfs_create_link(&port_dev->kobj, &dev->kobj, "partner");
	if (err)
		sysfs_remove_link(&dev->kobj, "port");

	return err;
}

static void typec_altmode_remove_links(struct altmode *alt)
{
	sysfs_remove_link(&alt->partner->adev.dev.kobj, "partner");
	sysfs_remove_link(&alt->adev.dev.kobj, "port");
}

static int typec_probe(struct device *dev)
{
	struct typec_altmode_driver *drv = to_altmode_driver(dev->driver);
	struct typec_altmode *adev = to_typec_altmode(dev);
	struct altmode *altmode = to_altmode(adev);
	int ret;

	/* Fail if the port does not support the alternate mode */
	if (!altmode->partner)
		return -ENODEV;

	ret = typec_altmode_create_links(altmode);
	if (ret) {
		dev_warn(dev, "failed to create symlinks\n");
		return ret;
	}

	ret = drv->probe(adev);
	if (ret)
		typec_altmode_remove_links(altmode);

	return ret;
}

static void typec_remove(struct device *dev)
{
	struct typec_altmode_driver *drv = to_altmode_driver(dev->driver);
	struct typec_altmode *adev = to_typec_altmode(dev);
	struct altmode *altmode = to_altmode(adev);

	typec_altmode_remove_links(altmode);

	if (drv->remove)
		drv->remove(to_typec_altmode(dev));

	if (adev->active) {
		WARN_ON(typec_altmode_set_state(adev, TYPEC_STATE_SAFE, NULL));
		typec_altmode_update_active(adev, false);
	}

	adev->desc = NULL;
	adev->ops = NULL;
}

const struct bus_type typec_bus = {
	.name = "typec",
	.dev_groups = typec_groups,
	.match = typec_match,
	.uevent = typec_uevent,
	.probe = typec_probe,
	.remove = typec_remove,
};

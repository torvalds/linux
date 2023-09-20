// SPDX-License-Identifier: GPL-2.0-only

/*
 * drm_sysfs.c - Modifications to drm_sysfs_class.c to support
 *               extra sysfs attribute from DRM. Normal drm_sysfs_class
 *               does not allow adding attributes.
 *
 * Copyright (c) 2004 Jon Smirl <jonsmirl@gmail.com>
 * Copyright (c) 2003-2004 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (c) 2003-2004 IBM Corp.
 */

#include <linux/acpi.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/gfp.h>
#include <linux/i2c.h>
#include <linux/kdev_t.h>
#include <linux/property.h>
#include <linux/slab.h>

#include <drm/drm_accel.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_modes.h>
#include <drm/drm_print.h>
#include <drm/drm_property.h>
#include <drm/drm_sysfs.h>

#include "drm_internal.h"
#include "drm_crtc_internal.h"

#define to_drm_minor(d) dev_get_drvdata(d)
#define to_drm_connector(d) dev_get_drvdata(d)

/**
 * DOC: overview
 *
 * DRM provides very little additional support to drivers for sysfs
 * interactions, beyond just all the standard stuff. Drivers who want to expose
 * additional sysfs properties and property groups can attach them at either
 * &drm_device.dev or &drm_connector.kdev.
 *
 * Registration is automatically handled when calling drm_dev_register(), or
 * drm_connector_register() in case of hot-plugged connectors. Unregistration is
 * also automatically handled by drm_dev_unregister() and
 * drm_connector_unregister().
 */

static struct device_type drm_sysfs_device_minor = {
	.name = "drm_minor"
};

static struct device_type drm_sysfs_device_connector = {
	.name = "drm_connector",
};

struct class *drm_class;

#ifdef CONFIG_ACPI
static bool drm_connector_acpi_bus_match(struct device *dev)
{
	return dev->type == &drm_sysfs_device_connector;
}

static struct acpi_device *drm_connector_acpi_find_companion(struct device *dev)
{
	struct drm_connector *connector = to_drm_connector(dev);

	return to_acpi_device_node(connector->fwnode);
}

static struct acpi_bus_type drm_connector_acpi_bus = {
	.name = "drm_connector",
	.match = drm_connector_acpi_bus_match,
	.find_companion = drm_connector_acpi_find_companion,
};

static void drm_sysfs_acpi_register(void)
{
	register_acpi_bus_type(&drm_connector_acpi_bus);
}

static void drm_sysfs_acpi_unregister(void)
{
	unregister_acpi_bus_type(&drm_connector_acpi_bus);
}
#else
static void drm_sysfs_acpi_register(void) { }
static void drm_sysfs_acpi_unregister(void) { }
#endif

static char *drm_devnode(const struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "dri/%s", dev_name(dev));
}

static int typec_connector_bind(struct device *dev,
				struct device *typec_connector, void *data)
{
	int ret;

	ret = sysfs_create_link(&dev->kobj, &typec_connector->kobj, "typec_connector");
	if (ret)
		return ret;

	ret = sysfs_create_link(&typec_connector->kobj, &dev->kobj, "drm_connector");
	if (ret)
		sysfs_remove_link(&dev->kobj, "typec_connector");

	return ret;
}

static void typec_connector_unbind(struct device *dev,
				   struct device *typec_connector, void *data)
{
	sysfs_remove_link(&typec_connector->kobj, "drm_connector");
	sysfs_remove_link(&dev->kobj, "typec_connector");
}

static const struct component_ops typec_connector_ops = {
	.bind = typec_connector_bind,
	.unbind = typec_connector_unbind,
};

static CLASS_ATTR_STRING(version, S_IRUGO, "drm 1.1.0 20060810");

/**
 * drm_sysfs_init - initialize sysfs helpers
 *
 * This is used to create the DRM class, which is the implicit parent of any
 * other top-level DRM sysfs objects.
 *
 * You must call drm_sysfs_destroy() to release the allocated resources.
 *
 * Return: 0 on success, negative error code on failure.
 */
int drm_sysfs_init(void)
{
	int err;

	drm_class = class_create("drm");
	if (IS_ERR(drm_class))
		return PTR_ERR(drm_class);

	err = class_create_file(drm_class, &class_attr_version.attr);
	if (err) {
		class_destroy(drm_class);
		drm_class = NULL;
		return err;
	}

	drm_class->devnode = drm_devnode;

	drm_sysfs_acpi_register();
	return 0;
}

/**
 * drm_sysfs_destroy - destroys DRM class
 *
 * Destroy the DRM device class.
 */
void drm_sysfs_destroy(void)
{
	if (IS_ERR_OR_NULL(drm_class))
		return;
	drm_sysfs_acpi_unregister();
	class_remove_file(drm_class, &class_attr_version.attr);
	class_destroy(drm_class);
	drm_class = NULL;
}

static void drm_sysfs_release(struct device *dev)
{
	kfree(dev);
}

/*
 * Connector properties
 */
static ssize_t status_store(struct device *device,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_device *dev = connector->dev;
	enum drm_connector_force old_force;
	int ret;

	ret = mutex_lock_interruptible(&dev->mode_config.mutex);
	if (ret)
		return ret;

	old_force = connector->force;

	if (sysfs_streq(buf, "detect"))
		connector->force = 0;
	else if (sysfs_streq(buf, "on"))
		connector->force = DRM_FORCE_ON;
	else if (sysfs_streq(buf, "on-digital"))
		connector->force = DRM_FORCE_ON_DIGITAL;
	else if (sysfs_streq(buf, "off"))
		connector->force = DRM_FORCE_OFF;
	else
		ret = -EINVAL;

	if (old_force != connector->force || !connector->force) {
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] force updated from %d to %d or reprobing\n",
			      connector->base.id,
			      connector->name,
			      old_force, connector->force);

		connector->funcs->fill_modes(connector,
					     dev->mode_config.max_width,
					     dev->mode_config.max_height);
	}

	mutex_unlock(&dev->mode_config.mutex);

	return ret ? ret : count;
}

static ssize_t status_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	enum drm_connector_status status;

	status = READ_ONCE(connector->status);

	return sysfs_emit(buf, "%s\n",
			  drm_get_connector_status_name(status));
}

static ssize_t dpms_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	int dpms;

	dpms = READ_ONCE(connector->dpms);

	return sysfs_emit(buf, "%s\n", drm_get_dpms_name(dpms));
}

static ssize_t enabled_show(struct device *device,
			    struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	bool enabled;

	enabled = READ_ONCE(connector->encoder);

	return sysfs_emit(buf, enabled ? "enabled\n" : "disabled\n");
}

static ssize_t edid_show(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *attr, char *buf, loff_t off,
			 size_t count)
{
	struct device *connector_dev = kobj_to_dev(kobj);
	struct drm_connector *connector = to_drm_connector(connector_dev);
	unsigned char *edid;
	size_t size;
	ssize_t ret = 0;

	mutex_lock(&connector->dev->mode_config.mutex);
	if (!connector->edid_blob_ptr)
		goto unlock;

	edid = connector->edid_blob_ptr->data;
	size = connector->edid_blob_ptr->length;
	if (!edid)
		goto unlock;

	if (off >= size)
		goto unlock;

	if (off + count > size)
		count = size - off;
	memcpy(buf, edid + off, count);

	ret = count;
unlock:
	mutex_unlock(&connector->dev->mode_config.mutex);

	return ret;
}

static ssize_t modes_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_display_mode *mode;
	int written = 0;

	mutex_lock(&connector->dev->mode_config.mutex);
	list_for_each_entry(mode, &connector->modes, head) {
		written += scnprintf(buf + written, PAGE_SIZE - written, "%s\n",
				    mode->name);
	}
	mutex_unlock(&connector->dev->mode_config.mutex);

	return written;
}

static ssize_t connector_id_show(struct device *device,
				 struct device_attribute *attr,
				 char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);

	return sysfs_emit(buf, "%d\n", connector->base.id);
}

static DEVICE_ATTR_RW(status);
static DEVICE_ATTR_RO(enabled);
static DEVICE_ATTR_RO(dpms);
static DEVICE_ATTR_RO(modes);
static DEVICE_ATTR_RO(connector_id);

static struct attribute *connector_dev_attrs[] = {
	&dev_attr_status.attr,
	&dev_attr_enabled.attr,
	&dev_attr_dpms.attr,
	&dev_attr_modes.attr,
	&dev_attr_connector_id.attr,
	NULL
};

static struct bin_attribute edid_attr = {
	.attr.name = "edid",
	.attr.mode = 0444,
	.size = 0,
	.read = edid_show,
};

static struct bin_attribute *connector_bin_attrs[] = {
	&edid_attr,
	NULL
};

static const struct attribute_group connector_dev_group = {
	.attrs = connector_dev_attrs,
	.bin_attrs = connector_bin_attrs,
};

static const struct attribute_group *connector_dev_groups[] = {
	&connector_dev_group,
	NULL
};

int drm_sysfs_connector_add(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct device *kdev;
	int r;

	if (connector->kdev)
		return 0;

	kdev = kzalloc(sizeof(*kdev), GFP_KERNEL);
	if (!kdev)
		return -ENOMEM;

	device_initialize(kdev);
	kdev->class = drm_class;
	kdev->type = &drm_sysfs_device_connector;
	kdev->parent = dev->primary->kdev;
	kdev->groups = connector_dev_groups;
	kdev->release = drm_sysfs_release;
	dev_set_drvdata(kdev, connector);

	r = dev_set_name(kdev, "card%d-%s", dev->primary->index, connector->name);
	if (r)
		goto err_free;

	DRM_DEBUG("adding \"%s\" to sysfs\n",
		  connector->name);

	r = device_add(kdev);
	if (r) {
		drm_err(dev, "failed to register connector device: %d\n", r);
		goto err_free;
	}

	connector->kdev = kdev;

	if (dev_fwnode(kdev)) {
		r = component_add(kdev, &typec_connector_ops);
		if (r)
			drm_err(dev, "failed to add component to create link to typec connector\n");
	}

	if (connector->ddc)
		return sysfs_create_link(&connector->kdev->kobj,
				 &connector->ddc->dev.kobj, "ddc");

	return 0;

err_free:
	put_device(kdev);
	return r;
}

void drm_sysfs_connector_remove(struct drm_connector *connector)
{
	if (!connector->kdev)
		return;

	if (connector->ddc)
		sysfs_remove_link(&connector->kdev->kobj, "ddc");

	if (dev_fwnode(connector->kdev))
		component_del(connector->kdev, &typec_connector_ops);

	DRM_DEBUG("removing \"%s\" from sysfs\n",
		  connector->name);

	device_unregister(connector->kdev);
	connector->kdev = NULL;
}

void drm_sysfs_lease_event(struct drm_device *dev)
{
	char *event_string = "LEASE=1";
	char *envp[] = { event_string, NULL };

	DRM_DEBUG("generating lease event\n");

	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
}

/**
 * drm_sysfs_hotplug_event - generate a DRM uevent
 * @dev: DRM device
 *
 * Send a uevent for the DRM device specified by @dev.  Currently we only
 * set HOTPLUG=1 in the uevent environment, but this could be expanded to
 * deal with other types of events.
 *
 * Any new uapi should be using the drm_sysfs_connector_status_event()
 * for uevents on connector status change.
 */
void drm_sysfs_hotplug_event(struct drm_device *dev)
{
	char *event_string = "HOTPLUG=1";
	char *envp[] = { event_string, NULL };

	DRM_DEBUG("generating hotplug event\n");

	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
}
EXPORT_SYMBOL(drm_sysfs_hotplug_event);

/**
 * drm_sysfs_connector_hotplug_event - generate a DRM uevent for any connector
 * change
 * @connector: connector which has changed
 *
 * Send a uevent for the DRM connector specified by @connector. This will send
 * a uevent with the properties HOTPLUG=1 and CONNECTOR.
 */
void drm_sysfs_connector_hotplug_event(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	char hotplug_str[] = "HOTPLUG=1", conn_id[21];
	char *envp[] = { hotplug_str, conn_id, NULL };

	snprintf(conn_id, sizeof(conn_id),
		 "CONNECTOR=%u", connector->base.id);

	drm_dbg_kms(connector->dev,
		    "[CONNECTOR:%d:%s] generating connector hotplug event\n",
		    connector->base.id, connector->name);

	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
}
EXPORT_SYMBOL(drm_sysfs_connector_hotplug_event);

/**
 * drm_sysfs_connector_status_event - generate a DRM uevent for connector
 * property status change
 * @connector: connector on which property status changed
 * @property: connector property whose status changed.
 *
 * Send a uevent for the DRM device specified by @dev.  Currently we
 * set HOTPLUG=1 and connector id along with the attached property id
 * related to the status change.
 */
void drm_sysfs_connector_status_event(struct drm_connector *connector,
				      struct drm_property *property)
{
	struct drm_device *dev = connector->dev;
	char hotplug_str[] = "HOTPLUG=1", conn_id[21], prop_id[21];
	char *envp[4] = { hotplug_str, conn_id, prop_id, NULL };

	WARN_ON(!drm_mode_obj_find_prop_id(&connector->base,
					   property->base.id));

	snprintf(conn_id, ARRAY_SIZE(conn_id),
		 "CONNECTOR=%u", connector->base.id);
	snprintf(prop_id, ARRAY_SIZE(prop_id),
		 "PROPERTY=%u", property->base.id);

	DRM_DEBUG("generating connector status event\n");

	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
}
EXPORT_SYMBOL(drm_sysfs_connector_status_event);

struct device *drm_sysfs_minor_alloc(struct drm_minor *minor)
{
	const char *minor_str;
	struct device *kdev;
	int r;

	kdev = kzalloc(sizeof(*kdev), GFP_KERNEL);
	if (!kdev)
		return ERR_PTR(-ENOMEM);

	device_initialize(kdev);

	if (minor->type == DRM_MINOR_ACCEL) {
		minor_str = "accel%d";
		accel_set_device_instance_params(kdev, minor->index);
	} else {
		if (minor->type == DRM_MINOR_RENDER)
			minor_str = "renderD%d";
		else
			minor_str = "card%d";

		kdev->devt = MKDEV(DRM_MAJOR, minor->index);
		kdev->class = drm_class;
		kdev->type = &drm_sysfs_device_minor;
	}

	kdev->parent = minor->dev->dev;
	kdev->release = drm_sysfs_release;
	dev_set_drvdata(kdev, minor);

	r = dev_set_name(kdev, minor_str, minor->index);
	if (r < 0)
		goto err_free;

	return kdev;

err_free:
	put_device(kdev);
	return ERR_PTR(r);
}

/**
 * drm_class_device_register - register new device with the DRM sysfs class
 * @dev: device to register
 *
 * Registers a new &struct device within the DRM sysfs class. Essentially only
 * used by ttm to have a place for its global settings. Drivers should never use
 * this.
 */
int drm_class_device_register(struct device *dev)
{
	if (!drm_class || IS_ERR(drm_class))
		return -ENOENT;

	dev->class = drm_class;
	return device_register(dev);
}
EXPORT_SYMBOL_GPL(drm_class_device_register);

/**
 * drm_class_device_unregister - unregister device with the DRM sysfs class
 * @dev: device to unregister
 *
 * Unregisters a &struct device from the DRM sysfs class. Essentially only used
 * by ttm to have a place for its global settings. Drivers should never use
 * this.
 */
void drm_class_device_unregister(struct device *dev)
{
	return device_unregister(dev);
}
EXPORT_SYMBOL_GPL(drm_class_device_unregister);

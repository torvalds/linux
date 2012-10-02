
/*
 * drm_sysfs.c - Modifications to drm_sysfs_class.c to support
 *               extra sysfs attribute from DRM. Normal drm_sysfs_class
 *               does not allow adding attributes.
 *
 * Copyright (c) 2004 Jon Smirl <jonsmirl@gmail.com>
 * Copyright (c) 2003-2004 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (c) 2003-2004 IBM Corp.
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/gfp.h>
#include <linux/err.h>
#include <linux/export.h>

#include <drm/drm_sysfs.h>
#include <drm/drm_core.h>
#include <drm/drmP.h>

#define to_drm_minor(d) container_of(d, struct drm_minor, kdev)
#define to_drm_connector(d) container_of(d, struct drm_connector, kdev)

static struct device_type drm_sysfs_device_minor = {
	.name = "drm_minor"
};

/**
 * drm_class_suspend - DRM class suspend hook
 * @dev: Linux device to suspend
 * @state: power state to enter
 *
 * Just figures out what the actual struct drm_device associated with
 * @dev is and calls its suspend hook, if present.
 */
static int drm_class_suspend(struct device *dev, pm_message_t state)
{
	if (dev->type == &drm_sysfs_device_minor) {
		struct drm_minor *drm_minor = to_drm_minor(dev);
		struct drm_device *drm_dev = drm_minor->dev;

		if (drm_minor->type == DRM_MINOR_LEGACY &&
		    !drm_core_check_feature(drm_dev, DRIVER_MODESET) &&
		    drm_dev->driver->suspend)
			return drm_dev->driver->suspend(drm_dev, state);
	}
	return 0;
}

/**
 * drm_class_resume - DRM class resume hook
 * @dev: Linux device to resume
 *
 * Just figures out what the actual struct drm_device associated with
 * @dev is and calls its resume hook, if present.
 */
static int drm_class_resume(struct device *dev)
{
	if (dev->type == &drm_sysfs_device_minor) {
		struct drm_minor *drm_minor = to_drm_minor(dev);
		struct drm_device *drm_dev = drm_minor->dev;

		if (drm_minor->type == DRM_MINOR_LEGACY &&
		    !drm_core_check_feature(drm_dev, DRIVER_MODESET) &&
		    drm_dev->driver->resume)
			return drm_dev->driver->resume(drm_dev);
	}
	return 0;
}

static char *drm_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "dri/%s", dev_name(dev));
}

static CLASS_ATTR_STRING(version, S_IRUGO,
		CORE_NAME " "
		__stringify(CORE_MAJOR) "."
		__stringify(CORE_MINOR) "."
		__stringify(CORE_PATCHLEVEL) " "
		CORE_DATE);

/**
 * drm_sysfs_create - create a struct drm_sysfs_class structure
 * @owner: pointer to the module that is to "own" this struct drm_sysfs_class
 * @name: pointer to a string for the name of this class.
 *
 * This is used to create DRM class pointer that can then be used
 * in calls to drm_sysfs_device_add().
 *
 * Note, the pointer created here is to be destroyed when finished by making a
 * call to drm_sysfs_destroy().
 */
struct class *drm_sysfs_create(struct module *owner, char *name)
{
	struct class *class;
	int err;

	class = class_create(owner, name);
	if (IS_ERR(class)) {
		err = PTR_ERR(class);
		goto err_out;
	}

	class->suspend = drm_class_suspend;
	class->resume = drm_class_resume;

	err = class_create_file(class, &class_attr_version.attr);
	if (err)
		goto err_out_class;

	class->devnode = drm_devnode;

	return class;

err_out_class:
	class_destroy(class);
err_out:
	return ERR_PTR(err);
}

/**
 * drm_sysfs_destroy - destroys DRM class
 *
 * Destroy the DRM device class.
 */
void drm_sysfs_destroy(void)
{
	if ((drm_class == NULL) || (IS_ERR(drm_class)))
		return;
	class_remove_file(drm_class, &class_attr_version.attr);
	class_destroy(drm_class);
	drm_class = NULL;
}

/**
 * drm_sysfs_device_release - do nothing
 * @dev: Linux device
 *
 * Normally, this would free the DRM device associated with @dev, along
 * with cleaning up any other stuff.  But we do that in the DRM core, so
 * this function can just return and hope that the core does its job.
 */
static void drm_sysfs_device_release(struct device *dev)
{
	memset(dev, 0, sizeof(struct device));
	return;
}

/*
 * Connector properties
 */
static ssize_t status_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	enum drm_connector_status status;
	int ret;

	ret = mutex_lock_interruptible(&connector->dev->mode_config.mutex);
	if (ret)
		return ret;

	status = connector->funcs->detect(connector, true);
	mutex_unlock(&connector->dev->mode_config.mutex);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			drm_get_connector_status_name(status));
}

static ssize_t dpms_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_device *dev = connector->dev;
	uint64_t dpms_status;
	int ret;

	ret = drm_connector_property_get_value(connector,
					    dev->mode_config.dpms_property,
					    &dpms_status);
	if (ret)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%s\n",
			drm_get_dpms_name((int)dpms_status));
}

static ssize_t enabled_show(struct device *device,
			    struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);

	return snprintf(buf, PAGE_SIZE, "%s\n", connector->encoder ? "enabled" :
			"disabled");
}

static ssize_t edid_show(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *attr, char *buf, loff_t off,
			 size_t count)
{
	struct device *connector_dev = container_of(kobj, struct device, kobj);
	struct drm_connector *connector = to_drm_connector(connector_dev);
	unsigned char *edid;
	size_t size;

	if (!connector->edid_blob_ptr)
		return 0;

	edid = connector->edid_blob_ptr->data;
	size = connector->edid_blob_ptr->length;
	if (!edid)
		return 0;

	if (off >= size)
		return 0;

	if (off + count > size)
		count = size - off;
	memcpy(buf, edid + off, count);

	return count;
}

static ssize_t modes_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_display_mode *mode;
	int written = 0;

	list_for_each_entry(mode, &connector->modes, head) {
		written += snprintf(buf + written, PAGE_SIZE - written, "%s\n",
				    mode->name);
	}

	return written;
}

static ssize_t subconnector_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_device *dev = connector->dev;
	struct drm_property *prop = NULL;
	uint64_t subconnector;
	int is_tv = 0;
	int ret;

	switch (connector->connector_type) {
		case DRM_MODE_CONNECTOR_DVII:
			prop = dev->mode_config.dvi_i_subconnector_property;
			break;
		case DRM_MODE_CONNECTOR_Composite:
		case DRM_MODE_CONNECTOR_SVIDEO:
		case DRM_MODE_CONNECTOR_Component:
		case DRM_MODE_CONNECTOR_TV:
			prop = dev->mode_config.tv_subconnector_property;
			is_tv = 1;
			break;
		default:
			DRM_ERROR("Wrong connector type for this property\n");
			return 0;
	}

	if (!prop) {
		DRM_ERROR("Unable to find subconnector property\n");
		return 0;
	}

	ret = drm_connector_property_get_value(connector, prop, &subconnector);
	if (ret)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%s", is_tv ?
			drm_get_tv_subconnector_name((int)subconnector) :
			drm_get_dvi_i_subconnector_name((int)subconnector));
}

static ssize_t select_subconnector_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_device *dev = connector->dev;
	struct drm_property *prop = NULL;
	uint64_t subconnector;
	int is_tv = 0;
	int ret;

	switch (connector->connector_type) {
		case DRM_MODE_CONNECTOR_DVII:
			prop = dev->mode_config.dvi_i_select_subconnector_property;
			break;
		case DRM_MODE_CONNECTOR_Composite:
		case DRM_MODE_CONNECTOR_SVIDEO:
		case DRM_MODE_CONNECTOR_Component:
		case DRM_MODE_CONNECTOR_TV:
			prop = dev->mode_config.tv_select_subconnector_property;
			is_tv = 1;
			break;
		default:
			DRM_ERROR("Wrong connector type for this property\n");
			return 0;
	}

	if (!prop) {
		DRM_ERROR("Unable to find select subconnector property\n");
		return 0;
	}

	ret = drm_connector_property_get_value(connector, prop, &subconnector);
	if (ret)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%s", is_tv ?
			drm_get_tv_select_name((int)subconnector) :
			drm_get_dvi_i_select_name((int)subconnector));
}

static struct device_attribute connector_attrs[] = {
	__ATTR_RO(status),
	__ATTR_RO(enabled),
	__ATTR_RO(dpms),
	__ATTR_RO(modes),
};

/* These attributes are for both DVI-I connectors and all types of tv-out. */
static struct device_attribute connector_attrs_opt1[] = {
	__ATTR_RO(subconnector),
	__ATTR_RO(select_subconnector),
};

static struct bin_attribute edid_attr = {
	.attr.name = "edid",
	.attr.mode = 0444,
	.size = 0,
	.read = edid_show,
};

/**
 * drm_sysfs_connector_add - add a connector to sysfs
 * @connector: connector to add
 *
 * Create a connector device in sysfs, along with its associated connector
 * properties (so far, connection status, dpms, mode list & edid) and
 * generate a hotplug event so userspace knows there's a new connector
 * available.
 *
 * Note:
 * This routine should only be called *once* for each registered connector.
 * A second call for an already registered connector will trigger the BUG_ON
 * below.
 */
int drm_sysfs_connector_add(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	int attr_cnt = 0;
	int opt_cnt = 0;
	int i;
	int ret;

	/* We shouldn't get called more than once for the same connector */
	BUG_ON(device_is_registered(&connector->kdev));

	connector->kdev.parent = &dev->primary->kdev;
	connector->kdev.class = drm_class;
	connector->kdev.release = drm_sysfs_device_release;

	DRM_DEBUG("adding \"%s\" to sysfs\n",
		  drm_get_connector_name(connector));

	dev_set_name(&connector->kdev, "card%d-%s",
		     dev->primary->index, drm_get_connector_name(connector));
	ret = device_register(&connector->kdev);

	if (ret) {
		DRM_ERROR("failed to register connector device: %d\n", ret);
		goto out;
	}

	/* Standard attributes */

	for (attr_cnt = 0; attr_cnt < ARRAY_SIZE(connector_attrs); attr_cnt++) {
		ret = device_create_file(&connector->kdev, &connector_attrs[attr_cnt]);
		if (ret)
			goto err_out_files;
	}

	/* Optional attributes */
	/*
	 * In the long run it maybe a good idea to make one set of
	 * optionals per connector type.
	 */
	switch (connector->connector_type) {
		case DRM_MODE_CONNECTOR_DVII:
		case DRM_MODE_CONNECTOR_Composite:
		case DRM_MODE_CONNECTOR_SVIDEO:
		case DRM_MODE_CONNECTOR_Component:
		case DRM_MODE_CONNECTOR_TV:
			for (opt_cnt = 0; opt_cnt < ARRAY_SIZE(connector_attrs_opt1); opt_cnt++) {
				ret = device_create_file(&connector->kdev, &connector_attrs_opt1[opt_cnt]);
				if (ret)
					goto err_out_files;
			}
			break;
		default:
			break;
	}

	ret = sysfs_create_bin_file(&connector->kdev.kobj, &edid_attr);
	if (ret)
		goto err_out_files;

	/* Let userspace know we have a new connector */
	drm_sysfs_hotplug_event(dev);

	return 0;

err_out_files:
	for (i = 0; i < opt_cnt; i++)
		device_remove_file(&connector->kdev, &connector_attrs_opt1[i]);
	for (i = 0; i < attr_cnt; i++)
		device_remove_file(&connector->kdev, &connector_attrs[i]);
	device_unregister(&connector->kdev);

out:
	return ret;
}
EXPORT_SYMBOL(drm_sysfs_connector_add);

/**
 * drm_sysfs_connector_remove - remove an connector device from sysfs
 * @connector: connector to remove
 *
 * Remove @connector and its associated attributes from sysfs.  Note that
 * the device model core will take care of sending the "remove" uevent
 * at this time, so we don't need to do it.
 *
 * Note:
 * This routine should only be called if the connector was previously
 * successfully registered.  If @connector hasn't been registered yet,
 * you'll likely see a panic somewhere deep in sysfs code when called.
 */
void drm_sysfs_connector_remove(struct drm_connector *connector)
{
	int i;

	if (!connector->kdev.parent)
		return;
	DRM_DEBUG("removing \"%s\" from sysfs\n",
		  drm_get_connector_name(connector));

	for (i = 0; i < ARRAY_SIZE(connector_attrs); i++)
		device_remove_file(&connector->kdev, &connector_attrs[i]);
	sysfs_remove_bin_file(&connector->kdev.kobj, &edid_attr);
	device_unregister(&connector->kdev);
	connector->kdev.parent = NULL;
}
EXPORT_SYMBOL(drm_sysfs_connector_remove);

/**
 * drm_sysfs_hotplug_event - generate a DRM uevent
 * @dev: DRM device
 *
 * Send a uevent for the DRM device specified by @dev.  Currently we only
 * set HOTPLUG=1 in the uevent environment, but this could be expanded to
 * deal with other types of events.
 */
void drm_sysfs_hotplug_event(struct drm_device *dev)
{
	char *event_string = "HOTPLUG=1";
	char *envp[] = { event_string, NULL };

	DRM_DEBUG("generating hotplug event\n");

	kobject_uevent_env(&dev->primary->kdev.kobj, KOBJ_CHANGE, envp);
}
EXPORT_SYMBOL(drm_sysfs_hotplug_event);

/**
 * drm_sysfs_device_add - adds a class device to sysfs for a character driver
 * @dev: DRM device to be added
 * @head: DRM head in question
 *
 * Add a DRM device to the DRM's device model class.  We use @dev's PCI device
 * as the parent for the Linux device, and make sure it has a file containing
 * the driver we're using (for userspace compatibility).
 */
int drm_sysfs_device_add(struct drm_minor *minor)
{
	int err;
	char *minor_str;

	minor->kdev.parent = minor->dev->dev;

	minor->kdev.class = drm_class;
	minor->kdev.release = drm_sysfs_device_release;
	minor->kdev.devt = minor->device;
	minor->kdev.type = &drm_sysfs_device_minor;
	if (minor->type == DRM_MINOR_CONTROL)
		minor_str = "controlD%d";
        else if (minor->type == DRM_MINOR_RENDER)
                minor_str = "renderD%d";
        else
                minor_str = "card%d";

	dev_set_name(&minor->kdev, minor_str, minor->index);

	err = device_register(&minor->kdev);
	if (err) {
		DRM_ERROR("device add failed: %d\n", err);
		goto err_out;
	}

	return 0;

err_out:
	return err;
}

/**
 * drm_sysfs_device_remove - remove DRM device
 * @dev: DRM device to remove
 *
 * This call unregisters and cleans up a class device that was created with a
 * call to drm_sysfs_device_add()
 */
void drm_sysfs_device_remove(struct drm_minor *minor)
{
	if (minor->kdev.parent)
		device_unregister(&minor->kdev);
	minor->kdev.parent = NULL;
}


/**
 * drm_class_device_register - Register a struct device in the drm class.
 *
 * @dev: pointer to struct device to register.
 *
 * @dev should have all relevant members pre-filled with the exception
 * of the class member. In particular, the device_type member must
 * be set.
 */

int drm_class_device_register(struct device *dev)
{
	if (!drm_class || IS_ERR(drm_class))
		return -ENOENT;

	dev->class = drm_class;
	return device_register(dev);
}
EXPORT_SYMBOL_GPL(drm_class_device_register);

void drm_class_device_unregister(struct device *dev)
{
	return device_unregister(dev);
}
EXPORT_SYMBOL_GPL(drm_class_device_unregister);

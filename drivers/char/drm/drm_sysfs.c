
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
#include <linux/err.h>

#include "drm_core.h"
#include "drmP.h"

#define to_drm_minor(d) container_of(d, struct drm_minor, kdev)

/**
 * drm_sysfs_suspend - DRM class suspend hook
 * @dev: Linux device to suspend
 * @state: power state to enter
 *
 * Just figures out what the actual struct drm_device associated with
 * @dev is and calls its suspend hook, if present.
 */
static int drm_sysfs_suspend(struct device *dev, pm_message_t state)
{
	struct drm_minor *drm_minor = to_drm_minor(dev);
	struct drm_device *drm_dev = drm_minor->dev;

	printk(KERN_ERR "%s\n", __FUNCTION__);

	if (drm_dev->driver->suspend)
		return drm_dev->driver->suspend(drm_dev, state);

	return 0;
}

/**
 * drm_sysfs_resume - DRM class resume hook
 * @dev: Linux device to resume
 *
 * Just figures out what the actual struct drm_device associated with
 * @dev is and calls its resume hook, if present.
 */
static int drm_sysfs_resume(struct device *dev)
{
	struct drm_minor *drm_minor = to_drm_minor(dev);
	struct drm_device *drm_dev = drm_minor->dev;

	if (drm_dev->driver->resume)
		return drm_dev->driver->resume(drm_dev);

	return 0;
}

/* Display the version of drm_core. This doesn't work right in current design */
static ssize_t version_show(struct class *dev, char *buf)
{
	return sprintf(buf, "%s %d.%d.%d %s\n", CORE_NAME, CORE_MAJOR,
		       CORE_MINOR, CORE_PATCHLEVEL, CORE_DATE);
}

static CLASS_ATTR(version, S_IRUGO, version_show, NULL);

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

	class->suspend = drm_sysfs_suspend;
	class->resume = drm_sysfs_resume;

	err = class_create_file(class, &class_attr_version);
	if (err)
		goto err_out_class;

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
	class_remove_file(drm_class, &class_attr_version);
	class_destroy(drm_class);
}

static ssize_t show_dri(struct device *device, struct device_attribute *attr,
			char *buf)
{
	struct drm_minor *drm_minor = to_drm_minor(device);
	struct drm_device *drm_dev = drm_minor->dev;
	if (drm_dev->driver->dri_library_name)
		return drm_dev->driver->dri_library_name(drm_dev, buf);
	return snprintf(buf, PAGE_SIZE, "%s\n", drm_dev->driver->pci_driver.name);
}

static struct device_attribute device_attrs[] = {
	__ATTR(dri_library_name, S_IRUGO, show_dri, NULL),
};

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
	return;
}

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
	int i, j;
	char *minor_str;

	minor->kdev.parent = &minor->dev->pdev->dev;
	minor->kdev.class = drm_class;
	minor->kdev.release = drm_sysfs_device_release;
	minor->kdev.devt = minor->device;
	minor_str = "card%d";

	snprintf(minor->kdev.bus_id, BUS_ID_SIZE, minor_str, minor->index);

	err = device_register(&minor->kdev);
	if (err) {
		DRM_ERROR("device add failed: %d\n", err);
		goto err_out;
	}

	for (i = 0; i < ARRAY_SIZE(device_attrs); i++) {
		err = device_create_file(&minor->kdev, &device_attrs[i]);
		if (err)
			goto err_out_files;
	}

	return 0;

err_out_files:
	if (i > 0)
		for (j = 0; j < i; j++)
			device_remove_file(&minor->kdev, &device_attrs[i]);
	device_unregister(&minor->kdev);
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
	int i;

	for (i = 0; i < ARRAY_SIZE(device_attrs); i++)
		device_remove_file(&minor->kdev, &device_attrs[i]);
	device_unregister(&minor->kdev);
}

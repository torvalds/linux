
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
 * This is used to create a struct drm_sysfs_class pointer that can then be used
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
 * drm_sysfs_destroy - destroys a struct drm_sysfs_class structure
 * @cs: pointer to the struct drm_sysfs_class that is to be destroyed
 *
 * Note, the pointer to be destroyed must have been created with a call to
 * drm_sysfs_create().
 */
void drm_sysfs_destroy(struct class *class)
{
	if ((class == NULL) || (IS_ERR(class)))
		return;

	class_remove_file(class, &class_attr_version);
	class_destroy(class);
}

static ssize_t show_dri(struct class_device *class_device, char *buf)
{
	struct drm_device * dev = ((struct drm_head *)class_get_devdata(class_device))->dev;
	if (dev->driver->dri_library_name)
		return dev->driver->dri_library_name(dev, buf);
	return snprintf(buf, PAGE_SIZE, "%s\n", dev->driver->pci_driver.name);
}

static struct class_device_attribute class_device_attrs[] = {
	__ATTR(dri_library_name, S_IRUGO, show_dri, NULL),
};

/**
 * drm_sysfs_device_add - adds a class device to sysfs for a character driver
 * @cs: pointer to the struct class that this device should be registered to.
 * @dev: the dev_t for the device to be added.
 * @device: a pointer to a struct device that is assiociated with this class device.
 * @fmt: string for the class device's name
 *
 * A struct class_device will be created in sysfs, registered to the specified
 * class.  A "dev" file will be created, showing the dev_t for the device.  The
 * pointer to the struct class_device will be returned from the call.  Any further
 * sysfs files that might be required can be created using this pointer.
 * Note: the struct class passed to this function must have previously been
 * created with a call to drm_sysfs_create().
 */
struct class_device *drm_sysfs_device_add(struct class *cs, struct drm_head *head)
{
	struct class_device *class_dev;
	int i, j, err;

	class_dev = class_device_create(cs, NULL,
					MKDEV(DRM_MAJOR, head->minor),
					&(head->dev->pdev)->dev,
					"card%d", head->minor);
	if (IS_ERR(class_dev)) {
		err = PTR_ERR(class_dev);
		goto err_out;
	}

	class_set_devdata(class_dev, head);

	for (i = 0; i < ARRAY_SIZE(class_device_attrs); i++) {
		err = class_device_create_file(class_dev,
					       &class_device_attrs[i]);
		if (err)
			goto err_out_files;
	}

	return class_dev;

err_out_files:
	if (i > 0)
		for (j = 0; j < i; j++)
			class_device_remove_file(class_dev,
						 &class_device_attrs[i]);
	class_device_unregister(class_dev);
err_out:
	return ERR_PTR(err);
}

/**
 * drm_sysfs_device_remove - removes a class device that was created with drm_sysfs_device_add()
 * @dev: the dev_t of the device that was previously registered.
 *
 * This call unregisters and cleans up a class device that was created with a
 * call to drm_sysfs_device_add()
 */
void drm_sysfs_device_remove(struct class_device *class_dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(class_device_attrs); i++)
		class_device_remove_file(class_dev, &class_device_attrs[i]);
	class_device_unregister(class_dev);
}

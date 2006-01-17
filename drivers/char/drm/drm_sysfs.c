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

#include <linux/config.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/err.h>

#include "drm_core.h"
#include "drmP.h"

struct drm_sysfs_class {
	struct class_device_attribute attr;
	struct class class;
};
#define to_drm_sysfs_class(d) container_of(d, struct drm_sysfs_class, class)

struct simple_dev {
	dev_t dev;
	struct class_device class_dev;
};
#define to_simple_dev(d) container_of(d, struct simple_dev, class_dev)

static void release_simple_dev(struct class_device *class_dev)
{
	struct simple_dev *s_dev = to_simple_dev(class_dev);
	kfree(s_dev);
}

static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	struct simple_dev *s_dev = to_simple_dev(class_dev);
	return print_dev_t(buf, s_dev->dev);
}

static void drm_sysfs_class_release(struct class *class)
{
	struct drm_sysfs_class *cs = to_drm_sysfs_class(class);
	kfree(cs);
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
 * This is used to create a struct drm_sysfs_class pointer that can then be used
 * in calls to drm_sysfs_device_add().
 *
 * Note, the pointer created here is to be destroyed when finished by making a
 * call to drm_sysfs_destroy().
 */
struct drm_sysfs_class *drm_sysfs_create(struct module *owner, char *name)
{
	struct drm_sysfs_class *cs;
	int retval;

	cs = kmalloc(sizeof(*cs), GFP_KERNEL);
	if (!cs) {
		retval = -ENOMEM;
		goto error;
	}
	memset(cs, 0x00, sizeof(*cs));

	cs->class.name = name;
	cs->class.class_release = drm_sysfs_class_release;
	cs->class.release = release_simple_dev;

	cs->attr.attr.name = "dev";
	cs->attr.attr.mode = S_IRUGO;
	cs->attr.attr.owner = owner;
	cs->attr.show = show_dev;
	cs->attr.store = NULL;

	retval = class_register(&cs->class);
	if (retval)
		goto error;
	class_create_file(&cs->class, &class_attr_version);

	return cs;

      error:
	kfree(cs);
	return ERR_PTR(retval);
}

/**
 * drm_sysfs_destroy - destroys a struct drm_sysfs_class structure
 * @cs: pointer to the struct drm_sysfs_class that is to be destroyed
 *
 * Note, the pointer to be destroyed must have been created with a call to
 * drm_sysfs_create().
 */
void drm_sysfs_destroy(struct drm_sysfs_class *cs)
{
	if ((cs == NULL) || (IS_ERR(cs)))
		return;

	class_unregister(&cs->class);
}

static ssize_t show_dri(struct class_device *class_device, char *buf)
{
	drm_device_t * dev = ((drm_head_t *)class_get_devdata(class_device))->dev;
	if (dev->driver->dri_library_name)
		return dev->driver->dri_library_name(dev, buf);
	return snprintf(buf, PAGE_SIZE, "%s\n", dev->driver->pci_driver.name);
}

static struct class_device_attribute class_device_attrs[] = {
	__ATTR(dri_library_name, S_IRUGO, show_dri, NULL),
};

/**
 * drm_sysfs_device_add - adds a class device to sysfs for a character driver
 * @cs: pointer to the struct drm_sysfs_class that this device should be registered to.
 * @dev: the dev_t for the device to be added.
 * @device: a pointer to a struct device that is assiociated with this class device.
 * @fmt: string for the class device's name
 *
 * A struct class_device will be created in sysfs, registered to the specified
 * class.  A "dev" file will be created, showing the dev_t for the device.  The
 * pointer to the struct class_device will be returned from the call.  Any further
 * sysfs files that might be required can be created using this pointer.
 * Note: the struct drm_sysfs_class passed to this function must have previously been
 * created with a call to drm_sysfs_create().
 */
struct class_device *drm_sysfs_device_add(struct drm_sysfs_class *cs,
					  drm_head_t *head)
{
	struct simple_dev *s_dev = NULL;
	int i, retval;

	if ((cs == NULL) || (IS_ERR(cs))) {
		retval = -ENODEV;
		goto error;
	}

	s_dev = kmalloc(sizeof(*s_dev), GFP_KERNEL);
	if (!s_dev) {
		retval = -ENOMEM;
		goto error;
	}
	memset(s_dev, 0x00, sizeof(*s_dev));

	s_dev->dev = MKDEV(DRM_MAJOR, head->minor);
	s_dev->class_dev.dev = &(head->dev->pdev)->dev;
	s_dev->class_dev.class = &cs->class;

	snprintf(s_dev->class_dev.class_id, BUS_ID_SIZE, "card%d", head->minor);
	retval = class_device_register(&s_dev->class_dev);
	if (retval)
		goto error;

	class_device_create_file(&s_dev->class_dev, &cs->attr);
	class_set_devdata(&s_dev->class_dev, head);

	for (i = 0; i < ARRAY_SIZE(class_device_attrs); i++)
		class_device_create_file(&s_dev->class_dev, &class_device_attrs[i]);
	return &s_dev->class_dev;

error:
	kfree(s_dev);
	return ERR_PTR(retval);
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
	struct simple_dev *s_dev = to_simple_dev(class_dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(class_device_attrs); i++)
		class_device_remove_file(&s_dev->class_dev, &class_device_attrs[i]);
	class_device_unregister(&s_dev->class_dev);
}

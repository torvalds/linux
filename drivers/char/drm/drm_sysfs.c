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
#include <linux/slab.h>
#include <linux/string.h>

#include "drm_core.h"
#include "drmP.h"

struct drm_sysfs_class {
	struct class_device_attribute attr;
	struct class class;
};
#define to_drm_sysfs_class(d) container_of(d, struct drm_sysfs_class, class)

struct simple_dev {
	struct list_head node;
	dev_t dev;
	struct class_device class_dev;
};
#define to_simple_dev(d) container_of(d, struct simple_dev, class_dev)

static LIST_HEAD(simple_dev_list);
static DEFINE_SPINLOCK(simple_dev_list_lock);

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
struct class_device *drm_sysfs_device_add(struct drm_sysfs_class *cs, dev_t dev,
					  struct device *device,
					  const char *fmt, ...)
{
	va_list args;
	struct simple_dev *s_dev = NULL;
	int retval;

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

	s_dev->dev = dev;
	s_dev->class_dev.dev = device;
	s_dev->class_dev.class = &cs->class;

	va_start(args, fmt);
	vsnprintf(s_dev->class_dev.class_id, BUS_ID_SIZE, fmt, args);
	va_end(args);
	retval = class_device_register(&s_dev->class_dev);
	if (retval)
		goto error;

	class_device_create_file(&s_dev->class_dev, &cs->attr);

	spin_lock(&simple_dev_list_lock);
	list_add(&s_dev->node, &simple_dev_list);
	spin_unlock(&simple_dev_list_lock);

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
void drm_sysfs_device_remove(dev_t dev)
{
	struct simple_dev *s_dev = NULL;
	int found = 0;

	spin_lock(&simple_dev_list_lock);
	list_for_each_entry(s_dev, &simple_dev_list, node) {
		if (s_dev->dev == dev) {
			found = 1;
			break;
		}
	}
	if (found) {
		list_del(&s_dev->node);
		spin_unlock(&simple_dev_list_lock);
		class_device_unregister(&s_dev->class_dev);
	} else {
		spin_unlock(&simple_dev_list_lock);
	}
}

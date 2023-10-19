// SPDX-License-Identifier: GPL-2.0
/*
 * media-dev-allocator.c - Media Controller Device Allocator API
 *
 * Copyright (c) 2019 Shuah Khan <shuah@kernel.org>
 *
 * Credits: Suggested by Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

/*
 * This file adds a global refcounted Media Controller Device Instance API.
 * A system wide global media device list is managed and each media device
 * includes a kref count. The last put on the media device releases the media
 * device instance.
 *
 */

#include <linux/kref.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <media/media-device.h>
#include <media/media-dev-allocator.h>

static LIST_HEAD(media_device_list);
static DEFINE_MUTEX(media_device_lock);

struct media_device_instance {
	struct media_device mdev;
	struct module *owner;
	struct list_head list;
	struct kref refcount;
};

static inline struct media_device_instance *
to_media_device_instance(struct media_device *mdev)
{
	return container_of(mdev, struct media_device_instance, mdev);
}

static void media_device_instance_release(struct kref *kref)
{
	struct media_device_instance *mdi =
		container_of(kref, struct media_device_instance, refcount);

	dev_dbg(mdi->mdev.dev, "%s: releasing Media Device\n", __func__);

	mutex_lock(&media_device_lock);

	media_device_unregister(&mdi->mdev);
	media_device_cleanup(&mdi->mdev);

	list_del(&mdi->list);
	mutex_unlock(&media_device_lock);

	kfree(mdi);
}

/* Callers should hold media_device_lock when calling this function */
static struct media_device *__media_device_get(struct device *dev,
						const char *module_name,
						struct module *owner)
{
	struct media_device_instance *mdi;

	list_for_each_entry(mdi, &media_device_list, list) {
		if (mdi->mdev.dev != dev)
			continue;

		kref_get(&mdi->refcount);

		/* get module reference for the media_device owner */
		if (owner != mdi->owner && !try_module_get(mdi->owner))
			dev_err(dev,
				"%s: module %s get owner reference error\n",
					__func__, module_name);
		else
			dev_dbg(dev, "%s: module %s got owner reference\n",
					__func__, module_name);
		return &mdi->mdev;
	}

	mdi = kzalloc(sizeof(*mdi), GFP_KERNEL);
	if (!mdi)
		return NULL;

	mdi->owner = owner;
	kref_init(&mdi->refcount);
	list_add_tail(&mdi->list, &media_device_list);

	dev_dbg(dev, "%s: Allocated media device for owner %s\n",
			__func__, module_name);
	return &mdi->mdev;
}

struct media_device *media_device_usb_allocate(struct usb_device *udev,
					       const char *module_name,
					       struct module *owner)
{
	struct media_device *mdev;

	mutex_lock(&media_device_lock);
	mdev = __media_device_get(&udev->dev, module_name, owner);
	if (!mdev) {
		mutex_unlock(&media_device_lock);
		return ERR_PTR(-ENOMEM);
	}

	/* check if media device is already initialized */
	if (!mdev->dev)
		__media_device_usb_init(mdev, udev, udev->product,
					module_name);
	mutex_unlock(&media_device_lock);
	return mdev;
}
EXPORT_SYMBOL_GPL(media_device_usb_allocate);

void media_device_delete(struct media_device *mdev, const char *module_name,
			 struct module *owner)
{
	struct media_device_instance *mdi = to_media_device_instance(mdev);

	mutex_lock(&media_device_lock);
	/* put module reference for the media_device owner */
	if (mdi->owner != owner) {
		module_put(mdi->owner);
		dev_dbg(mdi->mdev.dev,
			"%s: module %s put owner module reference\n",
			__func__, module_name);
	}
	mutex_unlock(&media_device_lock);
	kref_put(&mdi->refcount, media_device_instance_release);
}
EXPORT_SYMBOL_GPL(media_device_delete);

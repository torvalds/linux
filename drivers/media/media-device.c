/*
 * Media device
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/media.h>

#include <media/media-device.h>
#include <media/media-devnode.h>
#include <media/media-entity.h>

/* -----------------------------------------------------------------------------
 * Userspace API
 */

static int media_device_open(struct file *filp)
{
	return 0;
}

static int media_device_close(struct file *filp)
{
	return 0;
}

static int media_device_get_info(struct media_device *dev,
				 struct media_device_info __user *__info)
{
	struct media_device_info info;

	memset(&info, 0, sizeof(info));

	strlcpy(info.driver, dev->dev->driver->name, sizeof(info.driver));
	strlcpy(info.model, dev->model, sizeof(info.model));
	strlcpy(info.serial, dev->serial, sizeof(info.serial));
	strlcpy(info.bus_info, dev->bus_info, sizeof(info.bus_info));

	info.media_version = MEDIA_API_VERSION;
	info.hw_revision = dev->hw_revision;
	info.driver_version = dev->driver_version;

	return copy_to_user(__info, &info, sizeof(*__info));
}

static long media_device_ioctl(struct file *filp, unsigned int cmd,
			       unsigned long arg)
{
	struct media_devnode *devnode = media_devnode_data(filp);
	struct media_device *dev = to_media_device(devnode);
	long ret;

	switch (cmd) {
	case MEDIA_IOC_DEVICE_INFO:
		ret = media_device_get_info(dev,
				(struct media_device_info __user *)arg);
		break;

	default:
		ret = -ENOIOCTLCMD;
	}

	return ret;
}

static const struct media_file_operations media_device_fops = {
	.owner = THIS_MODULE,
	.open = media_device_open,
	.ioctl = media_device_ioctl,
	.release = media_device_close,
};

/* -----------------------------------------------------------------------------
 * sysfs
 */

static ssize_t show_model(struct device *cd,
			  struct device_attribute *attr, char *buf)
{
	struct media_device *mdev = to_media_device(to_media_devnode(cd));

	return sprintf(buf, "%.*s\n", (int)sizeof(mdev->model), mdev->model);
}

static DEVICE_ATTR(model, S_IRUGO, show_model, NULL);

/* -----------------------------------------------------------------------------
 * Registration/unregistration
 */

static void media_device_release(struct media_devnode *mdev)
{
}

/**
 * media_device_register - register a media device
 * @mdev:	The media device
 *
 * The caller is responsible for initializing the media device before
 * registration. The following fields must be set:
 *
 * - dev must point to the parent device
 * - model must be filled with the device model name
 */
int __must_check media_device_register(struct media_device *mdev)
{
	int ret;

	if (WARN_ON(mdev->dev == NULL || mdev->model[0] == 0))
		return -EINVAL;

	mdev->entity_id = 1;
	INIT_LIST_HEAD(&mdev->entities);
	spin_lock_init(&mdev->lock);
	mutex_init(&mdev->graph_mutex);

	/* Register the device node. */
	mdev->devnode.fops = &media_device_fops;
	mdev->devnode.parent = mdev->dev;
	mdev->devnode.release = media_device_release;
	ret = media_devnode_register(&mdev->devnode);
	if (ret < 0)
		return ret;

	ret = device_create_file(&mdev->devnode.dev, &dev_attr_model);
	if (ret < 0) {
		media_devnode_unregister(&mdev->devnode);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(media_device_register);

/**
 * media_device_unregister - unregister a media device
 * @mdev:	The media device
 *
 */
void media_device_unregister(struct media_device *mdev)
{
	struct media_entity *entity;
	struct media_entity *next;

	list_for_each_entry_safe(entity, next, &mdev->entities, list)
		media_device_unregister_entity(entity);

	device_remove_file(&mdev->devnode.dev, &dev_attr_model);
	media_devnode_unregister(&mdev->devnode);
}
EXPORT_SYMBOL_GPL(media_device_unregister);

/**
 * media_device_register_entity - Register an entity with a media device
 * @mdev:	The media device
 * @entity:	The entity
 */
int __must_check media_device_register_entity(struct media_device *mdev,
					      struct media_entity *entity)
{
	/* Warn if we apparently re-register an entity */
	WARN_ON(entity->parent != NULL);
	entity->parent = mdev;

	spin_lock(&mdev->lock);
	if (entity->id == 0)
		entity->id = mdev->entity_id++;
	else
		mdev->entity_id = max(entity->id + 1, mdev->entity_id);
	list_add_tail(&entity->list, &mdev->entities);
	spin_unlock(&mdev->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(media_device_register_entity);

/**
 * media_device_unregister_entity - Unregister an entity
 * @entity:	The entity
 *
 * If the entity has never been registered this function will return
 * immediately.
 */
void media_device_unregister_entity(struct media_entity *entity)
{
	struct media_device *mdev = entity->parent;

	if (mdev == NULL)
		return;

	spin_lock(&mdev->lock);
	list_del(&entity->list);
	spin_unlock(&mdev->lock);
	entity->parent = NULL;
}
EXPORT_SYMBOL_GPL(media_device_unregister_entity);

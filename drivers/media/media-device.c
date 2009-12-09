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

#include <media/media-device.h>
#include <media/media-devnode.h>

static const struct media_file_operations media_device_fops = {
	.owner = THIS_MODULE,
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
	device_remove_file(&mdev->devnode.dev, &dev_attr_model);
	media_devnode_unregister(&mdev->devnode);
}
EXPORT_SYMBOL_GPL(media_device_unregister);

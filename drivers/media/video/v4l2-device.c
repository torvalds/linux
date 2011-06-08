/*
    V4L2 device support.

    Copyright (C) 2008  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/i2c.h>
#if defined(CONFIG_SPI)
#include <linux/spi/spi.h>
#endif
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

int v4l2_device_register(struct device *dev, struct v4l2_device *v4l2_dev)
{
	if (v4l2_dev == NULL)
		return -EINVAL;

	INIT_LIST_HEAD(&v4l2_dev->subdevs);
	spin_lock_init(&v4l2_dev->lock);
	mutex_init(&v4l2_dev->ioctl_lock);
	v4l2_prio_init(&v4l2_dev->prio);
	kref_init(&v4l2_dev->ref);
	v4l2_dev->dev = dev;
	if (dev == NULL) {
		/* If dev == NULL, then name must be filled in by the caller */
		WARN_ON(!v4l2_dev->name[0]);
		return 0;
	}

	/* Set name to driver name + device name if it is empty. */
	if (!v4l2_dev->name[0])
		snprintf(v4l2_dev->name, sizeof(v4l2_dev->name), "%s %s",
			dev->driver->name, dev_name(dev));
	if (!dev_get_drvdata(dev))
		dev_set_drvdata(dev, v4l2_dev);
	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_device_register);

static void v4l2_device_release(struct kref *ref)
{
	struct v4l2_device *v4l2_dev =
		container_of(ref, struct v4l2_device, ref);

	if (v4l2_dev->release)
		v4l2_dev->release(v4l2_dev);
}

int v4l2_device_put(struct v4l2_device *v4l2_dev)
{
	return kref_put(&v4l2_dev->ref, v4l2_device_release);
}
EXPORT_SYMBOL_GPL(v4l2_device_put);

int v4l2_device_set_name(struct v4l2_device *v4l2_dev, const char *basename,
						atomic_t *instance)
{
	int num = atomic_inc_return(instance) - 1;
	int len = strlen(basename);

	if (basename[len - 1] >= '0' && basename[len - 1] <= '9')
		snprintf(v4l2_dev->name, sizeof(v4l2_dev->name),
				"%s-%d", basename, num);
	else
		snprintf(v4l2_dev->name, sizeof(v4l2_dev->name),
				"%s%d", basename, num);
	return num;
}
EXPORT_SYMBOL_GPL(v4l2_device_set_name);

void v4l2_device_disconnect(struct v4l2_device *v4l2_dev)
{
	if (v4l2_dev->dev == NULL)
		return;

	if (dev_get_drvdata(v4l2_dev->dev) == v4l2_dev)
		dev_set_drvdata(v4l2_dev->dev, NULL);
	v4l2_dev->dev = NULL;
}
EXPORT_SYMBOL_GPL(v4l2_device_disconnect);

void v4l2_device_unregister(struct v4l2_device *v4l2_dev)
{
	struct v4l2_subdev *sd, *next;

	if (v4l2_dev == NULL)
		return;
	v4l2_device_disconnect(v4l2_dev);

	/* Unregister subdevs */
	list_for_each_entry_safe(sd, next, &v4l2_dev->subdevs, list) {
		v4l2_device_unregister_subdev(sd);
#if defined(CONFIG_I2C) || (defined(CONFIG_I2C_MODULE) && defined(MODULE))
		if (sd->flags & V4L2_SUBDEV_FL_IS_I2C) {
			struct i2c_client *client = v4l2_get_subdevdata(sd);

			/* We need to unregister the i2c client explicitly.
			   We cannot rely on i2c_del_adapter to always
			   unregister clients for us, since if the i2c bus
			   is a platform bus, then it is never deleted. */
			if (client)
				i2c_unregister_device(client);
			continue;
		}
#endif
#if defined(CONFIG_SPI)
		if (sd->flags & V4L2_SUBDEV_FL_IS_SPI) {
			struct spi_device *spi = v4l2_get_subdevdata(sd);

			if (spi)
				spi_unregister_device(spi);
			continue;
		}
#endif
	}
}
EXPORT_SYMBOL_GPL(v4l2_device_unregister);

int v4l2_device_register_subdev(struct v4l2_device *v4l2_dev,
				struct v4l2_subdev *sd)
{
#if defined(CONFIG_MEDIA_CONTROLLER)
	struct media_entity *entity = &sd->entity;
#endif
	int err;

	/* Check for valid input */
	if (v4l2_dev == NULL || sd == NULL || !sd->name[0])
		return -EINVAL;

	/* Warn if we apparently re-register a subdev */
	WARN_ON(sd->v4l2_dev != NULL);

	if (!try_module_get(sd->owner))
		return -ENODEV;

	sd->v4l2_dev = v4l2_dev;
	if (sd->internal_ops && sd->internal_ops->registered) {
		err = sd->internal_ops->registered(sd);
		if (err) {
			module_put(sd->owner);
			return err;
		}
	}

	/* This just returns 0 if either of the two args is NULL */
	err = v4l2_ctrl_add_handler(v4l2_dev->ctrl_handler, sd->ctrl_handler);
	if (err) {
		if (sd->internal_ops && sd->internal_ops->unregistered)
			sd->internal_ops->unregistered(sd);
		module_put(sd->owner);
		return err;
	}

#if defined(CONFIG_MEDIA_CONTROLLER)
	/* Register the entity. */
	if (v4l2_dev->mdev) {
		err = media_device_register_entity(v4l2_dev->mdev, entity);
		if (err < 0) {
			if (sd->internal_ops && sd->internal_ops->unregistered)
				sd->internal_ops->unregistered(sd);
			module_put(sd->owner);
			return err;
		}
	}
#endif

	spin_lock(&v4l2_dev->lock);
	list_add_tail(&sd->list, &v4l2_dev->subdevs);
	spin_unlock(&v4l2_dev->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_device_register_subdev);

int v4l2_device_register_subdev_nodes(struct v4l2_device *v4l2_dev)
{
	struct video_device *vdev;
	struct v4l2_subdev *sd;
	int err;

	/* Register a device node for every subdev marked with the
	 * V4L2_SUBDEV_FL_HAS_DEVNODE flag.
	 */
	list_for_each_entry(sd, &v4l2_dev->subdevs, list) {
		if (!(sd->flags & V4L2_SUBDEV_FL_HAS_DEVNODE))
			continue;

		vdev = &sd->devnode;
		strlcpy(vdev->name, sd->name, sizeof(vdev->name));
		vdev->v4l2_dev = v4l2_dev;
		vdev->fops = &v4l2_subdev_fops;
		vdev->release = video_device_release_empty;
		err = __video_register_device(vdev, VFL_TYPE_SUBDEV, -1, 1,
					      sd->owner);
		if (err < 0)
			return err;
#if defined(CONFIG_MEDIA_CONTROLLER)
		sd->entity.v4l.major = VIDEO_MAJOR;
		sd->entity.v4l.minor = vdev->minor;
#endif
	}
	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_device_register_subdev_nodes);

void v4l2_device_unregister_subdev(struct v4l2_subdev *sd)
{
	struct v4l2_device *v4l2_dev;

	/* return if it isn't registered */
	if (sd == NULL || sd->v4l2_dev == NULL)
		return;

	v4l2_dev = sd->v4l2_dev;

	spin_lock(&v4l2_dev->lock);
	list_del(&sd->list);
	spin_unlock(&v4l2_dev->lock);

	if (sd->internal_ops && sd->internal_ops->unregistered)
		sd->internal_ops->unregistered(sd);
	sd->v4l2_dev = NULL;

#if defined(CONFIG_MEDIA_CONTROLLER)
	if (v4l2_dev->mdev)
		media_device_unregister_entity(&sd->entity);
#endif
	video_unregister_device(&sd->devnode);
	module_put(sd->owner);
}
EXPORT_SYMBOL_GPL(v4l2_device_unregister_subdev);

/*
 * V4L2 sub-device
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contact: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	    Sakari Ailus <sakari.ailus@iki.fi>
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

#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>

static int subdev_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_subdev *sd = vdev_to_v4l2_subdev(vdev);
#if defined(CONFIG_MEDIA_CONTROLLER)
	struct media_entity *entity;
#endif
	struct v4l2_fh *vfh = NULL;
	int ret;

	if (sd->flags & V4L2_SUBDEV_FL_HAS_EVENTS) {
		vfh = kzalloc(sizeof(*vfh), GFP_KERNEL);
		if (vfh == NULL)
			return -ENOMEM;

		ret = v4l2_fh_init(vfh, vdev);
		if (ret)
			goto err;

		ret = v4l2_event_init(vfh);
		if (ret)
			goto err;

		ret = v4l2_event_alloc(vfh, sd->nevents);
		if (ret)
			goto err;

		v4l2_fh_add(vfh);
		file->private_data = vfh;
	}
#if defined(CONFIG_MEDIA_CONTROLLER)
	if (sd->v4l2_dev->mdev) {
		entity = media_entity_get(&sd->entity);
		if (!entity) {
			ret = -EBUSY;
			goto err;
		}
	}
#endif
	return 0;

err:
	if (vfh != NULL) {
		v4l2_fh_del(vfh);
		v4l2_fh_exit(vfh);
		kfree(vfh);
	}

	return ret;
}

static int subdev_close(struct file *file)
{
#if defined(CONFIG_MEDIA_CONTROLLER)
	struct video_device *vdev = video_devdata(file);
	struct v4l2_subdev *sd = vdev_to_v4l2_subdev(vdev);
#endif
	struct v4l2_fh *vfh = file->private_data;

#if defined(CONFIG_MEDIA_CONTROLLER)
	if (sd->v4l2_dev->mdev)
		media_entity_put(&sd->entity);
#endif
	if (vfh != NULL) {
		v4l2_fh_del(vfh);
		v4l2_fh_exit(vfh);
		kfree(vfh);
	}

	return 0;
}

static long subdev_do_ioctl(struct file *file, unsigned int cmd, void *arg)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_subdev *sd = vdev_to_v4l2_subdev(vdev);
	struct v4l2_fh *fh = file->private_data;

	switch (cmd) {
	case VIDIOC_QUERYCTRL:
		return v4l2_subdev_queryctrl(sd, arg);

	case VIDIOC_QUERYMENU:
		return v4l2_subdev_querymenu(sd, arg);

	case VIDIOC_G_CTRL:
		return v4l2_subdev_g_ctrl(sd, arg);

	case VIDIOC_S_CTRL:
		return v4l2_subdev_s_ctrl(sd, arg);

	case VIDIOC_G_EXT_CTRLS:
		return v4l2_subdev_g_ext_ctrls(sd, arg);

	case VIDIOC_S_EXT_CTRLS:
		return v4l2_subdev_s_ext_ctrls(sd, arg);

	case VIDIOC_TRY_EXT_CTRLS:
		return v4l2_subdev_try_ext_ctrls(sd, arg);

	case VIDIOC_DQEVENT:
		if (!(sd->flags & V4L2_SUBDEV_FL_HAS_EVENTS))
			return -ENOIOCTLCMD;

		return v4l2_event_dequeue(fh, arg, file->f_flags & O_NONBLOCK);

	case VIDIOC_SUBSCRIBE_EVENT:
		return v4l2_subdev_call(sd, core, subscribe_event, fh, arg);

	case VIDIOC_UNSUBSCRIBE_EVENT:
		return v4l2_subdev_call(sd, core, unsubscribe_event, fh, arg);

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static long subdev_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg)
{
	return video_usercopy(file, cmd, arg, subdev_do_ioctl);
}

static unsigned int subdev_poll(struct file *file, poll_table *wait)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_subdev *sd = vdev_to_v4l2_subdev(vdev);
	struct v4l2_fh *fh = file->private_data;

	if (!(sd->flags & V4L2_SUBDEV_FL_HAS_EVENTS))
		return POLLERR;

	poll_wait(file, &fh->events->wait, wait);

	if (v4l2_event_pending(fh))
		return POLLPRI;

	return 0;
}

const struct v4l2_file_operations v4l2_subdev_fops = {
	.owner = THIS_MODULE,
	.open = subdev_open,
	.unlocked_ioctl = subdev_ioctl,
	.release = subdev_close,
	.poll = subdev_poll,
};

void v4l2_subdev_init(struct v4l2_subdev *sd, const struct v4l2_subdev_ops *ops)
{
	INIT_LIST_HEAD(&sd->list);
	BUG_ON(!ops);
	sd->ops = ops;
	sd->v4l2_dev = NULL;
	sd->flags = 0;
	sd->name[0] = '\0';
	sd->grp_id = 0;
	sd->dev_priv = NULL;
	sd->host_priv = NULL;
#if defined(CONFIG_MEDIA_CONTROLLER)
	sd->entity.name = sd->name;
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
#endif
}
EXPORT_SYMBOL(v4l2_subdev_init);

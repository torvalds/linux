// SPDX-License-Identifier: GPL-2.0
/*
 * V4L2 Media Controller Driver for Freescale common i.MX5/6/7 SOC
 *
 * Copyright (c) 2019 Linaro Ltd
 * Copyright (c) 2016 Mentor Graphics Inc.
 */

#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include "imx-media.h"

static inline struct imx_media_dev *notifier2dev(struct v4l2_async_notifier *n)
{
	return container_of(n, struct imx_media_dev, notifier);
}

/* async subdev bound notifier */
static int imx_media_subdev_bound(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *sd,
				  struct v4l2_async_subdev *asd)
{
	struct imx_media_dev *imxmd = notifier2dev(notifier);

	dev_dbg(imxmd->md.dev, "subdev %s bound\n", sd->name);

	return 0;
}

/*
 * Create the missing media links from the CSI-2 receiver.
 * Called after all async subdevs have bound.
 */
static void imx_media_create_csi2_links(struct imx_media_dev *imxmd)
{
	struct v4l2_subdev *sd, *csi2 = NULL;

	list_for_each_entry(sd, &imxmd->v4l2_dev.subdevs, list) {
		if (sd->grp_id == IMX_MEDIA_GRP_ID_CSI2) {
			csi2 = sd;
			break;
		}
	}
	if (!csi2)
		return;

	list_for_each_entry(sd, &imxmd->v4l2_dev.subdevs, list) {
		/* skip if not a CSI or a CSI mux */
		if (!(sd->grp_id & IMX_MEDIA_GRP_ID_IPU_CSI) &&
		    !(sd->grp_id & IMX_MEDIA_GRP_ID_CSI) &&
		    !(sd->grp_id & IMX_MEDIA_GRP_ID_CSI_MUX))
			continue;

		v4l2_create_fwnode_links(csi2, sd);
	}
}

/*
 * adds given video device to given imx-media source pad vdev list.
 * Continues upstream from the pad entity's sink pads.
 */
static int imx_media_add_vdev_to_pad(struct imx_media_dev *imxmd,
				     struct imx_media_video_dev *vdev,
				     struct media_pad *srcpad)
{
	struct media_entity *entity = srcpad->entity;
	struct imx_media_pad_vdev *pad_vdev;
	struct list_head *pad_vdev_list;
	struct media_link *link;
	struct v4l2_subdev *sd;
	int i, ret;

	/* skip this entity if not a v4l2_subdev */
	if (!is_media_entity_v4l2_subdev(entity))
		return 0;

	sd = media_entity_to_v4l2_subdev(entity);

	pad_vdev_list = to_pad_vdev_list(sd, srcpad->index);
	if (!pad_vdev_list) {
		v4l2_warn(&imxmd->v4l2_dev, "%s:%u has no vdev list!\n",
			  entity->name, srcpad->index);
		/*
		 * shouldn't happen, but no reason to fail driver load,
		 * just skip this entity.
		 */
		return 0;
	}

	/* just return if we've been here before */
	list_for_each_entry(pad_vdev, pad_vdev_list, list) {
		if (pad_vdev->vdev == vdev)
			return 0;
	}

	dev_dbg(imxmd->md.dev, "adding %s to pad %s:%u\n",
		vdev->vfd->entity.name, entity->name, srcpad->index);

	pad_vdev = devm_kzalloc(imxmd->md.dev, sizeof(*pad_vdev), GFP_KERNEL);
	if (!pad_vdev)
		return -ENOMEM;

	/* attach this vdev to this pad */
	pad_vdev->vdev = vdev;
	list_add_tail(&pad_vdev->list, pad_vdev_list);

	/* move upstream from this entity's sink pads */
	for (i = 0; i < entity->num_pads; i++) {
		struct media_pad *pad = &entity->pads[i];

		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			continue;

		list_for_each_entry(link, &entity->links, list) {
			if (link->sink != pad)
				continue;
			ret = imx_media_add_vdev_to_pad(imxmd, vdev,
							link->source);
			if (ret)
				return ret;
		}
	}

	return 0;
}

/*
 * For every subdevice, allocate an array of list_head's, one list_head
 * for each pad, to hold the list of video devices reachable from that
 * pad.
 */
static int imx_media_alloc_pad_vdev_lists(struct imx_media_dev *imxmd)
{
	struct list_head *vdev_lists;
	struct media_entity *entity;
	struct v4l2_subdev *sd;
	int i;

	list_for_each_entry(sd, &imxmd->v4l2_dev.subdevs, list) {
		entity = &sd->entity;
		vdev_lists = devm_kcalloc(imxmd->md.dev,
					  entity->num_pads, sizeof(*vdev_lists),
					  GFP_KERNEL);
		if (!vdev_lists)
			return -ENOMEM;

		/* attach to the subdev's host private pointer */
		sd->host_priv = vdev_lists;

		for (i = 0; i < entity->num_pads; i++)
			INIT_LIST_HEAD(to_pad_vdev_list(sd, i));
	}

	return 0;
}

/* form the vdev lists in all imx-media source pads */
static int imx_media_create_pad_vdev_lists(struct imx_media_dev *imxmd)
{
	struct imx_media_video_dev *vdev;
	struct media_link *link;
	int ret;

	ret = imx_media_alloc_pad_vdev_lists(imxmd);
	if (ret)
		return ret;

	list_for_each_entry(vdev, &imxmd->vdev_list, list) {
		link = list_first_entry(&vdev->vfd->entity.links,
					struct media_link, list);
		ret = imx_media_add_vdev_to_pad(imxmd, vdev, link->source);
		if (ret)
			return ret;
	}

	return 0;
}

/* async subdev complete notifier */
int imx_media_probe_complete(struct v4l2_async_notifier *notifier)
{
	struct imx_media_dev *imxmd = notifier2dev(notifier);
	int ret;

	mutex_lock(&imxmd->mutex);

	imx_media_create_csi2_links(imxmd);

	ret = imx_media_create_pad_vdev_lists(imxmd);
	if (ret)
		goto unlock;

	ret = v4l2_device_register_subdev_nodes(&imxmd->v4l2_dev);
unlock:
	mutex_unlock(&imxmd->mutex);
	if (ret)
		return ret;

	return media_device_register(&imxmd->md);
}
EXPORT_SYMBOL_GPL(imx_media_probe_complete);

/*
 * adds controls to a video device from an entity subdevice.
 * Continues upstream from the entity's sink pads.
 */
static int imx_media_inherit_controls(struct imx_media_dev *imxmd,
				      struct video_device *vfd,
				      struct media_entity *entity)
{
	int i, ret = 0;

	if (is_media_entity_v4l2_subdev(entity)) {
		struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);

		dev_dbg(imxmd->md.dev,
			"adding controls to %s from %s\n",
			vfd->entity.name, sd->entity.name);

		ret = v4l2_ctrl_add_handler(vfd->ctrl_handler,
					    sd->ctrl_handler,
					    NULL, true);
		if (ret)
			return ret;
	}

	/* move upstream */
	for (i = 0; i < entity->num_pads; i++) {
		struct media_pad *pad, *spad = &entity->pads[i];

		if (!(spad->flags & MEDIA_PAD_FL_SINK))
			continue;

		pad = media_entity_remote_pad(spad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			continue;

		ret = imx_media_inherit_controls(imxmd, vfd, pad->entity);
		if (ret)
			break;
	}

	return ret;
}

static int imx_media_link_notify(struct media_link *link, u32 flags,
				 unsigned int notification)
{
	struct imx_media_dev *imxmd = container_of(link->graph_obj.mdev,
						   struct imx_media_dev, md);
	struct media_entity *source = link->source->entity;
	struct imx_media_pad_vdev *pad_vdev;
	struct list_head *pad_vdev_list;
	struct video_device *vfd;
	struct v4l2_subdev *sd;
	int pad_idx, ret;

	ret = v4l2_pipeline_link_notify(link, flags, notification);
	if (ret)
		return ret;

	/* don't bother if source is not a subdev */
	if (!is_media_entity_v4l2_subdev(source))
		return 0;

	sd = media_entity_to_v4l2_subdev(source);
	pad_idx = link->source->index;

	pad_vdev_list = to_pad_vdev_list(sd, pad_idx);
	if (!pad_vdev_list) {
		/* nothing to do if source sd has no pad vdev list */
		return 0;
	}

	/*
	 * Before disabling a link, reset controls for all video
	 * devices reachable from this link.
	 *
	 * After enabling a link, refresh controls for all video
	 * devices reachable from this link.
	 */
	if (notification == MEDIA_DEV_NOTIFY_PRE_LINK_CH &&
	    !(flags & MEDIA_LNK_FL_ENABLED)) {
		list_for_each_entry(pad_vdev, pad_vdev_list, list) {
			vfd = pad_vdev->vdev->vfd;
			dev_dbg(imxmd->md.dev,
				"reset controls for %s\n",
				vfd->entity.name);
			v4l2_ctrl_handler_free(vfd->ctrl_handler);
			v4l2_ctrl_handler_init(vfd->ctrl_handler, 0);
		}
	} else if (notification == MEDIA_DEV_NOTIFY_POST_LINK_CH &&
		   (link->flags & MEDIA_LNK_FL_ENABLED)) {
		list_for_each_entry(pad_vdev, pad_vdev_list, list) {
			vfd = pad_vdev->vdev->vfd;
			dev_dbg(imxmd->md.dev,
				"refresh controls for %s\n",
				vfd->entity.name);
			ret = imx_media_inherit_controls(imxmd, vfd,
							 &vfd->entity);
			if (ret)
				break;
		}
	}

	return ret;
}

static void imx_media_notify(struct v4l2_subdev *sd, unsigned int notification,
			     void *arg)
{
	struct media_entity *entity = &sd->entity;
	int i;

	if (notification != V4L2_DEVICE_NOTIFY_EVENT)
		return;

	for (i = 0; i < entity->num_pads; i++) {
		struct media_pad *pad = &entity->pads[i];
		struct imx_media_pad_vdev *pad_vdev;
		struct list_head *pad_vdev_list;

		pad_vdev_list = to_pad_vdev_list(sd, pad->index);
		if (!pad_vdev_list)
			continue;
		list_for_each_entry(pad_vdev, pad_vdev_list, list)
			v4l2_event_queue(pad_vdev->vdev->vfd, arg);
	}
}

static const struct v4l2_async_notifier_operations imx_media_notifier_ops = {
	.bound = imx_media_subdev_bound,
	.complete = imx_media_probe_complete,
};

static const struct media_device_ops imx_media_md_ops = {
	.link_notify = imx_media_link_notify,
};

struct imx_media_dev *imx_media_dev_init(struct device *dev,
					 const struct media_device_ops *ops)
{
	struct imx_media_dev *imxmd;
	int ret;

	imxmd = devm_kzalloc(dev, sizeof(*imxmd), GFP_KERNEL);
	if (!imxmd)
		return ERR_PTR(-ENOMEM);

	dev_set_drvdata(dev, imxmd);

	strscpy(imxmd->md.model, "imx-media", sizeof(imxmd->md.model));
	imxmd->md.ops = ops ? ops : &imx_media_md_ops;
	imxmd->md.dev = dev;

	mutex_init(&imxmd->mutex);

	imxmd->v4l2_dev.mdev = &imxmd->md;
	imxmd->v4l2_dev.notify = imx_media_notify;
	strscpy(imxmd->v4l2_dev.name, "imx-media",
		sizeof(imxmd->v4l2_dev.name));

	media_device_init(&imxmd->md);

	ret = v4l2_device_register(dev, &imxmd->v4l2_dev);
	if (ret < 0) {
		v4l2_err(&imxmd->v4l2_dev,
			 "Failed to register v4l2_device: %d\n", ret);
		goto cleanup;
	}

	INIT_LIST_HEAD(&imxmd->vdev_list);

	v4l2_async_notifier_init(&imxmd->notifier);

	return imxmd;

cleanup:
	media_device_cleanup(&imxmd->md);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(imx_media_dev_init);

int imx_media_dev_notifier_register(struct imx_media_dev *imxmd,
			    const struct v4l2_async_notifier_operations *ops)
{
	int ret;

	/* no subdevs? just bail */
	if (list_empty(&imxmd->notifier.asd_list)) {
		v4l2_err(&imxmd->v4l2_dev, "no subdevs\n");
		return -ENODEV;
	}

	/* prepare the async subdev notifier and register it */
	imxmd->notifier.ops = ops ? ops : &imx_media_notifier_ops;
	ret = v4l2_async_notifier_register(&imxmd->v4l2_dev,
					   &imxmd->notifier);
	if (ret) {
		v4l2_err(&imxmd->v4l2_dev,
			 "v4l2_async_notifier_register failed with %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(imx_media_dev_notifier_register);

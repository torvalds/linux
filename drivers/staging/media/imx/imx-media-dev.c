/*
 * V4L2 Media Controller Driver for Freescale i.MX5/6 SOC
 *
 * Copyright (c) 2016 Mentor Graphics Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <video/imx-ipu-v3.h>
#include <media/imx.h>
#include "imx-media.h"

static inline struct imx_media_dev *notifier2dev(struct v4l2_async_notifier *n)
{
	return container_of(n, struct imx_media_dev, notifier);
}

/*
 * Adds a subdev to the root notifier's async subdev list. If fwnode is
 * non-NULL, adds the async as a V4L2_ASYNC_MATCH_FWNODE match type,
 * otherwise as a V4L2_ASYNC_MATCH_DEVNAME match type using the dev_name
 * of the given platform_device. This is called during driver load when
 * forming the async subdev list.
 */
int imx_media_add_async_subdev(struct imx_media_dev *imxmd,
			       struct fwnode_handle *fwnode,
			       struct platform_device *pdev)
{
	struct device_node *np = to_of_node(fwnode);
	struct imx_media_async_subdev *imxasd;
	struct v4l2_async_subdev *asd;
	const char *devname = NULL;
	int ret;

	if (fwnode) {
		asd = v4l2_async_notifier_add_fwnode_subdev(&imxmd->notifier,
							    fwnode,
							    sizeof(*imxasd));
	} else {
		devname = dev_name(&pdev->dev);
		asd = v4l2_async_notifier_add_devname_subdev(&imxmd->notifier,
							     devname,
							     sizeof(*imxasd));
	}

	if (IS_ERR(asd)) {
		ret = PTR_ERR(asd);
		if (ret == -EEXIST) {
			if (np)
				dev_dbg(imxmd->md.dev, "%s: already added %pOFn\n",
					__func__, np);
			else
				dev_dbg(imxmd->md.dev, "%s: already added %s\n",
					__func__, devname);
		}
		return ret;
	}

	imxasd = to_imx_media_asd(asd);

	if (devname)
		imxasd->pdev = pdev;

	if (np)
		dev_dbg(imxmd->md.dev, "%s: added %pOFn, match type FWNODE\n",
			__func__, np);
	else
		dev_dbg(imxmd->md.dev, "%s: added %s, match type DEVNAME\n",
			__func__, devname);

	return 0;
}

/*
 * get IPU from this CSI and add it to the list of IPUs
 * the media driver will control.
 */
static int imx_media_get_ipu(struct imx_media_dev *imxmd,
			     struct v4l2_subdev *csi_sd)
{
	struct ipu_soc *ipu;
	int ipu_id;

	ipu = dev_get_drvdata(csi_sd->dev->parent);
	if (!ipu) {
		v4l2_err(&imxmd->v4l2_dev,
			 "CSI %s has no parent IPU!\n", csi_sd->name);
		return -ENODEV;
	}

	ipu_id = ipu_get_num(ipu);
	if (ipu_id > 1) {
		v4l2_err(&imxmd->v4l2_dev, "invalid IPU id %d!\n", ipu_id);
		return -ENODEV;
	}

	if (!imxmd->ipu[ipu_id])
		imxmd->ipu[ipu_id] = ipu;

	return 0;
}

/* async subdev bound notifier */
int imx_media_subdev_bound(struct v4l2_async_notifier *notifier,
			   struct v4l2_subdev *sd,
			   struct v4l2_async_subdev *asd)
{
	struct imx_media_dev *imxmd = notifier2dev(notifier);
	int ret = 0;

	mutex_lock(&imxmd->mutex);

	if (sd->grp_id & IMX_MEDIA_GRP_ID_IPU_CSI) {
		ret = imx_media_get_ipu(imxmd, sd);
		if (ret)
			goto out;
	}

	v4l2_info(&imxmd->v4l2_dev, "subdev %s bound\n", sd->name);
out:
	mutex_unlock(&imxmd->mutex);
	return ret;
}

/*
 * Create the media links for all subdevs that registered.
 * Called after all async subdevs have bound.
 */
static int imx_media_create_links(struct v4l2_async_notifier *notifier)
{
	struct imx_media_dev *imxmd = notifier2dev(notifier);
	struct v4l2_subdev *sd;
	int ret;

	list_for_each_entry(sd, &imxmd->v4l2_dev.subdevs, list) {
		switch (sd->grp_id) {
		case IMX_MEDIA_GRP_ID_IPU_VDIC:
		case IMX_MEDIA_GRP_ID_IPU_IC_PRP:
		case IMX_MEDIA_GRP_ID_IPU_IC_PRPENC:
		case IMX_MEDIA_GRP_ID_IPU_IC_PRPVF:
		case IMX_MEDIA_GRP_ID_IPU_CSI0:
		case IMX_MEDIA_GRP_ID_IPU_CSI1:
			ret = imx_media_create_ipu_internal_links(imxmd, sd);
			if (ret)
				return ret;
			/*
			 * the CSIs straddle between the external and the IPU
			 * internal entities, so create the external links
			 * to the CSI sink pads.
			 */
			if (sd->grp_id & IMX_MEDIA_GRP_ID_IPU_CSI)
				imx_media_create_csi_of_links(imxmd, sd);
			break;
		case IMX_MEDIA_GRP_ID_CSI:
			imx_media_create_csi_of_links(imxmd, sd);

			break;
		default:
			/*
			 * if this subdev has fwnode links, create media
			 * links for them.
			 */
			imx_media_create_of_links(imxmd, sd);
			break;
		}
	}

	return 0;
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

	ret = imx_media_create_links(notifier);
	if (ret)
		goto unlock;

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

int imx_media_link_notify(struct media_link *link, u32 flags,
			  unsigned int notification)
{
	struct media_entity *source = link->source->entity;
	struct imx_media_pad_vdev *pad_vdev;
	struct list_head *pad_vdev_list;
	struct imx_media_dev *imxmd;
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

	imxmd = dev_get_drvdata(sd->v4l2_dev->dev);

	pad_vdev_list = to_pad_vdev_list(sd, pad_idx);
	if (!pad_vdev_list) {
		/* shouldn't happen, but no reason to fail link setup */
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

void imx_media_notify(struct v4l2_subdev *sd, unsigned int notification,
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

static int imx_media_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct imx_media_dev *imxmd;
	int ret;

	imxmd = imx_media_dev_init(dev);
	if (IS_ERR(imxmd))
		return PTR_ERR(imxmd);

	ret = imx_media_add_of_subdevs(imxmd, node);
	if (ret) {
		v4l2_err(&imxmd->v4l2_dev,
			 "add_of_subdevs failed with %d\n", ret);
		goto cleanup;
	}

	ret = imx_media_dev_notifier_register(imxmd);
	if (ret)
		goto del_int;

	return 0;

del_int:
	imx_media_remove_ipu_internal_subdevs(imxmd);
cleanup:
	v4l2_async_notifier_cleanup(&imxmd->notifier);
	v4l2_device_unregister(&imxmd->v4l2_dev);
	media_device_cleanup(&imxmd->md);

	return ret;
}

static int imx_media_remove(struct platform_device *pdev)
{
	struct imx_media_dev *imxmd =
		(struct imx_media_dev *)platform_get_drvdata(pdev);

	v4l2_info(&imxmd->v4l2_dev, "Removing imx-media\n");

	v4l2_async_notifier_unregister(&imxmd->notifier);
	imx_media_remove_ipu_internal_subdevs(imxmd);
	v4l2_async_notifier_cleanup(&imxmd->notifier);
	media_device_unregister(&imxmd->md);
	v4l2_device_unregister(&imxmd->v4l2_dev);
	media_device_cleanup(&imxmd->md);

	return 0;
}

static const struct of_device_id imx_media_dt_ids[] = {
	{ .compatible = "fsl,imx-capture-subsystem" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_media_dt_ids);

static struct platform_driver imx_media_pdrv = {
	.probe		= imx_media_probe,
	.remove		= imx_media_remove,
	.driver		= {
		.name	= "imx-media",
		.of_match_table	= imx_media_dt_ids,
	},
};

module_platform_driver(imx_media_pdrv);

MODULE_DESCRIPTION("i.MX5/6 v4l2 media controller driver");
MODULE_AUTHOR("Steve Longerbeam <steve_longerbeam@mentor.com>");
MODULE_LICENSE("GPL");

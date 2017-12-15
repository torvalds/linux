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
	return container_of(n, struct imx_media_dev, subdev_notifier);
}

/*
 * Find a subdev by device node or device name. This is called during
 * driver load to form the async subdev list and bind them.
 */
struct imx_media_subdev *
imx_media_find_async_subdev(struct imx_media_dev *imxmd,
			    struct device_node *np,
			    const char *devname)
{
	struct fwnode_handle *fwnode = np ? of_fwnode_handle(np) : NULL;
	struct imx_media_subdev *imxsd;
	int i;

	for (i = 0; i < imxmd->subdev_notifier.num_subdevs; i++) {
		imxsd = &imxmd->subdev[i];
		switch (imxsd->asd.match_type) {
		case V4L2_ASYNC_MATCH_FWNODE:
			if (fwnode && imxsd->asd.match.fwnode.fwnode == fwnode)
				return imxsd;
			break;
		case V4L2_ASYNC_MATCH_DEVNAME:
			if (devname &&
			    !strcmp(imxsd->asd.match.device_name.name, devname))
				return imxsd;
			break;
		default:
			break;
		}
	}

	return NULL;
}


/*
 * Adds a subdev to the async subdev list. If np is non-NULL, adds
 * the async as a V4L2_ASYNC_MATCH_FWNODE match type, otherwise as
 * a V4L2_ASYNC_MATCH_DEVNAME match type using the dev_name of the
 * given platform_device. This is called during driver load when
 * forming the async subdev list.
 */
struct imx_media_subdev *
imx_media_add_async_subdev(struct imx_media_dev *imxmd,
			   struct device_node *np,
			   struct platform_device *pdev)
{
	struct imx_media_subdev *imxsd;
	struct v4l2_async_subdev *asd;
	const char *devname = NULL;
	int sd_idx;

	mutex_lock(&imxmd->mutex);

	if (pdev)
		devname = dev_name(&pdev->dev);

	/* return -EEXIST if this subdev already added */
	if (imx_media_find_async_subdev(imxmd, np, devname)) {
		dev_dbg(imxmd->md.dev, "%s: already added %s\n",
			__func__, np ? np->name : devname);
		imxsd = ERR_PTR(-EEXIST);
		goto out;
	}

	sd_idx = imxmd->subdev_notifier.num_subdevs;
	if (sd_idx >= IMX_MEDIA_MAX_SUBDEVS) {
		dev_err(imxmd->md.dev, "%s: too many subdevs! can't add %s\n",
			__func__, np ? np->name : devname);
		imxsd = ERR_PTR(-ENOSPC);
		goto out;
	}

	imxsd = &imxmd->subdev[sd_idx];

	asd = &imxsd->asd;
	if (np) {
		asd->match_type = V4L2_ASYNC_MATCH_FWNODE;
		asd->match.fwnode.fwnode = of_fwnode_handle(np);
	} else {
		asd->match_type = V4L2_ASYNC_MATCH_DEVNAME;
		strncpy(imxsd->devname, devname, sizeof(imxsd->devname));
		asd->match.device_name.name = imxsd->devname;
		imxsd->pdev = pdev;
	}

	imxmd->async_ptrs[sd_idx] = asd;
	imxmd->subdev_notifier.num_subdevs++;

	dev_dbg(imxmd->md.dev, "%s: added %s, match type %s\n",
		__func__, np ? np->name : devname, np ? "FWNODE" : "DEVNAME");

out:
	mutex_unlock(&imxmd->mutex);
	return imxsd;
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
static int imx_media_subdev_bound(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *sd,
				  struct v4l2_async_subdev *asd)
{
	struct imx_media_dev *imxmd = notifier2dev(notifier);
	struct device_node *np = to_of_node(sd->fwnode);
	struct imx_media_subdev *imxsd;
	int ret = 0;

	mutex_lock(&imxmd->mutex);

	imxsd = imx_media_find_async_subdev(imxmd, np, dev_name(sd->dev));
	if (!imxsd) {
		ret = -EINVAL;
		goto out;
	}

	if (sd->grp_id & IMX_MEDIA_GRP_ID_CSI) {
		ret = imx_media_get_ipu(imxmd, sd);
		if (ret)
			goto out_unlock;
	}

	/* attach the subdev */
	imxsd->sd = sd;
out:
	if (ret)
		v4l2_warn(&imxmd->v4l2_dev,
			  "Received unknown subdev %s\n", sd->name);
	else
		v4l2_info(&imxmd->v4l2_dev,
			  "Registered subdev %s\n", sd->name);

out_unlock:
	mutex_unlock(&imxmd->mutex);
	return ret;
}

/*
 * create the media links from all pads and their links.
 * Called after all subdevs have registered.
 */
static int imx_media_create_links(struct imx_media_dev *imxmd)
{
	struct imx_media_subdev *imxsd;
	struct v4l2_subdev *sd;
	int i, ret;

	for (i = 0; i < imxmd->num_subdevs; i++) {
		imxsd = &imxmd->subdev[i];
		sd = imxsd->sd;

		if (((sd->grp_id & IMX_MEDIA_GRP_ID_CSI) || imxsd->pdev)) {
			/* this is an internal subdev or a CSI */
			ret = imx_media_create_internal_links(imxmd, imxsd);
			if (ret)
				return ret;
			/*
			 * the CSIs straddle between the external and the IPU
			 * internal entities, so create the external links
			 * to the CSI sink pads.
			 */
			if (sd->grp_id & IMX_MEDIA_GRP_ID_CSI)
				imx_media_create_csi_of_links(imxmd, imxsd);
		} else {
			/* this is an external fwnode subdev */
			imx_media_create_of_links(imxmd, imxsd);
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
	struct imx_media_subdev *imxsd;
	struct imx_media_pad *imxpad;
	struct media_link *link;
	struct v4l2_subdev *sd;
	int i, vdev_idx, ret;

	/* skip this entity if not a v4l2_subdev */
	if (!is_media_entity_v4l2_subdev(entity))
		return 0;

	sd = media_entity_to_v4l2_subdev(entity);
	imxsd = imx_media_find_subdev_by_sd(imxmd, sd);
	if (IS_ERR(imxsd)) {
		v4l2_err(&imxmd->v4l2_dev, "failed to find subdev for entity %s, sd %p err %ld\n",
			 entity->name, sd, PTR_ERR(imxsd));
		return 0;
	}

	imxpad = &imxsd->pad[srcpad->index];
	vdev_idx = imxpad->num_vdevs;

	/* just return if we've been here before */
	for (i = 0; i < vdev_idx; i++)
		if (vdev == imxpad->vdev[i])
			return 0;

	if (vdev_idx >= IMX_MEDIA_MAX_VDEVS) {
		dev_err(imxmd->md.dev, "can't add %s to pad %s:%u\n",
			vdev->vfd->entity.name, entity->name, srcpad->index);
		return -ENOSPC;
	}

	dev_dbg(imxmd->md.dev, "adding %s to pad %s:%u\n",
		vdev->vfd->entity.name, entity->name, srcpad->index);
	imxpad->vdev[vdev_idx] = vdev;
	imxpad->num_vdevs++;

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

/* form the vdev lists in all imx-media source pads */
static int imx_media_create_pad_vdev_lists(struct imx_media_dev *imxmd)
{
	struct imx_media_video_dev *vdev;
	struct media_link *link;
	int i, ret;

	for (i = 0; i < imxmd->num_vdevs; i++) {
		vdev = imxmd->vdev[i];
		link = list_first_entry(&vdev->vfd->entity.links,
					struct media_link, list);
		ret = imx_media_add_vdev_to_pad(imxmd, vdev, link->source);
		if (ret)
			return ret;
	}

	return 0;
}

/* async subdev complete notifier */
static int imx_media_probe_complete(struct v4l2_async_notifier *notifier)
{
	struct imx_media_dev *imxmd = notifier2dev(notifier);
	int i, ret;

	mutex_lock(&imxmd->mutex);

	/* make sure all subdevs were bound */
	for (i = 0; i < imxmd->num_subdevs; i++) {
		if (!imxmd->subdev[i].sd) {
			v4l2_err(&imxmd->v4l2_dev, "unbound subdev!\n");
			ret = -ENODEV;
			goto unlock;
		}
	}

	ret = imx_media_create_links(imxmd);
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

static const struct v4l2_async_notifier_operations imx_media_subdev_ops = {
	.bound = imx_media_subdev_bound,
	.complete = imx_media_probe_complete,
};

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
					    NULL);
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
	struct media_entity *source = link->source->entity;
	struct imx_media_subdev *imxsd;
	struct imx_media_pad *imxpad;
	struct imx_media_dev *imxmd;
	struct video_device *vfd;
	struct v4l2_subdev *sd;
	int i, pad_idx, ret;

	ret = v4l2_pipeline_link_notify(link, flags, notification);
	if (ret)
		return ret;

	/* don't bother if source is not a subdev */
	if (!is_media_entity_v4l2_subdev(source))
		return 0;

	sd = media_entity_to_v4l2_subdev(source);
	pad_idx = link->source->index;

	imxmd = dev_get_drvdata(sd->v4l2_dev->dev);

	imxsd = imx_media_find_subdev_by_sd(imxmd, sd);
	if (IS_ERR(imxsd))
		return PTR_ERR(imxsd);
	imxpad = &imxsd->pad[pad_idx];

	/*
	 * Before disabling a link, reset controls for all video
	 * devices reachable from this link.
	 *
	 * After enabling a link, refresh controls for all video
	 * devices reachable from this link.
	 */
	if (notification == MEDIA_DEV_NOTIFY_PRE_LINK_CH &&
	    !(flags & MEDIA_LNK_FL_ENABLED)) {
		for (i = 0; i < imxpad->num_vdevs; i++) {
			vfd = imxpad->vdev[i]->vfd;
			dev_dbg(imxmd->md.dev,
				"reset controls for %s\n",
				vfd->entity.name);
			v4l2_ctrl_handler_free(vfd->ctrl_handler);
			v4l2_ctrl_handler_init(vfd->ctrl_handler, 0);
		}
	} else if (notification == MEDIA_DEV_NOTIFY_POST_LINK_CH &&
		   (link->flags & MEDIA_LNK_FL_ENABLED)) {
		for (i = 0; i < imxpad->num_vdevs; i++) {
			vfd = imxpad->vdev[i]->vfd;
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

static const struct media_device_ops imx_media_md_ops = {
	.link_notify = imx_media_link_notify,
};

static int imx_media_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct imx_media_dev *imxmd;
	int ret;

	imxmd = devm_kzalloc(dev, sizeof(*imxmd), GFP_KERNEL);
	if (!imxmd)
		return -ENOMEM;

	dev_set_drvdata(dev, imxmd);

	strlcpy(imxmd->md.model, "imx-media", sizeof(imxmd->md.model));
	imxmd->md.ops = &imx_media_md_ops;
	imxmd->md.dev = dev;

	mutex_init(&imxmd->mutex);

	imxmd->v4l2_dev.mdev = &imxmd->md;
	strlcpy(imxmd->v4l2_dev.name, "imx-media",
		sizeof(imxmd->v4l2_dev.name));

	media_device_init(&imxmd->md);

	ret = v4l2_device_register(dev, &imxmd->v4l2_dev);
	if (ret < 0) {
		v4l2_err(&imxmd->v4l2_dev,
			 "Failed to register v4l2_device: %d\n", ret);
		goto cleanup;
	}

	dev_set_drvdata(imxmd->v4l2_dev.dev, imxmd);

	ret = imx_media_add_of_subdevs(imxmd, node);
	if (ret) {
		v4l2_err(&imxmd->v4l2_dev,
			 "add_of_subdevs failed with %d\n", ret);
		goto unreg_dev;
	}

	ret = imx_media_add_internal_subdevs(imxmd);
	if (ret) {
		v4l2_err(&imxmd->v4l2_dev,
			 "add_internal_subdevs failed with %d\n", ret);
		goto unreg_dev;
	}

	/* no subdevs? just bail */
	imxmd->num_subdevs = imxmd->subdev_notifier.num_subdevs;
	if (imxmd->num_subdevs == 0) {
		ret = -ENODEV;
		goto unreg_dev;
	}

	/* prepare the async subdev notifier and register it */
	imxmd->subdev_notifier.subdevs = imxmd->async_ptrs;
	imxmd->subdev_notifier.ops = &imx_media_subdev_ops;
	ret = v4l2_async_notifier_register(&imxmd->v4l2_dev,
					   &imxmd->subdev_notifier);
	if (ret) {
		v4l2_err(&imxmd->v4l2_dev,
			 "v4l2_async_notifier_register failed with %d\n", ret);
		goto del_int;
	}

	return 0;

del_int:
	imx_media_remove_internal_subdevs(imxmd);
unreg_dev:
	v4l2_device_unregister(&imxmd->v4l2_dev);
cleanup:
	media_device_cleanup(&imxmd->md);
	return ret;
}

static int imx_media_remove(struct platform_device *pdev)
{
	struct imx_media_dev *imxmd =
		(struct imx_media_dev *)platform_get_drvdata(pdev);

	v4l2_info(&imxmd->v4l2_dev, "Removing imx-media\n");

	v4l2_async_notifier_unregister(&imxmd->subdev_notifier);
	imx_media_remove_internal_subdevs(imxmd);
	v4l2_device_unregister(&imxmd->v4l2_dev);
	media_device_unregister(&imxmd->md);
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

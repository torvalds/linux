/*
 * Driver for Renesas R-Car VIN
 *
 * Copyright (C) 2016 Renesas Electronics Corp.
 * Copyright (C) 2011-2013 Renesas Solutions Corp.
 * Copyright (C) 2013 Cogent Embedded, Inc., <source@cogentembedded.com>
 * Copyright (C) 2008 Magnus Damm
 *
 * Based on the soc-camera rcar_vin driver
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <media/v4l2-async.h>
#include <media/v4l2-fwnode.h>

#include "rcar-vin.h"

/* -----------------------------------------------------------------------------
 * Gen3 CSI2 Group Allocator
 */

/* FIXME:  This should if we find a system that supports more
 * than one group for the whole system be replaced with a linked
 * list of groups. And eventually all of this should be replaced
 * with a global device allocator API.
 *
 * But for now this works as on all supported systems there will
 * be only one group for all instances.
 */

static DEFINE_MUTEX(rvin_group_lock);
static struct rvin_group *rvin_group_data;

static void rvin_group_cleanup(struct rvin_group *group)
{
	media_device_unregister(&group->mdev);
	media_device_cleanup(&group->mdev);
	mutex_destroy(&group->lock);
}

static int rvin_group_init(struct rvin_group *group, struct rvin_dev *vin)
{
	struct media_device *mdev = &group->mdev;
	const struct of_device_id *match;
	struct device_node *np;
	int ret;

	mutex_init(&group->lock);

	/* Count number of VINs in the system */
	group->count = 0;
	for_each_matching_node(np, vin->dev->driver->of_match_table)
		if (of_device_is_available(np))
			group->count++;

	vin_dbg(vin, "found %u enabled VIN's in DT", group->count);

	mdev->dev = vin->dev;

	match = of_match_node(vin->dev->driver->of_match_table,
			      vin->dev->of_node);

	strlcpy(mdev->driver_name, KBUILD_MODNAME, sizeof(mdev->driver_name));
	strlcpy(mdev->model, match->compatible, sizeof(mdev->model));
	snprintf(mdev->bus_info, sizeof(mdev->bus_info), "platform:%s",
		 dev_name(mdev->dev));

	media_device_init(mdev);

	ret = media_device_register(&group->mdev);
	if (ret)
		rvin_group_cleanup(group);

	return ret;
}

static void rvin_group_release(struct kref *kref)
{
	struct rvin_group *group =
		container_of(kref, struct rvin_group, refcount);

	mutex_lock(&rvin_group_lock);

	rvin_group_data = NULL;

	rvin_group_cleanup(group);

	kfree(group);

	mutex_unlock(&rvin_group_lock);
}

static int rvin_group_get(struct rvin_dev *vin)
{
	struct rvin_group *group;
	u32 id;
	int ret;

	/* Make sure VIN id is present and sane */
	ret = of_property_read_u32(vin->dev->of_node, "renesas,id", &id);
	if (ret) {
		vin_err(vin, "%pOF: No renesas,id property found\n",
			vin->dev->of_node);
		return -EINVAL;
	}

	if (id >= RCAR_VIN_NUM) {
		vin_err(vin, "%pOF: Invalid renesas,id '%u'\n",
			vin->dev->of_node, id);
		return -EINVAL;
	}

	/* Join or create a VIN group */
	mutex_lock(&rvin_group_lock);
	if (rvin_group_data) {
		group = rvin_group_data;
		kref_get(&group->refcount);
	} else {
		group = kzalloc(sizeof(*group), GFP_KERNEL);
		if (!group) {
			ret = -ENOMEM;
			goto err_group;
		}

		ret = rvin_group_init(group, vin);
		if (ret) {
			kfree(group);
			vin_err(vin, "Failed to initialize group\n");
			goto err_group;
		}

		kref_init(&group->refcount);

		rvin_group_data = group;
	}
	mutex_unlock(&rvin_group_lock);

	/* Add VIN to group */
	mutex_lock(&group->lock);

	if (group->vin[id]) {
		vin_err(vin, "Duplicate renesas,id property value %u\n", id);
		mutex_unlock(&group->lock);
		kref_put(&group->refcount, rvin_group_release);
		return -EINVAL;
	}

	group->vin[id] = vin;

	vin->id = id;
	vin->group = group;
	vin->v4l2_dev.mdev = &group->mdev;

	mutex_unlock(&group->lock);

	return 0;
err_group:
	mutex_unlock(&rvin_group_lock);
	return ret;
}

static void rvin_group_put(struct rvin_dev *vin)
{
	mutex_lock(&vin->group->lock);

	vin->group = NULL;
	vin->v4l2_dev.mdev = NULL;

	if (WARN_ON(vin->group->vin[vin->id] != vin))
		goto out;

	vin->group->vin[vin->id] = NULL;
out:
	mutex_unlock(&vin->group->lock);

	kref_put(&vin->group->refcount, rvin_group_release);
}

/* -----------------------------------------------------------------------------
 * Async notifier
 */

#define notifier_to_vin(n) container_of(n, struct rvin_dev, notifier)

static int rvin_find_pad(struct v4l2_subdev *sd, int direction)
{
	unsigned int pad;

	if (sd->entity.num_pads <= 1)
		return 0;

	for (pad = 0; pad < sd->entity.num_pads; pad++)
		if (sd->entity.pads[pad].flags & direction)
			return pad;

	return -EINVAL;
}

/* -----------------------------------------------------------------------------
 * Digital async notifier
 */

/* The vin lock should be held when calling the subdevice attach and detach */
static int rvin_digital_subdevice_attach(struct rvin_dev *vin,
					 struct v4l2_subdev *subdev)
{
	struct v4l2_subdev_mbus_code_enum code = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	/* Find source and sink pad of remote subdevice */
	ret = rvin_find_pad(subdev, MEDIA_PAD_FL_SOURCE);
	if (ret < 0)
		return ret;
	vin->digital->source_pad = ret;

	ret = rvin_find_pad(subdev, MEDIA_PAD_FL_SINK);
	vin->digital->sink_pad = ret < 0 ? 0 : ret;

	/* Find compatible subdevices mbus format */
	vin->mbus_code = 0;
	code.index = 0;
	code.pad = vin->digital->source_pad;
	while (!vin->mbus_code &&
	       !v4l2_subdev_call(subdev, pad, enum_mbus_code, NULL, &code)) {
		code.index++;
		switch (code.code) {
		case MEDIA_BUS_FMT_YUYV8_1X16:
		case MEDIA_BUS_FMT_UYVY8_2X8:
		case MEDIA_BUS_FMT_UYVY10_2X10:
		case MEDIA_BUS_FMT_RGB888_1X24:
			vin->mbus_code = code.code;
			vin_dbg(vin, "Found media bus format for %s: %d\n",
				subdev->name, vin->mbus_code);
			break;
		default:
			break;
		}
	}

	if (!vin->mbus_code) {
		vin_err(vin, "Unsupported media bus format for %s\n",
			subdev->name);
		return -EINVAL;
	}

	/* Read tvnorms */
	ret = v4l2_subdev_call(subdev, video, g_tvnorms, &vin->vdev.tvnorms);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		return ret;

	/* Read standard */
	vin->std = V4L2_STD_UNKNOWN;
	ret = v4l2_subdev_call(subdev, video, g_std, &vin->std);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		return ret;

	/* Add the controls */
	ret = v4l2_ctrl_handler_init(&vin->ctrl_handler, 16);
	if (ret < 0)
		return ret;

	ret = v4l2_ctrl_add_handler(&vin->ctrl_handler, subdev->ctrl_handler,
				    NULL);
	if (ret < 0) {
		v4l2_ctrl_handler_free(&vin->ctrl_handler);
		return ret;
	}

	vin->vdev.ctrl_handler = &vin->ctrl_handler;

	vin->digital->subdev = subdev;

	return 0;
}

static void rvin_digital_subdevice_detach(struct rvin_dev *vin)
{
	rvin_v4l2_unregister(vin);
	v4l2_ctrl_handler_free(&vin->ctrl_handler);

	vin->vdev.ctrl_handler = NULL;
	vin->digital->subdev = NULL;
}

static int rvin_digital_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct rvin_dev *vin = notifier_to_vin(notifier);
	int ret;

	ret = v4l2_device_register_subdev_nodes(&vin->v4l2_dev);
	if (ret < 0) {
		vin_err(vin, "Failed to register subdev nodes\n");
		return ret;
	}

	return rvin_v4l2_register(vin);
}

static void rvin_digital_notify_unbind(struct v4l2_async_notifier *notifier,
				       struct v4l2_subdev *subdev,
				       struct v4l2_async_subdev *asd)
{
	struct rvin_dev *vin = notifier_to_vin(notifier);

	vin_dbg(vin, "unbind digital subdev %s\n", subdev->name);

	mutex_lock(&vin->lock);
	rvin_digital_subdevice_detach(vin);
	mutex_unlock(&vin->lock);
}

static int rvin_digital_notify_bound(struct v4l2_async_notifier *notifier,
				     struct v4l2_subdev *subdev,
				     struct v4l2_async_subdev *asd)
{
	struct rvin_dev *vin = notifier_to_vin(notifier);
	int ret;

	mutex_lock(&vin->lock);
	ret = rvin_digital_subdevice_attach(vin, subdev);
	mutex_unlock(&vin->lock);
	if (ret)
		return ret;

	v4l2_set_subdev_hostdata(subdev, vin);

	vin_dbg(vin, "bound subdev %s source pad: %u sink pad: %u\n",
		subdev->name, vin->digital->source_pad,
		vin->digital->sink_pad);

	return 0;
}

static const struct v4l2_async_notifier_operations rvin_digital_notify_ops = {
	.bound = rvin_digital_notify_bound,
	.unbind = rvin_digital_notify_unbind,
	.complete = rvin_digital_notify_complete,
};

static int rvin_digital_parse_v4l2(struct device *dev,
				   struct v4l2_fwnode_endpoint *vep,
				   struct v4l2_async_subdev *asd)
{
	struct rvin_dev *vin = dev_get_drvdata(dev);
	struct rvin_graph_entity *rvge =
		container_of(asd, struct rvin_graph_entity, asd);

	if (vep->base.port || vep->base.id)
		return -ENOTCONN;

	vin->mbus_cfg.type = vep->bus_type;

	switch (vin->mbus_cfg.type) {
	case V4L2_MBUS_PARALLEL:
		vin_dbg(vin, "Found PARALLEL media bus\n");
		vin->mbus_cfg.flags = vep->bus.parallel.flags;
		break;
	case V4L2_MBUS_BT656:
		vin_dbg(vin, "Found BT656 media bus\n");
		vin->mbus_cfg.flags = 0;
		break;
	default:
		vin_err(vin, "Unknown media bus type\n");
		return -EINVAL;
	}

	vin->digital = rvge;

	return 0;
}

static int rvin_digital_graph_init(struct rvin_dev *vin)
{
	int ret;

	ret = v4l2_async_notifier_parse_fwnode_endpoints(
		vin->dev, &vin->notifier,
		sizeof(struct rvin_graph_entity), rvin_digital_parse_v4l2);
	if (ret)
		return ret;

	if (!vin->digital)
		return -ENODEV;

	vin_dbg(vin, "Found digital subdevice %pOF\n",
		to_of_node(vin->digital->asd.match.fwnode));

	vin->notifier.ops = &rvin_digital_notify_ops;
	ret = v4l2_async_notifier_register(&vin->v4l2_dev, &vin->notifier);
	if (ret < 0) {
		vin_err(vin, "Notifier registration failed\n");
		return ret;
	}

	return 0;
}

static int rvin_mc_init(struct rvin_dev *vin)
{
	int ret;

	/* All our sources are CSI-2 */
	vin->mbus_cfg.type = V4L2_MBUS_CSI2;
	vin->mbus_cfg.flags = 0;

	vin->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vin->vdev.entity, 1, &vin->pad);
	if (ret)
		return ret;

	return rvin_group_get(vin);
}

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static const struct rvin_info rcar_info_h1 = {
	.model = RCAR_H1,
	.use_mc = false,
	.max_width = 2048,
	.max_height = 2048,
};

static const struct rvin_info rcar_info_m1 = {
	.model = RCAR_M1,
	.use_mc = false,
	.max_width = 2048,
	.max_height = 2048,
};

static const struct rvin_info rcar_info_gen2 = {
	.model = RCAR_GEN2,
	.use_mc = false,
	.max_width = 2048,
	.max_height = 2048,
};

static const struct of_device_id rvin_of_id_table[] = {
	{
		.compatible = "renesas,vin-r8a7778",
		.data = &rcar_info_m1,
	},
	{
		.compatible = "renesas,vin-r8a7779",
		.data = &rcar_info_h1,
	},
	{
		.compatible = "renesas,vin-r8a7790",
		.data = &rcar_info_gen2,
	},
	{
		.compatible = "renesas,vin-r8a7791",
		.data = &rcar_info_gen2,
	},
	{
		.compatible = "renesas,vin-r8a7793",
		.data = &rcar_info_gen2,
	},
	{
		.compatible = "renesas,vin-r8a7794",
		.data = &rcar_info_gen2,
	},
	{
		.compatible = "renesas,rcar-gen2-vin",
		.data = &rcar_info_gen2,
	},
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, rvin_of_id_table);

static int rcar_vin_probe(struct platform_device *pdev)
{
	struct rvin_dev *vin;
	struct resource *mem;
	int irq, ret;

	vin = devm_kzalloc(&pdev->dev, sizeof(*vin), GFP_KERNEL);
	if (!vin)
		return -ENOMEM;

	vin->dev = &pdev->dev;
	vin->info = of_device_get_match_data(&pdev->dev);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem == NULL)
		return -EINVAL;

	vin->base = devm_ioremap_resource(vin->dev, mem);
	if (IS_ERR(vin->base))
		return PTR_ERR(vin->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = rvin_dma_register(vin, irq);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, vin);
	if (vin->info->use_mc)
		ret = rvin_mc_init(vin);
	else
		ret = rvin_digital_graph_init(vin);
	if (ret < 0)
		goto error;

	pm_suspend_ignore_children(&pdev->dev, true);
	pm_runtime_enable(&pdev->dev);

	return 0;
error:
	rvin_dma_unregister(vin);
	v4l2_async_notifier_cleanup(&vin->notifier);

	return ret;
}

static int rcar_vin_remove(struct platform_device *pdev)
{
	struct rvin_dev *vin = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	rvin_v4l2_unregister(vin);

	v4l2_async_notifier_unregister(&vin->notifier);
	v4l2_async_notifier_cleanup(&vin->notifier);

	if (vin->info->use_mc)
		rvin_group_put(vin);
	else
		v4l2_ctrl_handler_free(&vin->ctrl_handler);

	rvin_dma_unregister(vin);

	return 0;
}

static struct platform_driver rcar_vin_driver = {
	.driver = {
		.name = "rcar-vin",
		.of_match_table = rvin_of_id_table,
	},
	.probe = rcar_vin_probe,
	.remove = rcar_vin_remove,
};

module_platform_driver(rcar_vin_driver);

MODULE_AUTHOR("Niklas Söderlund <niklas.soderlund@ragnatech.se>");
MODULE_DESCRIPTION("Renesas R-Car VIN camera host driver");
MODULE_LICENSE("GPL v2");

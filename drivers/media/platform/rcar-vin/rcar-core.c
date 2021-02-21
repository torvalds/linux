// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Renesas R-Car VIN
 *
 * Copyright (C) 2016 Renesas Electronics Corp.
 * Copyright (C) 2011-2013 Renesas Solutions Corp.
 * Copyright (C) 2013 Cogent Embedded, Inc., <source@cogentembedded.com>
 * Copyright (C) 2008 Magnus Damm
 *
 * Based on the soc-camera rcar_vin driver
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#include <media/v4l2-async.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>

#include "rcar-vin.h"

/*
 * The companion CSI-2 receiver driver (rcar-csi2) is known
 * and we know it has one source pad (pad 0) and four sink
 * pads (pad 1-4). So to translate a pad on the remote
 * CSI-2 receiver to/from the VIN internal channel number simply
 * subtract/add one from the pad/channel number.
 */
#define rvin_group_csi_pad_to_channel(pad) ((pad) - 1)
#define rvin_group_csi_channel_to_pad(channel) ((channel) + 1)

/*
 * Not all VINs are created equal, master VINs control the
 * routing for other VIN's. We can figure out which VIN is
 * master by looking at a VINs id.
 */
#define rvin_group_id_to_master(vin) ((vin) < 4 ? 0 : 4)

#define v4l2_dev_to_vin(d)	container_of(d, struct rvin_dev, v4l2_dev)

/* -----------------------------------------------------------------------------
 * Media Controller link notification
 */

/* group lock should be held when calling this function. */
static int rvin_group_entity_to_csi_id(struct rvin_group *group,
				       struct media_entity *entity)
{
	struct v4l2_subdev *sd;
	unsigned int i;

	sd = media_entity_to_v4l2_subdev(entity);

	for (i = 0; i < RVIN_CSI_MAX; i++)
		if (group->csi[i].subdev == sd)
			return i;

	return -ENODEV;
}

static unsigned int rvin_group_get_mask(struct rvin_dev *vin,
					enum rvin_csi_id csi_id,
					unsigned char channel)
{
	const struct rvin_group_route *route;
	unsigned int mask = 0;

	for (route = vin->info->routes; route->mask; route++) {
		if (route->vin == vin->id &&
		    route->csi == csi_id &&
		    route->channel == channel) {
			vin_dbg(vin,
				"Adding route: vin: %d csi: %d channel: %d\n",
				route->vin, route->csi, route->channel);
			mask |= route->mask;
		}
	}

	return mask;
}

/*
 * Link setup for the links between a VIN and a CSI-2 receiver is a bit
 * complex. The reason for this is that the register controlling routing
 * is not present in each VIN instance. There are special VINs which
 * control routing for themselves and other VINs. There are not many
 * different possible links combinations that can be enabled at the same
 * time, therefor all already enabled links which are controlled by a
 * master VIN need to be taken into account when making the decision
 * if a new link can be enabled or not.
 *
 * 1. Find out which VIN the link the user tries to enable is connected to.
 * 2. Lookup which master VIN controls the links for this VIN.
 * 3. Start with a bitmask with all bits set.
 * 4. For each previously enabled link from the master VIN bitwise AND its
 *    route mask (see documentation for mask in struct rvin_group_route)
 *    with the bitmask.
 * 5. Bitwise AND the mask for the link the user tries to enable to the bitmask.
 * 6. If the bitmask is not empty at this point the new link can be enabled
 *    while keeping all previous links enabled. Update the CHSEL value of the
 *    master VIN and inform the user that the link could be enabled.
 *
 * Please note that no link can be enabled if any VIN in the group is
 * currently open.
 */
static int rvin_group_link_notify(struct media_link *link, u32 flags,
				  unsigned int notification)
{
	struct rvin_group *group = container_of(link->graph_obj.mdev,
						struct rvin_group, mdev);
	unsigned int master_id, channel, mask_new, i;
	unsigned int mask = ~0;
	struct media_entity *entity;
	struct video_device *vdev;
	struct media_pad *csi_pad;
	struct rvin_dev *vin = NULL;
	int csi_id, ret;

	ret = v4l2_pipeline_link_notify(link, flags, notification);
	if (ret)
		return ret;

	/* Only care about link enablement for VIN nodes. */
	if (!(flags & MEDIA_LNK_FL_ENABLED) ||
	    !is_media_entity_v4l2_video_device(link->sink->entity))
		return 0;

	/*
	 * Don't allow link changes if any entity in the graph is
	 * streaming, modifying the CHSEL register fields can disrupt
	 * running streams.
	 */
	media_device_for_each_entity(entity, &group->mdev)
		if (entity->stream_count)
			return -EBUSY;

	mutex_lock(&group->lock);

	/* Find the master VIN that controls the routes. */
	vdev = media_entity_to_video_device(link->sink->entity);
	vin = container_of(vdev, struct rvin_dev, vdev);
	master_id = rvin_group_id_to_master(vin->id);

	if (WARN_ON(!group->vin[master_id])) {
		ret = -ENODEV;
		goto out;
	}

	/* Build a mask for already enabled links. */
	for (i = master_id; i < master_id + 4; i++) {
		if (!group->vin[i])
			continue;

		/* Get remote CSI-2, if any. */
		csi_pad = media_entity_remote_pad(
				&group->vin[i]->vdev.entity.pads[0]);
		if (!csi_pad)
			continue;

		csi_id = rvin_group_entity_to_csi_id(group, csi_pad->entity);
		channel = rvin_group_csi_pad_to_channel(csi_pad->index);

		mask &= rvin_group_get_mask(group->vin[i], csi_id, channel);
	}

	/* Add the new link to the existing mask and check if it works. */
	csi_id = rvin_group_entity_to_csi_id(group, link->source->entity);

	if (csi_id == -ENODEV) {
		struct v4l2_subdev *sd;

		/*
		 * Make sure the source entity subdevice is registered as
		 * a parallel input of one of the enabled VINs if it is not
		 * one of the CSI-2 subdevices.
		 *
		 * No hardware configuration required for parallel inputs,
		 * we can return here.
		 */
		sd = media_entity_to_v4l2_subdev(link->source->entity);
		for (i = 0; i < RCAR_VIN_NUM; i++) {
			if (group->vin[i] &&
			    group->vin[i]->parallel.subdev == sd) {
				group->vin[i]->is_csi = false;
				ret = 0;
				goto out;
			}
		}

		vin_err(vin, "Subdevice %s not registered to any VIN\n",
			link->source->entity->name);
		ret = -ENODEV;
		goto out;
	}

	channel = rvin_group_csi_pad_to_channel(link->source->index);
	mask_new = mask & rvin_group_get_mask(vin, csi_id, channel);
	vin_dbg(vin, "Try link change mask: 0x%x new: 0x%x\n", mask, mask_new);

	if (!mask_new) {
		ret = -EMLINK;
		goto out;
	}

	/* New valid CHSEL found, set the new value. */
	ret = rvin_set_channel_routing(group->vin[master_id], __ffs(mask_new));
	if (ret)
		goto out;

	vin->is_csi = true;

out:
	mutex_unlock(&group->lock);

	return ret;
}

static const struct media_device_ops rvin_media_ops = {
	.link_notify = rvin_group_link_notify,
};

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
	media_device_cleanup(&group->mdev);
	mutex_destroy(&group->lock);
}

static int rvin_group_init(struct rvin_group *group, struct rvin_dev *vin)
{
	struct media_device *mdev = &group->mdev;
	const struct of_device_id *match;
	struct device_node *np;

	mutex_init(&group->lock);

	/* Count number of VINs in the system */
	group->count = 0;
	for_each_matching_node(np, vin->dev->driver->of_match_table)
		if (of_device_is_available(np))
			group->count++;

	vin_dbg(vin, "found %u enabled VIN's in DT", group->count);

	mdev->dev = vin->dev;
	mdev->ops = &rvin_media_ops;

	match = of_match_node(vin->dev->driver->of_match_table,
			      vin->dev->of_node);

	strscpy(mdev->driver_name, KBUILD_MODNAME, sizeof(mdev->driver_name));
	strscpy(mdev->model, match->compatible, sizeof(mdev->model));
	snprintf(mdev->bus_info, sizeof(mdev->bus_info), "platform:%s",
		 dev_name(mdev->dev));

	media_device_init(mdev);

	return 0;
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
	struct rvin_group *group = vin->group;

	mutex_lock(&group->lock);

	vin->group = NULL;
	vin->v4l2_dev.mdev = NULL;

	if (WARN_ON(group->vin[vin->id] != vin))
		goto out;

	group->vin[vin->id] = NULL;
out:
	mutex_unlock(&group->lock);

	kref_put(&group->refcount, rvin_group_release);
}

/* -----------------------------------------------------------------------------
 * Controls
 */

static int rvin_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct rvin_dev *vin =
		container_of(ctrl->handler, struct rvin_dev, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_ALPHA_COMPONENT:
		rvin_set_alpha(vin, ctrl->val);
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops rvin_ctrl_ops = {
	.s_ctrl = rvin_s_ctrl,
};

/* -----------------------------------------------------------------------------
 * Async notifier
 */

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
 * Parallel async notifier
 */

/* The vin lock should be held when calling the subdevice attach and detach */
static int rvin_parallel_subdevice_attach(struct rvin_dev *vin,
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
	vin->parallel.source_pad = ret;

	ret = rvin_find_pad(subdev, MEDIA_PAD_FL_SINK);
	vin->parallel.sink_pad = ret < 0 ? 0 : ret;

	if (vin->info->use_mc) {
		vin->parallel.subdev = subdev;
		return 0;
	}

	/* Find compatible subdevices mbus format */
	vin->mbus_code = 0;
	code.index = 0;
	code.pad = vin->parallel.source_pad;
	while (!vin->mbus_code &&
	       !v4l2_subdev_call(subdev, pad, enum_mbus_code, NULL, &code)) {
		code.index++;
		switch (code.code) {
		case MEDIA_BUS_FMT_YUYV8_1X16:
		case MEDIA_BUS_FMT_UYVY8_1X16:
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

	v4l2_ctrl_new_std(&vin->ctrl_handler, &rvin_ctrl_ops,
			  V4L2_CID_ALPHA_COMPONENT, 0, 255, 1, 255);

	if (vin->ctrl_handler.error) {
		ret = vin->ctrl_handler.error;
		v4l2_ctrl_handler_free(&vin->ctrl_handler);
		return ret;
	}

	ret = v4l2_ctrl_add_handler(&vin->ctrl_handler, subdev->ctrl_handler,
				    NULL, true);
	if (ret < 0) {
		v4l2_ctrl_handler_free(&vin->ctrl_handler);
		return ret;
	}

	vin->vdev.ctrl_handler = &vin->ctrl_handler;

	vin->parallel.subdev = subdev;

	return 0;
}

static void rvin_parallel_subdevice_detach(struct rvin_dev *vin)
{
	rvin_v4l2_unregister(vin);
	vin->parallel.subdev = NULL;

	if (!vin->info->use_mc) {
		v4l2_ctrl_handler_free(&vin->ctrl_handler);
		vin->vdev.ctrl_handler = NULL;
	}
}

static int rvin_parallel_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct rvin_dev *vin = v4l2_dev_to_vin(notifier->v4l2_dev);
	struct media_entity *source;
	struct media_entity *sink;
	int ret;

	ret = v4l2_device_register_subdev_nodes(&vin->v4l2_dev);
	if (ret < 0) {
		vin_err(vin, "Failed to register subdev nodes\n");
		return ret;
	}

	if (!video_is_registered(&vin->vdev)) {
		ret = rvin_v4l2_register(vin);
		if (ret < 0)
			return ret;
	}

	if (!vin->info->use_mc)
		return 0;

	/* If we're running with media-controller, link the subdevs. */
	source = &vin->parallel.subdev->entity;
	sink = &vin->vdev.entity;

	ret = media_create_pad_link(source, vin->parallel.source_pad,
				    sink, vin->parallel.sink_pad, 0);
	if (ret)
		vin_err(vin, "Error adding link from %s to %s: %d\n",
			source->name, sink->name, ret);

	return ret;
}

static void rvin_parallel_notify_unbind(struct v4l2_async_notifier *notifier,
					struct v4l2_subdev *subdev,
					struct v4l2_async_subdev *asd)
{
	struct rvin_dev *vin = v4l2_dev_to_vin(notifier->v4l2_dev);

	vin_dbg(vin, "unbind parallel subdev %s\n", subdev->name);

	mutex_lock(&vin->lock);
	rvin_parallel_subdevice_detach(vin);
	mutex_unlock(&vin->lock);
}

static int rvin_parallel_notify_bound(struct v4l2_async_notifier *notifier,
				      struct v4l2_subdev *subdev,
				      struct v4l2_async_subdev *asd)
{
	struct rvin_dev *vin = v4l2_dev_to_vin(notifier->v4l2_dev);
	int ret;

	mutex_lock(&vin->lock);
	ret = rvin_parallel_subdevice_attach(vin, subdev);
	mutex_unlock(&vin->lock);
	if (ret)
		return ret;

	v4l2_set_subdev_hostdata(subdev, vin);

	vin_dbg(vin, "bound subdev %s source pad: %u sink pad: %u\n",
		subdev->name, vin->parallel.source_pad,
		vin->parallel.sink_pad);

	return 0;
}

static const struct v4l2_async_notifier_operations rvin_parallel_notify_ops = {
	.bound = rvin_parallel_notify_bound,
	.unbind = rvin_parallel_notify_unbind,
	.complete = rvin_parallel_notify_complete,
};

static int rvin_parallel_parse_of(struct rvin_dev *vin)
{
	struct fwnode_handle *ep, *fwnode;
	struct v4l2_fwnode_endpoint vep = {
		.bus_type = V4L2_MBUS_UNKNOWN,
	};
	struct v4l2_async_subdev *asd;
	int ret;

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(vin->dev), 0, 0, 0);
	if (!ep)
		return 0;

	fwnode = fwnode_graph_get_remote_endpoint(ep);
	ret = v4l2_fwnode_endpoint_parse(ep, &vep);
	fwnode_handle_put(ep);
	if (ret) {
		vin_err(vin, "Failed to parse %pOF\n", to_of_node(fwnode));
		ret = -EINVAL;
		goto out;
	}

	switch (vep.bus_type) {
	case V4L2_MBUS_PARALLEL:
	case V4L2_MBUS_BT656:
		vin_dbg(vin, "Found %s media bus\n",
			vep.bus_type == V4L2_MBUS_PARALLEL ?
			"PARALLEL" : "BT656");
		vin->parallel.mbus_type = vep.bus_type;
		vin->parallel.bus = vep.bus.parallel;
		break;
	default:
		vin_err(vin, "Unknown media bus type\n");
		ret = -EINVAL;
		goto out;
	}

	asd = v4l2_async_notifier_add_fwnode_subdev(&vin->notifier, fwnode,
						    struct v4l2_async_subdev);
	if (IS_ERR(asd)) {
		ret = PTR_ERR(asd);
		goto out;
	}

	vin->parallel.asd = asd;

	vin_dbg(vin, "Add parallel OF device %pOF\n", to_of_node(fwnode));
out:
	fwnode_handle_put(fwnode);

	return ret;
}

static int rvin_parallel_init(struct rvin_dev *vin)
{
	int ret;

	v4l2_async_notifier_init(&vin->notifier);

	ret = rvin_parallel_parse_of(vin);
	if (ret)
		return ret;

	/* If using mc, it's fine not to have any input registered. */
	if (!vin->parallel.asd)
		return vin->info->use_mc ? 0 : -ENODEV;

	vin_dbg(vin, "Found parallel subdevice %pOF\n",
		to_of_node(vin->parallel.asd->match.fwnode));

	vin->notifier.ops = &rvin_parallel_notify_ops;
	ret = v4l2_async_notifier_register(&vin->v4l2_dev, &vin->notifier);
	if (ret < 0) {
		vin_err(vin, "Notifier registration failed\n");
		v4l2_async_notifier_cleanup(&vin->notifier);
		return ret;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * Group async notifier
 */

static int rvin_group_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct rvin_dev *vin = v4l2_dev_to_vin(notifier->v4l2_dev);
	const struct rvin_group_route *route;
	unsigned int i;
	int ret;

	ret = media_device_register(&vin->group->mdev);
	if (ret)
		return ret;

	ret = v4l2_device_register_subdev_nodes(&vin->v4l2_dev);
	if (ret) {
		vin_err(vin, "Failed to register subdev nodes\n");
		return ret;
	}

	/* Register all video nodes for the group. */
	for (i = 0; i < RCAR_VIN_NUM; i++) {
		if (vin->group->vin[i] &&
		    !video_is_registered(&vin->group->vin[i]->vdev)) {
			ret = rvin_v4l2_register(vin->group->vin[i]);
			if (ret)
				return ret;
		}
	}

	/* Create all media device links between VINs and CSI-2's. */
	mutex_lock(&vin->group->lock);
	for (route = vin->info->routes; route->mask; route++) {
		struct media_pad *source_pad, *sink_pad;
		struct media_entity *source, *sink;
		unsigned int source_idx;

		/* Check that VIN is part of the group. */
		if (!vin->group->vin[route->vin])
			continue;

		/* Check that VIN' master is part of the group. */
		if (!vin->group->vin[rvin_group_id_to_master(route->vin)])
			continue;

		/* Check that CSI-2 is part of the group. */
		if (!vin->group->csi[route->csi].subdev)
			continue;

		source = &vin->group->csi[route->csi].subdev->entity;
		source_idx = rvin_group_csi_channel_to_pad(route->channel);
		source_pad = &source->pads[source_idx];

		sink = &vin->group->vin[route->vin]->vdev.entity;
		sink_pad = &sink->pads[0];

		/* Skip if link already exists. */
		if (media_entity_find_link(source_pad, sink_pad))
			continue;

		ret = media_create_pad_link(source, source_idx, sink, 0, 0);
		if (ret) {
			vin_err(vin, "Error adding link from %s to %s\n",
				source->name, sink->name);
			break;
		}
	}
	mutex_unlock(&vin->group->lock);

	return ret;
}

static void rvin_group_notify_unbind(struct v4l2_async_notifier *notifier,
				     struct v4l2_subdev *subdev,
				     struct v4l2_async_subdev *asd)
{
	struct rvin_dev *vin = v4l2_dev_to_vin(notifier->v4l2_dev);
	unsigned int i;

	for (i = 0; i < RCAR_VIN_NUM; i++)
		if (vin->group->vin[i])
			rvin_v4l2_unregister(vin->group->vin[i]);

	mutex_lock(&vin->group->lock);

	for (i = 0; i < RVIN_CSI_MAX; i++) {
		if (vin->group->csi[i].asd != asd)
			continue;
		vin->group->csi[i].subdev = NULL;
		vin_dbg(vin, "Unbind CSI-2 %s from slot %u\n", subdev->name, i);
		break;
	}

	mutex_unlock(&vin->group->lock);

	media_device_unregister(&vin->group->mdev);
}

static int rvin_group_notify_bound(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *subdev,
				   struct v4l2_async_subdev *asd)
{
	struct rvin_dev *vin = v4l2_dev_to_vin(notifier->v4l2_dev);
	unsigned int i;

	mutex_lock(&vin->group->lock);

	for (i = 0; i < RVIN_CSI_MAX; i++) {
		if (vin->group->csi[i].asd != asd)
			continue;
		vin->group->csi[i].subdev = subdev;
		vin_dbg(vin, "Bound CSI-2 %s to slot %u\n", subdev->name, i);
		break;
	}

	mutex_unlock(&vin->group->lock);

	return 0;
}

static const struct v4l2_async_notifier_operations rvin_group_notify_ops = {
	.bound = rvin_group_notify_bound,
	.unbind = rvin_group_notify_unbind,
	.complete = rvin_group_notify_complete,
};

static int rvin_mc_parse_of(struct rvin_dev *vin, unsigned int id)
{
	struct fwnode_handle *ep, *fwnode;
	struct v4l2_fwnode_endpoint vep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct v4l2_async_subdev *asd;
	int ret;

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(vin->dev), 1, id, 0);
	if (!ep)
		return 0;

	fwnode = fwnode_graph_get_remote_endpoint(ep);
	ret = v4l2_fwnode_endpoint_parse(ep, &vep);
	fwnode_handle_put(ep);
	if (ret) {
		vin_err(vin, "Failed to parse %pOF\n", to_of_node(fwnode));
		ret = -EINVAL;
		goto out;
	}

	if (!of_device_is_available(to_of_node(fwnode))) {
		vin_dbg(vin, "OF device %pOF disabled, ignoring\n",
			to_of_node(fwnode));
		ret = -ENOTCONN;
		goto out;
	}

	asd = v4l2_async_notifier_add_fwnode_subdev(&vin->group->notifier,
						    fwnode,
						    struct v4l2_async_subdev);
	if (IS_ERR(asd)) {
		ret = PTR_ERR(asd);
		goto out;
	}

	vin->group->csi[vep.base.id].asd = asd;

	vin_dbg(vin, "Add group OF device %pOF to slot %u\n",
		to_of_node(fwnode), vep.base.id);
out:
	fwnode_handle_put(fwnode);

	return ret;
}

static int rvin_mc_parse_of_graph(struct rvin_dev *vin)
{
	unsigned int count = 0, vin_mask = 0;
	unsigned int i, id;
	int ret;

	mutex_lock(&vin->group->lock);

	/* If not all VIN's are registered don't register the notifier. */
	for (i = 0; i < RCAR_VIN_NUM; i++) {
		if (vin->group->vin[i]) {
			count++;
			vin_mask |= BIT(i);
		}
	}

	if (vin->group->count != count) {
		mutex_unlock(&vin->group->lock);
		return 0;
	}

	mutex_unlock(&vin->group->lock);

	v4l2_async_notifier_init(&vin->group->notifier);

	/*
	 * Have all VIN's look for CSI-2 subdevices. Some subdevices will
	 * overlap but the parser function can handle it, so each subdevice
	 * will only be registered once with the group notifier.
	 */
	for (i = 0; i < RCAR_VIN_NUM; i++) {
		if (!(vin_mask & BIT(i)))
			continue;

		for (id = 0; id < RVIN_CSI_MAX; id++) {
			if (vin->group->csi[id].asd)
				continue;

			ret = rvin_mc_parse_of(vin->group->vin[i], id);
			if (ret)
				return ret;
		}
	}

	if (list_empty(&vin->group->notifier.asd_list))
		return 0;

	vin->group->notifier.ops = &rvin_group_notify_ops;
	ret = v4l2_async_notifier_register(&vin->v4l2_dev,
					   &vin->group->notifier);
	if (ret < 0) {
		vin_err(vin, "Notifier registration failed\n");
		v4l2_async_notifier_cleanup(&vin->group->notifier);
		return ret;
	}

	return 0;
}

static int rvin_mc_init(struct rvin_dev *vin)
{
	int ret;

	vin->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vin->vdev.entity, 1, &vin->pad);
	if (ret)
		return ret;

	ret = rvin_group_get(vin);
	if (ret)
		return ret;

	ret = rvin_mc_parse_of_graph(vin);
	if (ret)
		rvin_group_put(vin);

	ret = v4l2_ctrl_handler_init(&vin->ctrl_handler, 1);
	if (ret < 0)
		return ret;

	v4l2_ctrl_new_std(&vin->ctrl_handler, &rvin_ctrl_ops,
			  V4L2_CID_ALPHA_COMPONENT, 0, 255, 1, 255);

	if (vin->ctrl_handler.error) {
		ret = vin->ctrl_handler.error;
		v4l2_ctrl_handler_free(&vin->ctrl_handler);
		return ret;
	}

	vin->vdev.ctrl_handler = &vin->ctrl_handler;

	return ret;
}

/* -----------------------------------------------------------------------------
 * Suspend / Resume
 */

static int __maybe_unused rvin_suspend(struct device *dev)
{
	struct rvin_dev *vin = dev_get_drvdata(dev);

	if (vin->state != RUNNING)
		return 0;

	rvin_stop_streaming(vin);

	vin->state = SUSPENDED;

	return 0;
}

static int __maybe_unused rvin_resume(struct device *dev)
{
	struct rvin_dev *vin = dev_get_drvdata(dev);

	if (vin->state != SUSPENDED)
		return 0;

	/*
	 * Restore group master CHSEL setting.
	 *
	 * This needs to be done by every VIN resuming not only the master
	 * as we don't know if and in which order the master VINs will
	 * be resumed.
	 */
	if (vin->info->use_mc) {
		unsigned int master_id = rvin_group_id_to_master(vin->id);
		struct rvin_dev *master = vin->group->vin[master_id];
		int ret;

		if (WARN_ON(!master))
			return -ENODEV;

		ret = rvin_set_channel_routing(master, master->chsel);
		if (ret)
			return ret;
	}

	return rvin_start_streaming(vin);
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

static const struct rvin_group_route rcar_info_r8a774e1_routes[] = {
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 0, .mask = BIT(0) | BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 0, .mask = BIT(1) | BIT(4) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 0, .mask = BIT(2) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 1, .mask = BIT(0) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 1, .mask = BIT(1) | BIT(3) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 1, .mask = BIT(2) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 1, .mask = BIT(4) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 2, .mask = BIT(0) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 2, .mask = BIT(1) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 2, .mask = BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 2, .vin = 2, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 2, .vin = 2, .mask = BIT(4) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 3, .mask = BIT(0) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 3, .mask = BIT(1) | BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 3, .vin = 3, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 3, .vin = 3, .mask = BIT(4) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 4, .mask = BIT(1) | BIT(4) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 5, .mask = BIT(0) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 5, .mask = BIT(4) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 6, .mask = BIT(0) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 6, .mask = BIT(2) },
	{ .csi = RVIN_CSI20, .channel = 2, .vin = 6, .mask = BIT(4) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 7, .mask = BIT(1) | BIT(2) },
	{ .csi = RVIN_CSI20, .channel = 3, .vin = 7, .mask = BIT(4) },
	{ /* Sentinel */ }
};

static const struct rvin_info rcar_info_r8a774e1 = {
	.model = RCAR_GEN3,
	.use_mc = true,
	.max_width = 4096,
	.max_height = 4096,
	.routes = rcar_info_r8a774e1_routes,
};

static const struct rvin_group_route rcar_info_r8a7795_routes[] = {
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 0, .mask = BIT(0) | BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 0, .mask = BIT(1) | BIT(4) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 0, .mask = BIT(2) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 1, .mask = BIT(0) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 1, .mask = BIT(1) | BIT(3) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 1, .mask = BIT(2) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 1, .mask = BIT(4) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 2, .mask = BIT(0) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 2, .mask = BIT(1) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 2, .mask = BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 2, .vin = 2, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 2, .vin = 2, .mask = BIT(4) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 3, .mask = BIT(0) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 3, .mask = BIT(1) | BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 3, .vin = 3, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 3, .vin = 3, .mask = BIT(4) },
	{ .csi = RVIN_CSI41, .channel = 0, .vin = 4, .mask = BIT(0) | BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 4, .mask = BIT(1) | BIT(4) },
	{ .csi = RVIN_CSI41, .channel = 1, .vin = 4, .mask = BIT(2) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 5, .mask = BIT(0) },
	{ .csi = RVIN_CSI41, .channel = 1, .vin = 5, .mask = BIT(1) | BIT(3) },
	{ .csi = RVIN_CSI41, .channel = 0, .vin = 5, .mask = BIT(2) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 5, .mask = BIT(4) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 6, .mask = BIT(0) },
	{ .csi = RVIN_CSI41, .channel = 0, .vin = 6, .mask = BIT(1) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 6, .mask = BIT(2) },
	{ .csi = RVIN_CSI41, .channel = 2, .vin = 6, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 2, .vin = 6, .mask = BIT(4) },
	{ .csi = RVIN_CSI41, .channel = 1, .vin = 7, .mask = BIT(0) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 7, .mask = BIT(1) | BIT(2) },
	{ .csi = RVIN_CSI41, .channel = 3, .vin = 7, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 3, .vin = 7, .mask = BIT(4) },
	{ /* Sentinel */ }
};

static const struct rvin_info rcar_info_r8a7795 = {
	.model = RCAR_GEN3,
	.use_mc = true,
	.nv12 = true,
	.max_width = 4096,
	.max_height = 4096,
	.routes = rcar_info_r8a7795_routes,
};

static const struct rvin_group_route rcar_info_r8a7795es1_routes[] = {
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 0, .mask = BIT(0) | BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 0, .mask = BIT(1) | BIT(4) },
	{ .csi = RVIN_CSI21, .channel = 0, .vin = 0, .mask = BIT(2) | BIT(5) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 1, .mask = BIT(0) },
	{ .csi = RVIN_CSI21, .channel = 0, .vin = 1, .mask = BIT(1) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 1, .mask = BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 1, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 1, .mask = BIT(4) },
	{ .csi = RVIN_CSI21, .channel = 1, .vin = 1, .mask = BIT(5) },
	{ .csi = RVIN_CSI21, .channel = 0, .vin = 2, .mask = BIT(0) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 2, .mask = BIT(1) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 2, .mask = BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 2, .vin = 2, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 2, .vin = 2, .mask = BIT(4) },
	{ .csi = RVIN_CSI21, .channel = 2, .vin = 2, .mask = BIT(5) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 3, .mask = BIT(0) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 3, .mask = BIT(1) },
	{ .csi = RVIN_CSI21, .channel = 1, .vin = 3, .mask = BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 3, .vin = 3, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 3, .vin = 3, .mask = BIT(4) },
	{ .csi = RVIN_CSI21, .channel = 3, .vin = 3, .mask = BIT(5) },
	{ .csi = RVIN_CSI41, .channel = 0, .vin = 4, .mask = BIT(0) | BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 4, .mask = BIT(1) | BIT(4) },
	{ .csi = RVIN_CSI21, .channel = 0, .vin = 4, .mask = BIT(2) | BIT(5) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 5, .mask = BIT(0) },
	{ .csi = RVIN_CSI21, .channel = 0, .vin = 5, .mask = BIT(1) },
	{ .csi = RVIN_CSI41, .channel = 0, .vin = 5, .mask = BIT(2) },
	{ .csi = RVIN_CSI41, .channel = 1, .vin = 5, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 5, .mask = BIT(4) },
	{ .csi = RVIN_CSI21, .channel = 1, .vin = 5, .mask = BIT(5) },
	{ .csi = RVIN_CSI21, .channel = 0, .vin = 6, .mask = BIT(0) },
	{ .csi = RVIN_CSI41, .channel = 0, .vin = 6, .mask = BIT(1) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 6, .mask = BIT(2) },
	{ .csi = RVIN_CSI41, .channel = 2, .vin = 6, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 2, .vin = 6, .mask = BIT(4) },
	{ .csi = RVIN_CSI21, .channel = 2, .vin = 6, .mask = BIT(5) },
	{ .csi = RVIN_CSI41, .channel = 1, .vin = 7, .mask = BIT(0) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 7, .mask = BIT(1) },
	{ .csi = RVIN_CSI21, .channel = 1, .vin = 7, .mask = BIT(2) },
	{ .csi = RVIN_CSI41, .channel = 3, .vin = 7, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 3, .vin = 7, .mask = BIT(4) },
	{ .csi = RVIN_CSI21, .channel = 3, .vin = 7, .mask = BIT(5) },
	{ /* Sentinel */ }
};

static const struct rvin_info rcar_info_r8a7795es1 = {
	.model = RCAR_GEN3,
	.use_mc = true,
	.max_width = 4096,
	.max_height = 4096,
	.routes = rcar_info_r8a7795es1_routes,
};

static const struct rvin_group_route rcar_info_r8a7796_routes[] = {
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 0, .mask = BIT(0) | BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 0, .mask = BIT(1) | BIT(4) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 1, .mask = BIT(0) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 1, .mask = BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 1, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 1, .mask = BIT(4) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 2, .mask = BIT(1) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 2, .mask = BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 2, .vin = 2, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 2, .vin = 2, .mask = BIT(4) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 3, .mask = BIT(0) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 3, .mask = BIT(1) },
	{ .csi = RVIN_CSI40, .channel = 3, .vin = 3, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 3, .vin = 3, .mask = BIT(4) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 4, .mask = BIT(0) | BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 4, .mask = BIT(1) | BIT(4) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 5, .mask = BIT(0) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 5, .mask = BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 5, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 5, .mask = BIT(4) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 6, .mask = BIT(1) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 6, .mask = BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 2, .vin = 6, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 2, .vin = 6, .mask = BIT(4) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 7, .mask = BIT(0) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 7, .mask = BIT(1) },
	{ .csi = RVIN_CSI40, .channel = 3, .vin = 7, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 3, .vin = 7, .mask = BIT(4) },
	{ /* Sentinel */ }
};

static const struct rvin_info rcar_info_r8a7796 = {
	.model = RCAR_GEN3,
	.use_mc = true,
	.nv12 = true,
	.max_width = 4096,
	.max_height = 4096,
	.routes = rcar_info_r8a7796_routes,
};

static const struct rvin_group_route rcar_info_r8a77965_routes[] = {
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 0, .mask = BIT(0) | BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 0, .mask = BIT(1) | BIT(4) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 0, .mask = BIT(2) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 1, .mask = BIT(0) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 1, .mask = BIT(1) | BIT(3) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 1, .mask = BIT(2) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 1, .mask = BIT(4) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 2, .mask = BIT(0) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 2, .mask = BIT(1) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 2, .mask = BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 2, .vin = 2, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 2, .vin = 2, .mask = BIT(4) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 3, .mask = BIT(0) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 3, .mask = BIT(1) | BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 3, .vin = 3, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 3, .vin = 3, .mask = BIT(4) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 4, .mask = BIT(0) | BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 4, .mask = BIT(1) | BIT(4) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 4, .mask = BIT(2) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 5, .mask = BIT(0) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 5, .mask = BIT(1) | BIT(3) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 5, .mask = BIT(2) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 5, .mask = BIT(4) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 6, .mask = BIT(0) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 6, .mask = BIT(1) },
	{ .csi = RVIN_CSI20, .channel = 0, .vin = 6, .mask = BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 2, .vin = 6, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 2, .vin = 6, .mask = BIT(4) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 7, .mask = BIT(0) },
	{ .csi = RVIN_CSI20, .channel = 1, .vin = 7, .mask = BIT(1) | BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 3, .vin = 7, .mask = BIT(3) },
	{ .csi = RVIN_CSI20, .channel = 3, .vin = 7, .mask = BIT(4) },
	{ /* Sentinel */ }
};

static const struct rvin_info rcar_info_r8a77965 = {
	.model = RCAR_GEN3,
	.use_mc = true,
	.nv12 = true,
	.max_width = 4096,
	.max_height = 4096,
	.routes = rcar_info_r8a77965_routes,
};

static const struct rvin_group_route rcar_info_r8a77970_routes[] = {
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 0, .mask = BIT(0) | BIT(3) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 1, .mask = BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 1, .mask = BIT(3) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 2, .mask = BIT(1) },
	{ .csi = RVIN_CSI40, .channel = 2, .vin = 2, .mask = BIT(3) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 3, .mask = BIT(0) },
	{ .csi = RVIN_CSI40, .channel = 3, .vin = 3, .mask = BIT(3) },
	{ /* Sentinel */ }
};

static const struct rvin_info rcar_info_r8a77970 = {
	.model = RCAR_GEN3,
	.use_mc = true,
	.max_width = 4096,
	.max_height = 4096,
	.routes = rcar_info_r8a77970_routes,
};

static const struct rvin_group_route rcar_info_r8a77980_routes[] = {
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 0, .mask = BIT(0) | BIT(3) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 0, .mask = BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 1, .mask = BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 1, .mask = BIT(1) | BIT(3) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 2, .mask = BIT(1) },
	{ .csi = RVIN_CSI40, .channel = 2, .vin = 2, .mask = BIT(3) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 3, .mask = BIT(0) },
	{ .csi = RVIN_CSI40, .channel = 3, .vin = 3, .mask = BIT(3) },
	{ .csi = RVIN_CSI41, .channel = 0, .vin = 4, .mask = BIT(0) | BIT(3) },
	{ .csi = RVIN_CSI41, .channel = 1, .vin = 4, .mask = BIT(2) },
	{ .csi = RVIN_CSI41, .channel = 0, .vin = 5, .mask = BIT(2) },
	{ .csi = RVIN_CSI41, .channel = 1, .vin = 5, .mask = BIT(1) | BIT(3) },
	{ .csi = RVIN_CSI41, .channel = 0, .vin = 6, .mask = BIT(1) },
	{ .csi = RVIN_CSI41, .channel = 2, .vin = 6, .mask = BIT(3) },
	{ .csi = RVIN_CSI41, .channel = 1, .vin = 7, .mask = BIT(0) },
	{ .csi = RVIN_CSI41, .channel = 3, .vin = 7, .mask = BIT(3) },
	{ /* Sentinel */ }
};

static const struct rvin_info rcar_info_r8a77980 = {
	.model = RCAR_GEN3,
	.use_mc = true,
	.nv12 = true,
	.max_width = 4096,
	.max_height = 4096,
	.routes = rcar_info_r8a77980_routes,
};

static const struct rvin_group_route rcar_info_r8a77990_routes[] = {
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 4, .mask = BIT(0) | BIT(3) },
	{ .csi = RVIN_CSI40, .channel = 0, .vin = 5, .mask = BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 4, .mask = BIT(2) },
	{ .csi = RVIN_CSI40, .channel = 1, .vin = 5, .mask = BIT(1) | BIT(3) },
	{ /* Sentinel */ }
};

static const struct rvin_info rcar_info_r8a77990 = {
	.model = RCAR_GEN3,
	.use_mc = true,
	.nv12 = true,
	.max_width = 4096,
	.max_height = 4096,
	.routes = rcar_info_r8a77990_routes,
};

static const struct rvin_group_route rcar_info_r8a77995_routes[] = {
	{ /* Sentinel */ }
};

static const struct rvin_info rcar_info_r8a77995 = {
	.model = RCAR_GEN3,
	.use_mc = true,
	.nv12 = true,
	.max_width = 4096,
	.max_height = 4096,
	.routes = rcar_info_r8a77995_routes,
};

static const struct of_device_id rvin_of_id_table[] = {
	{
		.compatible = "renesas,vin-r8a774a1",
		.data = &rcar_info_r8a7796,
	},
	{
		.compatible = "renesas,vin-r8a774b1",
		.data = &rcar_info_r8a77965,
	},
	{
		.compatible = "renesas,vin-r8a774c0",
		.data = &rcar_info_r8a77990,
	},
	{
		.compatible = "renesas,vin-r8a774e1",
		.data = &rcar_info_r8a774e1,
	},
	{
		.compatible = "renesas,vin-r8a7778",
		.data = &rcar_info_m1,
	},
	{
		.compatible = "renesas,vin-r8a7779",
		.data = &rcar_info_h1,
	},
	{
		.compatible = "renesas,rcar-gen2-vin",
		.data = &rcar_info_gen2,
	},
	{
		.compatible = "renesas,vin-r8a7795",
		.data = &rcar_info_r8a7795,
	},
	{
		.compatible = "renesas,vin-r8a7796",
		.data = &rcar_info_r8a7796,
	},
	{
		.compatible = "renesas,vin-r8a77965",
		.data = &rcar_info_r8a77965,
	},
	{
		.compatible = "renesas,vin-r8a77970",
		.data = &rcar_info_r8a77970,
	},
	{
		.compatible = "renesas,vin-r8a77980",
		.data = &rcar_info_r8a77980,
	},
	{
		.compatible = "renesas,vin-r8a77990",
		.data = &rcar_info_r8a77990,
	},
	{
		.compatible = "renesas,vin-r8a77995",
		.data = &rcar_info_r8a77995,
	},
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, rvin_of_id_table);

static const struct soc_device_attribute r8a7795es1[] = {
	{
		.soc_id = "r8a7795", .revision = "ES1.*",
		.data = &rcar_info_r8a7795es1,
	},
	{ /* Sentinel */ }
};

static int rcar_vin_probe(struct platform_device *pdev)
{
	const struct soc_device_attribute *attr;
	struct rvin_dev *vin;
	int irq, ret;

	vin = devm_kzalloc(&pdev->dev, sizeof(*vin), GFP_KERNEL);
	if (!vin)
		return -ENOMEM;

	vin->dev = &pdev->dev;
	vin->info = of_device_get_match_data(&pdev->dev);
	vin->alpha = 0xff;

	/*
	 * Special care is needed on r8a7795 ES1.x since it
	 * uses different routing than r8a7795 ES2.0.
	 */
	attr = soc_device_match(r8a7795es1);
	if (attr)
		vin->info = attr->data;

	vin->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(vin->base))
		return PTR_ERR(vin->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = rvin_dma_register(vin, irq);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, vin);

	if (vin->info->use_mc) {
		ret = rvin_mc_init(vin);
		if (ret)
			goto error_dma_unregister;
	}

	ret = rvin_parallel_init(vin);
	if (ret)
		goto error_group_unregister;

	pm_suspend_ignore_children(&pdev->dev, true);
	pm_runtime_enable(&pdev->dev);

	return 0;

error_group_unregister:
	v4l2_ctrl_handler_free(&vin->ctrl_handler);

	if (vin->info->use_mc) {
		mutex_lock(&vin->group->lock);
		if (&vin->v4l2_dev == vin->group->notifier.v4l2_dev) {
			v4l2_async_notifier_unregister(&vin->group->notifier);
			v4l2_async_notifier_cleanup(&vin->group->notifier);
		}
		mutex_unlock(&vin->group->lock);
		rvin_group_put(vin);
	}

error_dma_unregister:
	rvin_dma_unregister(vin);

	return ret;
}

static int rcar_vin_remove(struct platform_device *pdev)
{
	struct rvin_dev *vin = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	rvin_v4l2_unregister(vin);

	v4l2_async_notifier_unregister(&vin->notifier);
	v4l2_async_notifier_cleanup(&vin->notifier);

	if (vin->info->use_mc) {
		v4l2_async_notifier_unregister(&vin->group->notifier);
		v4l2_async_notifier_cleanup(&vin->group->notifier);
		rvin_group_put(vin);
	}

	v4l2_ctrl_handler_free(&vin->ctrl_handler);

	rvin_dma_unregister(vin);

	return 0;
}

static SIMPLE_DEV_PM_OPS(rvin_pm_ops, rvin_suspend, rvin_resume);

static struct platform_driver rcar_vin_driver = {
	.driver = {
		.name = "rcar-vin",
		.pm = &rvin_pm_ops,
		.of_match_table = rvin_of_id_table,
	},
	.probe = rcar_vin_probe,
	.remove = rcar_vin_remove,
};

module_platform_driver(rcar_vin_driver);

MODULE_AUTHOR("Niklas Söderlund <niklas.soderlund@ragnatech.se>");
MODULE_DESCRIPTION("Renesas R-Car VIN camera host driver");
MODULE_LICENSE("GPL");

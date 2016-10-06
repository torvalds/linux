/*
 * Media Controller ancillary functions
 *
 * Copyright (c) 2016 Mauro Carvalho Chehab <mchehab@kernel.org>
 * Copyright (C) 2016 Shuah Khan <shuahkh@osg.samsung.com>
 * Copyright (C) 2006-2010 Nokia Corporation
 * Copyright (c) 2016 Intel Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/usb.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>
#include <media/media-device.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-core.h>

int v4l2_mc_create_media_graph(struct media_device *mdev)

{
	struct media_entity *entity;
	struct media_entity *if_vid = NULL, *if_aud = NULL;
	struct media_entity *tuner = NULL, *decoder = NULL;
	struct media_entity *io_v4l = NULL, *io_vbi = NULL, *io_swradio = NULL;
	bool is_webcam = false;
	u32 flags;
	int ret;

	if (!mdev)
		return 0;

	media_device_for_each_entity(entity, mdev) {
		switch (entity->function) {
		case MEDIA_ENT_F_IF_VID_DECODER:
			if_vid = entity;
			break;
		case MEDIA_ENT_F_IF_AUD_DECODER:
			if_aud = entity;
			break;
		case MEDIA_ENT_F_TUNER:
			tuner = entity;
			break;
		case MEDIA_ENT_F_ATV_DECODER:
			decoder = entity;
			break;
		case MEDIA_ENT_F_IO_V4L:
			io_v4l = entity;
			break;
		case MEDIA_ENT_F_IO_VBI:
			io_vbi = entity;
			break;
		case MEDIA_ENT_F_IO_SWRADIO:
			io_swradio = entity;
			break;
		case MEDIA_ENT_F_CAM_SENSOR:
			is_webcam = true;
			break;
		}
	}

	/* It should have at least one I/O entity */
	if (!io_v4l && !io_vbi && !io_swradio)
		return -EINVAL;

	/*
	 * Here, webcams are modelled on a very simple way: the sensor is
	 * connected directly to the I/O entity. All dirty details, like
	 * scaler and crop HW are hidden. While such mapping is not enough
	 * for mc-centric hardware, it is enough for v4l2 interface centric
	 * PC-consumer's hardware.
	 */
	if (is_webcam) {
		if (!io_v4l)
			return -EINVAL;

		media_device_for_each_entity(entity, mdev) {
			if (entity->function != MEDIA_ENT_F_CAM_SENSOR)
				continue;
			ret = media_create_pad_link(entity, 0,
						    io_v4l, 0,
						    MEDIA_LNK_FL_ENABLED);
			if (ret)
				return ret;
		}
		if (!decoder)
			return 0;
	}

	/* The device isn't a webcam. So, it should have a decoder */
	if (!decoder)
		return -EINVAL;

	/* Link the tuner and IF video output pads */
	if (tuner) {
		if (if_vid) {
			ret = media_create_pad_link(tuner, TUNER_PAD_OUTPUT,
						    if_vid,
						    IF_VID_DEC_PAD_IF_INPUT,
						    MEDIA_LNK_FL_ENABLED);
			if (ret)
				return ret;
			ret = media_create_pad_link(if_vid, IF_VID_DEC_PAD_OUT,
						decoder, DEMOD_PAD_IF_INPUT,
						MEDIA_LNK_FL_ENABLED);
			if (ret)
				return ret;
		} else {
			ret = media_create_pad_link(tuner, TUNER_PAD_OUTPUT,
						decoder, DEMOD_PAD_IF_INPUT,
						MEDIA_LNK_FL_ENABLED);
			if (ret)
				return ret;
		}

		if (if_aud) {
			ret = media_create_pad_link(tuner, TUNER_PAD_AUD_OUT,
						    if_aud,
						    IF_AUD_DEC_PAD_IF_INPUT,
						    MEDIA_LNK_FL_ENABLED);
			if (ret)
				return ret;
		} else {
			if_aud = tuner;
		}

	}

	/* Create demod to V4L, VBI and SDR radio links */
	if (io_v4l) {
		ret = media_create_pad_link(decoder, DEMOD_PAD_VID_OUT,
					io_v4l, 0,
					MEDIA_LNK_FL_ENABLED);
		if (ret)
			return ret;
	}

	if (io_swradio) {
		ret = media_create_pad_link(decoder, DEMOD_PAD_VID_OUT,
					io_swradio, 0,
					MEDIA_LNK_FL_ENABLED);
		if (ret)
			return ret;
	}

	if (io_vbi) {
		ret = media_create_pad_link(decoder, DEMOD_PAD_VBI_OUT,
					    io_vbi, 0,
					    MEDIA_LNK_FL_ENABLED);
		if (ret)
			return ret;
	}

	/* Create links for the media connectors */
	flags = MEDIA_LNK_FL_ENABLED;
	media_device_for_each_entity(entity, mdev) {
		switch (entity->function) {
		case MEDIA_ENT_F_CONN_RF:
			if (!tuner)
				continue;

			ret = media_create_pad_link(entity, 0, tuner,
						    TUNER_PAD_RF_INPUT,
						    flags);
			break;
		case MEDIA_ENT_F_CONN_SVIDEO:
		case MEDIA_ENT_F_CONN_COMPOSITE:
			ret = media_create_pad_link(entity, 0, decoder,
						    DEMOD_PAD_IF_INPUT,
						    flags);
			break;
		default:
			continue;
		}
		if (ret)
			return ret;

		flags = 0;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_mc_create_media_graph);

int v4l_enable_media_source(struct video_device *vdev)
{
	struct media_device *mdev = vdev->entity.graph_obj.mdev;
	int ret;

	if (!mdev || !mdev->enable_source)
		return 0;
	ret = mdev->enable_source(&vdev->entity, &vdev->pipe);
	if (ret)
		return -EBUSY;
	return 0;
}
EXPORT_SYMBOL_GPL(v4l_enable_media_source);

void v4l_disable_media_source(struct video_device *vdev)
{
	struct media_device *mdev = vdev->entity.graph_obj.mdev;

	if (mdev && mdev->disable_source)
		mdev->disable_source(&vdev->entity);
}
EXPORT_SYMBOL_GPL(v4l_disable_media_source);

int v4l_vb2q_enable_media_source(struct vb2_queue *q)
{
	struct v4l2_fh *fh = q->owner;

	if (fh && fh->vdev)
		return v4l_enable_media_source(fh->vdev);
	return 0;
}
EXPORT_SYMBOL_GPL(v4l_vb2q_enable_media_source);

/* -----------------------------------------------------------------------------
 * Pipeline power management
 *
 * Entities must be powered up when part of a pipeline that contains at least
 * one open video device node.
 *
 * To achieve this use the entity use_count field to track the number of users.
 * For entities corresponding to video device nodes the use_count field stores
 * the users count of the node. For entities corresponding to subdevs the
 * use_count field stores the total number of users of all video device nodes
 * in the pipeline.
 *
 * The v4l2_pipeline_pm_use() function must be called in the open() and
 * close() handlers of video device nodes. It increments or decrements the use
 * count of all subdev entities in the pipeline.
 *
 * To react to link management on powered pipelines, the link setup notification
 * callback updates the use count of all entities in the source and sink sides
 * of the link.
 */

/*
 * pipeline_pm_use_count - Count the number of users of a pipeline
 * @entity: The entity
 *
 * Return the total number of users of all video device nodes in the pipeline.
 */
static int pipeline_pm_use_count(struct media_entity *entity,
	struct media_entity_graph *graph)
{
	int use = 0;

	media_entity_graph_walk_start(graph, entity);

	while ((entity = media_entity_graph_walk_next(graph))) {
		if (is_media_entity_v4l2_video_device(entity))
			use += entity->use_count;
	}

	return use;
}

/*
 * pipeline_pm_power_one - Apply power change to an entity
 * @entity: The entity
 * @change: Use count change
 *
 * Change the entity use count by @change. If the entity is a subdev update its
 * power state by calling the core::s_power operation when the use count goes
 * from 0 to != 0 or from != 0 to 0.
 *
 * Return 0 on success or a negative error code on failure.
 */
static int pipeline_pm_power_one(struct media_entity *entity, int change)
{
	struct v4l2_subdev *subdev;
	int ret;

	subdev = is_media_entity_v4l2_subdev(entity)
	       ? media_entity_to_v4l2_subdev(entity) : NULL;

	if (entity->use_count == 0 && change > 0 && subdev != NULL) {
		ret = v4l2_subdev_call(subdev, core, s_power, 1);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return ret;
	}

	entity->use_count += change;
	WARN_ON(entity->use_count < 0);

	if (entity->use_count == 0 && change < 0 && subdev != NULL)
		v4l2_subdev_call(subdev, core, s_power, 0);

	return 0;
}

/*
 * pipeline_pm_power - Apply power change to all entities in a pipeline
 * @entity: The entity
 * @change: Use count change
 *
 * Walk the pipeline to update the use count and the power state of all non-node
 * entities.
 *
 * Return 0 on success or a negative error code on failure.
 */
static int pipeline_pm_power(struct media_entity *entity, int change,
	struct media_entity_graph *graph)
{
	struct media_entity *first = entity;
	int ret = 0;

	if (!change)
		return 0;

	media_entity_graph_walk_start(graph, entity);

	while (!ret && (entity = media_entity_graph_walk_next(graph)))
		if (is_media_entity_v4l2_subdev(entity))
			ret = pipeline_pm_power_one(entity, change);

	if (!ret)
		return ret;

	media_entity_graph_walk_start(graph, first);

	while ((first = media_entity_graph_walk_next(graph))
	       && first != entity)
		if (is_media_entity_v4l2_subdev(first))
			pipeline_pm_power_one(first, -change);

	return ret;
}

int v4l2_pipeline_pm_use(struct media_entity *entity, int use)
{
	struct media_device *mdev = entity->graph_obj.mdev;
	int change = use ? 1 : -1;
	int ret;

	mutex_lock(&mdev->graph_mutex);

	/* Apply use count to node. */
	entity->use_count += change;
	WARN_ON(entity->use_count < 0);

	/* Apply power change to connected non-nodes. */
	ret = pipeline_pm_power(entity, change, &mdev->pm_count_walk);
	if (ret < 0)
		entity->use_count -= change;

	mutex_unlock(&mdev->graph_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_pipeline_pm_use);

int v4l2_pipeline_link_notify(struct media_link *link, u32 flags,
			      unsigned int notification)
{
	struct media_entity_graph *graph = &link->graph_obj.mdev->pm_count_walk;
	struct media_entity *source = link->source->entity;
	struct media_entity *sink = link->sink->entity;
	int source_use;
	int sink_use;
	int ret = 0;

	source_use = pipeline_pm_use_count(source, graph);
	sink_use = pipeline_pm_use_count(sink, graph);

	if (notification == MEDIA_DEV_NOTIFY_POST_LINK_CH &&
	    !(flags & MEDIA_LNK_FL_ENABLED)) {
		/* Powering off entities is assumed to never fail. */
		pipeline_pm_power(source, -sink_use, graph);
		pipeline_pm_power(sink, -source_use, graph);
		return 0;
	}

	if (notification == MEDIA_DEV_NOTIFY_PRE_LINK_CH &&
		(flags & MEDIA_LNK_FL_ENABLED)) {

		ret = pipeline_pm_power(source, sink_use, graph);
		if (ret < 0)
			return ret;

		ret = pipeline_pm_power(sink, source_use, graph);
		if (ret < 0)
			pipeline_pm_power(source, -sink_use, graph);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_pipeline_link_notify);

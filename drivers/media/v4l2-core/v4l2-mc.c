// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Media Controller ancillary functions
 *
 * Copyright (c) 2016 Mauro Carvalho Chehab <mchehab@kernel.org>
 * Copyright (C) 2016 Shuah Khan <shuahkh@osg.samsung.com>
 * Copyright (C) 2006-2010 Nokia Corporation
 * Copyright (c) 2016 Intel Corporation.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/usb.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-core.h>

int v4l2_mc_create_media_graph(struct media_device *mdev)

{
	struct media_entity *entity;
	struct media_entity *if_vid = NULL, *if_aud = NULL;
	struct media_entity *tuner = NULL, *decoder = NULL;
	struct media_entity *io_v4l = NULL, *io_vbi = NULL, *io_swradio = NULL;
	bool is_webcam = false;
	u32 flags;
	int ret, pad_sink, pad_source;

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
	if (!io_v4l && !io_vbi && !io_swradio) {
		dev_warn(mdev->dev, "Didn't find any I/O entity\n");
		return -EINVAL;
	}

	/*
	 * Here, webcams are modelled on a very simple way: the sensor is
	 * connected directly to the I/O entity. All dirty details, like
	 * scaler and crop HW are hidden. While such mapping is not enough
	 * for mc-centric hardware, it is enough for v4l2 interface centric
	 * PC-consumer's hardware.
	 */
	if (is_webcam) {
		if (!io_v4l) {
			dev_warn(mdev->dev, "Didn't find a MEDIA_ENT_F_IO_V4L\n");
			return -EINVAL;
		}

		media_device_for_each_entity(entity, mdev) {
			if (entity->function != MEDIA_ENT_F_CAM_SENSOR)
				continue;
			ret = media_create_pad_link(entity, 0,
						    io_v4l, 0,
						    MEDIA_LNK_FL_ENABLED);
			if (ret) {
				dev_warn(mdev->dev, "Failed to create a sensor link\n");
				return ret;
			}
		}
		if (!decoder)
			return 0;
	}

	/* The device isn't a webcam. So, it should have a decoder */
	if (!decoder) {
		dev_warn(mdev->dev, "Decoder not found\n");
		return -EINVAL;
	}

	/* Link the tuner and IF video output pads */
	if (tuner) {
		if (if_vid) {
			pad_source = media_get_pad_index(tuner,
							 MEDIA_PAD_FL_SOURCE,
							 PAD_SIGNAL_ANALOG);
			pad_sink = media_get_pad_index(if_vid,
						       MEDIA_PAD_FL_SINK,
						       PAD_SIGNAL_ANALOG);
			if (pad_source < 0 || pad_sink < 0) {
				dev_warn(mdev->dev, "Couldn't get tuner and/or PLL pad(s): (%d, %d)\n",
					 pad_source, pad_sink);
				return -EINVAL;
			}
			ret = media_create_pad_link(tuner, pad_source,
						    if_vid, pad_sink,
						    MEDIA_LNK_FL_ENABLED);
			if (ret) {
				dev_warn(mdev->dev, "Couldn't create tuner->PLL link)\n");
				return ret;
			}

			pad_source = media_get_pad_index(if_vid,
							 MEDIA_PAD_FL_SOURCE,
							 PAD_SIGNAL_ANALOG);
			pad_sink = media_get_pad_index(decoder,
						       MEDIA_PAD_FL_SINK,
						       PAD_SIGNAL_ANALOG);
			if (pad_source < 0 || pad_sink < 0) {
				dev_warn(mdev->dev, "get decoder and/or PLL pad(s): (%d, %d)\n",
					 pad_source, pad_sink);
				return -EINVAL;
			}
			ret = media_create_pad_link(if_vid, pad_source,
						    decoder, pad_sink,
						    MEDIA_LNK_FL_ENABLED);
			if (ret) {
				dev_warn(mdev->dev, "couldn't link PLL to decoder\n");
				return ret;
			}
		} else {
			pad_source = media_get_pad_index(tuner,
							 MEDIA_PAD_FL_SOURCE,
							 PAD_SIGNAL_ANALOG);
			pad_sink = media_get_pad_index(decoder,
						       MEDIA_PAD_FL_SINK,
						       PAD_SIGNAL_ANALOG);
			if (pad_source < 0 || pad_sink < 0) {
				dev_warn(mdev->dev, "couldn't get tuner and/or decoder pad(s): (%d, %d)\n",
					 pad_source, pad_sink);
				return -EINVAL;
			}
			ret = media_create_pad_link(tuner, pad_source,
						    decoder, pad_sink,
						    MEDIA_LNK_FL_ENABLED);
			if (ret)
				return ret;
		}

		if (if_aud) {
			pad_source = media_get_pad_index(tuner,
							 MEDIA_PAD_FL_SOURCE,
							 PAD_SIGNAL_AUDIO);
			pad_sink = media_get_pad_index(if_aud,
						       MEDIA_PAD_FL_SINK,
						       PAD_SIGNAL_AUDIO);
			if (pad_source < 0 || pad_sink < 0) {
				dev_warn(mdev->dev, "couldn't get tuner and/or decoder pad(s) for audio: (%d, %d)\n",
					 pad_source, pad_sink);
				return -EINVAL;
			}
			ret = media_create_pad_link(tuner, pad_source,
						    if_aud, pad_sink,
						    MEDIA_LNK_FL_ENABLED);
			if (ret) {
				dev_warn(mdev->dev, "couldn't link tuner->audio PLL\n");
				return ret;
			}
		} else {
			if_aud = tuner;
		}

	}

	/* Create demod to V4L, VBI and SDR radio links */
	if (io_v4l) {
		pad_source = media_get_pad_index(decoder, MEDIA_PAD_FL_SOURCE,
						 PAD_SIGNAL_DV);
		if (pad_source < 0) {
			dev_warn(mdev->dev, "couldn't get decoder output pad for V4L I/O\n");
			return -EINVAL;
		}
		ret = media_create_pad_link(decoder, pad_source,
					    io_v4l, 0,
					    MEDIA_LNK_FL_ENABLED);
		if (ret) {
			dev_warn(mdev->dev, "couldn't link decoder output to V4L I/O\n");
			return ret;
		}
	}

	if (io_swradio) {
		pad_source = media_get_pad_index(decoder, MEDIA_PAD_FL_SOURCE,
						 PAD_SIGNAL_DV);
		if (pad_source < 0) {
			dev_warn(mdev->dev, "couldn't get decoder output pad for SDR\n");
			return -EINVAL;
		}
		ret = media_create_pad_link(decoder, pad_source,
					    io_swradio, 0,
					    MEDIA_LNK_FL_ENABLED);
		if (ret) {
			dev_warn(mdev->dev, "couldn't link decoder output to SDR\n");
			return ret;
		}
	}

	if (io_vbi) {
		pad_source = media_get_pad_index(decoder, MEDIA_PAD_FL_SOURCE,
						 PAD_SIGNAL_DV);
		if (pad_source < 0) {
			dev_warn(mdev->dev, "couldn't get decoder output pad for VBI\n");
			return -EINVAL;
		}
		ret = media_create_pad_link(decoder, pad_source,
					    io_vbi, 0,
					    MEDIA_LNK_FL_ENABLED);
		if (ret) {
			dev_warn(mdev->dev, "couldn't link decoder output to VBI\n");
			return ret;
		}
	}

	/* Create links for the media connectors */
	flags = MEDIA_LNK_FL_ENABLED;
	media_device_for_each_entity(entity, mdev) {
		switch (entity->function) {
		case MEDIA_ENT_F_CONN_RF:
			if (!tuner)
				continue;
			pad_sink = media_get_pad_index(tuner, MEDIA_PAD_FL_SINK,
						       PAD_SIGNAL_ANALOG);
			if (pad_sink < 0) {
				dev_warn(mdev->dev, "couldn't get tuner analog pad sink\n");
				return -EINVAL;
			}
			ret = media_create_pad_link(entity, 0, tuner,
						    pad_sink,
						    flags);
			break;
		case MEDIA_ENT_F_CONN_SVIDEO:
		case MEDIA_ENT_F_CONN_COMPOSITE:
			pad_sink = media_get_pad_index(decoder,
						       MEDIA_PAD_FL_SINK,
						       PAD_SIGNAL_ANALOG);
			if (pad_sink < 0) {
				dev_warn(mdev->dev, "couldn't get decoder analog pad sink\n");
				return -EINVAL;
			}
			ret = media_create_pad_link(entity, 0, decoder,
						    pad_sink,
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
	int ret = 0, err;

	if (!mdev)
		return 0;

	mutex_lock(&mdev->graph_mutex);
	if (!mdev->enable_source)
		goto end;
	err = mdev->enable_source(&vdev->entity, &vdev->pipe);
	if (err)
		ret = -EBUSY;
end:
	mutex_unlock(&mdev->graph_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(v4l_enable_media_source);

void v4l_disable_media_source(struct video_device *vdev)
{
	struct media_device *mdev = vdev->entity.graph_obj.mdev;

	if (mdev) {
		mutex_lock(&mdev->graph_mutex);
		if (mdev->disable_source)
			mdev->disable_source(&vdev->entity);
		mutex_unlock(&mdev->graph_mutex);
	}
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

int v4l2_create_fwnode_links_to_pad(struct v4l2_subdev *src_sd,
				    struct media_pad *sink, u32 flags)
{
	struct fwnode_handle *endpoint;

	if (!(sink->flags & MEDIA_PAD_FL_SINK))
		return -EINVAL;

	fwnode_graph_for_each_endpoint(dev_fwnode(src_sd->dev), endpoint) {
		struct fwnode_handle *remote_ep;
		int src_idx, sink_idx, ret;
		struct media_pad *src;

		src_idx = media_entity_get_fwnode_pad(&src_sd->entity,
						      endpoint,
						      MEDIA_PAD_FL_SOURCE);
		if (src_idx < 0)
			continue;

		remote_ep = fwnode_graph_get_remote_endpoint(endpoint);
		if (!remote_ep)
			continue;

		/*
		 * ask the sink to verify it owns the remote endpoint,
		 * and translate to a sink pad.
		 */
		sink_idx = media_entity_get_fwnode_pad(sink->entity,
						       remote_ep,
						       MEDIA_PAD_FL_SINK);
		fwnode_handle_put(remote_ep);

		if (sink_idx < 0 || sink_idx != sink->index)
			continue;

		/*
		 * the source endpoint corresponds to one of its source pads,
		 * the source endpoint connects to an endpoint at the sink
		 * entity, and the sink endpoint corresponds to the sink
		 * pad requested, so we have found an endpoint connection
		 * that works, create the media link for it.
		 */

		src = &src_sd->entity.pads[src_idx];

		/* skip if link already exists */
		if (media_entity_find_link(src, sink))
			continue;

		dev_dbg(src_sd->dev, "creating link %s:%d -> %s:%d\n",
			src_sd->entity.name, src_idx,
			sink->entity->name, sink_idx);

		ret = media_create_pad_link(&src_sd->entity, src_idx,
					    sink->entity, sink_idx, flags);
		if (ret) {
			dev_err(src_sd->dev,
				"link %s:%d -> %s:%d failed with %d\n",
				src_sd->entity.name, src_idx,
				sink->entity->name, sink_idx, ret);

			fwnode_handle_put(endpoint);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_create_fwnode_links_to_pad);

int v4l2_create_fwnode_links(struct v4l2_subdev *src_sd,
			     struct v4l2_subdev *sink_sd)
{
	unsigned int i;

	for (i = 0; i < sink_sd->entity.num_pads; i++) {
		struct media_pad *pad = &sink_sd->entity.pads[i];
		int ret;

		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			continue;

		ret = v4l2_create_fwnode_links_to_pad(src_sd, pad, 0);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_create_fwnode_links);

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
 * The v4l2_pipeline_pm_{get, put}() functions must be called in the open() and
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
	struct media_graph *graph)
{
	int use = 0;

	media_graph_walk_start(graph, entity);

	while ((entity = media_graph_walk_next(graph))) {
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
	struct media_graph *graph)
{
	struct media_entity *first = entity;
	int ret = 0;

	if (!change)
		return 0;

	media_graph_walk_start(graph, entity);

	while (!ret && (entity = media_graph_walk_next(graph)))
		if (is_media_entity_v4l2_subdev(entity))
			ret = pipeline_pm_power_one(entity, change);

	if (!ret)
		return ret;

	media_graph_walk_start(graph, first);

	while ((first = media_graph_walk_next(graph))
	       && first != entity)
		if (is_media_entity_v4l2_subdev(first))
			pipeline_pm_power_one(first, -change);

	return ret;
}

static int v4l2_pipeline_pm_use(struct media_entity *entity, unsigned int use)
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

int v4l2_pipeline_pm_get(struct media_entity *entity)
{
	return v4l2_pipeline_pm_use(entity, 1);
}
EXPORT_SYMBOL_GPL(v4l2_pipeline_pm_get);

void v4l2_pipeline_pm_put(struct media_entity *entity)
{
	/* Powering off entities shouldn't fail. */
	WARN_ON(v4l2_pipeline_pm_use(entity, 0));
}
EXPORT_SYMBOL_GPL(v4l2_pipeline_pm_put);

int v4l2_pipeline_link_notify(struct media_link *link, u32 flags,
			      unsigned int notification)
{
	struct media_graph *graph = &link->graph_obj.mdev->pm_count_walk;
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

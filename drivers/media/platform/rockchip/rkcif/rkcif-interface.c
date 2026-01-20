// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip Camera Interface (CIF) Driver
 *
 * Copyright (C) 2025 Michael Riesch <michael.riesch@wolfvision.net>
 * Copyright (C) 2025 Collabora, Ltd.
 */

#include <media/v4l2-common.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>

#include "rkcif-common.h"
#include "rkcif-interface.h"

static inline struct rkcif_interface *to_rkcif_interface(struct v4l2_subdev *sd)
{
	return container_of(sd, struct rkcif_interface, sd);
}

static const struct media_entity_operations rkcif_interface_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
	.has_pad_interdep = v4l2_subdev_has_pad_interdep,
};

static int rkcif_interface_set_fmt(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_format *format)
{
	struct rkcif_interface *interface = to_rkcif_interface(sd);
	const struct rkcif_input_fmt *input;
	struct v4l2_mbus_framefmt *sink, *src;
	struct v4l2_rect *crop;
	u32 other_pad, other_stream;
	int ret;

	/* the format on the source pad always matches the sink pad */
	if (format->pad == RKCIF_IF_PAD_SRC)
		return v4l2_subdev_get_fmt(sd, state, format);

	input = rkcif_interface_find_input_fmt(interface, true,
					       format->format.code);
	format->format.code = input->mbus_code;

	sink = v4l2_subdev_state_get_format(state, format->pad, format->stream);
	if (!sink)
		return -EINVAL;

	*sink = format->format;

	/* propagate the format to the source pad */
	src = v4l2_subdev_state_get_opposite_stream_format(state, format->pad,
							   format->stream);
	if (!src)
		return -EINVAL;

	*src = *sink;

	ret = v4l2_subdev_routing_find_opposite_end(&state->routing,
						    format->pad, format->stream,
						    &other_pad, &other_stream);
	if (ret)
		return -EINVAL;

	crop = v4l2_subdev_state_get_crop(state, other_pad, other_stream);
	if (!crop)
		return -EINVAL;

	/* reset crop */
	crop->left = 0;
	crop->top = 0;
	crop->width = sink->width;
	crop->height = sink->height;

	return 0;
}

static int rkcif_interface_get_sel(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_selection *sel)
{
	struct v4l2_mbus_framefmt *sink;
	struct v4l2_rect *crop;
	int ret = 0;

	if (sel->pad != RKCIF_IF_PAD_SRC)
		return -EINVAL;

	sink = v4l2_subdev_state_get_opposite_stream_format(state, sel->pad,
							    sel->stream);
	if (!sink)
		return -EINVAL;

	crop = v4l2_subdev_state_get_crop(state, sel->pad, sel->stream);
	if (!crop)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = sink->width;
		sel->r.height = sink->height;
		break;
	case V4L2_SEL_TGT_CROP:
		sel->r = *crop;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int rkcif_interface_set_sel(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_selection *sel)
{
	struct v4l2_mbus_framefmt *sink, *src;
	struct v4l2_rect *crop;

	if (sel->pad != RKCIF_IF_PAD_SRC || sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	sink = v4l2_subdev_state_get_opposite_stream_format(state, sel->pad,
							    sel->stream);
	if (!sink)
		return -EINVAL;

	src = v4l2_subdev_state_get_format(state, sel->pad, sel->stream);
	if (!src)
		return -EINVAL;

	crop = v4l2_subdev_state_get_crop(state, sel->pad, sel->stream);
	if (!crop)
		return -EINVAL;

	*crop = sel->r;

	src->height = sel->r.height;
	src->width = sel->r.width;

	return 0;
}

static int rkcif_interface_set_routing(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       enum v4l2_subdev_format_whence which,
				       struct v4l2_subdev_krouting *routing)
{
	int ret;

	ret = v4l2_subdev_routing_validate(sd, routing,
					   V4L2_SUBDEV_ROUTING_ONLY_1_TO_1);
	if (ret)
		return ret;

	for (unsigned int i = 0; i < routing->num_routes; i++) {
		const struct v4l2_subdev_route *route = &routing->routes[i];

		if (route->source_stream >= RKCIF_ID_MAX)
			return -EINVAL;
	}

	ret = v4l2_subdev_set_routing(sd, state, routing);

	return ret;
}

static int rkcif_interface_apply_crop(struct rkcif_stream *stream,
				      struct v4l2_subdev_state *state)
{
	struct rkcif_interface *interface = stream->interface;
	struct v4l2_rect *crop;

	crop = v4l2_subdev_state_get_crop(state, RKCIF_IF_PAD_SRC, stream->id);
	if (!crop)
		return -EINVAL;

	if (interface->set_crop)
		interface->set_crop(stream, crop->left, crop->top);

	return 0;
}

static int rkcif_interface_enable_streams(struct v4l2_subdev *sd,
					  struct v4l2_subdev_state *state,
					  u32 pad, u64 streams_mask)
{
	struct rkcif_interface *interface = to_rkcif_interface(sd);
	struct rkcif_stream *stream;
	struct v4l2_subdev_route *route;
	struct v4l2_subdev *remote_sd;
	struct media_pad *remote_pad;
	u64 mask;

	remote_pad =
		media_pad_remote_pad_first(&sd->entity.pads[RKCIF_IF_PAD_SINK]);
	remote_sd = media_entity_to_v4l2_subdev(remote_pad->entity);

	/* DVP has one crop setting for all IDs */
	if (interface->type == RKCIF_IF_DVP) {
		stream = &interface->streams[RKCIF_ID0];
		rkcif_interface_apply_crop(stream, state);
	} else {
		for_each_active_route(&state->routing, route) {
			stream = &interface->streams[route->sink_stream];
			rkcif_interface_apply_crop(stream, state);
		}
	}

	mask = v4l2_subdev_state_xlate_streams(state, RKCIF_IF_PAD_SINK,
					       RKCIF_IF_PAD_SRC, &streams_mask);

	return v4l2_subdev_enable_streams(remote_sd, remote_pad->index, mask);
}

static int rkcif_interface_disable_streams(struct v4l2_subdev *sd,
					   struct v4l2_subdev_state *state,
					   u32 pad, u64 streams_mask)
{
	struct v4l2_subdev *remote_sd;
	struct media_pad *remote_pad;
	u64 mask;

	remote_pad =
		media_pad_remote_pad_first(&sd->entity.pads[RKCIF_IF_PAD_SINK]);
	remote_sd = media_entity_to_v4l2_subdev(remote_pad->entity);

	mask = v4l2_subdev_state_xlate_streams(state, RKCIF_IF_PAD_SINK,
					       RKCIF_IF_PAD_SRC, &streams_mask);

	return v4l2_subdev_disable_streams(remote_sd, remote_pad->index, mask);
}

static const struct v4l2_subdev_pad_ops rkcif_interface_pad_ops = {
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = rkcif_interface_set_fmt,
	.get_selection = rkcif_interface_get_sel,
	.set_selection = rkcif_interface_set_sel,
	.set_routing = rkcif_interface_set_routing,
	.enable_streams = rkcif_interface_enable_streams,
	.disable_streams = rkcif_interface_disable_streams,
};

static const struct v4l2_subdev_ops rkcif_interface_ops = {
	.pad = &rkcif_interface_pad_ops,
};

static int rkcif_interface_init_state(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state)
{
	struct rkcif_interface *interface = to_rkcif_interface(sd);
	struct v4l2_subdev_route routes[] = {
		{
			.sink_pad = RKCIF_IF_PAD_SINK,
			.sink_stream = 0,
			.source_pad = RKCIF_IF_PAD_SRC,
			.source_stream = 0,
			.flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE,
		},
	};
	struct v4l2_subdev_krouting routing = {
		.len_routes = ARRAY_SIZE(routes),
		.num_routes = ARRAY_SIZE(routes),
		.routes = routes,
	};
	const struct v4l2_mbus_framefmt dvp_default_format = {
		.width = 3840,
		.height = 2160,
		.code = MEDIA_BUS_FMT_YUYV8_1X16,
		.field = V4L2_FIELD_NONE,
		.colorspace = V4L2_COLORSPACE_REC709,
		.ycbcr_enc = V4L2_YCBCR_ENC_709,
		.quantization = V4L2_QUANTIZATION_LIM_RANGE,
		.xfer_func = V4L2_XFER_FUNC_NONE,
	};
	const struct v4l2_mbus_framefmt mipi_default_format = {
		.width = 3840,
		.height = 2160,
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.field = V4L2_FIELD_NONE,
		.colorspace = V4L2_COLORSPACE_RAW,
		.ycbcr_enc = V4L2_YCBCR_ENC_601,
		.quantization = V4L2_QUANTIZATION_FULL_RANGE,
		.xfer_func = V4L2_XFER_FUNC_NONE,
	};
	const struct v4l2_mbus_framefmt *default_format;
	int ret;

	default_format = (interface->type == RKCIF_IF_DVP) ?
				 &dvp_default_format :
				 &mipi_default_format;

	ret = v4l2_subdev_set_routing_with_fmt(sd, state, &routing,
					       default_format);

	return ret;
}

static const struct v4l2_subdev_internal_ops rkcif_interface_internal_ops = {
	.init_state = rkcif_interface_init_state,
};

static int rkcif_interface_add(struct rkcif_interface *interface)
{
	struct rkcif_device *rkcif = interface->rkcif;
	struct rkcif_remote *remote;
	struct v4l2_async_notifier *ntf = &rkcif->notifier;
	struct v4l2_fwnode_endpoint *vep = &interface->vep;
	struct device *dev = rkcif->dev;
	struct fwnode_handle *ep;
	u32 dvp_clk_delay = 0;
	int ret;

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), interface->index,
					     0, 0);
	if (!ep)
		return -ENODEV;

	vep->bus_type = V4L2_MBUS_UNKNOWN;
	ret = v4l2_fwnode_endpoint_parse(ep, vep);
	if (ret)
		goto complete;

	if (interface->type == RKCIF_IF_DVP) {
		if (vep->bus_type != V4L2_MBUS_BT656 &&
		    vep->bus_type != V4L2_MBUS_PARALLEL) {
			ret = dev_err_probe(dev, -EINVAL,
					    "unsupported bus type\n");
			goto complete;
		}

		fwnode_property_read_u32(ep, "rockchip,dvp-clk-delay",
					 &dvp_clk_delay);
		interface->dvp.dvp_clk_delay = dvp_clk_delay;
	}

	remote = v4l2_async_nf_add_fwnode_remote(ntf, ep, struct rkcif_remote);
	if (IS_ERR(remote)) {
		ret = PTR_ERR(remote);
		goto complete;
	}

	remote->interface = interface;
	interface->remote = remote;
	interface->status = RKCIF_IF_ACTIVE;
	ret = 0;

complete:
	fwnode_handle_put(ep);

	return ret;
}

int rkcif_interface_register(struct rkcif_device *rkcif,
			     struct rkcif_interface *interface)
{
	struct media_pad *pads = interface->pads;
	struct v4l2_subdev *sd = &interface->sd;
	int ret;

	interface->rkcif = rkcif;

	v4l2_subdev_init(sd, &rkcif_interface_ops);
	sd->dev = rkcif->dev;
	sd->entity.ops = &rkcif_interface_media_ops;
	sd->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_STREAMS;
	sd->internal_ops = &rkcif_interface_internal_ops;
	sd->owner = THIS_MODULE;

	if (interface->type == RKCIF_IF_DVP)
		snprintf(sd->name, sizeof(sd->name), "rkcif-dvp0");
	else if (interface->type == RKCIF_IF_MIPI)
		snprintf(sd->name, sizeof(sd->name), "rkcif-mipi%d",
			 interface->index - RKCIF_MIPI_BASE);

	pads[RKCIF_IF_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[RKCIF_IF_PAD_SRC].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, RKCIF_IF_PAD_MAX, pads);
	if (ret)
		goto err;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret)
		goto err_entity_cleanup;

	ret = v4l2_device_register_subdev(&rkcif->v4l2_dev, sd);
	if (ret) {
		dev_err(sd->dev, "failed to register subdev\n");
		goto err_subdev_cleanup;
	}

	ret = rkcif_interface_add(interface);
	if (ret)
		goto err_subdev_unregister;

	return 0;

err_subdev_unregister:
	v4l2_device_unregister_subdev(sd);
err_subdev_cleanup:
	v4l2_subdev_cleanup(sd);
err_entity_cleanup:
	media_entity_cleanup(&sd->entity);
err:
	return ret;
}

void rkcif_interface_unregister(struct rkcif_interface *interface)
{
	struct v4l2_subdev *sd = &interface->sd;

	if (interface->status != RKCIF_IF_ACTIVE)
		return;

	v4l2_device_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
}

const struct rkcif_input_fmt *
rkcif_interface_find_input_fmt(struct rkcif_interface *interface, bool ret_def,
			       u32 mbus_code)
{
	const struct rkcif_input_fmt *fmt;

	WARN_ON(interface->in_fmts_num == 0);

	for (unsigned int i = 0; i < interface->in_fmts_num; i++) {
		fmt = &interface->in_fmts[i];
		if (fmt->mbus_code == mbus_code)
			return fmt;
	}
	if (ret_def)
		return &interface->in_fmts[0];
	else
		return NULL;
}

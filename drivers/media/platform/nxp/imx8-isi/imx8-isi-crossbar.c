// SPDX-License-Identifier: GPL-2.0-only
/*
 * i.MX8 ISI - Input crossbar switch
 *
 * Copyright (c) 2022 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>

#include "imx8-isi-core.h"

static inline struct mxc_isi_crossbar *to_isi_crossbar(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mxc_isi_crossbar, sd);
}

static int mxc_isi_crossbar_gasket_enable(struct mxc_isi_crossbar *xbar,
					  struct v4l2_subdev_state *state,
					  struct v4l2_subdev *remote_sd,
					  u32 remote_pad, unsigned int port)
{
	struct mxc_isi_dev *isi = xbar->isi;
	const struct mxc_gasket_ops *gasket_ops = isi->pdata->gasket_ops;
	const struct v4l2_mbus_framefmt *fmt;
	struct v4l2_mbus_frame_desc fd;
	int ret;

	if (!gasket_ops)
		return 0;

	/*
	 * Configure and enable the gasket with the frame size and CSI-2 data
	 * type. For YUV422 8-bit, enable dual component mode unconditionally,
	 * to match the configuration of the CSIS.
	 */

	ret = v4l2_subdev_call(remote_sd, pad, get_frame_desc, remote_pad, &fd);
	if (ret) {
		dev_err(isi->dev,
			"failed to get frame descriptor from '%s':%u: %d\n",
			remote_sd->name, remote_pad, ret);
		return ret;
	}

	if (fd.num_entries != 1) {
		dev_err(isi->dev, "invalid frame descriptor for '%s':%u\n",
			remote_sd->name, remote_pad);
		return -EINVAL;
	}

	fmt = v4l2_subdev_state_get_format(state, port, 0);
	if (!fmt)
		return -EINVAL;

	gasket_ops->enable(isi, &fd, fmt, port);
	return 0;
}

static void mxc_isi_crossbar_gasket_disable(struct mxc_isi_crossbar *xbar,
					    unsigned int port)
{
	struct mxc_isi_dev *isi = xbar->isi;
	const struct mxc_gasket_ops *gasket_ops = isi->pdata->gasket_ops;

	if (!gasket_ops)
		return;

	gasket_ops->disable(isi, port);
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev operations
 */

static const struct v4l2_mbus_framefmt mxc_isi_crossbar_default_format = {
	.code = MXC_ISI_DEF_MBUS_CODE_SINK,
	.width = MXC_ISI_DEF_WIDTH,
	.height = MXC_ISI_DEF_HEIGHT,
	.field = V4L2_FIELD_NONE,
	.colorspace = MXC_ISI_DEF_COLOR_SPACE,
	.ycbcr_enc = MXC_ISI_DEF_YCBCR_ENC,
	.quantization = MXC_ISI_DEF_QUANTIZATION,
	.xfer_func = MXC_ISI_DEF_XFER_FUNC,
};

static int __mxc_isi_crossbar_set_routing(struct v4l2_subdev *sd,
					  struct v4l2_subdev_state *state,
					  struct v4l2_subdev_krouting *routing)
{
	struct mxc_isi_crossbar *xbar = to_isi_crossbar(sd);
	struct v4l2_subdev_route *route;
	int ret;

	ret = v4l2_subdev_routing_validate(sd, routing,
					   V4L2_SUBDEV_ROUTING_NO_N_TO_1);
	if (ret)
		return ret;

	/* The memory input can be routed to the first pipeline only. */
	for_each_active_route(&state->routing, route) {
		if (route->sink_pad == xbar->num_sinks - 1 &&
		    route->source_pad != xbar->num_sinks) {
			dev_dbg(xbar->isi->dev,
				"invalid route from memory input (%u) to pipe %u\n",
				route->sink_pad,
				route->source_pad - xbar->num_sinks);
			return -EINVAL;
		}
	}

	return v4l2_subdev_set_routing_with_fmt(sd, state, routing,
						&mxc_isi_crossbar_default_format);
}

static struct v4l2_subdev *
mxc_isi_crossbar_xlate_streams(struct mxc_isi_crossbar *xbar,
			       struct v4l2_subdev_state *state,
			       u32 source_pad, u64 source_streams,
			       u32 *__sink_pad, u64 *__sink_streams,
			       u32 *remote_pad)
{
	struct v4l2_subdev_route *route;
	struct v4l2_subdev *sd;
	struct media_pad *pad;
	u64 sink_streams = 0;
	int sink_pad = -1;

	/*
	 * Translate the source pad and streams to the sink side. The routing
	 * validation forbids stream merging, so all matching entries in the
	 * routing table are guaranteed to have the same sink pad.
	 *
	 * TODO: This is likely worth a helper function, it could perhaps be
	 * supported by v4l2_subdev_state_xlate_streams() with pad1 set to -1.
	 */
	for_each_active_route(&state->routing, route) {
		if (route->source_pad != source_pad ||
		    !(source_streams & BIT(route->source_stream)))
			continue;

		sink_streams |= BIT(route->sink_stream);
		sink_pad = route->sink_pad;
	}

	if (sink_pad < 0) {
		dev_dbg(xbar->isi->dev,
			"no stream connected to pipeline %u\n",
			source_pad - xbar->num_sinks);
		return ERR_PTR(-EPIPE);
	}

	pad = media_pad_remote_pad_first(&xbar->pads[sink_pad]);
	sd = media_entity_to_v4l2_subdev(pad->entity);
	if (!sd) {
		dev_dbg(xbar->isi->dev,
			"no entity connected to crossbar input %u\n",
			sink_pad);
		return ERR_PTR(-EPIPE);
	}

	*__sink_pad = sink_pad;
	*__sink_streams = sink_streams;
	*remote_pad = pad->index;

	return sd;
}

static int mxc_isi_crossbar_init_state(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state)
{
	struct mxc_isi_crossbar *xbar = to_isi_crossbar(sd);
	struct v4l2_subdev_krouting routing = { };
	struct v4l2_subdev_route *routes;
	unsigned int i;
	int ret;

	/*
	 * Create a 1:1 mapping between pixel link inputs and outputs to
	 * pipelines by default.
	 */
	routes = kcalloc(xbar->num_sources, sizeof(*routes), GFP_KERNEL);
	if (!routes)
		return -ENOMEM;

	for (i = 0; i < xbar->num_sources; ++i) {
		struct v4l2_subdev_route *route = &routes[i];

		route->sink_pad = i;
		route->source_pad = i + xbar->num_sinks;
		route->flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE;
	}

	routing.num_routes = xbar->num_sources;
	routing.routes = routes;

	ret = __mxc_isi_crossbar_set_routing(sd, state, &routing);

	kfree(routes);

	return ret;
}

static int mxc_isi_crossbar_enum_mbus_code(struct v4l2_subdev *sd,
					   struct v4l2_subdev_state *state,
					   struct v4l2_subdev_mbus_code_enum *code)
{
	struct mxc_isi_crossbar *xbar = to_isi_crossbar(sd);
	const struct mxc_isi_bus_format_info *info;

	if (code->pad >= xbar->num_sinks) {
		const struct v4l2_mbus_framefmt *format;

		/*
		 * The media bus code on source pads is identical to the
		 * connected sink pad.
		 */
		if (code->index > 0)
			return -EINVAL;

		format = v4l2_subdev_state_get_opposite_stream_format(state,
								      code->pad,
								      code->stream);
		if (!format)
			return -EINVAL;

		code->code = format->code;

		return 0;
	}

	info = mxc_isi_bus_format_by_index(code->index, MXC_ISI_PIPE_PAD_SINK);
	if (!info)
		return -EINVAL;

	code->code = info->mbus_code;

	return 0;
}

static int mxc_isi_crossbar_set_fmt(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_format *fmt)
{
	struct mxc_isi_crossbar *xbar = to_isi_crossbar(sd);
	struct v4l2_mbus_framefmt *sink_fmt;
	struct v4l2_subdev_route *route;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE &&
	    media_pad_is_streaming(&xbar->pads[fmt->pad]))
		return -EBUSY;

	/*
	 * The source pad format is always identical to the sink pad format and
	 * can't be modified.
	 */
	if (fmt->pad >= xbar->num_sinks)
		return v4l2_subdev_get_fmt(sd, state, fmt);

	/* Validate the requested format. */
	if (!mxc_isi_bus_format_by_code(fmt->format.code, MXC_ISI_PIPE_PAD_SINK))
		fmt->format.code = MXC_ISI_DEF_MBUS_CODE_SINK;

	fmt->format.width = clamp_t(unsigned int, fmt->format.width,
				    MXC_ISI_MIN_WIDTH, MXC_ISI_MAX_WIDTH_CHAINED);
	fmt->format.height = clamp_t(unsigned int, fmt->format.height,
				     MXC_ISI_MIN_HEIGHT, MXC_ISI_MAX_HEIGHT);
	fmt->format.field = V4L2_FIELD_NONE;

	/*
	 * Set the format on the sink stream and propagate it to the source
	 * streams.
	 */
	sink_fmt = v4l2_subdev_state_get_format(state, fmt->pad, fmt->stream);
	if (!sink_fmt)
		return -EINVAL;

	*sink_fmt = fmt->format;

	/* TODO: A format propagation helper would be useful. */
	for_each_active_route(&state->routing, route) {
		struct v4l2_mbus_framefmt *source_fmt;

		if (route->sink_pad != fmt->pad ||
		    route->sink_stream != fmt->stream)
			continue;

		source_fmt = v4l2_subdev_state_get_format(state,
							  route->source_pad,
							  route->source_stream);
		if (!source_fmt)
			return -EINVAL;

		*source_fmt = fmt->format;
	}

	return 0;
}

static int mxc_isi_crossbar_set_routing(struct v4l2_subdev *sd,
					struct v4l2_subdev_state *state,
					enum v4l2_subdev_format_whence which,
					struct v4l2_subdev_krouting *routing)
{
	if (which == V4L2_SUBDEV_FORMAT_ACTIVE &&
	    media_entity_is_streaming(&sd->entity))
		return -EBUSY;

	return __mxc_isi_crossbar_set_routing(sd, state, routing);
}

static int mxc_isi_crossbar_enable_streams(struct v4l2_subdev *sd,
					   struct v4l2_subdev_state *state,
					   u32 pad, u64 streams_mask)
{
	struct mxc_isi_crossbar *xbar = to_isi_crossbar(sd);
	struct v4l2_subdev *remote_sd;
	struct mxc_isi_input *input;
	u64 sink_streams;
	u32 sink_pad;
	u32 remote_pad;
	int ret;

	remote_sd = mxc_isi_crossbar_xlate_streams(xbar, state, pad, streams_mask,
						   &sink_pad, &sink_streams,
						   &remote_pad);
	if (IS_ERR(remote_sd))
		return PTR_ERR(remote_sd);

	input = &xbar->inputs[sink_pad];

	/*
	 * TODO: Track per-stream enable counts to support multiplexed
	 * streams.
	 */
	if (!input->enable_count) {
		ret = mxc_isi_crossbar_gasket_enable(xbar, state, remote_sd,
						     remote_pad, sink_pad);
		if (ret)
			return ret;

		ret = v4l2_subdev_enable_streams(remote_sd, remote_pad,
						 sink_streams);
		if (ret) {
			dev_err(xbar->isi->dev,
				"failed to %s streams 0x%llx on '%s':%u: %d\n",
				"enable", sink_streams, remote_sd->name,
				remote_pad, ret);
			mxc_isi_crossbar_gasket_disable(xbar, sink_pad);
			return ret;
		}
	}

	input->enable_count++;

	return 0;
}

static int mxc_isi_crossbar_disable_streams(struct v4l2_subdev *sd,
					    struct v4l2_subdev_state *state,
					    u32 pad, u64 streams_mask)
{
	struct mxc_isi_crossbar *xbar = to_isi_crossbar(sd);
	struct v4l2_subdev *remote_sd;
	struct mxc_isi_input *input;
	u64 sink_streams;
	u32 sink_pad;
	u32 remote_pad;
	int ret = 0;

	remote_sd = mxc_isi_crossbar_xlate_streams(xbar, state, pad, streams_mask,
						   &sink_pad, &sink_streams,
						   &remote_pad);
	if (IS_ERR(remote_sd))
		return PTR_ERR(remote_sd);

	input = &xbar->inputs[sink_pad];

	input->enable_count--;

	if (!input->enable_count) {
		ret = v4l2_subdev_disable_streams(remote_sd, remote_pad,
						  sink_streams);
		if (ret)
			dev_err(xbar->isi->dev,
				"failed to %s streams 0x%llx on '%s':%u: %d\n",
				"disable", sink_streams, remote_sd->name,
				remote_pad, ret);

		mxc_isi_crossbar_gasket_disable(xbar, sink_pad);
	}

	return ret;
}

static const struct v4l2_subdev_pad_ops mxc_isi_crossbar_subdev_pad_ops = {
	.enum_mbus_code = mxc_isi_crossbar_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = mxc_isi_crossbar_set_fmt,
	.set_routing = mxc_isi_crossbar_set_routing,
	.enable_streams = mxc_isi_crossbar_enable_streams,
	.disable_streams = mxc_isi_crossbar_disable_streams,
};

static const struct v4l2_subdev_ops mxc_isi_crossbar_subdev_ops = {
	.pad = &mxc_isi_crossbar_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops mxc_isi_crossbar_internal_ops = {
	.init_state = mxc_isi_crossbar_init_state,
};

static const struct media_entity_operations mxc_isi_cross_entity_ops = {
	.get_fwnode_pad = v4l2_subdev_get_fwnode_pad_1_to_1,
	.link_validate	= v4l2_subdev_link_validate,
	.has_pad_interdep = v4l2_subdev_has_pad_interdep,
};

/* -----------------------------------------------------------------------------
 * Init & cleanup
 */

int mxc_isi_crossbar_init(struct mxc_isi_dev *isi)
{
	struct mxc_isi_crossbar *xbar = &isi->crossbar;
	struct v4l2_subdev *sd = &xbar->sd;
	unsigned int num_pads;
	unsigned int i;
	int ret;

	xbar->isi = isi;

	v4l2_subdev_init(sd, &mxc_isi_crossbar_subdev_ops);
	sd->internal_ops = &mxc_isi_crossbar_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_STREAMS;
	strscpy(sd->name, "crossbar", sizeof(sd->name));
	sd->dev = isi->dev;

	sd->entity.function = MEDIA_ENT_F_VID_MUX;
	sd->entity.ops = &mxc_isi_cross_entity_ops;

	/*
	 * The subdev has one sink and one source per port, plus one sink for
	 * the memory input.
	 */
	xbar->num_sinks = isi->pdata->num_ports + 1;
	xbar->num_sources = isi->pdata->num_ports;
	num_pads = xbar->num_sinks + xbar->num_sources;

	xbar->pads = kcalloc(num_pads, sizeof(*xbar->pads), GFP_KERNEL);
	if (!xbar->pads)
		return -ENOMEM;

	xbar->inputs = kcalloc(xbar->num_sinks, sizeof(*xbar->inputs),
			       GFP_KERNEL);
	if (!xbar->inputs) {
		ret = -ENOMEM;
		goto err_free;
	}

	for (i = 0; i < xbar->num_sinks; ++i)
		xbar->pads[i].flags = MEDIA_PAD_FL_SINK
				    | MEDIA_PAD_FL_MUST_CONNECT;
	for (i = 0; i < xbar->num_sources; ++i)
		xbar->pads[i + xbar->num_sinks].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&sd->entity, num_pads, xbar->pads);
	if (ret)
		goto err_free;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret < 0)
		goto err_entity;

	return 0;

err_entity:
	media_entity_cleanup(&sd->entity);
err_free:
	kfree(xbar->pads);
	kfree(xbar->inputs);

	return ret;
}

void mxc_isi_crossbar_cleanup(struct mxc_isi_crossbar *xbar)
{
	media_entity_cleanup(&xbar->sd.entity);
	kfree(xbar->pads);
	kfree(xbar->inputs);
}

int mxc_isi_crossbar_register(struct mxc_isi_crossbar *xbar)
{
	return v4l2_device_register_subdev(&xbar->isi->v4l2_dev, &xbar->sd);
}

void mxc_isi_crossbar_unregister(struct mxc_isi_crossbar *xbar)
{
}

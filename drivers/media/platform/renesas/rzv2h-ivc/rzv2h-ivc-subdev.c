// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/V2H(P) Input Video Control Block driver
 *
 * Copyright (C) 2025 Ideas on Board Oy
 */

#include "rzv2h-ivc.h"

#include <linux/media.h>
#include <linux/media-bus-format.h>
#include <linux/v4l2-mediabus.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-event.h>

#define RZV2H_IVC_N_INPUTS_PER_OUTPUT		6

/*
 * We support 8/10/12/14/16/20 bit input in any bayer order, but the output
 * format is fixed at 20-bits with the same order as the input.
 */
static const struct {
	u32 inputs[RZV2H_IVC_N_INPUTS_PER_OUTPUT];
	u32 output;
} rzv2h_ivc_formats[] = {
	{
		.inputs = {
			MEDIA_BUS_FMT_SBGGR8_1X8,
			MEDIA_BUS_FMT_SBGGR10_1X10,
			MEDIA_BUS_FMT_SBGGR12_1X12,
			MEDIA_BUS_FMT_SBGGR14_1X14,
			MEDIA_BUS_FMT_SBGGR16_1X16,
			MEDIA_BUS_FMT_SBGGR20_1X20,
		},
		.output = MEDIA_BUS_FMT_SBGGR20_1X20
	},
	{
		.inputs = {
			MEDIA_BUS_FMT_SGBRG8_1X8,
			MEDIA_BUS_FMT_SGBRG10_1X10,
			MEDIA_BUS_FMT_SGBRG12_1X12,
			MEDIA_BUS_FMT_SGBRG14_1X14,
			MEDIA_BUS_FMT_SGBRG16_1X16,
			MEDIA_BUS_FMT_SGBRG20_1X20,
		},
		.output = MEDIA_BUS_FMT_SGBRG20_1X20
	},
	{
		.inputs = {
			MEDIA_BUS_FMT_SGRBG8_1X8,
			MEDIA_BUS_FMT_SGRBG10_1X10,
			MEDIA_BUS_FMT_SGRBG12_1X12,
			MEDIA_BUS_FMT_SGRBG14_1X14,
			MEDIA_BUS_FMT_SGRBG16_1X16,
			MEDIA_BUS_FMT_SGRBG20_1X20,
		},
		.output = MEDIA_BUS_FMT_SGRBG20_1X20
	},
	{
		.inputs = {
			MEDIA_BUS_FMT_SRGGB8_1X8,
			MEDIA_BUS_FMT_SRGGB10_1X10,
			MEDIA_BUS_FMT_SRGGB12_1X12,
			MEDIA_BUS_FMT_SRGGB14_1X14,
			MEDIA_BUS_FMT_SRGGB16_1X16,
			MEDIA_BUS_FMT_SRGGB20_1X20,
		},
		.output = MEDIA_BUS_FMT_SRGGB20_1X20
	},
};

static u32 rzv2h_ivc_get_mbus_output_from_input(u32 mbus_code)
{
	unsigned int i, j;

	for (i = 0; i < ARRAY_SIZE(rzv2h_ivc_formats); i++) {
		for (j = 0; j < RZV2H_IVC_N_INPUTS_PER_OUTPUT; j++) {
			if (rzv2h_ivc_formats[i].inputs[j] == mbus_code)
				return rzv2h_ivc_formats[i].output;
		}
	}

	return 0;
}

static int rzv2h_ivc_enum_mbus_code(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_mbus_code_enum *code)
{
	const struct v4l2_mbus_framefmt *fmt;
	unsigned int order_index;
	unsigned int index;

	/*
	 * On the source pad, only the 20-bit format corresponding to the sink
	 * pad format's bayer order is supported.
	 */
	if (code->pad == RZV2H_IVC_SUBDEV_SOURCE_PAD) {
		if (code->index)
			return -EINVAL;

		fmt = v4l2_subdev_state_get_format(state,
						   RZV2H_IVC_SUBDEV_SINK_PAD);
		code->code = rzv2h_ivc_get_mbus_output_from_input(fmt->code);

		return 0;
	}

	if (code->index >= ARRAY_SIZE(rzv2h_ivc_formats) *
				      RZV2H_IVC_N_INPUTS_PER_OUTPUT)
		return -EINVAL;

	order_index = code->index / RZV2H_IVC_N_INPUTS_PER_OUTPUT;
	index = code->index % RZV2H_IVC_N_INPUTS_PER_OUTPUT;

	code->code = rzv2h_ivc_formats[order_index].inputs[index];

	return 0;
}

static int rzv2h_ivc_enum_frame_size(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     struct v4l2_subdev_frame_size_enum *fse)
{
	const struct v4l2_mbus_framefmt *fmt;

	if (fse->index > 0)
		return -EINVAL;

	if (fse->pad == RZV2H_IVC_SUBDEV_SOURCE_PAD) {
		fmt = v4l2_subdev_state_get_format(state,
						   RZV2H_IVC_SUBDEV_SINK_PAD);

		if (fse->code != rzv2h_ivc_get_mbus_output_from_input(fmt->code))
			return -EINVAL;

		fse->min_width = fmt->width;
		fse->max_width = fmt->width;
		fse->min_height = fmt->height;
		fse->max_height = fmt->height;

		return 0;
	}

	if (!rzv2h_ivc_get_mbus_output_from_input(fse->code))
		return -EINVAL;

	fse->min_width = RZV2H_IVC_MIN_WIDTH;
	fse->max_width = RZV2H_IVC_MAX_WIDTH;
	fse->min_height = RZV2H_IVC_MIN_HEIGHT;
	fse->max_height = RZV2H_IVC_MAX_HEIGHT;

	return 0;
}

static int rzv2h_ivc_set_fmt(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct v4l2_mbus_framefmt *src_fmt, *sink_fmt;

	if (format->pad == RZV2H_IVC_SUBDEV_SOURCE_PAD)
		return v4l2_subdev_get_fmt(sd, state, format);

	sink_fmt = v4l2_subdev_state_get_format(state,
						RZV2H_IVC_SUBDEV_SINK_PAD);

	sink_fmt->code = rzv2h_ivc_get_mbus_output_from_input(fmt->code) ?
			 fmt->code : rzv2h_ivc_formats[0].inputs[0];

	sink_fmt->width = clamp(fmt->width, RZV2H_IVC_MIN_WIDTH,
				RZV2H_IVC_MAX_WIDTH);
	sink_fmt->height = clamp(fmt->height, RZV2H_IVC_MIN_HEIGHT,
				 RZV2H_IVC_MAX_HEIGHT);

	*fmt = *sink_fmt;

	src_fmt = v4l2_subdev_state_get_format(state,
					       RZV2H_IVC_SUBDEV_SOURCE_PAD);
	*src_fmt = *sink_fmt;
	src_fmt->code = rzv2h_ivc_get_mbus_output_from_input(sink_fmt->code);

	return 0;
}

static int rzv2h_ivc_enable_streams(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state, u32 pad,
				    u64 streams_mask)
{
	/*
	 * We have a single source pad, which has a single stream. V4L2 core has
	 * already validated those things. The actual power-on and programming
	 * of registers will be done through the video device's .vidioc_streamon
	 * so there's nothing to actually do here...
	 */

	return 0;
}

static int rzv2h_ivc_disable_streams(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state, u32 pad,
				     u64 streams_mask)
{
	return 0;
}

static const struct v4l2_subdev_pad_ops rzv2h_ivc_pad_ops = {
	.enum_mbus_code		= rzv2h_ivc_enum_mbus_code,
	.enum_frame_size	= rzv2h_ivc_enum_frame_size,
	.get_fmt		= v4l2_subdev_get_fmt,
	.set_fmt		= rzv2h_ivc_set_fmt,
	.enable_streams		= rzv2h_ivc_enable_streams,
	.disable_streams	= rzv2h_ivc_disable_streams,
};

static const struct v4l2_subdev_core_ops rzv2h_ivc_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_ops rzv2h_ivc_subdev_ops = {
	.core	= &rzv2h_ivc_core_ops,
	.pad	= &rzv2h_ivc_pad_ops,
};

static int rzv2h_ivc_init_state(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state)
{
	struct v4l2_mbus_framefmt *sink_fmt, *src_fmt;

	sink_fmt = v4l2_subdev_state_get_format(state,
						RZV2H_IVC_SUBDEV_SINK_PAD);
	sink_fmt->width = RZV2H_IVC_DEFAULT_WIDTH;
	sink_fmt->height = RZV2H_IVC_DEFAULT_HEIGHT;
	sink_fmt->field = V4L2_FIELD_NONE;
	sink_fmt->code = MEDIA_BUS_FMT_SRGGB16_1X16;
	sink_fmt->colorspace = V4L2_COLORSPACE_RAW;
	sink_fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(sink_fmt->colorspace);
	sink_fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(sink_fmt->colorspace);
	sink_fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							sink_fmt->colorspace,
							sink_fmt->ycbcr_enc);

	src_fmt = v4l2_subdev_state_get_format(state,
					       RZV2H_IVC_SUBDEV_SOURCE_PAD);

	*src_fmt = *sink_fmt;
	src_fmt->code = MEDIA_BUS_FMT_SRGGB20_1X20;

	return 0;
}

static int rzv2h_ivc_registered(struct v4l2_subdev *sd)
{
	struct rzv2h_ivc *ivc = container_of(sd, struct rzv2h_ivc, subdev.sd);

	return rzv2h_ivc_init_vdev(ivc, sd->v4l2_dev);
}

static const struct v4l2_subdev_internal_ops rzv2h_ivc_subdev_internal_ops = {
	.init_state = rzv2h_ivc_init_state,
	.registered = rzv2h_ivc_registered,
};

static int rzv2h_ivc_link_validate(struct media_link *link)
{
	struct video_device *vdev =
		media_entity_to_video_device(link->source->entity);
	struct rzv2h_ivc *ivc = video_get_drvdata(vdev);
	struct v4l2_subdev *sd =
		media_entity_to_v4l2_subdev(link->sink->entity);
	const struct rzv2h_ivc_format *fmt;
	const struct v4l2_pix_format_mplane *pix;
	struct v4l2_subdev_state *state;
	struct v4l2_mbus_framefmt *mf;
	unsigned int i;
	int ret = 0;

	state = v4l2_subdev_lock_and_get_active_state(sd);
	mf = v4l2_subdev_state_get_format(state, link->sink->index);

	pix = &ivc->format.pix;
	fmt = ivc->format.fmt;

	if (mf->width != pix->width || mf->height != pix->height) {
		dev_dbg(ivc->dev,
			"link '%s':%u -> '%s':%u not valid: %ux%u != %ux%u\n",
			link->source->entity->name, link->source->index,
			link->sink->entity->name, link->sink->index,
			mf->width, mf->height, pix->width, pix->height);
		ret = -EPIPE;
	}

	for (i = 0; i < ARRAY_SIZE(fmt->mbus_codes); i++)
		if (mf->code == fmt->mbus_codes[i])
			break;

	if (i == ARRAY_SIZE(fmt->mbus_codes)) {
		dev_dbg(ivc->dev,
			"link '%s':%u -> '%s':%u not valid: pixel format %p4cc cannot produce mbus_code 0x%04x\n",
			link->source->entity->name, link->source->index,
			link->sink->entity->name, link->sink->index,
			&pix->pixelformat, mf->code);
		ret = -EPIPE;
	}

	v4l2_subdev_unlock_state(state);

	return ret;
}

static const struct media_entity_operations rzv2h_ivc_media_ops = {
	.link_validate = rzv2h_ivc_link_validate,
};

int rzv2h_ivc_initialise_subdevice(struct rzv2h_ivc *ivc)
{
	struct v4l2_subdev *sd;
	int ret;

	/* Initialise subdevice */
	sd = &ivc->subdev.sd;
	sd->dev = ivc->dev;
	v4l2_subdev_init(sd, &rzv2h_ivc_subdev_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	sd->internal_ops = &rzv2h_ivc_subdev_internal_ops;
	sd->entity.ops = &rzv2h_ivc_media_ops;

	ivc->subdev.pads[RZV2H_IVC_SUBDEV_SINK_PAD].flags = MEDIA_PAD_FL_SINK;
	ivc->subdev.pads[RZV2H_IVC_SUBDEV_SOURCE_PAD].flags = MEDIA_PAD_FL_SOURCE;

	snprintf(sd->name, sizeof(sd->name), "rzv2h ivc block");

	ret = media_entity_pads_init(&sd->entity, RZV2H_IVC_NUM_SUBDEV_PADS,
				     ivc->subdev.pads);
	if (ret) {
		dev_err(ivc->dev, "failed to initialise media entity\n");
		return ret;
	}

	ret = v4l2_subdev_init_finalize(sd);
	if (ret) {
		dev_err(ivc->dev, "failed to finalize subdev init\n");
		goto err_cleanup_subdev_entity;
	}

	ret = v4l2_async_register_subdev(sd);
	if (ret) {
		dev_err(ivc->dev, "failed to register subdevice\n");
		goto err_cleanup_subdev;
	}

	return 0;

err_cleanup_subdev:
	v4l2_subdev_cleanup(sd);
err_cleanup_subdev_entity:
	media_entity_cleanup(&sd->entity);

	return ret;
}

void rzv2h_ivc_deinit_subdevice(struct rzv2h_ivc *ivc)
{
	struct v4l2_subdev *sd = &ivc->subdev.sd;

	v4l2_subdev_cleanup(sd);
	media_entity_remove_links(&sd->entity);
	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
}

// SPDX-License-Identifier: GPL-2.0
/*
 * V4L2 Capture ISI subdev driver for i.MX8QXP/QM platform
 *
 * ISI is a Image Sensor Interface of i.MX8QXP/QM platform, which
 * used to process image from camera sensor to memory or DC
 *
 * Copyright (c) 2019 NXP Semiconductor
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>

#include "imx8-isi-core.h"
#include "imx8-isi-regs.h"

/*
 * While the ISI receives data from the gasket on a 3x12-bit bus, the pipeline
 * subdev conceptually includes the gasket in order to avoid exposing an extra
 * subdev between the CSIS and the ISI. We thus need to expose media bus codes
 * corresponding to the CSIS output, which is narrower.
 */
static const struct mxc_isi_bus_format_info mxc_isi_bus_formats[] = {
	/* YUV formats */
	{
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_1X16,
		.output		= MEDIA_BUS_FMT_YUV8_1X24,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK),
		.encoding	= MXC_ISI_ENC_YUV,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YUV8_1X24,
		.output		= MEDIA_BUS_FMT_YUV8_1X24,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_YUV,
	},
	/* RGB formats */
	{
		.mbus_code	= MEDIA_BUS_FMT_RGB565_1X16,
		.output		= MEDIA_BUS_FMT_RGB888_1X24,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK),
		.encoding	= MXC_ISI_ENC_RGB,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_RGB888_1X24,
		.output		= MEDIA_BUS_FMT_RGB888_1X24,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RGB,
	},
	/* RAW formats */
	{
		.mbus_code	= MEDIA_BUS_FMT_Y8_1X8,
		.output		= MEDIA_BUS_FMT_Y8_1X8,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_Y10_1X10,
		.output		= MEDIA_BUS_FMT_Y10_1X10,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_Y12_1X12,
		.output		= MEDIA_BUS_FMT_Y12_1X12,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_Y14_1X14,
		.output		= MEDIA_BUS_FMT_Y14_1X14,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR8_1X8,
		.output		= MEDIA_BUS_FMT_SBGGR8_1X8,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG8_1X8,
		.output		= MEDIA_BUS_FMT_SGBRG8_1X8,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG8_1X8,
		.output		= MEDIA_BUS_FMT_SGRBG8_1X8,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB8_1X8,
		.output		= MEDIA_BUS_FMT_SRGGB8_1X8,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.output		= MEDIA_BUS_FMT_SBGGR10_1X10,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG10_1X10,
		.output		= MEDIA_BUS_FMT_SGBRG10_1X10,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG10_1X10,
		.output		= MEDIA_BUS_FMT_SGRBG10_1X10,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.output		= MEDIA_BUS_FMT_SRGGB10_1X10,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR12_1X12,
		.output		= MEDIA_BUS_FMT_SBGGR12_1X12,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG12_1X12,
		.output		= MEDIA_BUS_FMT_SGBRG12_1X12,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG12_1X12,
		.output		= MEDIA_BUS_FMT_SGRBG12_1X12,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB12_1X12,
		.output		= MEDIA_BUS_FMT_SRGGB12_1X12,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR14_1X14,
		.output		= MEDIA_BUS_FMT_SBGGR14_1X14,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG14_1X14,
		.output		= MEDIA_BUS_FMT_SGBRG14_1X14,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG14_1X14,
		.output		= MEDIA_BUS_FMT_SGRBG14_1X14,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB14_1X14,
		.output		= MEDIA_BUS_FMT_SRGGB14_1X14,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	},
	/* JPEG */
	{
		.mbus_code	= MEDIA_BUS_FMT_JPEG_1X8,
		.output		= MEDIA_BUS_FMT_JPEG_1X8,
		.pads		= BIT(MXC_ISI_PIPE_PAD_SINK)
				| BIT(MXC_ISI_PIPE_PAD_SOURCE),
		.encoding	= MXC_ISI_ENC_RAW,
	}
};

const struct mxc_isi_bus_format_info *
mxc_isi_bus_format_by_code(u32 code, unsigned int pad)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mxc_isi_bus_formats); i++) {
		const struct mxc_isi_bus_format_info *info =
			&mxc_isi_bus_formats[i];

		if (info->mbus_code == code && info->pads & BIT(pad))
			return info;
	}

	return NULL;
}

const struct mxc_isi_bus_format_info *
mxc_isi_bus_format_by_index(unsigned int index, unsigned int pad)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mxc_isi_bus_formats); i++) {
		const struct mxc_isi_bus_format_info *info =
			&mxc_isi_bus_formats[i];

		if (!(info->pads & BIT(pad)))
			continue;

		if (!index)
			return info;

		index--;
	}

	return NULL;
}

static inline struct mxc_isi_pipe *to_isi_pipe(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mxc_isi_pipe, sd);
}

int mxc_isi_pipe_enable(struct mxc_isi_pipe *pipe)
{
	struct mxc_isi_crossbar *xbar = &pipe->isi->crossbar;
	const struct mxc_isi_bus_format_info *sink_info;
	const struct mxc_isi_bus_format_info *src_info;
	const struct v4l2_mbus_framefmt *sink_fmt;
	const struct v4l2_mbus_framefmt *src_fmt;
	const struct v4l2_rect *compose;
	struct v4l2_subdev_state *state;
	struct v4l2_subdev *sd = &pipe->sd;
	struct v4l2_area in_size, scale;
	struct v4l2_rect crop;
	u32 input;
	int ret;

	/*
	 * Find the connected input by inspecting the crossbar switch routing
	 * table.
	 */
	state = v4l2_subdev_lock_and_get_active_state(&xbar->sd);
	ret = v4l2_subdev_routing_find_opposite_end(&state->routing,
						    xbar->num_sinks + pipe->id,
						    0, &input, NULL);
	v4l2_subdev_unlock_state(state);

	if (ret)
		return -EPIPE;

	/* Configure the pipeline. */
	state = v4l2_subdev_lock_and_get_active_state(sd);

	sink_fmt = v4l2_subdev_state_get_format(state, MXC_ISI_PIPE_PAD_SINK);
	src_fmt = v4l2_subdev_state_get_format(state, MXC_ISI_PIPE_PAD_SOURCE);
	compose = v4l2_subdev_state_get_compose(state, MXC_ISI_PIPE_PAD_SINK);
	crop = *v4l2_subdev_state_get_crop(state, MXC_ISI_PIPE_PAD_SOURCE);

	sink_info = mxc_isi_bus_format_by_code(sink_fmt->code,
					       MXC_ISI_PIPE_PAD_SINK);
	src_info = mxc_isi_bus_format_by_code(src_fmt->code,
					      MXC_ISI_PIPE_PAD_SOURCE);

	in_size.width = sink_fmt->width;
	in_size.height = sink_fmt->height;
	scale.width = compose->width;
	scale.height = compose->height;

	v4l2_subdev_unlock_state(state);

	/* Configure the ISI channel. */
	mxc_isi_channel_config(pipe, input, &in_size, &scale, &crop,
			       sink_info->encoding, src_info->encoding);

	mxc_isi_channel_enable(pipe);

	/* Enable streams on the crossbar switch. */
	ret = v4l2_subdev_enable_streams(&xbar->sd, xbar->num_sinks + pipe->id,
					 BIT(0));
	if (ret) {
		mxc_isi_channel_disable(pipe);
		dev_err(pipe->isi->dev, "Failed to enable pipe %u\n",
			pipe->id);
		return ret;
	}

	return 0;
}

void mxc_isi_pipe_disable(struct mxc_isi_pipe *pipe)
{
	struct mxc_isi_crossbar *xbar = &pipe->isi->crossbar;
	int ret;

	ret = v4l2_subdev_disable_streams(&xbar->sd, xbar->num_sinks + pipe->id,
					  BIT(0));
	if (ret)
		dev_err(pipe->isi->dev, "Failed to disable pipe %u\n",
			pipe->id);

	mxc_isi_channel_disable(pipe);
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev operations
 */

static struct v4l2_mbus_framefmt *
mxc_isi_pipe_get_pad_format(struct mxc_isi_pipe *pipe,
			    struct v4l2_subdev_state *state,
			    unsigned int pad)
{
	return v4l2_subdev_state_get_format(state, pad);
}

static struct v4l2_rect *
mxc_isi_pipe_get_pad_crop(struct mxc_isi_pipe *pipe,
			  struct v4l2_subdev_state *state,
			  unsigned int pad)
{
	return v4l2_subdev_state_get_crop(state, pad);
}

static struct v4l2_rect *
mxc_isi_pipe_get_pad_compose(struct mxc_isi_pipe *pipe,
			     struct v4l2_subdev_state *state,
			     unsigned int pad)
{
	return v4l2_subdev_state_get_compose(state, pad);
}

static int mxc_isi_pipe_init_state(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state)
{
	struct mxc_isi_pipe *pipe = to_isi_pipe(sd);
	struct v4l2_mbus_framefmt *fmt_source;
	struct v4l2_mbus_framefmt *fmt_sink;
	struct v4l2_rect *compose;
	struct v4l2_rect *crop;

	fmt_sink = mxc_isi_pipe_get_pad_format(pipe, state,
					       MXC_ISI_PIPE_PAD_SINK);
	fmt_source = mxc_isi_pipe_get_pad_format(pipe, state,
						 MXC_ISI_PIPE_PAD_SOURCE);

	fmt_sink->width = MXC_ISI_DEF_WIDTH;
	fmt_sink->height = MXC_ISI_DEF_HEIGHT;
	fmt_sink->code = MXC_ISI_DEF_MBUS_CODE_SINK;
	fmt_sink->field = V4L2_FIELD_NONE;
	fmt_sink->colorspace = V4L2_COLORSPACE_JPEG;
	fmt_sink->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt_sink->colorspace);
	fmt_sink->quantization =
		V4L2_MAP_QUANTIZATION_DEFAULT(false, fmt_sink->colorspace,
					      fmt_sink->ycbcr_enc);
	fmt_sink->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt_sink->colorspace);

	*fmt_source = *fmt_sink;
	fmt_source->code = MXC_ISI_DEF_MBUS_CODE_SOURCE;

	compose = mxc_isi_pipe_get_pad_compose(pipe, state,
					       MXC_ISI_PIPE_PAD_SINK);
	crop = mxc_isi_pipe_get_pad_crop(pipe, state, MXC_ISI_PIPE_PAD_SOURCE);

	compose->left = 0;
	compose->top = 0;
	compose->width = MXC_ISI_DEF_WIDTH;
	compose->height = MXC_ISI_DEF_HEIGHT;

	*crop = *compose;

	return 0;
}

static int mxc_isi_pipe_enum_mbus_code(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       struct v4l2_subdev_mbus_code_enum *code)
{
	static const u32 output_codes[] = {
		MEDIA_BUS_FMT_YUV8_1X24,
		MEDIA_BUS_FMT_RGB888_1X24,
	};
	struct mxc_isi_pipe *pipe = to_isi_pipe(sd);
	const struct mxc_isi_bus_format_info *info;
	unsigned int index;
	unsigned int i;

	if (code->pad == MXC_ISI_PIPE_PAD_SOURCE) {
		const struct v4l2_mbus_framefmt *format;

		format = mxc_isi_pipe_get_pad_format(pipe, state,
						     MXC_ISI_PIPE_PAD_SINK);
		info = mxc_isi_bus_format_by_code(format->code,
						  MXC_ISI_PIPE_PAD_SINK);

		if (info->encoding == MXC_ISI_ENC_RAW) {
			/*
			 * For RAW formats, the sink and source media bus codes
			 * must match.
			 */
			if (code->index)
				return -EINVAL;

			code->code = info->output;
		} else {
			/*
			 * For RGB or YUV formats, the ISI supports format
			 * conversion. Either of the two output formats can be
			 * used regardless of the input.
			 */
			if (code->index > 1)
				return -EINVAL;

			code->code = output_codes[code->index];
		}

		return 0;
	}

	index = code->index;

	for (i = 0; i < ARRAY_SIZE(mxc_isi_bus_formats); ++i) {
		info = &mxc_isi_bus_formats[i];

		if (!(info->pads & BIT(MXC_ISI_PIPE_PAD_SINK)))
			continue;

		if (index == 0) {
			code->code = info->mbus_code;
			return 0;
		}

		index--;
	}

	return -EINVAL;
}

static int mxc_isi_pipe_set_fmt(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_format *fmt)
{
	struct mxc_isi_pipe *pipe = to_isi_pipe(sd);
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	const struct mxc_isi_bus_format_info *info;
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *rect;

	if (vb2_is_busy(&pipe->video.vb2_q))
		return -EBUSY;

	if (fmt->pad == MXC_ISI_PIPE_PAD_SINK) {
		unsigned int max_width;

		info = mxc_isi_bus_format_by_code(mf->code,
						  MXC_ISI_PIPE_PAD_SINK);
		if (!info)
			info = mxc_isi_bus_format_by_code(MXC_ISI_DEF_MBUS_CODE_SINK,
							  MXC_ISI_PIPE_PAD_SINK);

		/*
		 * Limit the max line length if there's no adjacent pipe to
		 * chain with.
		 */
		max_width = pipe->id == pipe->isi->pdata->num_channels - 1
			  ? MXC_ISI_MAX_WIDTH_UNCHAINED
			  : MXC_ISI_MAX_WIDTH_CHAINED;

		mf->code = info->mbus_code;
		mf->width = clamp(mf->width, MXC_ISI_MIN_WIDTH, max_width);
		mf->height = clamp(mf->height, MXC_ISI_MIN_HEIGHT,
				   MXC_ISI_MAX_HEIGHT);

		/* Propagate the format to the source pad. */
		rect = mxc_isi_pipe_get_pad_compose(pipe, state,
						    MXC_ISI_PIPE_PAD_SINK);
		rect->width = mf->width;
		rect->height = mf->height;

		rect = mxc_isi_pipe_get_pad_crop(pipe, state,
						 MXC_ISI_PIPE_PAD_SOURCE);
		rect->left = 0;
		rect->top = 0;
		rect->width = mf->width;
		rect->height = mf->height;

		format = mxc_isi_pipe_get_pad_format(pipe, state,
						     MXC_ISI_PIPE_PAD_SOURCE);
		format->code = info->output;
		format->width = mf->width;
		format->height = mf->height;
	} else {
		/*
		 * For RGB or YUV formats, the ISI supports RGB <-> YUV format
		 * conversion. For RAW formats, the sink and source media bus
		 * codes must match.
		 */
		format = mxc_isi_pipe_get_pad_format(pipe, state,
						     MXC_ISI_PIPE_PAD_SINK);
		info = mxc_isi_bus_format_by_code(format->code,
						  MXC_ISI_PIPE_PAD_SINK);

		if (info->encoding != MXC_ISI_ENC_RAW) {
			if (mf->code != MEDIA_BUS_FMT_YUV8_1X24 &&
			    mf->code != MEDIA_BUS_FMT_RGB888_1X24)
				mf->code = info->output;

			info = mxc_isi_bus_format_by_code(mf->code,
							  MXC_ISI_PIPE_PAD_SOURCE);
		}

		mf->code = info->output;

		/*
		 * The width and height on the source can't be changed, they
		 * must match the crop rectangle size.
		 */
		rect = mxc_isi_pipe_get_pad_crop(pipe, state,
						 MXC_ISI_PIPE_PAD_SOURCE);

		mf->width = rect->width;
		mf->height = rect->height;
	}

	format = mxc_isi_pipe_get_pad_format(pipe, state, fmt->pad);
	*format = *mf;

	dev_dbg(pipe->isi->dev, "pad%u: code: 0x%04x, %ux%u",
		fmt->pad, mf->code, mf->width, mf->height);

	return 0;
}

static int mxc_isi_pipe_get_selection(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_selection *sel)
{
	struct mxc_isi_pipe *pipe = to_isi_pipe(sd);
	const struct v4l2_mbus_framefmt *format;
	const struct v4l2_rect *rect;

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		if (sel->pad != MXC_ISI_PIPE_PAD_SINK)
			/* No compose rectangle on source pad. */
			return -EINVAL;

		/* The sink compose is bound by the sink format. */
		format = mxc_isi_pipe_get_pad_format(pipe, state,
						     MXC_ISI_PIPE_PAD_SINK);
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = format->width;
		sel->r.height = format->height;
		break;

	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (sel->pad != MXC_ISI_PIPE_PAD_SOURCE)
			/* No crop rectangle on sink pad. */
			return -EINVAL;

		/* The source crop is bound by the sink compose. */
		rect = mxc_isi_pipe_get_pad_compose(pipe, state,
						    MXC_ISI_PIPE_PAD_SINK);
		sel->r = *rect;
		break;

	case V4L2_SEL_TGT_CROP:
		if (sel->pad != MXC_ISI_PIPE_PAD_SOURCE)
			/* No crop rectangle on sink pad. */
			return -EINVAL;

		rect = mxc_isi_pipe_get_pad_crop(pipe, state, sel->pad);
		sel->r = *rect;
		break;

	case V4L2_SEL_TGT_COMPOSE:
		if (sel->pad != MXC_ISI_PIPE_PAD_SINK)
			/* No compose rectangle on source pad. */
			return -EINVAL;

		rect = mxc_isi_pipe_get_pad_compose(pipe, state, sel->pad);
		sel->r = *rect;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int mxc_isi_pipe_set_selection(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_selection *sel)
{
	struct mxc_isi_pipe *pipe = to_isi_pipe(sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *rect;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		if (sel->pad != MXC_ISI_PIPE_PAD_SOURCE)
			/* The pipeline support cropping on the source only. */
			return -EINVAL;

		/* The source crop is bound by the sink compose. */
		rect = mxc_isi_pipe_get_pad_compose(pipe, state,
						    MXC_ISI_PIPE_PAD_SINK);
		sel->r.left = clamp_t(s32, sel->r.left, 0, rect->width - 1);
		sel->r.top = clamp_t(s32, sel->r.top, 0, rect->height - 1);
		sel->r.width = clamp(sel->r.width, MXC_ISI_MIN_WIDTH,
				     rect->width - sel->r.left);
		sel->r.height = clamp(sel->r.height, MXC_ISI_MIN_HEIGHT,
				      rect->height - sel->r.top);

		rect = mxc_isi_pipe_get_pad_crop(pipe, state,
						 MXC_ISI_PIPE_PAD_SOURCE);
		*rect = sel->r;

		/* Propagate the crop rectangle to the source pad. */
		format = mxc_isi_pipe_get_pad_format(pipe, state,
						     MXC_ISI_PIPE_PAD_SOURCE);
		format->width = sel->r.width;
		format->height = sel->r.height;
		break;

	case V4L2_SEL_TGT_COMPOSE:
		if (sel->pad != MXC_ISI_PIPE_PAD_SINK)
			/* Composing is supported on the sink only. */
			return -EINVAL;

		/* The sink crop is bound by the sink format downscaling only). */
		format = mxc_isi_pipe_get_pad_format(pipe, state,
						     MXC_ISI_PIPE_PAD_SINK);

		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = clamp(sel->r.width, MXC_ISI_MIN_WIDTH,
				     format->width);
		sel->r.height = clamp(sel->r.height, MXC_ISI_MIN_HEIGHT,
				      format->height);

		rect = mxc_isi_pipe_get_pad_compose(pipe, state,
						    MXC_ISI_PIPE_PAD_SINK);
		*rect = sel->r;

		/* Propagate the compose rectangle to the source pad. */
		rect = mxc_isi_pipe_get_pad_crop(pipe, state,
						 MXC_ISI_PIPE_PAD_SOURCE);
		rect->left = 0;
		rect->top = 0;
		rect->width = sel->r.width;
		rect->height = sel->r.height;

		format = mxc_isi_pipe_get_pad_format(pipe, state,
						     MXC_ISI_PIPE_PAD_SOURCE);
		format->width = sel->r.width;
		format->height = sel->r.height;
		break;

	default:
		return -EINVAL;
	}

	dev_dbg(pipe->isi->dev, "%s, target %#x: (%d,%d)/%dx%d", __func__,
		sel->target, sel->r.left, sel->r.top, sel->r.width,
		sel->r.height);

	return 0;
}

static const struct v4l2_subdev_pad_ops mxc_isi_pipe_subdev_pad_ops = {
	.enum_mbus_code = mxc_isi_pipe_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = mxc_isi_pipe_set_fmt,
	.get_selection = mxc_isi_pipe_get_selection,
	.set_selection = mxc_isi_pipe_set_selection,
};

static const struct v4l2_subdev_ops mxc_isi_pipe_subdev_ops = {
	.pad = &mxc_isi_pipe_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops mxc_isi_pipe_internal_ops = {
	.init_state = mxc_isi_pipe_init_state,
};

/* -----------------------------------------------------------------------------
 * IRQ handling
 */

static irqreturn_t mxc_isi_pipe_irq_handler(int irq, void *priv)
{
	struct mxc_isi_pipe *pipe = priv;
	const struct mxc_isi_ier_reg *ier_reg = pipe->isi->pdata->ier_reg;
	u32 status;

	status = mxc_isi_channel_irq_status(pipe, true);

	if (status & CHNL_STS_FRM_STRD) {
		if (!WARN_ON(!pipe->irq_handler))
			pipe->irq_handler(pipe, status);
	}

	if (status & (CHNL_STS_AXI_WR_ERR_Y |
		      CHNL_STS_AXI_WR_ERR_U |
		      CHNL_STS_AXI_WR_ERR_V))
		dev_dbg(pipe->isi->dev, "%s: IRQ AXI Error stat=0x%X\n",
			__func__, status);

	if (status & (ier_reg->panic_y_buf_en.mask |
		      ier_reg->panic_u_buf_en.mask |
		      ier_reg->panic_v_buf_en.mask))
		dev_dbg(pipe->isi->dev, "%s: IRQ Panic OFLW Error stat=0x%X\n",
			__func__, status);

	if (status & (ier_reg->oflw_y_buf_en.mask |
		      ier_reg->oflw_u_buf_en.mask |
		      ier_reg->oflw_v_buf_en.mask))
		dev_dbg(pipe->isi->dev, "%s: IRQ OFLW Error stat=0x%X\n",
			__func__, status);

	if (status & (ier_reg->excs_oflw_y_buf_en.mask |
		      ier_reg->excs_oflw_u_buf_en.mask |
		      ier_reg->excs_oflw_v_buf_en.mask))
		dev_dbg(pipe->isi->dev, "%s: IRQ EXCS OFLW Error stat=0x%X\n",
			__func__, status);

	return IRQ_HANDLED;
}

/* -----------------------------------------------------------------------------
 * Init & cleanup
 */

static const struct media_entity_operations mxc_isi_pipe_entity_ops = {
	.link_validate	= v4l2_subdev_link_validate,
};

int mxc_isi_pipe_init(struct mxc_isi_dev *isi, unsigned int id)
{
	struct mxc_isi_pipe *pipe = &isi->pipes[id];
	struct v4l2_subdev *sd;
	int irq;
	int ret;

	pipe->id = id;
	pipe->isi = isi;
	pipe->regs = isi->regs + id * isi->pdata->reg_offset;

	mutex_init(&pipe->lock);

	pipe->available_res = MXC_ISI_CHANNEL_RES_LINE_BUF
			    | MXC_ISI_CHANNEL_RES_OUTPUT_BUF;
	pipe->acquired_res = 0;
	pipe->chained_res = 0;
	pipe->chained = false;

	sd = &pipe->sd;
	v4l2_subdev_init(sd, &mxc_isi_pipe_subdev_ops);
	sd->internal_ops = &mxc_isi_pipe_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "mxc_isi.%d", pipe->id);
	sd->dev = isi->dev;

	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	sd->entity.ops = &mxc_isi_pipe_entity_ops;

	pipe->pads[MXC_ISI_PIPE_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pipe->pads[MXC_ISI_PIPE_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&sd->entity, MXC_ISI_PIPE_PADS_NUM,
				     pipe->pads);
	if (ret)
		goto error;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret < 0)
		goto error;

	/* Register IRQ handler. */
	mxc_isi_channel_irq_clear(pipe);

	irq = platform_get_irq(to_platform_device(isi->dev), id);
	if (irq < 0) {
		ret = irq;
		goto error;
	}

	ret = devm_request_irq(isi->dev, irq, mxc_isi_pipe_irq_handler,
			       0, dev_name(isi->dev), pipe);
	if (ret < 0) {
		dev_err(isi->dev, "failed to request IRQ (%d)\n", ret);
		goto error;
	}

	return 0;

error:
	media_entity_cleanup(&sd->entity);
	mutex_destroy(&pipe->lock);

	return ret;
}

void mxc_isi_pipe_cleanup(struct mxc_isi_pipe *pipe)
{
	struct v4l2_subdev *sd = &pipe->sd;

	media_entity_cleanup(&sd->entity);
	mutex_destroy(&pipe->lock);
}

int mxc_isi_pipe_acquire(struct mxc_isi_pipe *pipe,
			 mxc_isi_pipe_irq_t irq_handler)
{
	const struct mxc_isi_bus_format_info *sink_info;
	const struct mxc_isi_bus_format_info *src_info;
	struct v4l2_mbus_framefmt *sink_fmt;
	const struct v4l2_mbus_framefmt *src_fmt;
	struct v4l2_subdev *sd = &pipe->sd;
	struct v4l2_subdev_state *state;
	bool bypass;
	int ret;

	state = v4l2_subdev_lock_and_get_active_state(sd);
	sink_fmt = v4l2_subdev_state_get_format(state, MXC_ISI_PIPE_PAD_SINK);
	src_fmt = v4l2_subdev_state_get_format(state, MXC_ISI_PIPE_PAD_SOURCE);
	v4l2_subdev_unlock_state(state);

	sink_info = mxc_isi_bus_format_by_code(sink_fmt->code,
					       MXC_ISI_PIPE_PAD_SINK);
	src_info = mxc_isi_bus_format_by_code(src_fmt->code,
					      MXC_ISI_PIPE_PAD_SOURCE);

	bypass = sink_fmt->width == src_fmt->width &&
		 sink_fmt->height == src_fmt->height &&
		 sink_info->encoding == src_info->encoding;

	ret = mxc_isi_channel_acquire(pipe, irq_handler, bypass);
	if (ret)
		return ret;

	/* Chain the channel if needed for wide resolutions. */
	if (sink_fmt->width > MXC_ISI_MAX_WIDTH_UNCHAINED) {
		ret = mxc_isi_channel_chain(pipe);
		if (ret)
			mxc_isi_channel_release(pipe);
	}

	return ret;
}

void mxc_isi_pipe_release(struct mxc_isi_pipe *pipe)
{
	mxc_isi_channel_release(pipe);
	mxc_isi_channel_unchain(pipe);
}

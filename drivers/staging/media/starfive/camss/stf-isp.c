// SPDX-License-Identifier: GPL-2.0
/*
 * stf_isp.c
 *
 * StarFive Camera Subsystem - ISP Module
 *
 * Copyright (C) 2021-2023 StarFive Technology Co., Ltd.
 */
#include <media/v4l2-rect.h>

#include "stf-camss.h"

static int isp_set_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_selection *sel);

static const struct stf_isp_format isp_formats_sink[] = {
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 10 },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 10 },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 10 },
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 10 },
};

static const struct stf_isp_format isp_formats_source[] = {
	{ MEDIA_BUS_FMT_YUYV8_1_5X8, 8 },
};

static const struct stf_isp_format_table isp_formats_st7110[] = {
	{ isp_formats_sink, ARRAY_SIZE(isp_formats_sink) },
	{ isp_formats_source, ARRAY_SIZE(isp_formats_source) },
};

static const struct stf_isp_format *
stf_g_fmt_by_mcode(const struct stf_isp_format_table *fmt_table, u32 mcode)
{
	unsigned int i;

	for (i = 0; i < fmt_table->nfmts; i++) {
		if (fmt_table->fmts[i].code == mcode)
			return &fmt_table->fmts[i];
	}

	return NULL;
}

int stf_isp_init(struct stfcamss *stfcamss)
{
	struct stf_isp_dev *isp_dev = &stfcamss->isp_dev;

	isp_dev->stfcamss = stfcamss;
	isp_dev->formats = isp_formats_st7110;
	isp_dev->nformats = ARRAY_SIZE(isp_formats_st7110);
	isp_dev->current_fmt = &isp_formats_source[0];

	return 0;
}

static int isp_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct v4l2_subdev_state *sd_state;
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_rect *crop;

	sd_state = v4l2_subdev_lock_and_get_active_state(sd);
	fmt = v4l2_subdev_state_get_format(sd_state, STF_ISP_PAD_SINK);
	crop = v4l2_subdev_state_get_crop(sd_state, STF_ISP_PAD_SRC);

	if (enable) {
		stf_isp_reset(isp_dev);
		stf_isp_init_cfg(isp_dev);
		stf_isp_settings(isp_dev, crop, fmt->code);
		stf_isp_stream_set(isp_dev);
	}

	v4l2_subdev_call(isp_dev->source_subdev, video, s_stream, enable);

	v4l2_subdev_unlock_state(sd_state);
	return 0;
}

static void isp_try_format(struct stf_isp_dev *isp_dev,
			   struct v4l2_subdev_state *state,
			   unsigned int pad,
			   struct v4l2_mbus_framefmt *fmt)
{
	const struct stf_isp_format_table *formats;

	if (pad >= STF_ISP_PAD_MAX) {
		fmt->colorspace = V4L2_COLORSPACE_SRGB;
		return;
	}

	formats = &isp_dev->formats[pad];

	fmt->width = clamp_t(u32, fmt->width, STFCAMSS_FRAME_MIN_WIDTH,
			     STFCAMSS_FRAME_MAX_WIDTH);
	fmt->height = clamp_t(u32, fmt->height, STFCAMSS_FRAME_MIN_HEIGHT,
			      STFCAMSS_FRAME_MAX_HEIGHT);
	fmt->height &= ~0x1;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->flags = 0;

	if (!stf_g_fmt_by_mcode(formats, fmt->code))
		fmt->code = formats->fmts[0].code;
}

static int isp_enum_mbus_code(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	const struct stf_isp_format_table *formats;

	if (code->pad == STF_ISP_PAD_SINK) {
		if (code->index >= ARRAY_SIZE(isp_formats_sink))
			return -EINVAL;

		formats = &isp_dev->formats[code->pad];
		code->code = formats->fmts[code->index].code;
	} else {
		struct v4l2_mbus_framefmt *sink_fmt;

		if (code->index >= ARRAY_SIZE(isp_formats_source))
			return -EINVAL;

		sink_fmt = v4l2_subdev_state_get_format(state,
							STF_ISP_PAD_SRC);

		code->code = sink_fmt->code;
		if (!code->code)
			return -EINVAL;
	}
	code->flags = 0;

	return 0;
}

static int isp_set_format(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_state_get_format(state, fmt->pad);
	if (!format)
		return -EINVAL;

	isp_try_format(isp_dev, state, fmt->pad, &fmt->format);
	*format = fmt->format;

	isp_dev->current_fmt = stf_g_fmt_by_mcode(&isp_dev->formats[fmt->pad],
						  fmt->format.code);

	/* Propagate to in crop */
	if (fmt->pad == STF_ISP_PAD_SINK) {
		struct v4l2_subdev_selection sel = { 0 };

		/* Reset sink pad compose selection */
		sel.which = fmt->which;
		sel.pad = STF_ISP_PAD_SINK;
		sel.target = V4L2_SEL_TGT_CROP;
		sel.r.width = fmt->format.width;
		sel.r.height = fmt->format.height;
		isp_set_selection(sd, state, &sel);
	}

	return 0;
}

static const struct v4l2_rect stf_frame_min_crop = {
	.width = STFCAMSS_FRAME_MIN_WIDTH,
	.height = STFCAMSS_FRAME_MIN_HEIGHT,
	.top = 0,
	.left = 0,
};

static void isp_try_crop(struct stf_isp_dev *isp_dev,
			 struct v4l2_subdev_state *state,
			 struct v4l2_rect *crop)
{
	struct v4l2_mbus_framefmt *fmt =
		v4l2_subdev_state_get_format(state, STF_ISP_PAD_SINK);

	const struct v4l2_rect bounds = {
		.width = fmt->width,
		.height = fmt->height,
		.left = 0,
		.top = 0,
	};

	v4l2_rect_set_min_size(crop, &stf_frame_min_crop);
	v4l2_rect_map_inside(crop, &bounds);
}

static int isp_get_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_selection *sel)
{
	struct v4l2_subdev_format fmt = { 0 };
	struct v4l2_rect *rect;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (sel->pad == STF_ISP_PAD_SINK) {
			fmt.format = *v4l2_subdev_state_get_format(state,
								   sel->pad);
			sel->r.left = 0;
			sel->r.top = 0;
			sel->r.width = fmt.format.width;
			sel->r.height = fmt.format.height;
		} else if (sel->pad == STF_ISP_PAD_SRC) {
			rect = v4l2_subdev_state_get_crop(state, sel->pad);
			sel->r = *rect;
		}
		break;

	case V4L2_SEL_TGT_CROP:
		rect = v4l2_subdev_state_get_crop(state, sel->pad);
		if (!rect)
			return -EINVAL;

		sel->r = *rect;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int isp_set_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_selection *sel)
{
	struct stf_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct v4l2_rect *rect;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	if (sel->target == V4L2_SEL_TGT_CROP &&
	    sel->pad == STF_ISP_PAD_SINK) {
		struct v4l2_subdev_selection crop = { 0 };

		rect = v4l2_subdev_state_get_crop(state, sel->pad);
		if (!rect)
			return -EINVAL;

		isp_try_crop(isp_dev, state, &sel->r);
		*rect = sel->r;

		/* Reset source crop selection */
		crop.which = sel->which;
		crop.pad = STF_ISP_PAD_SRC;
		crop.target = V4L2_SEL_TGT_CROP;
		crop.r = *rect;
		isp_set_selection(sd, state, &crop);
	} else if (sel->target == V4L2_SEL_TGT_CROP &&
		   sel->pad == STF_ISP_PAD_SRC) {
		struct v4l2_subdev_format fmt = { 0 };

		rect = v4l2_subdev_state_get_crop(state, sel->pad);
		if (!rect)
			return -EINVAL;

		isp_try_crop(isp_dev, state, &sel->r);
		*rect = sel->r;

		/* Reset source pad format width and height */
		fmt.which = sel->which;
		fmt.pad = STF_ISP_PAD_SRC;
		fmt.format.width = rect->width;
		fmt.format.height = rect->height;
		isp_set_format(sd, state, &fmt);
	}

	dev_dbg(isp_dev->stfcamss->dev, "pad: %d sel(%d,%d)/%ux%u\n",
		sel->pad, sel->r.left, sel->r.top, sel->r.width, sel->r.height);

	return 0;
}

static int isp_init_formats(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state)
{
	struct v4l2_subdev_format format = {
		.pad = STF_ISP_PAD_SINK,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.format = {
			.code = MEDIA_BUS_FMT_SRGGB10_1X10,
			.width = 1920,
			.height = 1080
		}
	};

	return isp_set_format(sd, sd_state, &format);
}

static const struct v4l2_subdev_video_ops isp_video_ops = {
	.s_stream = isp_set_stream,
};

static const struct v4l2_subdev_pad_ops isp_pad_ops = {
	.enum_mbus_code = isp_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = isp_set_format,
	.get_selection = isp_get_selection,
	.set_selection = isp_set_selection,
};

static const struct v4l2_subdev_ops isp_v4l2_ops = {
	.video = &isp_video_ops,
	.pad = &isp_pad_ops,
};

static const struct v4l2_subdev_internal_ops isp_internal_ops = {
	.init_state = isp_init_formats,
};

static const struct media_entity_operations isp_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

int stf_isp_register(struct stf_isp_dev *isp_dev, struct v4l2_device *v4l2_dev)
{
	struct v4l2_subdev *sd = &isp_dev->subdev;
	struct media_pad *pads = isp_dev->pads;
	int ret;

	v4l2_subdev_init(sd, &isp_v4l2_ops);
	sd->internal_ops = &isp_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, ARRAY_SIZE(sd->name), "stf_isp");
	v4l2_set_subdevdata(sd, isp_dev);

	pads[STF_ISP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[STF_ISP_PAD_SRC].flags = MEDIA_PAD_FL_SOURCE;

	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_ISP;
	sd->entity.ops = &isp_media_ops;
	ret = media_entity_pads_init(&sd->entity, STF_ISP_PAD_MAX, pads);
	if (ret) {
		dev_err(isp_dev->stfcamss->dev,
			"Failed to init media entity: %d\n", ret);
		return ret;
	}

	ret = v4l2_subdev_init_finalize(sd);
	if (ret)
		goto err_entity_cleanup;

	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret) {
		dev_err(isp_dev->stfcamss->dev,
			"Failed to register subdev: %d\n", ret);
		goto err_subdev_cleanup;
	}

	return 0;

err_subdev_cleanup:
	v4l2_subdev_cleanup(sd);
err_entity_cleanup:
	media_entity_cleanup(&sd->entity);
	return ret;
}

int stf_isp_unregister(struct stf_isp_dev *isp_dev)
{
	v4l2_device_unregister_subdev(&isp_dev->subdev);
	v4l2_subdev_cleanup(&isp_dev->subdev);
	media_entity_cleanup(&isp_dev->subdev.entity);

	return 0;
}

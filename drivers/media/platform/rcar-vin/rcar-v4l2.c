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

#include <linux/pm_runtime.h>

#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-rect.h>

#include "rcar-vin.h"

#define RVIN_DEFAULT_FORMAT	V4L2_PIX_FMT_YUYV
#define RVIN_DEFAULT_WIDTH	800
#define RVIN_DEFAULT_HEIGHT	600
#define RVIN_DEFAULT_FIELD	V4L2_FIELD_NONE
#define RVIN_DEFAULT_COLORSPACE	V4L2_COLORSPACE_SRGB

/* -----------------------------------------------------------------------------
 * Format Conversions
 */

static const struct rvin_video_format rvin_formats[] = {
	{
		.fourcc			= V4L2_PIX_FMT_NV12,
		.bpp			= 1,
	},
	{
		.fourcc			= V4L2_PIX_FMT_NV16,
		.bpp			= 1,
	},
	{
		.fourcc			= V4L2_PIX_FMT_YUYV,
		.bpp			= 2,
	},
	{
		.fourcc			= V4L2_PIX_FMT_UYVY,
		.bpp			= 2,
	},
	{
		.fourcc			= V4L2_PIX_FMT_RGB565,
		.bpp			= 2,
	},
	{
		.fourcc			= V4L2_PIX_FMT_XRGB555,
		.bpp			= 2,
	},
	{
		.fourcc			= V4L2_PIX_FMT_XBGR32,
		.bpp			= 4,
	},
	{
		.fourcc			= V4L2_PIX_FMT_ARGB555,
		.bpp			= 2,
	},
	{
		.fourcc			= V4L2_PIX_FMT_ABGR32,
		.bpp			= 4,
	},
	{
		.fourcc			= V4L2_PIX_FMT_SBGGR8,
		.bpp			= 1,
	},
	{
		.fourcc			= V4L2_PIX_FMT_SGBRG8,
		.bpp			= 1,
	},
	{
		.fourcc			= V4L2_PIX_FMT_SGRBG8,
		.bpp			= 1,
	},
	{
		.fourcc			= V4L2_PIX_FMT_SRGGB8,
		.bpp			= 1,
	},
	{
		.fourcc			= V4L2_PIX_FMT_GREY,
		.bpp			= 1,
	},
};

const struct rvin_video_format *rvin_format_from_pixel(struct rvin_dev *vin,
						       u32 pixelformat)
{
	int i;

	switch (pixelformat) {
	case V4L2_PIX_FMT_XBGR32:
		if (vin->info->model == RCAR_M1)
			return NULL;
		break;
	case V4L2_PIX_FMT_NV12:
		/*
		 * If NV12 is supported it's only supported on channels 0, 1, 4,
		 * 5, 8, 9, 12 and 13.
		 */
		if (!vin->info->nv12 || !(BIT(vin->id) & 0x3333))
			return NULL;
		break;
	default:
		break;
	}

	for (i = 0; i < ARRAY_SIZE(rvin_formats); i++)
		if (rvin_formats[i].fourcc == pixelformat)
			return rvin_formats + i;

	return NULL;
}

static u32 rvin_format_bytesperline(struct rvin_dev *vin,
				    struct v4l2_pix_format *pix)
{
	const struct rvin_video_format *fmt;
	u32 align;

	fmt = rvin_format_from_pixel(vin, pix->pixelformat);

	if (WARN_ON(!fmt))
		return -EINVAL;

	switch (pix->pixelformat) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV16:
		align = 0x20;
		break;
	default:
		align = 0x10;
		break;
	}

	if (V4L2_FIELD_IS_SEQUENTIAL(pix->field))
		align = 0x80;

	return ALIGN(pix->width, align) * fmt->bpp;
}

static u32 rvin_format_sizeimage(struct v4l2_pix_format *pix)
{
	switch (pix->pixelformat) {
	case V4L2_PIX_FMT_NV12:
		return pix->bytesperline * pix->height * 3 / 2;
	case V4L2_PIX_FMT_NV16:
		return pix->bytesperline * pix->height * 2;
	default:
		return pix->bytesperline * pix->height;
	}
}

static void rvin_format_align(struct rvin_dev *vin, struct v4l2_pix_format *pix)
{
	u32 walign;

	if (!rvin_format_from_pixel(vin, pix->pixelformat))
		pix->pixelformat = RVIN_DEFAULT_FORMAT;

	switch (pix->field) {
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
	case V4L2_FIELD_NONE:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
	case V4L2_FIELD_INTERLACED:
	case V4L2_FIELD_ALTERNATE:
	case V4L2_FIELD_SEQ_TB:
	case V4L2_FIELD_SEQ_BT:
		break;
	default:
		pix->field = RVIN_DEFAULT_FIELD;
		break;
	}

	/* HW limit width to a multiple of 32 (2^5) for NV12/16 else 2 (2^1) */
	switch (pix->pixelformat) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV16:
		walign = 5;
		break;
	default:
		walign = 1;
		break;
	}

	/* Limit to VIN capabilities */
	v4l_bound_align_image(&pix->width, 2, vin->info->max_width, walign,
			      &pix->height, 4, vin->info->max_height, 2, 0);

	pix->bytesperline = rvin_format_bytesperline(vin, pix);
	pix->sizeimage = rvin_format_sizeimage(pix);

	vin_dbg(vin, "Format %ux%u bpl: %u size: %u\n",
		pix->width, pix->height, pix->bytesperline, pix->sizeimage);
}

/* -----------------------------------------------------------------------------
 * V4L2
 */

static int rvin_reset_format(struct rvin_dev *vin)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.pad = vin->parallel.source_pad,
	};
	int ret;

	ret = v4l2_subdev_call(vin_to_source(vin), pad, get_fmt, NULL, &fmt);
	if (ret)
		return ret;

	v4l2_fill_pix_format(&vin->format, &fmt.format);

	vin->src_rect.top = 0;
	vin->src_rect.left = 0;
	vin->src_rect.width = vin->format.width;
	vin->src_rect.height = vin->format.height;

	/*  Make use of the hardware interlacer by default. */
	if (vin->format.field == V4L2_FIELD_ALTERNATE) {
		vin->format.field = V4L2_FIELD_INTERLACED;
		vin->format.height *= 2;
	}

	rvin_format_align(vin, &vin->format);

	vin->crop = vin->src_rect;

	vin->compose.top = 0;
	vin->compose.left = 0;
	vin->compose.width = vin->format.width;
	vin->compose.height = vin->format.height;

	return 0;
}

static int rvin_try_format(struct rvin_dev *vin, u32 which,
			   struct v4l2_pix_format *pix,
			   struct v4l2_rect *src_rect)
{
	struct v4l2_subdev *sd = vin_to_source(vin);
	struct v4l2_subdev_state *sd_state;
	struct v4l2_subdev_format format = {
		.which = which,
		.pad = vin->parallel.source_pad,
	};
	enum v4l2_field field;
	u32 width, height;
	int ret;

	sd_state = v4l2_subdev_alloc_state(sd);
	if (IS_ERR(sd_state))
		return PTR_ERR(sd_state);

	if (!rvin_format_from_pixel(vin, pix->pixelformat))
		pix->pixelformat = RVIN_DEFAULT_FORMAT;

	v4l2_fill_mbus_format(&format.format, pix, vin->mbus_code);

	/* Allow the video device to override field and to scale */
	field = pix->field;
	width = pix->width;
	height = pix->height;

	ret = v4l2_subdev_call(sd, pad, set_fmt, sd_state, &format);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		goto done;
	ret = 0;

	v4l2_fill_pix_format(pix, &format.format);

	if (src_rect) {
		src_rect->top = 0;
		src_rect->left = 0;
		src_rect->width = pix->width;
		src_rect->height = pix->height;
	}

	if (field != V4L2_FIELD_ANY)
		pix->field = field;

	pix->width = width;
	pix->height = height;

	rvin_format_align(vin, pix);
done:
	v4l2_subdev_free_state(sd_state);

	return ret;
}

static int rvin_querycap(struct file *file, void *priv,
			 struct v4l2_capability *cap)
{
	struct rvin_dev *vin = video_drvdata(file);

	strscpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	strscpy(cap->card, "R_Car_VIN", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(vin->dev));
	return 0;
}

static int rvin_try_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct rvin_dev *vin = video_drvdata(file);

	return rvin_try_format(vin, V4L2_SUBDEV_FORMAT_TRY, &f->fmt.pix, NULL);
}

static int rvin_s_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_rect fmt_rect, src_rect;
	int ret;

	if (vb2_is_busy(&vin->queue))
		return -EBUSY;

	ret = rvin_try_format(vin, V4L2_SUBDEV_FORMAT_ACTIVE, &f->fmt.pix,
			      &src_rect);
	if (ret)
		return ret;

	vin->format = f->fmt.pix;

	fmt_rect.top = 0;
	fmt_rect.left = 0;
	fmt_rect.width = vin->format.width;
	fmt_rect.height = vin->format.height;

	v4l2_rect_map_inside(&vin->crop, &src_rect);
	v4l2_rect_map_inside(&vin->compose, &fmt_rect);
	vin->src_rect = src_rect;

	return 0;
}

static int rvin_g_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct rvin_dev *vin = video_drvdata(file);

	f->fmt.pix = vin->format;

	return 0;
}

static int rvin_enum_fmt_vid_cap(struct file *file, void *priv,
				 struct v4l2_fmtdesc *f)
{
	struct rvin_dev *vin = video_drvdata(file);
	unsigned int i;
	int matched;

	/*
	 * If mbus_code is set only enumerate supported pixel formats for that
	 * bus code. Converting from YCbCr to RGB and RGB to YCbCr is possible
	 * with VIN, so all supported YCbCr and RGB media bus codes can produce
	 * all of the related pixel formats. If mbus_code is not set enumerate
	 * all possible pixelformats.
	 *
	 * TODO: Once raw MEDIA_BUS_FMT_SRGGB12_1X12 format is added to the
	 * driver this needs to be extended so raw media bus code only result in
	 * raw pixel format.
	 */
	switch (f->mbus_code) {
	case 0:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_UYVY10_2X10:
	case MEDIA_BUS_FMT_RGB888_1X24:
		break;
	case MEDIA_BUS_FMT_SBGGR8_1X8:
		if (f->index)
			return -EINVAL;
		f->pixelformat = V4L2_PIX_FMT_SBGGR8;
		return 0;
	case MEDIA_BUS_FMT_SGBRG8_1X8:
		if (f->index)
			return -EINVAL;
		f->pixelformat = V4L2_PIX_FMT_SGBRG8;
		return 0;
	case MEDIA_BUS_FMT_SGRBG8_1X8:
		if (f->index)
			return -EINVAL;
		f->pixelformat = V4L2_PIX_FMT_SGRBG8;
		return 0;
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		if (f->index)
			return -EINVAL;
		f->pixelformat = V4L2_PIX_FMT_SRGGB8;
		return 0;
	default:
		return -EINVAL;
	}

	matched = -1;
	for (i = 0; i < ARRAY_SIZE(rvin_formats); i++) {
		if (rvin_format_from_pixel(vin, rvin_formats[i].fourcc))
			matched++;

		if (matched == f->index) {
			f->pixelformat = rvin_formats[i].fourcc;
			return 0;
		}
	}

	return -EINVAL;
}

static int rvin_g_selection(struct file *file, void *fh,
			    struct v4l2_selection *s)
{
	struct rvin_dev *vin = video_drvdata(file);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		s->r.left = s->r.top = 0;
		s->r.width = vin->src_rect.width;
		s->r.height = vin->src_rect.height;
		break;
	case V4L2_SEL_TGT_CROP:
		s->r = vin->crop;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		s->r.left = s->r.top = 0;
		s->r.width = vin->format.width;
		s->r.height = vin->format.height;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		s->r = vin->compose;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rvin_s_selection(struct file *file, void *fh,
			    struct v4l2_selection *s)
{
	struct rvin_dev *vin = video_drvdata(file);
	const struct rvin_video_format *fmt;
	struct v4l2_rect r = s->r;
	struct v4l2_rect max_rect;
	struct v4l2_rect min_rect = {
		.width = 6,
		.height = 2,
	};

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	v4l2_rect_set_min_size(&r, &min_rect);

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
		/* Can't crop outside of source input */
		max_rect.top = max_rect.left = 0;
		max_rect.width = vin->src_rect.width;
		max_rect.height = vin->src_rect.height;
		v4l2_rect_map_inside(&r, &max_rect);

		v4l_bound_align_image(&r.width, 6, vin->src_rect.width, 0,
				      &r.height, 2, vin->src_rect.height, 0, 0);

		r.top  = clamp_t(s32, r.top, 0,
				 vin->src_rect.height - r.height);
		r.left = clamp_t(s32, r.left, 0, vin->src_rect.width - r.width);

		vin->crop = s->r = r;

		vin_dbg(vin, "Cropped %dx%d@%d:%d of %dx%d\n",
			r.width, r.height, r.left, r.top,
			vin->src_rect.width, vin->src_rect.height);
		break;
	case V4L2_SEL_TGT_COMPOSE:
		/* Make sure compose rect fits inside output format */
		max_rect.top = max_rect.left = 0;
		max_rect.width = vin->format.width;
		max_rect.height = vin->format.height;
		v4l2_rect_map_inside(&r, &max_rect);

		/*
		 * Composing is done by adding a offset to the buffer address,
		 * the HW wants this address to be aligned to HW_BUFFER_MASK.
		 * Make sure the top and left values meets this requirement.
		 */
		while ((r.top * vin->format.bytesperline) & HW_BUFFER_MASK)
			r.top--;

		fmt = rvin_format_from_pixel(vin, vin->format.pixelformat);
		while ((r.left * fmt->bpp) & HW_BUFFER_MASK)
			r.left--;

		vin->compose = s->r = r;

		vin_dbg(vin, "Compose %dx%d@%d:%d in %dx%d\n",
			r.width, r.height, r.left, r.top,
			vin->format.width, vin->format.height);
		break;
	default:
		return -EINVAL;
	}

	/* HW supports modifying configuration while running */
	rvin_crop_scale_comp(vin);

	return 0;
}

static int rvin_g_parm(struct file *file, void *priv,
		       struct v4l2_streamparm *parm)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);

	return v4l2_g_parm_cap(&vin->vdev, sd, parm);
}

static int rvin_s_parm(struct file *file, void *priv,
		       struct v4l2_streamparm *parm)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);

	return v4l2_s_parm_cap(&vin->vdev, sd, parm);
}

static int rvin_g_pixelaspect(struct file *file, void *priv,
			      int type, struct v4l2_fract *f)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	return v4l2_subdev_call(sd, video, g_pixelaspect, f);
}

static int rvin_enum_input(struct file *file, void *priv,
			   struct v4l2_input *i)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);
	int ret;

	if (i->index != 0)
		return -EINVAL;

	ret = v4l2_subdev_call(sd, video, g_input_status, &i->status);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		return ret;

	i->type = V4L2_INPUT_TYPE_CAMERA;

	if (v4l2_subdev_has_op(sd, pad, dv_timings_cap)) {
		i->capabilities = V4L2_IN_CAP_DV_TIMINGS;
		i->std = 0;
	} else {
		i->capabilities = V4L2_IN_CAP_STD;
		i->std = vin->vdev.tvnorms;
	}

	strscpy(i->name, "Camera", sizeof(i->name));

	return 0;
}

static int rvin_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int rvin_s_input(struct file *file, void *priv, unsigned int i)
{
	if (i > 0)
		return -EINVAL;
	return 0;
}

static int rvin_querystd(struct file *file, void *priv, v4l2_std_id *a)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);

	return v4l2_subdev_call(sd, video, querystd, a);
}

static int rvin_s_std(struct file *file, void *priv, v4l2_std_id a)
{
	struct rvin_dev *vin = video_drvdata(file);
	int ret;

	ret = v4l2_subdev_call(vin_to_source(vin), video, s_std, a);
	if (ret < 0)
		return ret;

	vin->std = a;

	/* Changing the standard will change the width/height */
	return rvin_reset_format(vin);
}

static int rvin_g_std(struct file *file, void *priv, v4l2_std_id *a)
{
	struct rvin_dev *vin = video_drvdata(file);

	if (v4l2_subdev_has_op(vin_to_source(vin), pad, dv_timings_cap))
		return -ENOIOCTLCMD;

	*a = vin->std;

	return 0;
}

static int rvin_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_event_subscribe(fh, sub, 4, NULL);
	}
	return v4l2_ctrl_subscribe_event(fh, sub);
}

static int rvin_enum_dv_timings(struct file *file, void *priv_fh,
				struct v4l2_enum_dv_timings *timings)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);
	int ret;

	if (timings->pad)
		return -EINVAL;

	timings->pad = vin->parallel.sink_pad;

	ret = v4l2_subdev_call(sd, pad, enum_dv_timings, timings);

	timings->pad = 0;

	return ret;
}

static int rvin_s_dv_timings(struct file *file, void *priv_fh,
			     struct v4l2_dv_timings *timings)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);
	int ret;

	ret = v4l2_subdev_call(sd, video, s_dv_timings, timings);
	if (ret)
		return ret;

	/* Changing the timings will change the width/height */
	return rvin_reset_format(vin);
}

static int rvin_g_dv_timings(struct file *file, void *priv_fh,
			     struct v4l2_dv_timings *timings)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);

	return v4l2_subdev_call(sd, video, g_dv_timings, timings);
}

static int rvin_query_dv_timings(struct file *file, void *priv_fh,
				 struct v4l2_dv_timings *timings)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);

	return v4l2_subdev_call(sd, video, query_dv_timings, timings);
}

static int rvin_dv_timings_cap(struct file *file, void *priv_fh,
			       struct v4l2_dv_timings_cap *cap)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);
	int ret;

	if (cap->pad)
		return -EINVAL;

	cap->pad = vin->parallel.sink_pad;

	ret = v4l2_subdev_call(sd, pad, dv_timings_cap, cap);

	cap->pad = 0;

	return ret;
}

static int rvin_g_edid(struct file *file, void *fh, struct v4l2_edid *edid)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);
	int ret;

	if (edid->pad)
		return -EINVAL;

	edid->pad = vin->parallel.sink_pad;

	ret = v4l2_subdev_call(sd, pad, get_edid, edid);

	edid->pad = 0;

	return ret;
}

static int rvin_s_edid(struct file *file, void *fh, struct v4l2_edid *edid)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);
	int ret;

	if (edid->pad)
		return -EINVAL;

	edid->pad = vin->parallel.sink_pad;

	ret = v4l2_subdev_call(sd, pad, set_edid, edid);

	edid->pad = 0;

	return ret;
}

static const struct v4l2_ioctl_ops rvin_ioctl_ops = {
	.vidioc_querycap		= rvin_querycap,
	.vidioc_try_fmt_vid_cap		= rvin_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= rvin_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= rvin_s_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap	= rvin_enum_fmt_vid_cap,

	.vidioc_g_selection		= rvin_g_selection,
	.vidioc_s_selection		= rvin_s_selection,

	.vidioc_g_parm			= rvin_g_parm,
	.vidioc_s_parm			= rvin_s_parm,

	.vidioc_g_pixelaspect		= rvin_g_pixelaspect,

	.vidioc_enum_input		= rvin_enum_input,
	.vidioc_g_input			= rvin_g_input,
	.vidioc_s_input			= rvin_s_input,

	.vidioc_dv_timings_cap		= rvin_dv_timings_cap,
	.vidioc_enum_dv_timings		= rvin_enum_dv_timings,
	.vidioc_g_dv_timings		= rvin_g_dv_timings,
	.vidioc_s_dv_timings		= rvin_s_dv_timings,
	.vidioc_query_dv_timings	= rvin_query_dv_timings,

	.vidioc_g_edid			= rvin_g_edid,
	.vidioc_s_edid			= rvin_s_edid,

	.vidioc_querystd		= rvin_querystd,
	.vidioc_g_std			= rvin_g_std,
	.vidioc_s_std			= rvin_s_std,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= rvin_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/* -----------------------------------------------------------------------------
 * V4L2 Media Controller
 */

static void rvin_mc_try_format(struct rvin_dev *vin,
			       struct v4l2_pix_format *pix)
{
	/*
	 * The V4L2 specification clearly documents the colorspace fields
	 * as being set by drivers for capture devices. Using the values
	 * supplied by userspace thus wouldn't comply with the API. Until
	 * the API is updated force fixed values.
	 */
	pix->colorspace = RVIN_DEFAULT_COLORSPACE;
	pix->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(pix->colorspace);
	pix->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(pix->colorspace);
	pix->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true, pix->colorspace,
							  pix->ycbcr_enc);

	rvin_format_align(vin, pix);
}

static int rvin_mc_try_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct rvin_dev *vin = video_drvdata(file);

	rvin_mc_try_format(vin, &f->fmt.pix);

	return 0;
}

static int rvin_mc_s_fmt_vid_cap(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct rvin_dev *vin = video_drvdata(file);

	if (vb2_is_busy(&vin->queue))
		return -EBUSY;

	rvin_mc_try_format(vin, &f->fmt.pix);

	vin->format = f->fmt.pix;

	vin->crop.top = 0;
	vin->crop.left = 0;
	vin->crop.width = vin->format.width;
	vin->crop.height = vin->format.height;
	vin->compose = vin->crop;

	return 0;
}

static const struct v4l2_ioctl_ops rvin_mc_ioctl_ops = {
	.vidioc_querycap		= rvin_querycap,
	.vidioc_try_fmt_vid_cap		= rvin_mc_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= rvin_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= rvin_mc_s_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap	= rvin_enum_fmt_vid_cap,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= rvin_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/* -----------------------------------------------------------------------------
 * File Operations
 */

static int rvin_power_parallel(struct rvin_dev *vin, bool on)
{
	struct v4l2_subdev *sd = vin_to_source(vin);
	int power = on ? 1 : 0;
	int ret;

	ret = v4l2_subdev_call(sd, core, s_power, power);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		return ret;

	return 0;
}

static int rvin_open(struct file *file)
{
	struct rvin_dev *vin = video_drvdata(file);
	int ret;

	ret = pm_runtime_resume_and_get(vin->dev);
	if (ret < 0)
		return ret;

	ret = mutex_lock_interruptible(&vin->lock);
	if (ret)
		goto err_pm;

	file->private_data = vin;

	ret = v4l2_fh_open(file);
	if (ret)
		goto err_unlock;

	if (vin->info->use_mc)
		ret = v4l2_pipeline_pm_get(&vin->vdev.entity);
	else if (v4l2_fh_is_singular_file(file))
		ret = rvin_power_parallel(vin, true);

	if (ret < 0)
		goto err_open;

	ret = v4l2_ctrl_handler_setup(&vin->ctrl_handler);
	if (ret)
		goto err_power;

	mutex_unlock(&vin->lock);

	return 0;
err_power:
	if (vin->info->use_mc)
		v4l2_pipeline_pm_put(&vin->vdev.entity);
	else if (v4l2_fh_is_singular_file(file))
		rvin_power_parallel(vin, false);
err_open:
	v4l2_fh_release(file);
err_unlock:
	mutex_unlock(&vin->lock);
err_pm:
	pm_runtime_put(vin->dev);

	return ret;
}

static int rvin_release(struct file *file)
{
	struct rvin_dev *vin = video_drvdata(file);
	bool fh_singular;
	int ret;

	mutex_lock(&vin->lock);

	/* Save the singular status before we call the clean-up helper */
	fh_singular = v4l2_fh_is_singular_file(file);

	/* the release helper will cleanup any on-going streaming */
	ret = _vb2_fop_release(file, NULL);

	if (vin->info->use_mc) {
		v4l2_pipeline_pm_put(&vin->vdev.entity);
	} else {
		if (fh_singular)
			rvin_power_parallel(vin, false);
	}

	mutex_unlock(&vin->lock);

	pm_runtime_put(vin->dev);

	return ret;
}

static const struct v4l2_file_operations rvin_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
	.open		= rvin_open,
	.release	= rvin_release,
	.poll		= vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
	.read		= vb2_fop_read,
};

void rvin_v4l2_unregister(struct rvin_dev *vin)
{
	if (!video_is_registered(&vin->vdev))
		return;

	v4l2_info(&vin->v4l2_dev, "Removing %s\n",
		  video_device_node_name(&vin->vdev));

	/* Checks internally if vdev have been init or not */
	video_unregister_device(&vin->vdev);
}

static void rvin_notify_video_device(struct rvin_dev *vin,
				     unsigned int notification, void *arg)
{
	switch (notification) {
	case V4L2_DEVICE_NOTIFY_EVENT:
		v4l2_event_queue(&vin->vdev, arg);
		break;
	default:
		break;
	}
}

static void rvin_notify(struct v4l2_subdev *sd,
			unsigned int notification, void *arg)
{
	struct v4l2_subdev *remote;
	struct rvin_group *group;
	struct media_pad *pad;
	struct rvin_dev *vin =
		container_of(sd->v4l2_dev, struct rvin_dev, v4l2_dev);
	unsigned int i;

	/* If no media controller, no need to route the event. */
	if (!vin->info->use_mc) {
		rvin_notify_video_device(vin, notification, arg);
		return;
	}

	group = vin->group;

	for (i = 0; i < RCAR_VIN_NUM; i++) {
		vin = group->vin[i];
		if (!vin)
			continue;

		pad = media_entity_remote_pad(&vin->pad);
		if (!pad)
			continue;

		remote = media_entity_to_v4l2_subdev(pad->entity);
		if (remote != sd)
			continue;

		rvin_notify_video_device(vin, notification, arg);
	}
}

int rvin_v4l2_register(struct rvin_dev *vin)
{
	struct video_device *vdev = &vin->vdev;
	int ret;

	vin->v4l2_dev.notify = rvin_notify;

	/* video node */
	vdev->v4l2_dev = &vin->v4l2_dev;
	vdev->queue = &vin->queue;
	snprintf(vdev->name, sizeof(vdev->name), "VIN%u output", vin->id);
	vdev->release = video_device_release_empty;
	vdev->lock = &vin->lock;
	vdev->fops = &rvin_fops;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
		V4L2_CAP_READWRITE;

	/* Set a default format */
	vin->format.pixelformat	= RVIN_DEFAULT_FORMAT;
	vin->format.width = RVIN_DEFAULT_WIDTH;
	vin->format.height = RVIN_DEFAULT_HEIGHT;
	vin->format.field = RVIN_DEFAULT_FIELD;
	vin->format.colorspace = RVIN_DEFAULT_COLORSPACE;

	if (vin->info->use_mc) {
		vdev->device_caps |= V4L2_CAP_IO_MC;
		vdev->ioctl_ops = &rvin_mc_ioctl_ops;
	} else {
		vdev->ioctl_ops = &rvin_ioctl_ops;
		rvin_reset_format(vin);
	}

	rvin_format_align(vin, &vin->format);

	ret = video_register_device(&vin->vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		vin_err(vin, "Failed to register video device\n");
		return ret;
	}

	video_set_drvdata(&vin->vdev, vin);

	v4l2_info(&vin->v4l2_dev, "Device registered as %s\n",
		  video_device_node_name(&vin->vdev));

	return ret;
}

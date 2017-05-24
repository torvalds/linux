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

#include <linux/pm_runtime.h>

#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-rect.h>

#include "rcar-vin.h"

#define RVIN_DEFAULT_FORMAT	V4L2_PIX_FMT_YUYV
#define RVIN_MAX_WIDTH		2048
#define RVIN_MAX_HEIGHT		2048

/* -----------------------------------------------------------------------------
 * Format Conversions
 */

static const struct rvin_video_format rvin_formats[] = {
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
};

const struct rvin_video_format *rvin_format_from_pixel(u32 pixelformat)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rvin_formats); i++)
		if (rvin_formats[i].fourcc == pixelformat)
			return rvin_formats + i;

	return NULL;
}

static u32 rvin_format_bytesperline(struct v4l2_pix_format *pix)
{
	const struct rvin_video_format *fmt;

	fmt = rvin_format_from_pixel(pix->pixelformat);

	if (WARN_ON(!fmt))
		return -EINVAL;

	return pix->width * fmt->bpp;
}

static u32 rvin_format_sizeimage(struct v4l2_pix_format *pix)
{
	if (pix->pixelformat == V4L2_PIX_FMT_NV16)
		return pix->bytesperline * pix->height * 2;

	return pix->bytesperline * pix->height;
}

/* -----------------------------------------------------------------------------
 * V4L2
 */

static void rvin_reset_crop_compose(struct rvin_dev *vin)
{
	vin->crop.top = vin->crop.left = 0;
	vin->crop.width = vin->source.width;
	vin->crop.height = vin->source.height;

	vin->compose.top = vin->compose.left = 0;
	vin->compose.width = vin->format.width;
	vin->compose.height = vin->format.height;
}

static int rvin_reset_format(struct rvin_dev *vin)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct v4l2_mbus_framefmt *mf = &fmt.format;
	int ret;

	fmt.pad = vin->digital.source_pad;

	ret = v4l2_subdev_call(vin_to_source(vin), pad, get_fmt, NULL, &fmt);
	if (ret)
		return ret;

	vin->format.width	= mf->width;
	vin->format.height	= mf->height;
	vin->format.colorspace	= mf->colorspace;
	vin->format.field	= mf->field;

	/*
	 * If the subdevice uses ALTERNATE field mode and G_STD is
	 * implemented use the VIN HW to combine the two fields to
	 * one INTERLACED frame. The ALTERNATE field mode can still
	 * be requested in S_FMT and be respected, this is just the
	 * default which is applied at probing or when S_STD is called.
	 */
	if (vin->format.field == V4L2_FIELD_ALTERNATE &&
	    v4l2_subdev_has_op(vin_to_source(vin), video, g_std))
		vin->format.field = V4L2_FIELD_INTERLACED;

	switch (vin->format.field) {
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
	case V4L2_FIELD_ALTERNATE:
		vin->format.height /= 2;
		break;
	case V4L2_FIELD_NONE:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
	case V4L2_FIELD_INTERLACED:
		break;
	default:
		vin->format.field = V4L2_FIELD_NONE;
		break;
	}

	rvin_reset_crop_compose(vin);

	vin->format.bytesperline = rvin_format_bytesperline(&vin->format);
	vin->format.sizeimage = rvin_format_sizeimage(&vin->format);

	return 0;
}

static int __rvin_try_format_source(struct rvin_dev *vin,
				    u32 which,
				    struct v4l2_pix_format *pix,
				    struct rvin_source_fmt *source)
{
	struct v4l2_subdev *sd;
	struct v4l2_subdev_pad_config *pad_cfg;
	struct v4l2_subdev_format format = {
		.which = which,
	};
	enum v4l2_field field;
	int ret;

	sd = vin_to_source(vin);

	v4l2_fill_mbus_format(&format.format, pix, vin->digital.code);

	pad_cfg = v4l2_subdev_alloc_pad_config(sd);
	if (pad_cfg == NULL)
		return -ENOMEM;

	format.pad = vin->digital.source_pad;

	field = pix->field;

	ret = v4l2_subdev_call(sd, pad, set_fmt, pad_cfg, &format);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		goto done;

	v4l2_fill_pix_format(pix, &format.format);

	pix->field = field;

	source->width = pix->width;
	source->height = pix->height;

	vin_dbg(vin, "Source resolution: %ux%u\n", source->width,
		source->height);

done:
	v4l2_subdev_free_pad_config(pad_cfg);
	return ret;
}

static int __rvin_try_format(struct rvin_dev *vin,
			     u32 which,
			     struct v4l2_pix_format *pix,
			     struct rvin_source_fmt *source)
{
	const struct rvin_video_format *info;
	u32 rwidth, rheight, walign;

	/* Requested */
	rwidth = pix->width;
	rheight = pix->height;

	/* Keep current field if no specific one is asked for */
	if (pix->field == V4L2_FIELD_ANY)
		pix->field = vin->format.field;

	/*
	 * Retrieve format information and select the current format if the
	 * requested format isn't supported.
	 */
	info = rvin_format_from_pixel(pix->pixelformat);
	if (!info) {
		vin_dbg(vin, "Format %x not found, keeping %x\n",
			pix->pixelformat, vin->format.pixelformat);
		*pix = vin->format;
		pix->width = rwidth;
		pix->height = rheight;
	}

	/* Always recalculate */
	pix->bytesperline = 0;
	pix->sizeimage = 0;

	/* Limit to source capabilities */
	__rvin_try_format_source(vin, which, pix, source);

	switch (pix->field) {
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
	case V4L2_FIELD_ALTERNATE:
		pix->height /= 2;
		source->height /= 2;
		break;
	case V4L2_FIELD_NONE:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
	case V4L2_FIELD_INTERLACED:
		break;
	default:
		pix->field = V4L2_FIELD_NONE;
		break;
	}

	/* If source can't match format try if VIN can scale */
	if (source->width != rwidth || source->height != rheight)
		rvin_scale_try(vin, pix, rwidth, rheight);

	/* HW limit width to a multiple of 32 (2^5) for NV16 else 2 (2^1) */
	walign = vin->format.pixelformat == V4L2_PIX_FMT_NV16 ? 5 : 1;

	/* Limit to VIN capabilities */
	v4l_bound_align_image(&pix->width, 2, RVIN_MAX_WIDTH, walign,
			      &pix->height, 4, RVIN_MAX_HEIGHT, 2, 0);

	pix->bytesperline = max_t(u32, pix->bytesperline,
				  rvin_format_bytesperline(pix));
	pix->sizeimage = max_t(u32, pix->sizeimage,
			       rvin_format_sizeimage(pix));

	if (vin->chip == RCAR_M1 && pix->pixelformat == V4L2_PIX_FMT_XBGR32) {
		vin_err(vin, "pixel format XBGR32 not supported on M1\n");
		return -EINVAL;
	}

	vin_dbg(vin, "Requested %ux%u Got %ux%u bpl: %d size: %d\n",
		rwidth, rheight, pix->width, pix->height,
		pix->bytesperline, pix->sizeimage);

	return 0;
}

static int rvin_querycap(struct file *file, void *priv,
			 struct v4l2_capability *cap)
{
	struct rvin_dev *vin = video_drvdata(file);

	strlcpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	strlcpy(cap->card, "R_Car_VIN", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(vin->dev));
	return 0;
}

static int rvin_try_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct rvin_source_fmt source;

	return __rvin_try_format(vin, V4L2_SUBDEV_FORMAT_TRY, &f->fmt.pix,
				 &source);
}

static int rvin_s_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct rvin_source_fmt source;
	int ret;

	if (vb2_is_busy(&vin->queue))
		return -EBUSY;

	ret = __rvin_try_format(vin, V4L2_SUBDEV_FORMAT_ACTIVE, &f->fmt.pix,
				&source);
	if (ret)
		return ret;

	vin->source.width = source.width;
	vin->source.height = source.height;

	vin->format = f->fmt.pix;

	rvin_reset_crop_compose(vin);

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
	if (f->index >= ARRAY_SIZE(rvin_formats))
		return -EINVAL;

	f->pixelformat = rvin_formats[f->index].fourcc;

	return 0;
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
		s->r.width = vin->source.width;
		s->r.height = vin->source.height;
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
		max_rect.width = vin->source.width;
		max_rect.height = vin->source.height;
		v4l2_rect_map_inside(&r, &max_rect);

		v4l_bound_align_image(&r.width, 2, vin->source.width, 1,
				      &r.height, 4, vin->source.height, 2, 0);

		r.top  = clamp_t(s32, r.top, 0, vin->source.height - r.height);
		r.left = clamp_t(s32, r.left, 0, vin->source.width - r.width);

		vin->crop = s->r = r;

		vin_dbg(vin, "Cropped %dx%d@%d:%d of %dx%d\n",
			r.width, r.height, r.left, r.top,
			vin->source.width, vin->source.height);
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

		fmt = rvin_format_from_pixel(vin->format.pixelformat);
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

static int rvin_cropcap(struct file *file, void *priv,
			struct v4l2_cropcap *crop)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);

	if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	return v4l2_subdev_call(sd, video, g_pixelaspect, &crop->pixelaspect);
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

	strlcpy(i->name, "Camera", sizeof(i->name));

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

	/* Changing the standard will change the width/height */
	return rvin_reset_format(vin);
}

static int rvin_g_std(struct file *file, void *priv, v4l2_std_id *a)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);

	return v4l2_subdev_call(sd, video, g_std, a);
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

	timings->pad = vin->digital.sink_pad;

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

	cap->pad = vin->digital.sink_pad;

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

	edid->pad = vin->digital.sink_pad;

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

	edid->pad = vin->digital.sink_pad;

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

	.vidioc_cropcap			= rvin_cropcap,

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
 * File Operations
 */

static int rvin_power_on(struct rvin_dev *vin)
{
	int ret;
	struct v4l2_subdev *sd = vin_to_source(vin);

	pm_runtime_get_sync(vin->v4l2_dev.dev);

	ret = v4l2_subdev_call(sd, core, s_power, 1);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		return ret;
	return 0;
}

static int rvin_power_off(struct rvin_dev *vin)
{
	int ret;
	struct v4l2_subdev *sd = vin_to_source(vin);

	ret = v4l2_subdev_call(sd, core, s_power, 0);

	pm_runtime_put(vin->v4l2_dev.dev);

	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		return ret;

	return 0;
}

static int rvin_initialize_device(struct file *file)
{
	struct rvin_dev *vin = video_drvdata(file);
	int ret;

	struct v4l2_format f = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.fmt.pix = {
			.width		= vin->format.width,
			.height		= vin->format.height,
			.field		= vin->format.field,
			.colorspace	= vin->format.colorspace,
			.pixelformat	= vin->format.pixelformat,
		},
	};

	ret = rvin_power_on(vin);
	if (ret < 0)
		return ret;

	pm_runtime_enable(&vin->vdev.dev);
	ret = pm_runtime_resume(&vin->vdev.dev);
	if (ret < 0 && ret != -ENOSYS)
		goto eresume;

	/*
	 * Try to configure with default parameters. Notice: this is the
	 * very first open, so, we cannot race against other calls,
	 * apart from someone else calling open() simultaneously, but
	 * .host_lock is protecting us against it.
	 */
	ret = rvin_s_fmt_vid_cap(file, NULL, &f);
	if (ret < 0)
		goto esfmt;

	v4l2_ctrl_handler_setup(&vin->ctrl_handler);

	return 0;
esfmt:
	pm_runtime_disable(&vin->vdev.dev);
eresume:
	rvin_power_off(vin);

	return ret;
}

static int rvin_open(struct file *file)
{
	struct rvin_dev *vin = video_drvdata(file);
	int ret;

	mutex_lock(&vin->lock);

	file->private_data = vin;

	ret = v4l2_fh_open(file);
	if (ret)
		goto unlock;

	if (!v4l2_fh_is_singular_file(file))
		goto unlock;

	if (rvin_initialize_device(file)) {
		v4l2_fh_release(file);
		ret = -ENODEV;
	}

unlock:
	mutex_unlock(&vin->lock);
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

	/*
	 * If this was the last open file.
	 * Then de-initialize hw module.
	 */
	if (fh_singular) {
		pm_runtime_suspend(&vin->vdev.dev);
		pm_runtime_disable(&vin->vdev.dev);
		rvin_power_off(vin);
	}

	mutex_unlock(&vin->lock);

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

void rvin_v4l2_remove(struct rvin_dev *vin)
{
	v4l2_info(&vin->v4l2_dev, "Removing %s\n",
		  video_device_node_name(&vin->vdev));

	/* Checks internaly if handlers have been init or not */
	v4l2_ctrl_handler_free(&vin->ctrl_handler);

	/* Checks internaly if vdev have been init or not */
	video_unregister_device(&vin->vdev);
}

static void rvin_notify(struct v4l2_subdev *sd,
			unsigned int notification, void *arg)
{
	struct rvin_dev *vin =
		container_of(sd->v4l2_dev, struct rvin_dev, v4l2_dev);

	switch (notification) {
	case V4L2_DEVICE_NOTIFY_EVENT:
		v4l2_event_queue(&vin->vdev, arg);
		break;
	default:
		break;
	}
}

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

int rvin_v4l2_probe(struct rvin_dev *vin)
{
	struct video_device *vdev = &vin->vdev;
	struct v4l2_subdev *sd = vin_to_source(vin);
	int ret;

	v4l2_set_subdev_hostdata(sd, vin);

	vin->v4l2_dev.notify = rvin_notify;

	ret = v4l2_subdev_call(sd, video, g_tvnorms, &vin->vdev.tvnorms);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		return ret;

	if (vin->vdev.tvnorms == 0) {
		/* Disable the STD API if there are no tvnorms defined */
		v4l2_disable_ioctl(&vin->vdev, VIDIOC_G_STD);
		v4l2_disable_ioctl(&vin->vdev, VIDIOC_S_STD);
		v4l2_disable_ioctl(&vin->vdev, VIDIOC_QUERYSTD);
		v4l2_disable_ioctl(&vin->vdev, VIDIOC_ENUMSTD);
	}

	/* Add the controls */
	/*
	 * Currently the subdev with the largest number of controls (13) is
	 * ov6550. So let's pick 16 as a hint for the control handler. Note
	 * that this is a hint only: too large and you waste some memory, too
	 * small and there is a (very) small performance hit when looking up
	 * controls in the internal hash.
	 */
	ret = v4l2_ctrl_handler_init(&vin->ctrl_handler, 16);
	if (ret < 0)
		return ret;

	ret = v4l2_ctrl_add_handler(&vin->ctrl_handler, sd->ctrl_handler, NULL);
	if (ret < 0)
		return ret;

	/* video node */
	vdev->fops = &rvin_fops;
	vdev->v4l2_dev = &vin->v4l2_dev;
	vdev->queue = &vin->queue;
	strlcpy(vdev->name, KBUILD_MODNAME, sizeof(vdev->name));
	vdev->release = video_device_release_empty;
	vdev->ioctl_ops = &rvin_ioctl_ops;
	vdev->lock = &vin->lock;
	vdev->ctrl_handler = &vin->ctrl_handler;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
		V4L2_CAP_READWRITE;

	ret = rvin_find_pad(sd, MEDIA_PAD_FL_SOURCE);
	if (ret < 0)
		return ret;
	vin->digital.source_pad = ret;

	ret = rvin_find_pad(sd, MEDIA_PAD_FL_SINK);
	vin->digital.sink_pad = ret < 0 ? 0 : ret;

	vin->format.pixelformat	= RVIN_DEFAULT_FORMAT;
	rvin_reset_format(vin);

	ret = video_register_device(&vin->vdev, VFL_TYPE_GRABBER, -1);
	if (ret) {
		vin_err(vin, "Failed to register video device\n");
		return ret;
	}

	video_set_drvdata(&vin->vdev, vin);

	v4l2_info(&vin->v4l2_dev, "Device registered as %s\n",
		  video_device_node_name(&vin->vdev));

	return ret;
}

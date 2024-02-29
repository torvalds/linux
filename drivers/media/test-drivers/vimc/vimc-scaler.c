// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * vimc-scaler.c Virtual Media Controller Driver
 *
 * Copyright (C) 2015-2017 Helen Koike <helen.fornazier@gmail.com>
 */

#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-rect.h>
#include <media/v4l2-subdev.h>

#include "vimc-common.h"

/* Pad identifier */
enum vic_sca_pad {
	VIMC_SCALER_SINK = 0,
	VIMC_SCALER_SRC = 1,
};

#define VIMC_SCALER_FMT_WIDTH_DEFAULT  640
#define VIMC_SCALER_FMT_HEIGHT_DEFAULT 480

struct vimc_scaler_device {
	struct vimc_ent_device ved;
	struct v4l2_subdev sd;
	struct v4l2_rect crop_rect;
	/* Frame format for both sink and src pad */
	struct v4l2_mbus_framefmt fmt[2];
	/* Values calculated when the stream starts */
	u8 *src_frame;
	unsigned int bpp;
	struct media_pad pads[2];
};

static const struct v4l2_mbus_framefmt fmt_default = {
	.width = VIMC_SCALER_FMT_WIDTH_DEFAULT,
	.height = VIMC_SCALER_FMT_HEIGHT_DEFAULT,
	.code = MEDIA_BUS_FMT_RGB888_1X24,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_SRGB,
};

static const struct v4l2_rect crop_rect_default = {
	.width = VIMC_SCALER_FMT_WIDTH_DEFAULT,
	.height = VIMC_SCALER_FMT_HEIGHT_DEFAULT,
	.top = 0,
	.left = 0,
};

static const struct v4l2_rect crop_rect_min = {
	.width = VIMC_FRAME_MIN_WIDTH,
	.height = VIMC_FRAME_MIN_HEIGHT,
	.top = 0,
	.left = 0,
};

static struct v4l2_rect
vimc_scaler_get_crop_bound_sink(const struct v4l2_mbus_framefmt *sink_fmt)
{
	/* Get the crop bounds to clamp the crop rectangle correctly */
	struct v4l2_rect r = {
		.left = 0,
		.top = 0,
		.width = sink_fmt->width,
		.height = sink_fmt->height,
	};
	return r;
}

static int vimc_scaler_init_state(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state)
{
	struct v4l2_mbus_framefmt *mf;
	struct v4l2_rect *r;
	unsigned int i;

	for (i = 0; i < sd->entity.num_pads; i++) {
		mf = v4l2_subdev_state_get_format(sd_state, i);
		*mf = fmt_default;
	}

	r = v4l2_subdev_state_get_crop(sd_state, VIMC_SCALER_SINK);
	*r = crop_rect_default;

	return 0;
}

static int vimc_scaler_enum_mbus_code(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	u32 mbus_code = vimc_mbus_code_by_index(code->index);
	const struct vimc_pix_map *vpix;

	if (!mbus_code)
		return -EINVAL;

	vpix = vimc_pix_map_by_code(mbus_code);

	/* We don't support bayer format */
	if (!vpix || vpix->bayer)
		return -EINVAL;

	code->code = mbus_code;

	return 0;
}

static int vimc_scaler_enum_frame_size(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *sd_state,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	const struct vimc_pix_map *vpix;

	if (fse->index)
		return -EINVAL;

	/* Only accept code in the pix map table in non bayer format */
	vpix = vimc_pix_map_by_code(fse->code);
	if (!vpix || vpix->bayer)
		return -EINVAL;

	fse->min_width = VIMC_FRAME_MIN_WIDTH;
	fse->min_height = VIMC_FRAME_MIN_HEIGHT;

	fse->max_width = VIMC_FRAME_MAX_WIDTH;
	fse->max_height = VIMC_FRAME_MAX_HEIGHT;

	return 0;
}

static struct v4l2_mbus_framefmt *
vimc_scaler_pad_format(struct vimc_scaler_device *vscaler,
		    struct v4l2_subdev_state *sd_state, u32 pad,
		    enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_state_get_format(sd_state, pad);
	else
		return &vscaler->fmt[pad];
}

static struct v4l2_rect *
vimc_scaler_pad_crop(struct vimc_scaler_device *vscaler,
		  struct v4l2_subdev_state *sd_state,
		  enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_state_get_crop(sd_state, VIMC_SCALER_SINK);
	else
		return &vscaler->crop_rect;
}

static int vimc_scaler_get_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *format)
{
	struct vimc_scaler_device *vscaler = v4l2_get_subdevdata(sd);

	format->format = *vimc_scaler_pad_format(vscaler, sd_state, format->pad,
					      format->which);
	return 0;
}

static int vimc_scaler_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *format)
{
	struct vimc_scaler_device *vscaler = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *fmt;

	/* Do not change the active format while stream is on */
	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE && vscaler->src_frame)
		return -EBUSY;

	fmt = vimc_scaler_pad_format(vscaler, sd_state, format->pad, format->which);

	/*
	 * The media bus code and colorspace can only be changed on the sink
	 * pad, the source pad only follows.
	 */
	if (format->pad == VIMC_SCALER_SINK) {
		const struct vimc_pix_map *vpix;

		/* Only accept code in the pix map table in non bayer format. */
		vpix = vimc_pix_map_by_code(format->format.code);
		if (vpix && !vpix->bayer)
			fmt->code = format->format.code;
		else
			fmt->code = fmt_default.code;

		/* Clamp the colorspace to valid values. */
		fmt->colorspace = format->format.colorspace;
		fmt->ycbcr_enc = format->format.ycbcr_enc;
		fmt->quantization = format->format.quantization;
		fmt->xfer_func = format->format.xfer_func;
		vimc_colorimetry_clamp(fmt);
	}

	/* Clamp and align the width and height */
	fmt->width = clamp_t(u32, format->format.width, VIMC_FRAME_MIN_WIDTH,
			     VIMC_FRAME_MAX_WIDTH) & ~1;
	fmt->height = clamp_t(u32, format->format.height, VIMC_FRAME_MIN_HEIGHT,
			      VIMC_FRAME_MAX_HEIGHT) & ~1;

	/*
	 * Propagate the sink pad format to the crop rectangle and the source
	 * pad.
	 */
	if (format->pad == VIMC_SCALER_SINK) {
		struct v4l2_mbus_framefmt *src_fmt;
		struct v4l2_rect *crop;

		crop = vimc_scaler_pad_crop(vscaler, sd_state, format->which);
		crop->width = fmt->width;
		crop->height = fmt->height;
		crop->top = 0;
		crop->left = 0;

		src_fmt = vimc_scaler_pad_format(vscaler, sd_state, VIMC_SCALER_SRC,
					      format->which);
		*src_fmt = *fmt;
	}

	format->format = *fmt;

	return 0;
}

static int vimc_scaler_get_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_selection *sel)
{
	struct vimc_scaler_device *vscaler = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *sink_fmt;

	if (VIMC_IS_SRC(sel->pad))
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *vimc_scaler_pad_crop(vscaler, sd_state, sel->which);
		break;
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sink_fmt = vimc_scaler_pad_format(vscaler, sd_state, VIMC_SCALER_SINK,
					       sel->which);
		sel->r = vimc_scaler_get_crop_bound_sink(sink_fmt);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void vimc_scaler_adjust_sink_crop(struct v4l2_rect *r,
				      const struct v4l2_mbus_framefmt *sink_fmt)
{
	const struct v4l2_rect sink_rect =
		vimc_scaler_get_crop_bound_sink(sink_fmt);

	/* Disallow rectangles smaller than the minimal one. */
	v4l2_rect_set_min_size(r, &crop_rect_min);
	v4l2_rect_map_inside(r, &sink_rect);
}

static int vimc_scaler_set_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_selection *sel)
{
	struct vimc_scaler_device *vscaler = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *sink_fmt;
	struct v4l2_rect *crop_rect;

	/* Only support setting the crop of the sink pad */
	if (VIMC_IS_SRC(sel->pad) || sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE && vscaler->src_frame)
		return -EBUSY;

	crop_rect = vimc_scaler_pad_crop(vscaler, sd_state, sel->which);
	sink_fmt = vimc_scaler_pad_format(vscaler, sd_state, VIMC_SCALER_SINK,
				       sel->which);
	vimc_scaler_adjust_sink_crop(&sel->r, sink_fmt);
	*crop_rect = sel->r;

	return 0;
}

static const struct v4l2_subdev_pad_ops vimc_scaler_pad_ops = {
	.enum_mbus_code		= vimc_scaler_enum_mbus_code,
	.enum_frame_size	= vimc_scaler_enum_frame_size,
	.get_fmt		= vimc_scaler_get_fmt,
	.set_fmt		= vimc_scaler_set_fmt,
	.get_selection		= vimc_scaler_get_selection,
	.set_selection		= vimc_scaler_set_selection,
};

static int vimc_scaler_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct vimc_scaler_device *vscaler = v4l2_get_subdevdata(sd);

	if (enable) {
		const struct vimc_pix_map *vpix;
		unsigned int frame_size;

		if (vscaler->src_frame)
			return 0;

		/* Save the bytes per pixel of the sink */
		vpix = vimc_pix_map_by_code(vscaler->fmt[VIMC_SCALER_SINK].code);
		vscaler->bpp = vpix->bpp;

		/* Calculate the frame size of the source pad */
		frame_size = vscaler->fmt[VIMC_SCALER_SRC].width
			   * vscaler->fmt[VIMC_SCALER_SRC].height * vscaler->bpp;

		/* Allocate the frame buffer. Use vmalloc to be able to
		 * allocate a large amount of memory
		 */
		vscaler->src_frame = vmalloc(frame_size);
		if (!vscaler->src_frame)
			return -ENOMEM;

	} else {
		if (!vscaler->src_frame)
			return 0;

		vfree(vscaler->src_frame);
		vscaler->src_frame = NULL;
	}

	return 0;
}

static const struct v4l2_subdev_video_ops vimc_scaler_video_ops = {
	.s_stream = vimc_scaler_s_stream,
};

static const struct v4l2_subdev_ops vimc_scaler_ops = {
	.pad = &vimc_scaler_pad_ops,
	.video = &vimc_scaler_video_ops,
};

static const struct v4l2_subdev_internal_ops vimc_scaler_internal_ops = {
	.init_state = vimc_scaler_init_state,
};

static void vimc_scaler_fill_src_frame(const struct vimc_scaler_device *const vscaler,
				    const u8 *const sink_frame)
{
	const struct v4l2_mbus_framefmt *src_fmt = &vscaler->fmt[VIMC_SCALER_SRC];
	const struct v4l2_rect *r = &vscaler->crop_rect;
	unsigned int snk_width = vscaler->fmt[VIMC_SCALER_SINK].width;
	unsigned int src_x, src_y;
	u8 *walker = vscaler->src_frame;

	/* Set each pixel at the src_frame to its sink_frame equivalent */
	for (src_y = 0; src_y < src_fmt->height; src_y++) {
		unsigned int snk_y, y_offset;

		snk_y = (src_y * r->height) / src_fmt->height + r->top;
		y_offset = snk_y * snk_width * vscaler->bpp;

		for (src_x = 0; src_x < src_fmt->width; src_x++) {
			unsigned int snk_x, x_offset, index;

			snk_x = (src_x * r->width) / src_fmt->width + r->left;
			x_offset = snk_x * vscaler->bpp;
			index = y_offset + x_offset;
			memcpy(walker, &sink_frame[index], vscaler->bpp);
			walker += vscaler->bpp;
		}
	}
}

static void *vimc_scaler_process_frame(struct vimc_ent_device *ved,
				    const void *sink_frame)
{
	struct vimc_scaler_device *vscaler = container_of(ved, struct vimc_scaler_device,
						    ved);

	/* If the stream in this node is not active, just return */
	if (!vscaler->src_frame)
		return ERR_PTR(-EINVAL);

	vimc_scaler_fill_src_frame(vscaler, sink_frame);

	return vscaler->src_frame;
};

static void vimc_scaler_release(struct vimc_ent_device *ved)
{
	struct vimc_scaler_device *vscaler =
		container_of(ved, struct vimc_scaler_device, ved);

	media_entity_cleanup(vscaler->ved.ent);
	kfree(vscaler);
}

static struct vimc_ent_device *vimc_scaler_add(struct vimc_device *vimc,
					    const char *vcfg_name)
{
	struct v4l2_device *v4l2_dev = &vimc->v4l2_dev;
	struct vimc_scaler_device *vscaler;
	int ret;

	/* Allocate the vscaler struct */
	vscaler = kzalloc(sizeof(*vscaler), GFP_KERNEL);
	if (!vscaler)
		return ERR_PTR(-ENOMEM);

	/* Initialize ved and sd */
	vscaler->pads[VIMC_SCALER_SINK].flags = MEDIA_PAD_FL_SINK;
	vscaler->pads[VIMC_SCALER_SRC].flags = MEDIA_PAD_FL_SOURCE;

	ret = vimc_ent_sd_register(&vscaler->ved, &vscaler->sd, v4l2_dev,
				   vcfg_name,
				   MEDIA_ENT_F_PROC_VIDEO_SCALER, 2,
				   vscaler->pads, &vimc_scaler_ops);
	if (ret) {
		kfree(vscaler);
		return ERR_PTR(ret);
	}

	vscaler->sd.internal_ops = &vimc_scaler_internal_ops;

	vscaler->ved.process_frame = vimc_scaler_process_frame;
	vscaler->ved.dev = vimc->mdev.dev;

	/* Initialize the frame format */
	vscaler->fmt[VIMC_SCALER_SINK] = fmt_default;
	vscaler->fmt[VIMC_SCALER_SRC] = fmt_default;

	/* Initialize the crop selection */
	vscaler->crop_rect = crop_rect_default;

	return &vscaler->ved;
}

struct vimc_ent_type vimc_scaler_type = {
	.add = vimc_scaler_add,
	.release = vimc_scaler_release
};

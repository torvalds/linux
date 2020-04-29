// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * vimc-scaler.c Virtual Media Controller Driver
 *
 * Copyright (C) 2015-2017 Helen Koike <helen.fornazier@gmail.com>
 */

#include <linux/moduleparam.h>
#include <linux/vmalloc.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-rect.h>
#include <media/v4l2-subdev.h>

#include "vimc-common.h"

static unsigned int sca_mult = 3;
module_param(sca_mult, uint, 0000);
MODULE_PARM_DESC(sca_mult, " the image size multiplier");

#define MAX_ZOOM	8

#define VIMC_SCA_FMT_WIDTH_DEFAULT  640
#define VIMC_SCA_FMT_HEIGHT_DEFAULT 480

struct vimc_sca_device {
	struct vimc_ent_device ved;
	struct v4l2_subdev sd;
	/* NOTE: the source fmt is the same as the sink
	 * with the width and hight multiplied by mult
	 */
	struct v4l2_mbus_framefmt sink_fmt;
	struct v4l2_rect crop_rect;
	/* Values calculated when the stream starts */
	u8 *src_frame;
	unsigned int src_line_size;
	unsigned int bpp;
	struct media_pad pads[2];
};

static const struct v4l2_mbus_framefmt sink_fmt_default = {
	.width = VIMC_SCA_FMT_WIDTH_DEFAULT,
	.height = VIMC_SCA_FMT_HEIGHT_DEFAULT,
	.code = MEDIA_BUS_FMT_RGB888_1X24,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_DEFAULT,
};

static const struct v4l2_rect crop_rect_default = {
	.width = VIMC_SCA_FMT_WIDTH_DEFAULT,
	.height = VIMC_SCA_FMT_HEIGHT_DEFAULT,
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
vimc_sca_get_crop_bound_sink(const struct v4l2_mbus_framefmt *sink_fmt)
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

static void vimc_sca_adjust_sink_crop(struct v4l2_rect *r,
				      const struct v4l2_mbus_framefmt *sink_fmt)
{
	const struct v4l2_rect sink_rect =
		vimc_sca_get_crop_bound_sink(sink_fmt);

	/* Disallow rectangles smaller than the minimal one. */
	v4l2_rect_set_min_size(r, &crop_rect_min);
	v4l2_rect_map_inside(r, &sink_rect);
}

static int vimc_sca_init_cfg(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg)
{
	struct v4l2_mbus_framefmt *mf;
	struct v4l2_rect *r;
	unsigned int i;

	mf = v4l2_subdev_get_try_format(sd, cfg, 0);
	*mf = sink_fmt_default;

	r = v4l2_subdev_get_try_crop(sd, cfg, 0);
	*r = crop_rect_default;

	for (i = 1; i < sd->entity.num_pads; i++) {
		mf = v4l2_subdev_get_try_format(sd, cfg, i);
		*mf = sink_fmt_default;
		mf->width = mf->width * sca_mult;
		mf->height = mf->height * sca_mult;
	}

	return 0;
}

static int vimc_sca_enum_mbus_code(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	const struct vimc_pix_map *vpix = vimc_pix_map_by_index(code->index);

	/* We don't support bayer format */
	if (!vpix || vpix->bayer)
		return -EINVAL;

	code->code = vpix->code;

	return 0;
}

static int vimc_sca_enum_frame_size(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
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

	if (VIMC_IS_SINK(fse->pad)) {
		fse->max_width = VIMC_FRAME_MAX_WIDTH;
		fse->max_height = VIMC_FRAME_MAX_HEIGHT;
	} else {
		fse->max_width = VIMC_FRAME_MAX_WIDTH * MAX_ZOOM;
		fse->max_height = VIMC_FRAME_MAX_HEIGHT * MAX_ZOOM;
	}

	return 0;
}

static int vimc_sca_get_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *format)
{
	struct vimc_sca_device *vsca = v4l2_get_subdevdata(sd);
	struct v4l2_rect *crop_rect;

	/* Get the current sink format */
	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		format->format = *v4l2_subdev_get_try_format(sd, cfg, 0);
		crop_rect = v4l2_subdev_get_try_crop(sd, cfg, 0);
	} else {
		format->format = vsca->sink_fmt;
		crop_rect = &vsca->crop_rect;
	}

	/* Scale the frame size for the source pad */
	if (VIMC_IS_SRC(format->pad)) {
		format->format.width = crop_rect->width * sca_mult;
		format->format.height = crop_rect->height * sca_mult;
	}

	return 0;
}

static void vimc_sca_adjust_sink_fmt(struct v4l2_mbus_framefmt *fmt)
{
	const struct vimc_pix_map *vpix;

	/* Only accept code in the pix map table in non bayer format */
	vpix = vimc_pix_map_by_code(fmt->code);
	if (!vpix || vpix->bayer)
		fmt->code = sink_fmt_default.code;

	fmt->width = clamp_t(u32, fmt->width, VIMC_FRAME_MIN_WIDTH,
			     VIMC_FRAME_MAX_WIDTH) & ~1;
	fmt->height = clamp_t(u32, fmt->height, VIMC_FRAME_MIN_HEIGHT,
			      VIMC_FRAME_MAX_HEIGHT) & ~1;

	if (fmt->field == V4L2_FIELD_ANY)
		fmt->field = sink_fmt_default.field;

	vimc_colorimetry_clamp(fmt);
}

static int vimc_sca_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *fmt)
{
	struct vimc_sca_device *vsca = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *sink_fmt;
	struct v4l2_rect *crop_rect;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		/* Do not change the format while stream is on */
		if (vsca->src_frame)
			return -EBUSY;

		sink_fmt = &vsca->sink_fmt;
		crop_rect = &vsca->crop_rect;
	} else {
		sink_fmt = v4l2_subdev_get_try_format(sd, cfg, 0);
		crop_rect = v4l2_subdev_get_try_crop(sd, cfg, 0);
	}

	/*
	 * Do not change the format of the source pad,
	 * it is propagated from the sink
	 */
	if (VIMC_IS_SRC(fmt->pad)) {
		fmt->format = *sink_fmt;
		fmt->format.width = crop_rect->width * sca_mult;
		fmt->format.height = crop_rect->height * sca_mult;
	} else {
		/* Set the new format in the sink pad */
		vimc_sca_adjust_sink_fmt(&fmt->format);

		dev_dbg(vsca->ved.dev, "%s: sink format update: "
			"old:%dx%d (0x%x, %d, %d, %d, %d) "
			"new:%dx%d (0x%x, %d, %d, %d, %d)\n", vsca->sd.name,
			/* old */
			sink_fmt->width, sink_fmt->height, sink_fmt->code,
			sink_fmt->colorspace, sink_fmt->quantization,
			sink_fmt->xfer_func, sink_fmt->ycbcr_enc,
			/* new */
			fmt->format.width, fmt->format.height, fmt->format.code,
			fmt->format.colorspace,	fmt->format.quantization,
			fmt->format.xfer_func, fmt->format.ycbcr_enc);

		*sink_fmt = fmt->format;

		/* Do the crop, but respect the current bounds */
		vimc_sca_adjust_sink_crop(crop_rect, sink_fmt);
	}

	return 0;
}

static int vimc_sca_get_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_selection *sel)
{
	struct vimc_sca_device *vsca = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *sink_fmt;
	struct v4l2_rect *crop_rect;

	if (VIMC_IS_SRC(sel->pad))
		return -EINVAL;

	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		sink_fmt = &vsca->sink_fmt;
		crop_rect = &vsca->crop_rect;
	} else {
		sink_fmt = v4l2_subdev_get_try_format(sd, cfg, 0);
		crop_rect = v4l2_subdev_get_try_crop(sd, cfg, 0);
	}

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *crop_rect;
		break;
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r = vimc_sca_get_crop_bound_sink(sink_fmt);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vimc_sca_set_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_selection *sel)
{
	struct vimc_sca_device *vsca = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *sink_fmt;
	struct v4l2_rect *crop_rect;

	if (VIMC_IS_SRC(sel->pad))
		return -EINVAL;

	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		/* Do not change the format while stream is on */
		if (vsca->src_frame)
			return -EBUSY;

		crop_rect = &vsca->crop_rect;
		sink_fmt = &vsca->sink_fmt;
	} else {
		crop_rect = v4l2_subdev_get_try_crop(sd, cfg, 0);
		sink_fmt = v4l2_subdev_get_try_format(sd, cfg, 0);
	}

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		/* Do the crop, but respect the current bounds */
		vimc_sca_adjust_sink_crop(&sel->r, sink_fmt);
		*crop_rect = sel->r;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_subdev_pad_ops vimc_sca_pad_ops = {
	.init_cfg		= vimc_sca_init_cfg,
	.enum_mbus_code		= vimc_sca_enum_mbus_code,
	.enum_frame_size	= vimc_sca_enum_frame_size,
	.get_fmt		= vimc_sca_get_fmt,
	.set_fmt		= vimc_sca_set_fmt,
	.get_selection		= vimc_sca_get_selection,
	.set_selection		= vimc_sca_set_selection,
};

static int vimc_sca_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct vimc_sca_device *vsca = v4l2_get_subdevdata(sd);

	if (enable) {
		const struct vimc_pix_map *vpix;
		unsigned int frame_size;

		if (vsca->src_frame)
			return 0;

		/* Save the bytes per pixel of the sink */
		vpix = vimc_pix_map_by_code(vsca->sink_fmt.code);
		vsca->bpp = vpix->bpp;

		/* Calculate the width in bytes of the src frame */
		vsca->src_line_size = vsca->crop_rect.width *
				      sca_mult * vsca->bpp;

		/* Calculate the frame size of the source pad */
		frame_size = vsca->src_line_size * vsca->crop_rect.height *
			     sca_mult;

		/* Allocate the frame buffer. Use vmalloc to be able to
		 * allocate a large amount of memory
		 */
		vsca->src_frame = vmalloc(frame_size);
		if (!vsca->src_frame)
			return -ENOMEM;

	} else {
		if (!vsca->src_frame)
			return 0;

		vfree(vsca->src_frame);
		vsca->src_frame = NULL;
	}

	return 0;
}

static const struct v4l2_subdev_video_ops vimc_sca_video_ops = {
	.s_stream = vimc_sca_s_stream,
};

static const struct v4l2_subdev_ops vimc_sca_ops = {
	.pad = &vimc_sca_pad_ops,
	.video = &vimc_sca_video_ops,
};

static void vimc_sca_fill_pix(u8 *const ptr,
			      const u8 *const pixel,
			      const unsigned int bpp)
{
	unsigned int i;

	/* copy the pixel to the pointer */
	for (i = 0; i < bpp; i++)
		ptr[i] = pixel[i];
}

static void vimc_sca_scale_pix(const struct vimc_sca_device *const vsca,
			       unsigned int lin, unsigned int col,
			       const u8 *const sink_frame)
{
	const struct v4l2_rect crop_rect = vsca->crop_rect;
	unsigned int i, j, index;
	const u8 *pixel;

	/* Point to the pixel value in position (lin, col) in the sink frame */
	index = VIMC_FRAME_INDEX(lin, col,
				 vsca->sink_fmt.width,
				 vsca->bpp);
	pixel = &sink_frame[index];

	dev_dbg(vsca->ved.dev,
		"sca: %s: --- scale_pix sink pos %dx%d, index %d ---\n",
		vsca->sd.name, lin, col, index);

	/* point to the place we are going to put the first pixel
	 * in the scaled src frame
	 */
	lin -= crop_rect.top;
	col -= crop_rect.left;
	index = VIMC_FRAME_INDEX(lin * sca_mult, col * sca_mult,
				 crop_rect.width * sca_mult, vsca->bpp);

	dev_dbg(vsca->ved.dev, "sca: %s: scale_pix src pos %dx%d, index %d\n",
		vsca->sd.name, lin * sca_mult, col * sca_mult, index);

	/* Repeat this pixel mult times */
	for (i = 0; i < sca_mult; i++) {
		/* Iterate through each beginning of a
		 * pixel repetition in a line
		 */
		for (j = 0; j < sca_mult * vsca->bpp; j += vsca->bpp) {
			dev_dbg(vsca->ved.dev,
				"sca: %s: sca: scale_pix src pos %d\n",
				vsca->sd.name, index + j);

			/* copy the pixel to the position index + j */
			vimc_sca_fill_pix(&vsca->src_frame[index + j],
					  pixel, vsca->bpp);
		}

		/* move the index to the next line */
		index += vsca->src_line_size;
	}
}

static void vimc_sca_fill_src_frame(const struct vimc_sca_device *const vsca,
				    const u8 *const sink_frame)
{
	const struct v4l2_rect r = vsca->crop_rect;
	unsigned int i, j;

	/* Scale each pixel from the original sink frame */
	/* TODO: implement scale down, only scale up is supported for now */
	for (i = r.top; i < r.top + r.height; i++)
		for (j = r.left; j < r.left + r.width; j++)
			vimc_sca_scale_pix(vsca, i, j, sink_frame);
}

static void *vimc_sca_process_frame(struct vimc_ent_device *ved,
				    const void *sink_frame)
{
	struct vimc_sca_device *vsca = container_of(ved, struct vimc_sca_device,
						    ved);

	/* If the stream in this node is not active, just return */
	if (!vsca->src_frame)
		return ERR_PTR(-EINVAL);

	vimc_sca_fill_src_frame(vsca, sink_frame);

	return vsca->src_frame;
};

void vimc_sca_release(struct vimc_ent_device *ved)
{
	struct vimc_sca_device *vsca =
		container_of(ved, struct vimc_sca_device, ved);

	media_entity_cleanup(vsca->ved.ent);
	kfree(vsca);
}

struct vimc_ent_device *vimc_sca_add(struct vimc_device *vimc,
				     const char *vcfg_name)
{
	struct v4l2_device *v4l2_dev = &vimc->v4l2_dev;
	struct vimc_sca_device *vsca;
	int ret;

	/* Allocate the vsca struct */
	vsca = kzalloc(sizeof(*vsca), GFP_KERNEL);
	if (!vsca)
		return NULL;

	/* Initialize ved and sd */
	vsca->pads[0].flags = MEDIA_PAD_FL_SINK;
	vsca->pads[1].flags = MEDIA_PAD_FL_SOURCE;

	ret = vimc_ent_sd_register(&vsca->ved, &vsca->sd, v4l2_dev,
				   vcfg_name,
				   MEDIA_ENT_F_PROC_VIDEO_SCALER, 2,
				   vsca->pads, &vimc_sca_ops);
	if (ret) {
		kfree(vsca);
		return NULL;
	}

	vsca->ved.process_frame = vimc_sca_process_frame;
	vsca->ved.dev = vimc->mdev.dev;

	/* Initialize the frame format */
	vsca->sink_fmt = sink_fmt_default;

	/* Initialize the crop selection */
	vsca->crop_rect = crop_rect_default;

	return &vsca->ved;
}

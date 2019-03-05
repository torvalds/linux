/*
 * vimc-scaler.c Virtual Media Controller Driver
 *
 * Copyright (C) 2015-2017 Helen Koike <helen.fornazier@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/component.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#include "vimc-common.h"

#define VIMC_SCA_DRV_NAME "vimc-scaler"

static unsigned int sca_mult = 3;
module_param(sca_mult, uint, 0000);
MODULE_PARM_DESC(sca_mult, " the image size multiplier");

#define IS_SINK(pad)	(!pad)
#define IS_SRC(pad)	(pad)
#define MAX_ZOOM	8

struct vimc_sca_device {
	struct vimc_ent_device ved;
	struct v4l2_subdev sd;
	struct device *dev;
	/* NOTE: the source fmt is the same as the sink
	 * with the width and hight multiplied by mult
	 */
	struct v4l2_mbus_framefmt sink_fmt;
	/* Values calculated when the stream starts */
	u8 *src_frame;
	unsigned int src_line_size;
	unsigned int bpp;
};

static const struct v4l2_mbus_framefmt sink_fmt_default = {
	.width = 640,
	.height = 480,
	.code = MEDIA_BUS_FMT_RGB888_1X24,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_DEFAULT,
};

static int vimc_sca_init_cfg(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg)
{
	struct v4l2_mbus_framefmt *mf;
	unsigned int i;

	mf = v4l2_subdev_get_try_format(sd, cfg, 0);
	*mf = sink_fmt_default;

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

	if (IS_SINK(fse->pad)) {
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

	/* Get the current sink format */
	format->format = (format->which == V4L2_SUBDEV_FORMAT_TRY) ?
			 *v4l2_subdev_get_try_format(sd, cfg, 0) :
			 vsca->sink_fmt;

	/* Scale the frame size for the source pad */
	if (IS_SRC(format->pad)) {
		format->format.width = vsca->sink_fmt.width * sca_mult;
		format->format.height = vsca->sink_fmt.height * sca_mult;
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

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		/* Do not change the format while stream is on */
		if (vsca->src_frame)
			return -EBUSY;

		sink_fmt = &vsca->sink_fmt;
	} else {
		sink_fmt = v4l2_subdev_get_try_format(sd, cfg, 0);
	}

	/*
	 * Do not change the format of the source pad,
	 * it is propagated from the sink
	 */
	if (IS_SRC(fmt->pad)) {
		fmt->format = *sink_fmt;
		fmt->format.width = sink_fmt->width * sca_mult;
		fmt->format.height = sink_fmt->height * sca_mult;
	} else {
		/* Set the new format in the sink pad */
		vimc_sca_adjust_sink_fmt(&fmt->format);

		dev_dbg(vsca->dev, "%s: sink format update: "
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
	}

	return 0;
}

static const struct v4l2_subdev_pad_ops vimc_sca_pad_ops = {
	.init_cfg		= vimc_sca_init_cfg,
	.enum_mbus_code		= vimc_sca_enum_mbus_code,
	.enum_frame_size	= vimc_sca_enum_frame_size,
	.get_fmt		= vimc_sca_get_fmt,
	.set_fmt		= vimc_sca_set_fmt,
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
		vsca->src_line_size = vsca->sink_fmt.width *
				      sca_mult * vsca->bpp;

		/* Calculate the frame size of the source pad */
		frame_size = vsca->src_line_size * vsca->sink_fmt.height *
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
			       const unsigned int lin, const unsigned int col,
			       const u8 *const sink_frame)
{
	unsigned int i, j, index;
	const u8 *pixel;

	/* Point to the pixel value in position (lin, col) in the sink frame */
	index = VIMC_FRAME_INDEX(lin, col,
				 vsca->sink_fmt.width,
				 vsca->bpp);
	pixel = &sink_frame[index];

	dev_dbg(vsca->dev,
		"sca: %s: --- scale_pix sink pos %dx%d, index %d ---\n",
		vsca->sd.name, lin, col, index);

	/* point to the place we are going to put the first pixel
	 * in the scaled src frame
	 */
	index = VIMC_FRAME_INDEX(lin * sca_mult, col * sca_mult,
				 vsca->sink_fmt.width * sca_mult, vsca->bpp);

	dev_dbg(vsca->dev, "sca: %s: scale_pix src pos %dx%d, index %d\n",
		vsca->sd.name, lin * sca_mult, col * sca_mult, index);

	/* Repeat this pixel mult times */
	for (i = 0; i < sca_mult; i++) {
		/* Iterate through each beginning of a
		 * pixel repetition in a line
		 */
		for (j = 0; j < sca_mult * vsca->bpp; j += vsca->bpp) {
			dev_dbg(vsca->dev,
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
	unsigned int i, j;

	/* Scale each pixel from the original sink frame */
	/* TODO: implement scale down, only scale up is supported for now */
	for (i = 0; i < vsca->sink_fmt.height; i++)
		for (j = 0; j < vsca->sink_fmt.width; j++)
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

static void vimc_sca_release(struct v4l2_subdev *sd)
{
	struct vimc_sca_device *vsca =
				container_of(sd, struct vimc_sca_device, sd);

	kfree(vsca);
}

static const struct v4l2_subdev_internal_ops vimc_sca_int_ops = {
	.release = vimc_sca_release,
};

static void vimc_sca_comp_unbind(struct device *comp, struct device *master,
				 void *master_data)
{
	struct vimc_ent_device *ved = dev_get_drvdata(comp);
	struct vimc_sca_device *vsca = container_of(ved, struct vimc_sca_device,
						    ved);

	vimc_ent_sd_unregister(ved, &vsca->sd);
}


static int vimc_sca_comp_bind(struct device *comp, struct device *master,
			      void *master_data)
{
	struct v4l2_device *v4l2_dev = master_data;
	struct vimc_platform_data *pdata = comp->platform_data;
	struct vimc_sca_device *vsca;
	int ret;

	/* Allocate the vsca struct */
	vsca = kzalloc(sizeof(*vsca), GFP_KERNEL);
	if (!vsca)
		return -ENOMEM;

	/* Initialize ved and sd */
	ret = vimc_ent_sd_register(&vsca->ved, &vsca->sd, v4l2_dev,
				   pdata->entity_name,
				   MEDIA_ENT_F_PROC_VIDEO_SCALER, 2,
				   (const unsigned long[2]) {MEDIA_PAD_FL_SINK,
				   MEDIA_PAD_FL_SOURCE},
				   &vimc_sca_int_ops, &vimc_sca_ops);
	if (ret) {
		kfree(vsca);
		return ret;
	}

	vsca->ved.process_frame = vimc_sca_process_frame;
	dev_set_drvdata(comp, &vsca->ved);
	vsca->dev = comp;

	/* Initialize the frame format */
	vsca->sink_fmt = sink_fmt_default;

	return 0;
}

static const struct component_ops vimc_sca_comp_ops = {
	.bind = vimc_sca_comp_bind,
	.unbind = vimc_sca_comp_unbind,
};

static int vimc_sca_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &vimc_sca_comp_ops);
}

static int vimc_sca_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vimc_sca_comp_ops);

	return 0;
}

static const struct platform_device_id vimc_sca_driver_ids[] = {
	{
		.name           = VIMC_SCA_DRV_NAME,
	},
	{ }
};

static struct platform_driver vimc_sca_pdrv = {
	.probe		= vimc_sca_probe,
	.remove		= vimc_sca_remove,
	.id_table	= vimc_sca_driver_ids,
	.driver		= {
		.name	= VIMC_SCA_DRV_NAME,
	},
};

module_platform_driver(vimc_sca_pdrv);

MODULE_DEVICE_TABLE(platform, vimc_sca_driver_ids);

MODULE_DESCRIPTION("Virtual Media Controller Driver (VIMC) Scaler");
MODULE_AUTHOR("Helen Mae Koike Fornazier <helen.fornazier@gmail.com>");
MODULE_LICENSE("GPL");

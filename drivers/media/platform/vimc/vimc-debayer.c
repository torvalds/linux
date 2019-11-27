// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * vimc-debayer.c Virtual Media Controller Driver
 *
 * Copyright (C) 2015-2017 Helen Koike <helen.fornazier@gmail.com>
 */

#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>

#include "vimc-common.h"

enum vimc_deb_rgb_colors {
	VIMC_DEB_RED = 0,
	VIMC_DEB_GREEN = 1,
	VIMC_DEB_BLUE = 2,
};

struct vimc_deb_pix_map {
	u32 code;
	enum vimc_deb_rgb_colors order[2][2];
};

struct vimc_deb_device {
	struct vimc_ent_device ved;
	struct v4l2_subdev sd;
	/* The active format */
	struct v4l2_mbus_framefmt sink_fmt;
	u32 src_code;
	void (*set_rgb_src)(struct vimc_deb_device *vdeb, unsigned int lin,
			    unsigned int col, unsigned int rgb[3]);
	/* Values calculated when the stream starts */
	u8 *src_frame;
	const struct vimc_deb_pix_map *sink_pix_map;
	unsigned int sink_bpp;
	unsigned int mean_win_size;
	struct v4l2_ctrl_handler hdl;
	struct media_pad pads[2];
};

static const struct v4l2_mbus_framefmt sink_fmt_default = {
	.width = 640,
	.height = 480,
	.code = MEDIA_BUS_FMT_SRGGB8_1X8,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_DEFAULT,
};

static const struct vimc_deb_pix_map vimc_deb_pix_map_list[] = {
	{
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.order = { { VIMC_DEB_BLUE, VIMC_DEB_GREEN },
			   { VIMC_DEB_GREEN, VIMC_DEB_RED } }
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.order = { { VIMC_DEB_GREEN, VIMC_DEB_BLUE },
			   { VIMC_DEB_RED, VIMC_DEB_GREEN } }
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.order = { { VIMC_DEB_GREEN, VIMC_DEB_RED },
			   { VIMC_DEB_BLUE, VIMC_DEB_GREEN } }
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.order = { { VIMC_DEB_RED, VIMC_DEB_GREEN },
			   { VIMC_DEB_GREEN, VIMC_DEB_BLUE } }
	},
	{
		.code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.order = { { VIMC_DEB_BLUE, VIMC_DEB_GREEN },
			   { VIMC_DEB_GREEN, VIMC_DEB_RED } }
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.order = { { VIMC_DEB_GREEN, VIMC_DEB_BLUE },
			   { VIMC_DEB_RED, VIMC_DEB_GREEN } }
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.order = { { VIMC_DEB_GREEN, VIMC_DEB_RED },
			   { VIMC_DEB_BLUE, VIMC_DEB_GREEN } }
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.order = { { VIMC_DEB_RED, VIMC_DEB_GREEN },
			   { VIMC_DEB_GREEN, VIMC_DEB_BLUE } }
	},
	{
		.code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.order = { { VIMC_DEB_BLUE, VIMC_DEB_GREEN },
			   { VIMC_DEB_GREEN, VIMC_DEB_RED } }
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.order = { { VIMC_DEB_GREEN, VIMC_DEB_BLUE },
			   { VIMC_DEB_RED, VIMC_DEB_GREEN } }
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.order = { { VIMC_DEB_GREEN, VIMC_DEB_RED },
			   { VIMC_DEB_BLUE, VIMC_DEB_GREEN } }
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.order = { { VIMC_DEB_RED, VIMC_DEB_GREEN },
			   { VIMC_DEB_GREEN, VIMC_DEB_BLUE } }
	},
};

static const struct vimc_deb_pix_map *vimc_deb_pix_map_by_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vimc_deb_pix_map_list); i++)
		if (vimc_deb_pix_map_list[i].code == code)
			return &vimc_deb_pix_map_list[i];

	return NULL;
}

static int vimc_deb_init_cfg(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg)
{
	struct vimc_deb_device *vdeb = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf;
	unsigned int i;

	mf = v4l2_subdev_get_try_format(sd, cfg, 0);
	*mf = sink_fmt_default;

	for (i = 1; i < sd->entity.num_pads; i++) {
		mf = v4l2_subdev_get_try_format(sd, cfg, i);
		*mf = sink_fmt_default;
		mf->code = vdeb->src_code;
	}

	return 0;
}

static int vimc_deb_enum_mbus_code(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	/* We only support one format for source pads */
	if (VIMC_IS_SRC(code->pad)) {
		struct vimc_deb_device *vdeb = v4l2_get_subdevdata(sd);

		if (code->index)
			return -EINVAL;

		code->code = vdeb->src_code;
	} else {
		if (code->index >= ARRAY_SIZE(vimc_deb_pix_map_list))
			return -EINVAL;

		code->code = vimc_deb_pix_map_list[code->index].code;
	}

	return 0;
}

static int vimc_deb_enum_frame_size(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	struct vimc_deb_device *vdeb = v4l2_get_subdevdata(sd);

	if (fse->index)
		return -EINVAL;

	if (VIMC_IS_SINK(fse->pad)) {
		const struct vimc_deb_pix_map *vpix =
			vimc_deb_pix_map_by_code(fse->code);

		if (!vpix)
			return -EINVAL;
	} else if (fse->code != vdeb->src_code) {
		return -EINVAL;
	}

	fse->min_width = VIMC_FRAME_MIN_WIDTH;
	fse->max_width = VIMC_FRAME_MAX_WIDTH;
	fse->min_height = VIMC_FRAME_MIN_HEIGHT;
	fse->max_height = VIMC_FRAME_MAX_HEIGHT;

	return 0;
}

static int vimc_deb_get_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *fmt)
{
	struct vimc_deb_device *vdeb = v4l2_get_subdevdata(sd);

	/* Get the current sink format */
	fmt->format = fmt->which == V4L2_SUBDEV_FORMAT_TRY ?
		      *v4l2_subdev_get_try_format(sd, cfg, 0) :
		      vdeb->sink_fmt;

	/* Set the right code for the source pad */
	if (VIMC_IS_SRC(fmt->pad))
		fmt->format.code = vdeb->src_code;

	return 0;
}

static void vimc_deb_adjust_sink_fmt(struct v4l2_mbus_framefmt *fmt)
{
	const struct vimc_deb_pix_map *vpix;

	/* Don't accept a code that is not on the debayer table */
	vpix = vimc_deb_pix_map_by_code(fmt->code);
	if (!vpix)
		fmt->code = sink_fmt_default.code;

	fmt->width = clamp_t(u32, fmt->width, VIMC_FRAME_MIN_WIDTH,
			     VIMC_FRAME_MAX_WIDTH) & ~1;
	fmt->height = clamp_t(u32, fmt->height, VIMC_FRAME_MIN_HEIGHT,
			      VIMC_FRAME_MAX_HEIGHT) & ~1;

	if (fmt->field == V4L2_FIELD_ANY)
		fmt->field = sink_fmt_default.field;

	vimc_colorimetry_clamp(fmt);
}

static int vimc_deb_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *fmt)
{
	struct vimc_deb_device *vdeb = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *sink_fmt;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		/* Do not change the format while stream is on */
		if (vdeb->src_frame)
			return -EBUSY;

		sink_fmt = &vdeb->sink_fmt;
	} else {
		sink_fmt = v4l2_subdev_get_try_format(sd, cfg, 0);
	}

	/*
	 * Do not change the format of the source pad,
	 * it is propagated from the sink
	 */
	if (VIMC_IS_SRC(fmt->pad)) {
		fmt->format = *sink_fmt;
		/* TODO: Add support for other formats */
		fmt->format.code = vdeb->src_code;
	} else {
		/* Set the new format in the sink pad */
		vimc_deb_adjust_sink_fmt(&fmt->format);

		dev_dbg(vdeb->ved.dev, "%s: sink format update: "
			"old:%dx%d (0x%x, %d, %d, %d, %d) "
			"new:%dx%d (0x%x, %d, %d, %d, %d)\n", vdeb->sd.name,
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

static const struct v4l2_subdev_pad_ops vimc_deb_pad_ops = {
	.init_cfg		= vimc_deb_init_cfg,
	.enum_mbus_code		= vimc_deb_enum_mbus_code,
	.enum_frame_size	= vimc_deb_enum_frame_size,
	.get_fmt		= vimc_deb_get_fmt,
	.set_fmt		= vimc_deb_set_fmt,
};

static void vimc_deb_set_rgb_mbus_fmt_rgb888_1x24(struct vimc_deb_device *vdeb,
						  unsigned int lin,
						  unsigned int col,
						  unsigned int rgb[3])
{
	unsigned int i, index;

	index = VIMC_FRAME_INDEX(lin, col, vdeb->sink_fmt.width, 3);
	for (i = 0; i < 3; i++)
		vdeb->src_frame[index + i] = rgb[i];
}

static int vimc_deb_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct vimc_deb_device *vdeb = v4l2_get_subdevdata(sd);

	if (enable) {
		const struct vimc_pix_map *vpix;
		unsigned int frame_size;

		if (vdeb->src_frame)
			return 0;

		/* Calculate the frame size of the source pad */
		vpix = vimc_pix_map_by_code(vdeb->src_code);
		frame_size = vdeb->sink_fmt.width * vdeb->sink_fmt.height *
				vpix->bpp;

		/* Save the bytes per pixel of the sink */
		vpix = vimc_pix_map_by_code(vdeb->sink_fmt.code);
		vdeb->sink_bpp = vpix->bpp;

		/* Get the corresponding pixel map from the table */
		vdeb->sink_pix_map =
			vimc_deb_pix_map_by_code(vdeb->sink_fmt.code);

		/*
		 * Allocate the frame buffer. Use vmalloc to be able to
		 * allocate a large amount of memory
		 */
		vdeb->src_frame = vmalloc(frame_size);
		if (!vdeb->src_frame)
			return -ENOMEM;

	} else {
		if (!vdeb->src_frame)
			return 0;

		vfree(vdeb->src_frame);
		vdeb->src_frame = NULL;
	}

	return 0;
}

static const struct v4l2_subdev_core_ops vimc_deb_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops vimc_deb_video_ops = {
	.s_stream = vimc_deb_s_stream,
};

static const struct v4l2_subdev_ops vimc_deb_ops = {
	.core = &vimc_deb_core_ops,
	.pad = &vimc_deb_pad_ops,
	.video = &vimc_deb_video_ops,
};

static unsigned int vimc_deb_get_val(const u8 *bytes,
				     const unsigned int n_bytes)
{
	unsigned int i;
	unsigned int acc = 0;

	for (i = 0; i < n_bytes; i++)
		acc = acc + (bytes[i] << (8 * i));

	return acc;
}

static void vimc_deb_calc_rgb_sink(struct vimc_deb_device *vdeb,
				   const u8 *frame,
				   const unsigned int lin,
				   const unsigned int col,
				   unsigned int rgb[3])
{
	unsigned int i, seek, wlin, wcol;
	unsigned int n_rgb[3] = {0, 0, 0};

	for (i = 0; i < 3; i++)
		rgb[i] = 0;

	/*
	 * Calculate how many we need to subtract to get to the pixel in
	 * the top left corner of the mean window (considering the current
	 * pixel as the center)
	 */
	seek = vdeb->mean_win_size / 2;

	/* Sum the values of the colors in the mean window */

	dev_dbg(vdeb->ved.dev,
		"deb: %s: --- Calc pixel %dx%d, window mean %d, seek %d ---\n",
		vdeb->sd.name, lin, col, vdeb->sink_fmt.height, seek);

	/*
	 * Iterate through all the lines in the mean window, start
	 * with zero if the pixel is outside the frame and don't pass
	 * the height when the pixel is in the bottom border of the
	 * frame
	 */
	for (wlin = seek > lin ? 0 : lin - seek;
	     wlin < lin + seek + 1 && wlin < vdeb->sink_fmt.height;
	     wlin++) {

		/*
		 * Iterate through all the columns in the mean window, start
		 * with zero if the pixel is outside the frame and don't pass
		 * the width when the pixel is in the right border of the
		 * frame
		 */
		for (wcol = seek > col ? 0 : col - seek;
		     wcol < col + seek + 1 && wcol < vdeb->sink_fmt.width;
		     wcol++) {
			enum vimc_deb_rgb_colors color;
			unsigned int index;

			/* Check which color this pixel is */
			color = vdeb->sink_pix_map->order[wlin % 2][wcol % 2];

			index = VIMC_FRAME_INDEX(wlin, wcol,
						 vdeb->sink_fmt.width,
						 vdeb->sink_bpp);

			dev_dbg(vdeb->ved.dev,
				"deb: %s: RGB CALC: frame index %d, win pos %dx%d, color %d\n",
				vdeb->sd.name, index, wlin, wcol, color);

			/* Get its value */
			rgb[color] = rgb[color] +
				vimc_deb_get_val(&frame[index], vdeb->sink_bpp);

			/* Save how many values we already added */
			n_rgb[color]++;

			dev_dbg(vdeb->ved.dev, "deb: %s: RGB CALC: val %d, n %d\n",
				vdeb->sd.name, rgb[color], n_rgb[color]);
		}
	}

	/* Calculate the mean */
	for (i = 0; i < 3; i++) {
		dev_dbg(vdeb->ved.dev,
			"deb: %s: PRE CALC: %dx%d Color %d, val %d, n %d\n",
			vdeb->sd.name, lin, col, i, rgb[i], n_rgb[i]);

		if (n_rgb[i])
			rgb[i] = rgb[i] / n_rgb[i];

		dev_dbg(vdeb->ved.dev,
			"deb: %s: FINAL CALC: %dx%d Color %d, val %d\n",
			vdeb->sd.name, lin, col, i, rgb[i]);
	}
}

static void *vimc_deb_process_frame(struct vimc_ent_device *ved,
				    const void *sink_frame)
{
	struct vimc_deb_device *vdeb = container_of(ved, struct vimc_deb_device,
						    ved);
	unsigned int rgb[3];
	unsigned int i, j;

	/* If the stream in this node is not active, just return */
	if (!vdeb->src_frame)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < vdeb->sink_fmt.height; i++)
		for (j = 0; j < vdeb->sink_fmt.width; j++) {
			vimc_deb_calc_rgb_sink(vdeb, sink_frame, i, j, rgb);
			vdeb->set_rgb_src(vdeb, i, j, rgb);
		}

	return vdeb->src_frame;
}

static int vimc_deb_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vimc_deb_device *vdeb =
		container_of(ctrl->handler, struct vimc_deb_device, hdl);

	switch (ctrl->id) {
	case VIMC_CID_MEAN_WIN_SIZE:
		vdeb->mean_win_size = ctrl->val;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct v4l2_ctrl_ops vimc_deb_ctrl_ops = {
	.s_ctrl = vimc_deb_s_ctrl,
};

static void vimc_deb_release(struct v4l2_subdev *sd)
{
	struct vimc_deb_device *vdeb =
				container_of(sd, struct vimc_deb_device, sd);

	v4l2_ctrl_handler_free(&vdeb->hdl);
	media_entity_cleanup(vdeb->ved.ent);
	kfree(vdeb);
}

static const struct v4l2_subdev_internal_ops vimc_deb_int_ops = {
	.release = vimc_deb_release,
};

void vimc_deb_rm(struct vimc_device *vimc, struct vimc_ent_device *ved)
{
	struct vimc_deb_device *vdeb;

	vdeb = container_of(ved, struct vimc_deb_device, ved);
	v4l2_device_unregister_subdev(&vdeb->sd);
}

static const struct v4l2_ctrl_config vimc_deb_ctrl_class = {
	.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_WRITE_ONLY,
	.id = VIMC_CID_VIMC_CLASS,
	.name = "VIMC Controls",
	.type = V4L2_CTRL_TYPE_CTRL_CLASS,
};

static const struct v4l2_ctrl_config vimc_deb_ctrl_mean_win_size = {
	.ops = &vimc_deb_ctrl_ops,
	.id = VIMC_CID_MEAN_WIN_SIZE,
	.name = "Debayer Mean Window Size",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 1,
	.max = 25,
	.step = 2,
	.def = 3,
};

struct vimc_ent_device *vimc_deb_add(struct vimc_device *vimc,
				     const char *vcfg_name)
{
	struct v4l2_device *v4l2_dev = &vimc->v4l2_dev;
	struct vimc_deb_device *vdeb;
	int ret;

	/* Allocate the vdeb struct */
	vdeb = kzalloc(sizeof(*vdeb), GFP_KERNEL);
	if (!vdeb)
		return NULL;

	/* Create controls: */
	v4l2_ctrl_handler_init(&vdeb->hdl, 2);
	v4l2_ctrl_new_custom(&vdeb->hdl, &vimc_deb_ctrl_class, NULL);
	v4l2_ctrl_new_custom(&vdeb->hdl, &vimc_deb_ctrl_mean_win_size, NULL);
	vdeb->sd.ctrl_handler = &vdeb->hdl;
	if (vdeb->hdl.error) {
		ret = vdeb->hdl.error;
		goto err_free_vdeb;
	}

	/* Initialize ved and sd */
	vdeb->pads[0].flags = MEDIA_PAD_FL_SINK;
	vdeb->pads[1].flags = MEDIA_PAD_FL_SOURCE;

	ret = vimc_ent_sd_register(&vdeb->ved, &vdeb->sd, v4l2_dev,
				   vcfg_name,
				   MEDIA_ENT_F_PROC_VIDEO_PIXEL_ENC_CONV, 2,
				   vdeb->pads,
				   &vimc_deb_int_ops, &vimc_deb_ops);
	if (ret)
		goto err_free_hdl;

	vdeb->ved.process_frame = vimc_deb_process_frame;
	vdeb->ved.dev = &vimc->pdev.dev;
	vdeb->mean_win_size = vimc_deb_ctrl_mean_win_size.def;

	/* Initialize the frame format */
	vdeb->sink_fmt = sink_fmt_default;
	/*
	 * TODO: Add support for more output formats, we only support
	 * RGB888 for now
	 * NOTE: the src format is always the same as the sink, except
	 * for the code
	 */
	vdeb->src_code = MEDIA_BUS_FMT_RGB888_1X24;
	vdeb->set_rgb_src = vimc_deb_set_rgb_mbus_fmt_rgb888_1x24;

	return &vdeb->ved;

err_free_hdl:
	v4l2_ctrl_handler_free(&vdeb->hdl);
err_free_vdeb:
	kfree(vdeb);

	return NULL;
}

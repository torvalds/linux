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

enum vimc_debayer_rgb_colors {
	VIMC_DEBAYER_RED = 0,
	VIMC_DEBAYER_GREEN = 1,
	VIMC_DEBAYER_BLUE = 2,
};

struct vimc_debayer_pix_map {
	u32 code;
	enum vimc_debayer_rgb_colors order[2][2];
};

struct vimc_debayer_device {
	struct vimc_ent_device ved;
	struct v4l2_subdev sd;
	/* The active format */
	struct v4l2_mbus_framefmt sink_fmt;
	u32 src_code;
	void (*set_rgb_src)(struct vimc_debayer_device *vdebayer,
			    unsigned int lin, unsigned int col,
			    unsigned int rgb[3]);
	/* Values calculated when the stream starts */
	u8 *src_frame;
	const struct vimc_debayer_pix_map *sink_pix_map;
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
	.colorspace = V4L2_COLORSPACE_SRGB,
};

static const u32 vimc_debayer_src_mbus_codes[] = {
	MEDIA_BUS_FMT_GBR888_1X24,
	MEDIA_BUS_FMT_BGR888_1X24,
	MEDIA_BUS_FMT_BGR888_3X8,
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_RGB888_2X12_BE,
	MEDIA_BUS_FMT_RGB888_2X12_LE,
	MEDIA_BUS_FMT_RGB888_3X8,
	MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
	MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA,
	MEDIA_BUS_FMT_RGB888_1X32_PADHI,
};

static const struct vimc_debayer_pix_map vimc_debayer_pix_map_list[] = {
	{
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.order = { { VIMC_DEBAYER_BLUE, VIMC_DEBAYER_GREEN },
			   { VIMC_DEBAYER_GREEN, VIMC_DEBAYER_RED } }
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.order = { { VIMC_DEBAYER_GREEN, VIMC_DEBAYER_BLUE },
			   { VIMC_DEBAYER_RED, VIMC_DEBAYER_GREEN } }
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.order = { { VIMC_DEBAYER_GREEN, VIMC_DEBAYER_RED },
			   { VIMC_DEBAYER_BLUE, VIMC_DEBAYER_GREEN } }
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.order = { { VIMC_DEBAYER_RED, VIMC_DEBAYER_GREEN },
			   { VIMC_DEBAYER_GREEN, VIMC_DEBAYER_BLUE } }
	},
	{
		.code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.order = { { VIMC_DEBAYER_BLUE, VIMC_DEBAYER_GREEN },
			   { VIMC_DEBAYER_GREEN, VIMC_DEBAYER_RED } }
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.order = { { VIMC_DEBAYER_GREEN, VIMC_DEBAYER_BLUE },
			   { VIMC_DEBAYER_RED, VIMC_DEBAYER_GREEN } }
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.order = { { VIMC_DEBAYER_GREEN, VIMC_DEBAYER_RED },
			   { VIMC_DEBAYER_BLUE, VIMC_DEBAYER_GREEN } }
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.order = { { VIMC_DEBAYER_RED, VIMC_DEBAYER_GREEN },
			   { VIMC_DEBAYER_GREEN, VIMC_DEBAYER_BLUE } }
	},
	{
		.code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.order = { { VIMC_DEBAYER_BLUE, VIMC_DEBAYER_GREEN },
			   { VIMC_DEBAYER_GREEN, VIMC_DEBAYER_RED } }
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.order = { { VIMC_DEBAYER_GREEN, VIMC_DEBAYER_BLUE },
			   { VIMC_DEBAYER_RED, VIMC_DEBAYER_GREEN } }
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.order = { { VIMC_DEBAYER_GREEN, VIMC_DEBAYER_RED },
			   { VIMC_DEBAYER_BLUE, VIMC_DEBAYER_GREEN } }
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.order = { { VIMC_DEBAYER_RED, VIMC_DEBAYER_GREEN },
			   { VIMC_DEBAYER_GREEN, VIMC_DEBAYER_BLUE } }
	},
};

static const struct vimc_debayer_pix_map *vimc_debayer_pix_map_by_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vimc_debayer_pix_map_list); i++)
		if (vimc_debayer_pix_map_list[i].code == code)
			return &vimc_debayer_pix_map_list[i];

	return NULL;
}

static bool vimc_debayer_src_code_is_valid(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vimc_debayer_src_mbus_codes); i++)
		if (vimc_debayer_src_mbus_codes[i] == code)
			return true;

	return false;
}

static int vimc_debayer_init_state(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state)
{
	struct vimc_debayer_device *vdebayer = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf;
	unsigned int i;

	mf = v4l2_subdev_state_get_format(sd_state, 0);
	*mf = sink_fmt_default;

	for (i = 1; i < sd->entity.num_pads; i++) {
		mf = v4l2_subdev_state_get_format(sd_state, i);
		*mf = sink_fmt_default;
		mf->code = vdebayer->src_code;
	}

	return 0;
}

static int vimc_debayer_enum_mbus_code(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *sd_state,
				       struct v4l2_subdev_mbus_code_enum *code)
{
	if (VIMC_IS_SRC(code->pad)) {
		if (code->index >= ARRAY_SIZE(vimc_debayer_src_mbus_codes))
			return -EINVAL;

		code->code = vimc_debayer_src_mbus_codes[code->index];
	} else {
		if (code->index >= ARRAY_SIZE(vimc_debayer_pix_map_list))
			return -EINVAL;

		code->code = vimc_debayer_pix_map_list[code->index].code;
	}

	return 0;
}

static int vimc_debayer_enum_frame_size(struct v4l2_subdev *sd,
					struct v4l2_subdev_state *sd_state,
					struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index)
		return -EINVAL;

	if (VIMC_IS_SINK(fse->pad)) {
		const struct vimc_debayer_pix_map *vpix =
			vimc_debayer_pix_map_by_code(fse->code);

		if (!vpix)
			return -EINVAL;
	} else if (!vimc_debayer_src_code_is_valid(fse->code)) {
		return -EINVAL;
	}

	fse->min_width = VIMC_FRAME_MIN_WIDTH;
	fse->max_width = VIMC_FRAME_MAX_WIDTH;
	fse->min_height = VIMC_FRAME_MIN_HEIGHT;
	fse->max_height = VIMC_FRAME_MAX_HEIGHT;

	return 0;
}

static int vimc_debayer_get_fmt(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_format *fmt)
{
	struct vimc_debayer_device *vdebayer = v4l2_get_subdevdata(sd);

	/* Get the current sink format */
	fmt->format = fmt->which == V4L2_SUBDEV_FORMAT_TRY ?
		      *v4l2_subdev_state_get_format(sd_state, 0) :
		      vdebayer->sink_fmt;

	/* Set the right code for the source pad */
	if (VIMC_IS_SRC(fmt->pad))
		fmt->format.code = vdebayer->src_code;

	return 0;
}

static void vimc_debayer_adjust_sink_fmt(struct v4l2_mbus_framefmt *fmt)
{
	const struct vimc_debayer_pix_map *vpix;

	/* Don't accept a code that is not on the debayer table */
	vpix = vimc_debayer_pix_map_by_code(fmt->code);
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

static int vimc_debayer_set_fmt(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_format *fmt)
{
	struct vimc_debayer_device *vdebayer = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *sink_fmt;
	u32 *src_code;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		/* Do not change the format while stream is on */
		if (vdebayer->src_frame)
			return -EBUSY;

		sink_fmt = &vdebayer->sink_fmt;
		src_code = &vdebayer->src_code;
	} else {
		sink_fmt = v4l2_subdev_state_get_format(sd_state, 0);
		src_code = &v4l2_subdev_state_get_format(sd_state, 1)->code;
	}

	/*
	 * Do not change the format of the source pad,
	 * it is propagated from the sink
	 */
	if (VIMC_IS_SRC(fmt->pad)) {
		u32 code = fmt->format.code;

		fmt->format = *sink_fmt;

		if (vimc_debayer_src_code_is_valid(code))
			*src_code = code;

		fmt->format.code = *src_code;
	} else {
		/* Set the new format in the sink pad */
		vimc_debayer_adjust_sink_fmt(&fmt->format);

		dev_dbg(vdebayer->ved.dev, "%s: sink format update: "
			"old:%dx%d (0x%x, %d, %d, %d, %d) "
			"new:%dx%d (0x%x, %d, %d, %d, %d)\n", vdebayer->sd.name,
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

static const struct v4l2_subdev_pad_ops vimc_debayer_pad_ops = {
	.enum_mbus_code		= vimc_debayer_enum_mbus_code,
	.enum_frame_size	= vimc_debayer_enum_frame_size,
	.get_fmt		= vimc_debayer_get_fmt,
	.set_fmt		= vimc_debayer_set_fmt,
};

static void vimc_debayer_process_rgb_frame(struct vimc_debayer_device *vdebayer,
					   unsigned int lin,
					   unsigned int col,
					   unsigned int rgb[3])
{
	const struct vimc_pix_map *vpix;
	unsigned int i, index;

	vpix = vimc_pix_map_by_code(vdebayer->src_code);
	index = VIMC_FRAME_INDEX(lin, col, vdebayer->sink_fmt.width, 3);
	for (i = 0; i < 3; i++) {
		switch (vpix->pixelformat) {
		case V4L2_PIX_FMT_RGB24:
			vdebayer->src_frame[index + i] = rgb[i];
			break;
		case V4L2_PIX_FMT_BGR24:
			vdebayer->src_frame[index + i] = rgb[2 - i];
			break;
		}
	}
}

static int vimc_debayer_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct vimc_debayer_device *vdebayer = v4l2_get_subdevdata(sd);

	if (enable) {
		const struct vimc_pix_map *vpix;
		unsigned int frame_size;

		if (vdebayer->src_frame)
			return 0;

		/* Calculate the frame size of the source pad */
		vpix = vimc_pix_map_by_code(vdebayer->src_code);
		frame_size = vdebayer->sink_fmt.width * vdebayer->sink_fmt.height *
				vpix->bpp;

		/* Save the bytes per pixel of the sink */
		vpix = vimc_pix_map_by_code(vdebayer->sink_fmt.code);
		vdebayer->sink_bpp = vpix->bpp;

		/* Get the corresponding pixel map from the table */
		vdebayer->sink_pix_map =
			vimc_debayer_pix_map_by_code(vdebayer->sink_fmt.code);

		/*
		 * Allocate the frame buffer. Use vmalloc to be able to
		 * allocate a large amount of memory
		 */
		vdebayer->src_frame = vmalloc(frame_size);
		if (!vdebayer->src_frame)
			return -ENOMEM;

	} else {
		if (!vdebayer->src_frame)
			return 0;

		vfree(vdebayer->src_frame);
		vdebayer->src_frame = NULL;
	}

	return 0;
}

static const struct v4l2_subdev_core_ops vimc_debayer_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops vimc_debayer_video_ops = {
	.s_stream = vimc_debayer_s_stream,
};

static const struct v4l2_subdev_ops vimc_debayer_ops = {
	.core = &vimc_debayer_core_ops,
	.pad = &vimc_debayer_pad_ops,
	.video = &vimc_debayer_video_ops,
};

static const struct v4l2_subdev_internal_ops vimc_debayer_internal_ops = {
	.init_state = vimc_debayer_init_state,
};

static unsigned int vimc_debayer_get_val(const u8 *bytes,
					 const unsigned int n_bytes)
{
	unsigned int i;
	unsigned int acc = 0;

	for (i = 0; i < n_bytes; i++)
		acc = acc + (bytes[i] << (8 * i));

	return acc;
}

static void vimc_debayer_calc_rgb_sink(struct vimc_debayer_device *vdebayer,
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
	seek = vdebayer->mean_win_size / 2;

	/* Sum the values of the colors in the mean window */

	dev_dbg(vdebayer->ved.dev,
		"deb: %s: --- Calc pixel %dx%d, window mean %d, seek %d ---\n",
		vdebayer->sd.name, lin, col, vdebayer->sink_fmt.height, seek);

	/*
	 * Iterate through all the lines in the mean window, start
	 * with zero if the pixel is outside the frame and don't pass
	 * the height when the pixel is in the bottom border of the
	 * frame
	 */
	for (wlin = seek > lin ? 0 : lin - seek;
	     wlin < lin + seek + 1 && wlin < vdebayer->sink_fmt.height;
	     wlin++) {

		/*
		 * Iterate through all the columns in the mean window, start
		 * with zero if the pixel is outside the frame and don't pass
		 * the width when the pixel is in the right border of the
		 * frame
		 */
		for (wcol = seek > col ? 0 : col - seek;
		     wcol < col + seek + 1 && wcol < vdebayer->sink_fmt.width;
		     wcol++) {
			enum vimc_debayer_rgb_colors color;
			unsigned int index;

			/* Check which color this pixel is */
			color = vdebayer->sink_pix_map->order[wlin % 2][wcol % 2];

			index = VIMC_FRAME_INDEX(wlin, wcol,
						 vdebayer->sink_fmt.width,
						 vdebayer->sink_bpp);

			dev_dbg(vdebayer->ved.dev,
				"deb: %s: RGB CALC: frame index %d, win pos %dx%d, color %d\n",
				vdebayer->sd.name, index, wlin, wcol, color);

			/* Get its value */
			rgb[color] = rgb[color] +
				vimc_debayer_get_val(&frame[index],
						     vdebayer->sink_bpp);

			/* Save how many values we already added */
			n_rgb[color]++;

			dev_dbg(vdebayer->ved.dev, "deb: %s: RGB CALC: val %d, n %d\n",
				vdebayer->sd.name, rgb[color], n_rgb[color]);
		}
	}

	/* Calculate the mean */
	for (i = 0; i < 3; i++) {
		dev_dbg(vdebayer->ved.dev,
			"deb: %s: PRE CALC: %dx%d Color %d, val %d, n %d\n",
			vdebayer->sd.name, lin, col, i, rgb[i], n_rgb[i]);

		if (n_rgb[i])
			rgb[i] = rgb[i] / n_rgb[i];

		dev_dbg(vdebayer->ved.dev,
			"deb: %s: FINAL CALC: %dx%d Color %d, val %d\n",
			vdebayer->sd.name, lin, col, i, rgb[i]);
	}
}

static void *vimc_debayer_process_frame(struct vimc_ent_device *ved,
					const void *sink_frame)
{
	struct vimc_debayer_device *vdebayer =
		container_of(ved, struct vimc_debayer_device, ved);

	unsigned int rgb[3];
	unsigned int i, j;

	/* If the stream in this node is not active, just return */
	if (!vdebayer->src_frame)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < vdebayer->sink_fmt.height; i++)
		for (j = 0; j < vdebayer->sink_fmt.width; j++) {
			vimc_debayer_calc_rgb_sink(vdebayer, sink_frame, i, j, rgb);
			vdebayer->set_rgb_src(vdebayer, i, j, rgb);
		}

	return vdebayer->src_frame;
}

static int vimc_debayer_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vimc_debayer_device *vdebayer =
		container_of(ctrl->handler, struct vimc_debayer_device, hdl);

	switch (ctrl->id) {
	case VIMC_CID_MEAN_WIN_SIZE:
		vdebayer->mean_win_size = ctrl->val;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct v4l2_ctrl_ops vimc_debayer_ctrl_ops = {
	.s_ctrl = vimc_debayer_s_ctrl,
};

static void vimc_debayer_release(struct vimc_ent_device *ved)
{
	struct vimc_debayer_device *vdebayer =
		container_of(ved, struct vimc_debayer_device, ved);

	v4l2_ctrl_handler_free(&vdebayer->hdl);
	media_entity_cleanup(vdebayer->ved.ent);
	kfree(vdebayer);
}

static const struct v4l2_ctrl_config vimc_debayer_ctrl_class = {
	.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_WRITE_ONLY,
	.id = VIMC_CID_VIMC_CLASS,
	.name = "VIMC Controls",
	.type = V4L2_CTRL_TYPE_CTRL_CLASS,
};

static const struct v4l2_ctrl_config vimc_debayer_ctrl_mean_win_size = {
	.ops = &vimc_debayer_ctrl_ops,
	.id = VIMC_CID_MEAN_WIN_SIZE,
	.name = "Debayer Mean Window Size",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 1,
	.max = 25,
	.step = 2,
	.def = 3,
};

static struct vimc_ent_device *vimc_debayer_add(struct vimc_device *vimc,
						const char *vcfg_name)
{
	struct v4l2_device *v4l2_dev = &vimc->v4l2_dev;
	struct vimc_debayer_device *vdebayer;
	int ret;

	/* Allocate the vdebayer struct */
	vdebayer = kzalloc(sizeof(*vdebayer), GFP_KERNEL);
	if (!vdebayer)
		return ERR_PTR(-ENOMEM);

	/* Create controls: */
	v4l2_ctrl_handler_init(&vdebayer->hdl, 2);
	v4l2_ctrl_new_custom(&vdebayer->hdl, &vimc_debayer_ctrl_class, NULL);
	v4l2_ctrl_new_custom(&vdebayer->hdl, &vimc_debayer_ctrl_mean_win_size, NULL);
	vdebayer->sd.ctrl_handler = &vdebayer->hdl;
	if (vdebayer->hdl.error) {
		ret = vdebayer->hdl.error;
		goto err_free_vdebayer;
	}

	/* Initialize ved and sd */
	vdebayer->pads[0].flags = MEDIA_PAD_FL_SINK;
	vdebayer->pads[1].flags = MEDIA_PAD_FL_SOURCE;

	ret = vimc_ent_sd_register(&vdebayer->ved, &vdebayer->sd, v4l2_dev,
				   vcfg_name,
				   MEDIA_ENT_F_PROC_VIDEO_PIXEL_ENC_CONV, 2,
				   vdebayer->pads, &vimc_debayer_ops);
	if (ret)
		goto err_free_hdl;

	vdebayer->sd.internal_ops = &vimc_debayer_internal_ops;

	vdebayer->ved.process_frame = vimc_debayer_process_frame;
	vdebayer->ved.dev = vimc->mdev.dev;
	vdebayer->mean_win_size = vimc_debayer_ctrl_mean_win_size.def;

	/* Initialize the frame format */
	vdebayer->sink_fmt = sink_fmt_default;
	/*
	 * TODO: Add support for more output formats, we only support
	 * RGB888 for now
	 * NOTE: the src format is always the same as the sink, except
	 * for the code
	 */
	vdebayer->src_code = MEDIA_BUS_FMT_RGB888_1X24;
	vdebayer->set_rgb_src = vimc_debayer_process_rgb_frame;

	return &vdebayer->ved;

err_free_hdl:
	v4l2_ctrl_handler_free(&vdebayer->hdl);
err_free_vdebayer:
	kfree(vdebayer);

	return ERR_PTR(ret);
}

struct vimc_ent_type vimc_debayer_type = {
	.add = vimc_debayer_add,
	.release = vimc_debayer_release
};

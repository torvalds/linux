// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * vimc-sensor.c Virtual Media Controller Driver
 *
 * Copyright (C) 2015-2017 Helen Koike <helen.fornazier@gmail.com>
 */

#include <linux/v4l2-mediabus.h>
#include <linux/vmalloc.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>
#include <media/tpg/v4l2-tpg.h>

#include "vimc-common.h"

enum vimc_sensor_osd_mode {
	VIMC_SENSOR_OSD_SHOW_ALL = 0,
	VIMC_SENSOR_OSD_SHOW_COUNTERS = 1,
	VIMC_SENSOR_OSD_SHOW_NONE = 2
};

struct vimc_sensor_device {
	struct vimc_ent_device ved;
	struct v4l2_subdev sd;
	struct tpg_data tpg;
	struct v4l2_ctrl_handler hdl;
	struct media_pad pad;

	u8 *frame;

	/*
	 * Virtual "hardware" configuration, filled when the stream starts or
	 * when controls are set.
	 */
	struct {
		struct v4l2_area size;
		enum vimc_sensor_osd_mode osd_value;
		u64 start_stream_ts;
	} hw;
};

static const struct v4l2_mbus_framefmt fmt_default = {
	.width = 640,
	.height = 480,
	.code = MEDIA_BUS_FMT_RGB888_1X24,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_SRGB,
};

static int vimc_sensor_init_state(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state)
{
	struct v4l2_mbus_framefmt *mf;

	mf = v4l2_subdev_state_get_format(sd_state, 0);
	*mf = fmt_default;

	return 0;
}

static int vimc_sensor_enum_mbus_code(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *sd_state,
				      struct v4l2_subdev_mbus_code_enum *code)
{
	u32 mbus_code = vimc_mbus_code_by_index(code->index);

	if (!mbus_code)
		return -EINVAL;

	code->code = mbus_code;

	return 0;
}

static int vimc_sensor_enum_frame_size(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *sd_state,
				       struct v4l2_subdev_frame_size_enum *fse)
{
	const struct vimc_pix_map *vpix;

	if (fse->index)
		return -EINVAL;

	/* Only accept code in the pix map table */
	vpix = vimc_pix_map_by_code(fse->code);
	if (!vpix)
		return -EINVAL;

	fse->min_width = VIMC_FRAME_MIN_WIDTH;
	fse->max_width = VIMC_FRAME_MAX_WIDTH;
	fse->min_height = VIMC_FRAME_MIN_HEIGHT;
	fse->max_height = VIMC_FRAME_MAX_HEIGHT;

	return 0;
}

static void vimc_sensor_tpg_s_format(struct vimc_sensor_device *vsensor,
				     const struct v4l2_mbus_framefmt *format)
{
	const struct vimc_pix_map *vpix = vimc_pix_map_by_code(format->code);

	tpg_reset_source(&vsensor->tpg, format->width, format->height,
			 format->field);
	tpg_s_bytesperline(&vsensor->tpg, 0, format->width * vpix->bpp);
	tpg_s_buf_height(&vsensor->tpg, format->height);
	tpg_s_fourcc(&vsensor->tpg, vpix->pixelformat);
	/* TODO: add support for V4L2_FIELD_ALTERNATE */
	tpg_s_field(&vsensor->tpg, format->field, false);
	tpg_s_colorspace(&vsensor->tpg, format->colorspace);
	tpg_s_ycbcr_enc(&vsensor->tpg, format->ycbcr_enc);
	tpg_s_quantization(&vsensor->tpg, format->quantization);
	tpg_s_xfer_func(&vsensor->tpg, format->xfer_func);
}

static void vimc_sensor_adjust_fmt(struct v4l2_mbus_framefmt *fmt)
{
	const struct vimc_pix_map *vpix;

	/* Only accept code in the pix map table */
	vpix = vimc_pix_map_by_code(fmt->code);
	if (!vpix)
		fmt->code = fmt_default.code;

	fmt->width = clamp_t(u32, fmt->width, VIMC_FRAME_MIN_WIDTH,
			     VIMC_FRAME_MAX_WIDTH) & ~1;
	fmt->height = clamp_t(u32, fmt->height, VIMC_FRAME_MIN_HEIGHT,
			      VIMC_FRAME_MAX_HEIGHT) & ~1;

	/* TODO: add support for V4L2_FIELD_ALTERNATE */
	if (fmt->field == V4L2_FIELD_ANY || fmt->field == V4L2_FIELD_ALTERNATE)
		fmt->field = fmt_default.field;

	vimc_colorimetry_clamp(fmt);
}

static int vimc_sensor_set_fmt(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_format *fmt)
{
	struct vimc_sensor_device *vsensor = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf;

	/* Do not change the format while stream is on */
	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE && vsensor->frame)
		return -EBUSY;

	mf = v4l2_subdev_state_get_format(sd_state, fmt->pad);

	/* Set the new format */
	vimc_sensor_adjust_fmt(&fmt->format);

	dev_dbg(vsensor->ved.dev, "%s: format update: "
		"old:%dx%d (0x%x, %d, %d, %d, %d) "
		"new:%dx%d (0x%x, %d, %d, %d, %d)\n", vsensor->sd.name,
		/* old */
		mf->width, mf->height, mf->code,
		mf->colorspace,	mf->quantization,
		mf->xfer_func, mf->ycbcr_enc,
		/* new */
		fmt->format.width, fmt->format.height, fmt->format.code,
		fmt->format.colorspace, fmt->format.quantization,
		fmt->format.xfer_func, fmt->format.ycbcr_enc);

	*mf = fmt->format;

	return 0;
}

static const struct v4l2_subdev_pad_ops vimc_sensor_pad_ops = {
	.enum_mbus_code		= vimc_sensor_enum_mbus_code,
	.enum_frame_size	= vimc_sensor_enum_frame_size,
	.get_fmt		= v4l2_subdev_get_fmt,
	.set_fmt		= vimc_sensor_set_fmt,
};

static void *vimc_sensor_process_frame(struct vimc_ent_device *ved,
				       const void *sink_frame)
{
	struct vimc_sensor_device *vsensor =
		container_of(ved, struct vimc_sensor_device, ved);

	const unsigned int line_height = 16;
	u8 *basep[TPG_MAX_PLANES][2];
	unsigned int line = 1;
	char str[100];

	tpg_fill_plane_buffer(&vsensor->tpg, 0, 0, vsensor->frame);
	tpg_calc_text_basep(&vsensor->tpg, basep, 0, vsensor->frame);
	switch (vsensor->hw.osd_value) {
	case VIMC_SENSOR_OSD_SHOW_ALL: {
		const char *order = tpg_g_color_order(&vsensor->tpg);

		tpg_gen_text(&vsensor->tpg, basep, line++ * line_height,
			     16, order);
		snprintf(str, sizeof(str),
			 "brightness %3d, contrast %3d, saturation %3d, hue %d ",
			 vsensor->tpg.brightness,
			 vsensor->tpg.contrast,
			 vsensor->tpg.saturation,
			 vsensor->tpg.hue);
		tpg_gen_text(&vsensor->tpg, basep, line++ * line_height, 16, str);
		snprintf(str, sizeof(str), "sensor size: %dx%d",
			 vsensor->hw.size.width, vsensor->hw.size.height);
		tpg_gen_text(&vsensor->tpg, basep, line++ * line_height, 16, str);
		fallthrough;
	}
	case VIMC_SENSOR_OSD_SHOW_COUNTERS: {
		unsigned int ms;

		ms = div_u64(ktime_get_ns() - vsensor->hw.start_stream_ts, 1000000);
		snprintf(str, sizeof(str), "%02d:%02d:%02d:%03d",
			 (ms / (60 * 60 * 1000)) % 24,
			 (ms / (60 * 1000)) % 60,
			 (ms / 1000) % 60,
			 ms % 1000);
		tpg_gen_text(&vsensor->tpg, basep, line++ * line_height, 16, str);
		break;
	}
	case VIMC_SENSOR_OSD_SHOW_NONE:
	default:
		break;
	}

	return vsensor->frame;
}

static int vimc_sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct vimc_sensor_device *vsensor =
				container_of(sd, struct vimc_sensor_device, sd);

	if (enable) {
		const struct v4l2_mbus_framefmt *format;
		struct v4l2_subdev_state *state;
		const struct vimc_pix_map *vpix;
		unsigned int frame_size;

		state = v4l2_subdev_lock_and_get_active_state(sd);
		format = v4l2_subdev_state_get_format(state, 0);

		/* Configure the test pattern generator. */
		vimc_sensor_tpg_s_format(vsensor, format);

		/* Calculate the frame size. */
		vpix = vimc_pix_map_by_code(format->code);
		frame_size = format->width * vpix->bpp * format->height;

		vsensor->hw.size.width = format->width;
		vsensor->hw.size.height = format->height;

		v4l2_subdev_unlock_state(state);

		/*
		 * Allocate the frame buffer. Use vmalloc to be able to
		 * allocate a large amount of memory
		 */
		vsensor->frame = vmalloc(frame_size);
		if (!vsensor->frame)
			return -ENOMEM;

		vsensor->hw.start_stream_ts = ktime_get_ns();
	} else {

		vfree(vsensor->frame);
		vsensor->frame = NULL;
	}

	return 0;
}

static const struct v4l2_subdev_core_ops vimc_sensor_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops vimc_sensor_video_ops = {
	.s_stream = vimc_sensor_s_stream,
};

static const struct v4l2_subdev_ops vimc_sensor_ops = {
	.core = &vimc_sensor_core_ops,
	.pad = &vimc_sensor_pad_ops,
	.video = &vimc_sensor_video_ops,
};

static const struct v4l2_subdev_internal_ops vimc_sensor_internal_ops = {
	.init_state = vimc_sensor_init_state,
};

static int vimc_sensor_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vimc_sensor_device *vsensor =
		container_of(ctrl->handler, struct vimc_sensor_device, hdl);

	switch (ctrl->id) {
	case VIMC_CID_TEST_PATTERN:
		tpg_s_pattern(&vsensor->tpg, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		tpg_s_hflip(&vsensor->tpg, ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		tpg_s_vflip(&vsensor->tpg, ctrl->val);
		break;
	case V4L2_CID_BRIGHTNESS:
		tpg_s_brightness(&vsensor->tpg, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		tpg_s_contrast(&vsensor->tpg, ctrl->val);
		break;
	case V4L2_CID_HUE:
		tpg_s_hue(&vsensor->tpg, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		tpg_s_saturation(&vsensor->tpg, ctrl->val);
		break;
	case VIMC_CID_OSD_TEXT_MODE:
		vsensor->hw.osd_value = ctrl->val;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct v4l2_ctrl_ops vimc_sensor_ctrl_ops = {
	.s_ctrl = vimc_sensor_s_ctrl,
};

static void vimc_sensor_release(struct vimc_ent_device *ved)
{
	struct vimc_sensor_device *vsensor =
		container_of(ved, struct vimc_sensor_device, ved);

	v4l2_ctrl_handler_free(&vsensor->hdl);
	tpg_free(&vsensor->tpg);
	v4l2_subdev_cleanup(&vsensor->sd);
	media_entity_cleanup(vsensor->ved.ent);
	kfree(vsensor);
}

/* Image Processing Controls */
static const struct v4l2_ctrl_config vimc_sensor_ctrl_class = {
	.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_WRITE_ONLY,
	.id = VIMC_CID_VIMC_CLASS,
	.name = "VIMC Controls",
	.type = V4L2_CTRL_TYPE_CTRL_CLASS,
};

static const struct v4l2_ctrl_config vimc_sensor_ctrl_test_pattern = {
	.ops = &vimc_sensor_ctrl_ops,
	.id = VIMC_CID_TEST_PATTERN,
	.name = "Test Pattern",
	.type = V4L2_CTRL_TYPE_MENU,
	.max = TPG_PAT_NOISE,
	.qmenu = tpg_pattern_strings,
};

static const char * const vimc_ctrl_osd_mode_strings[] = {
	"All",
	"Counters Only",
	"None",
	NULL,
};

static const struct v4l2_ctrl_config vimc_sensor_ctrl_osd_mode = {
	.ops = &vimc_sensor_ctrl_ops,
	.id = VIMC_CID_OSD_TEXT_MODE,
	.name = "Show Information",
	.type = V4L2_CTRL_TYPE_MENU,
	.max = ARRAY_SIZE(vimc_ctrl_osd_mode_strings) - 2,
	.qmenu = vimc_ctrl_osd_mode_strings,
};

static struct vimc_ent_device *vimc_sensor_add(struct vimc_device *vimc,
					       const char *vcfg_name)
{
	struct v4l2_device *v4l2_dev = &vimc->v4l2_dev;
	struct vimc_sensor_device *vsensor;
	int ret;

	/* Allocate the vsensor struct */
	vsensor = kzalloc(sizeof(*vsensor), GFP_KERNEL);
	if (!vsensor)
		return ERR_PTR(-ENOMEM);

	v4l2_ctrl_handler_init(&vsensor->hdl, 4);

	v4l2_ctrl_new_custom(&vsensor->hdl, &vimc_sensor_ctrl_class, NULL);
	v4l2_ctrl_new_custom(&vsensor->hdl, &vimc_sensor_ctrl_test_pattern, NULL);
	v4l2_ctrl_new_custom(&vsensor->hdl, &vimc_sensor_ctrl_osd_mode, NULL);
	v4l2_ctrl_new_std(&vsensor->hdl, &vimc_sensor_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&vsensor->hdl, &vimc_sensor_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&vsensor->hdl, &vimc_sensor_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&vsensor->hdl, &vimc_sensor_ctrl_ops,
			  V4L2_CID_CONTRAST, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&vsensor->hdl, &vimc_sensor_ctrl_ops,
			  V4L2_CID_HUE, -128, 127, 1, 0);
	v4l2_ctrl_new_std(&vsensor->hdl, &vimc_sensor_ctrl_ops,
			  V4L2_CID_SATURATION, 0, 255, 1, 128);
	vsensor->sd.ctrl_handler = &vsensor->hdl;
	if (vsensor->hdl.error) {
		ret = vsensor->hdl.error;
		goto err_free_vsensor;
	}

	/* Initialize the test pattern generator */
	tpg_init(&vsensor->tpg, fmt_default.width, fmt_default.height);
	ret = tpg_alloc(&vsensor->tpg, VIMC_FRAME_MAX_WIDTH);
	if (ret)
		goto err_free_hdl;

	/* Initialize ved and sd */
	vsensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = vimc_ent_sd_register(&vsensor->ved, &vsensor->sd, v4l2_dev,
				   vcfg_name,
				   MEDIA_ENT_F_CAM_SENSOR, 1, &vsensor->pad,
				   &vimc_sensor_internal_ops, &vimc_sensor_ops);
	if (ret)
		goto err_free_tpg;

	vsensor->ved.process_frame = vimc_sensor_process_frame;
	vsensor->ved.dev = vimc->mdev.dev;

	return &vsensor->ved;

err_free_tpg:
	tpg_free(&vsensor->tpg);
err_free_hdl:
	v4l2_ctrl_handler_free(&vsensor->hdl);
err_free_vsensor:
	kfree(vsensor);

	return ERR_PTR(ret);
}

const struct vimc_ent_type vimc_sensor_type = {
	.add = vimc_sensor_add,
	.release = vimc_sensor_release
};

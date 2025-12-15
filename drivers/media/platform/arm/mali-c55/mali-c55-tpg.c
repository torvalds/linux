// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Mali-C55 ISP Driver - Test pattern generator
 *
 * Copyright (C) 2025 Ideas on Board Oy
 */

#include <linux/minmax.h>
#include <linux/pm_runtime.h>
#include <linux/string.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>

#include "mali-c55-common.h"
#include "mali-c55-registers.h"

#define MALI_C55_TPG_SRC_PAD			0
#define MALI_C55_TPG_FIXED_HBLANK		0x20
#define MALI_C55_TPG_DEFAULT_MIN_VBLANK		66
#define MALI_C55_TPG_DEFAULT_DEF_VBLANK		626
#define MALI_C55_TPG_MAX_VBLANK			0xffff
#define MALI_C55_TPG_PIXEL_RATE			100000000

static const char * const mali_c55_tpg_test_pattern_menu[] = {
	"Flat field",
	"Horizontal gradient",
	"Vertical gradient",
	"Vertical bars",
	"Arbitrary rectangle",
	"White frame on black field"
};

static const u32 mali_c55_tpg_mbus_codes[] = {
	MEDIA_BUS_FMT_SRGGB20_1X20,
	MEDIA_BUS_FMT_RGB202020_1X60,
};

static void mali_c55_tpg_update_vblank(struct mali_c55_tpg *tpg,
				       struct v4l2_mbus_framefmt *format)
{
	unsigned int def_vblank;
	unsigned int min_vblank;
	unsigned int hts;
	int tgt_fps;

	hts = format->width + MALI_C55_TPG_FIXED_HBLANK;

	/*
	 * The ISP has minimum vertical blanking requirements that must be
	 * adhered to by the TPG. The minimum is a function of the Iridix blocks
	 * clocking requirements and the width of the image and horizontal
	 * blanking, but if we assume the worst case iVariance and sVariance
	 * values then it boils down to the below (plus one to the numerator to
	 * ensure the answer is rounded up).
	 */
	min_vblank = 15 + (120501 / hts);

	/*
	 * We need to set a sensible default vblank for whatever format height
	 * we happen to be given from set_fmt(). This function just targets
	 * an even multiple of 15fps. If we can't get 15fps, let's target 5fps.
	 * If we can't get 5fps we'll take whatever the minimum vblank gives us.
	 */
	tgt_fps = MALI_C55_TPG_PIXEL_RATE / hts / (format->height + min_vblank);

	if (tgt_fps < 5)
		def_vblank = min_vblank;
	else
		def_vblank = (MALI_C55_TPG_PIXEL_RATE / hts
			   / max(rounddown(tgt_fps, 15), 5)) - format->height;

	def_vblank = ALIGN_DOWN(def_vblank, 2);

	__v4l2_ctrl_modify_range(tpg->ctrls.vblank, min_vblank,
				 MALI_C55_TPG_MAX_VBLANK, 1, def_vblank);
	__v4l2_ctrl_s_ctrl(tpg->ctrls.vblank, def_vblank);
}

static int mali_c55_tpg_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mali_c55_tpg *tpg = container_of(ctrl->handler,
						struct mali_c55_tpg,
						ctrls.handler);
	struct mali_c55 *mali_c55 = container_of(tpg, struct mali_c55, tpg);
	int ret = 0;

	if (!pm_runtime_get_if_in_use(mali_c55->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		mali_c55_ctx_write(mali_c55,
				   MALI_C55_REG_TEST_GEN_CH0_PATTERN_TYPE,
				   ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		mali_c55_update_bits(mali_c55, MALI_C55_REG_BLANKING,
				     MALI_C55_REG_VBLANK_MASK,
				     MALI_C55_VBLANK(ctrl->val));
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put_autosuspend(mali_c55->dev);

	return ret;
}

static const struct v4l2_ctrl_ops mali_c55_tpg_ctrl_ops = {
	.s_ctrl = &mali_c55_tpg_s_ctrl,
};

static void mali_c55_tpg_configure(struct mali_c55_tpg *tpg,
				   struct v4l2_subdev_state *state)
{
	struct v4l2_mbus_framefmt *fmt;
	u32 test_pattern_format;

	/*
	 * hblank needs setting, but is a read-only control and thus won't be
	 * called during __v4l2_ctrl_handler_setup(). Do it here instead.
	 */
	mali_c55_update_bits(tpg->mali_c55, MALI_C55_REG_BLANKING,
			     MALI_C55_REG_HBLANK_MASK,
			     MALI_C55_TPG_FIXED_HBLANK);
	mali_c55_update_bits(tpg->mali_c55, MALI_C55_REG_GEN_VIDEO,
			     MALI_C55_REG_GEN_VIDEO_MULTI_MASK,
			     MALI_C55_REG_GEN_VIDEO_MULTI_MASK);

	fmt = v4l2_subdev_state_get_format(state, MALI_C55_TPG_SRC_PAD);

	test_pattern_format = fmt->code == MEDIA_BUS_FMT_RGB202020_1X60 ?
			      0x01 : 0x0;

	mali_c55_ctx_update_bits(tpg->mali_c55, MALI_C55_REG_TPG_CH0,
				 MALI_C55_TEST_PATTERN_RGB_MASK,
				 MALI_C55_TEST_PATTERN_RGB(test_pattern_format));
}

static int mali_c55_tpg_enum_mbus_code(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(mali_c55_tpg_mbus_codes))
		return -EINVAL;

	code->code = mali_c55_tpg_mbus_codes[code->index];

	return 0;
}

static int mali_c55_tpg_enum_frame_size(struct v4l2_subdev *sd,
					struct v4l2_subdev_state *state,
					struct v4l2_subdev_frame_size_enum *fse)
{
	unsigned int i;

	if (fse->index > 0)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(mali_c55_tpg_mbus_codes); i++) {
		if (fse->code == mali_c55_tpg_mbus_codes[i])
			break;
	}

	if (i == ARRAY_SIZE(mali_c55_tpg_mbus_codes))
		return -EINVAL;

	fse->min_width = MALI_C55_MIN_WIDTH;
	fse->max_width = MALI_C55_MAX_WIDTH;
	fse->min_height = MALI_C55_MIN_HEIGHT;
	fse->max_height = MALI_C55_MAX_HEIGHT;

	return 0;
}

static int mali_c55_tpg_set_fmt(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_format *format)
{
	struct mali_c55_tpg *tpg = container_of(sd, struct mali_c55_tpg, sd);
	struct v4l2_mbus_framefmt *fmt;
	unsigned int i;

	fmt = v4l2_subdev_state_get_format(state, MALI_C55_TPG_SRC_PAD);
	fmt->code = format->format.code;

	for (i = 0; i < ARRAY_SIZE(mali_c55_tpg_mbus_codes); i++) {
		if (fmt->code == mali_c55_tpg_mbus_codes[i])
			break;
	}

	if (i == ARRAY_SIZE(mali_c55_tpg_mbus_codes))
		fmt->code = MEDIA_BUS_FMT_SRGGB20_1X20;

	/*
	 * The TPG says that the test frame timing generation logic expects a
	 * minimum framesize of 4x4 pixels, but given the rest of the ISP can't
	 * handle anything smaller than 128x128 it seems pointless to allow a
	 * smaller frame.
	 */
	fmt->width = clamp(format->format.width, MALI_C55_MIN_WIDTH,
			   MALI_C55_MAX_WIDTH);
	fmt->height = clamp(format->format.height, MALI_C55_MIN_HEIGHT,
			    MALI_C55_MAX_HEIGHT);

	format->format = *fmt;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	mali_c55_tpg_update_vblank(tpg, fmt);

	return 0;
}

static int mali_c55_tpg_enable_streams(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state, u32 pad,
				       u64 streams_mask)
{
	struct mali_c55_tpg *tpg = container_of(sd, struct mali_c55_tpg, sd);
	struct mali_c55 *mali_c55 = container_of(tpg, struct mali_c55, tpg);

	/*
	 * We only have a source pad and a single stream, and v4l2-core already
	 * validated both so we don't need to do that. One might reasonably
	 * expect the framesize to be set here given it's configurable in
	 * .set_fmt(), but it's done in the ISP subdevice's .enable_streams()
	 * instead, as the same register is also used to indicate the size of
	 * the data coming from the sensor.
	 */
	mali_c55_tpg_configure(tpg, state);
	__v4l2_ctrl_handler_setup(sd->ctrl_handler);

	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_TPG_CH0,
				 MALI_C55_TEST_PATTERN_ON_OFF,
				 MALI_C55_TEST_PATTERN_ON_OFF);
	mali_c55_update_bits(mali_c55, MALI_C55_REG_GEN_VIDEO,
			     MALI_C55_REG_GEN_VIDEO_ON_MASK,
			     MALI_C55_REG_GEN_VIDEO_ON_MASK);

	return 0;
}

static int mali_c55_tpg_disable_streams(struct v4l2_subdev *sd,
					struct v4l2_subdev_state *state, u32 pad,
					u64 streams_mask)
{
	struct mali_c55_tpg *tpg = container_of(sd, struct mali_c55_tpg, sd);
	struct mali_c55 *mali_c55 = container_of(tpg, struct mali_c55, tpg);

	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_TPG_CH0,
				 MALI_C55_TEST_PATTERN_ON_OFF, 0x00);
	mali_c55_update_bits(mali_c55, MALI_C55_REG_GEN_VIDEO,
			     MALI_C55_REG_GEN_VIDEO_ON_MASK, 0x00);

	return 0;
}

static const struct v4l2_subdev_pad_ops mali_c55_tpg_pad_ops = {
	.enum_mbus_code		= mali_c55_tpg_enum_mbus_code,
	.enum_frame_size	= mali_c55_tpg_enum_frame_size,
	.get_fmt		= v4l2_subdev_get_fmt,
	.set_fmt		= mali_c55_tpg_set_fmt,
	.enable_streams		= mali_c55_tpg_enable_streams,
	.disable_streams	= mali_c55_tpg_disable_streams,
};

static const struct v4l2_subdev_core_ops mali_c55_isp_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_ops mali_c55_tpg_ops = {
	.core	= &mali_c55_isp_core_ops,
	.pad	= &mali_c55_tpg_pad_ops,
};

static int mali_c55_tpg_init_state(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state)
{
	struct v4l2_mbus_framefmt *fmt =
		v4l2_subdev_state_get_format(state, MALI_C55_TPG_SRC_PAD);

	fmt->width = MALI_C55_DEFAULT_WIDTH;
	fmt->height = MALI_C55_DEFAULT_HEIGHT;
	fmt->field = V4L2_FIELD_NONE;
	fmt->code = MEDIA_BUS_FMT_SRGGB20_1X20;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(false,
							  fmt->colorspace,
							  fmt->ycbcr_enc);

	return 0;
}

static const struct v4l2_subdev_internal_ops mali_c55_tpg_internal_ops = {
	.init_state = mali_c55_tpg_init_state,
};

static int mali_c55_tpg_init_controls(struct mali_c55 *mali_c55)
{
	struct mali_c55_tpg_ctrls *ctrls = &mali_c55->tpg.ctrls;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *hblank;
	int ret;

	ret = v4l2_ctrl_handler_init(&ctrls->handler, 4);
	if (ret)
		return ret;

	v4l2_ctrl_new_std_menu_items(&ctrls->handler, &mali_c55_tpg_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(mali_c55_tpg_test_pattern_menu) - 1,
				     0, 3, mali_c55_tpg_test_pattern_menu);

	/*
	 * We fix hblank at the minimum allowed value and control framerate
	 * solely through the vblank control.
	 */
	hblank = v4l2_ctrl_new_std(&ctrls->handler, &mali_c55_tpg_ctrl_ops,
				   V4L2_CID_HBLANK, MALI_C55_TPG_FIXED_HBLANK,
				   MALI_C55_TPG_FIXED_HBLANK, 1,
				   MALI_C55_TPG_FIXED_HBLANK);
	if (hblank)
		hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ctrls->vblank = v4l2_ctrl_new_std(&ctrls->handler,
					  &mali_c55_tpg_ctrl_ops,
					  V4L2_CID_VBLANK,
					  MALI_C55_TPG_DEFAULT_MIN_VBLANK,
					  MALI_C55_TPG_MAX_VBLANK, 1,
					  MALI_C55_TPG_DEFAULT_DEF_VBLANK);

	pixel_rate = v4l2_ctrl_new_std(&ctrls->handler, &mali_c55_tpg_ctrl_ops,
				       V4L2_CID_PIXEL_RATE,
				       MALI_C55_TPG_PIXEL_RATE,
				       MALI_C55_TPG_PIXEL_RATE, 1,
				       MALI_C55_TPG_PIXEL_RATE);
	if (pixel_rate)
		pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	if (ctrls->handler.error) {
		dev_err(mali_c55->dev, "Error during v4l2 controls init\n");
		v4l2_ctrl_handler_free(&ctrls->handler);
		return ctrls->handler.error;
	}

	mali_c55->tpg.sd.ctrl_handler = &ctrls->handler;
	mali_c55->tpg.sd.state_lock = ctrls->handler.lock;

	return 0;
}

int mali_c55_register_tpg(struct mali_c55 *mali_c55)
{
	struct mali_c55_tpg *tpg = &mali_c55->tpg;
	struct v4l2_subdev *sd = &tpg->sd;
	struct media_pad *pad = &tpg->pad;
	int ret;

	v4l2_subdev_init(sd, &mali_c55_tpg_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sd->internal_ops = &mali_c55_tpg_internal_ops;
	strscpy(sd->name, MALI_C55_DRIVER_NAME " tpg", sizeof(sd->name));

	pad->flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, 1, pad);
	if (ret) {
		dev_err(mali_c55->dev,
			"Failed to initialize media entity pads\n");
		return ret;
	}

	ret = mali_c55_tpg_init_controls(mali_c55);
	if (ret) {
		dev_err(mali_c55->dev,
			"Error initialising controls\n");
		goto err_cleanup_media_entity;
	}

	ret = v4l2_subdev_init_finalize(sd);
	if (ret)
		goto err_free_ctrl_handler;

	ret = v4l2_device_register_subdev(&mali_c55->v4l2_dev, sd);
	if (ret) {
		dev_err(mali_c55->dev, "Failed to register tpg subdev\n");
		goto err_cleanup_subdev;
	}

	/*
	 * By default the colour settings lead to a very dim image that is
	 * nearly indistinguishable from black on some monitor settings. Ramp
	 * them up a bit so the image is brighter.
	 */
	mali_c55_ctx_write(mali_c55, MALI_C55_REG_TPG_R_BACKGROUND,
			   MALI_C55_TPG_BACKGROUND_MAX);
	mali_c55_ctx_write(mali_c55, MALI_C55_REG_TPG_G_BACKGROUND,
			   MALI_C55_TPG_BACKGROUND_MAX);
	mali_c55_ctx_write(mali_c55, MALI_C55_REG_TPG_B_BACKGROUND,
			   MALI_C55_TPG_BACKGROUND_MAX);

	tpg->mali_c55 = mali_c55;

	return 0;

err_cleanup_subdev:
	v4l2_subdev_cleanup(sd);
err_free_ctrl_handler:
	v4l2_ctrl_handler_free(&tpg->ctrls.handler);
err_cleanup_media_entity:
	media_entity_cleanup(&sd->entity);

	return ret;
}

void mali_c55_unregister_tpg(struct mali_c55 *mali_c55)
{
	struct mali_c55_tpg *tpg = &mali_c55->tpg;

	if (!tpg->mali_c55)
		return;

	v4l2_device_unregister_subdev(&tpg->sd);
	v4l2_ctrl_handler_free(&tpg->ctrls.handler);
	v4l2_subdev_cleanup(&tpg->sd);
	media_entity_cleanup(&tpg->sd.entity);
}

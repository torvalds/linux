// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung LSI S5C73M3 8M pixel camera driver
 *
 * Copyright (C) 2012, Samsung Electronics, Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 * Andrzej Hajda <a.hajda@samsung.com>
 */

#include <linux/sizes.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/media.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/videodev2.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-mediabus.h>
#include <media/i2c/s5c73m3.h>

#include "s5c73m3.h"

static int s5c73m3_get_af_status(struct s5c73m3 *state, struct v4l2_ctrl *ctrl)
{
	u16 reg = REG_AF_STATUS_UNFOCUSED;

	int ret = s5c73m3_read(state, REG_AF_STATUS, &reg);

	switch (reg) {
	case REG_CAF_STATUS_FIND_SEARCH_DIR:
	case REG_AF_STATUS_FOCUSING:
	case REG_CAF_STATUS_FOCUSING:
		ctrl->val = V4L2_AUTO_FOCUS_STATUS_BUSY;
		break;
	case REG_CAF_STATUS_FOCUSED:
	case REG_AF_STATUS_FOCUSED:
		ctrl->val = V4L2_AUTO_FOCUS_STATUS_REACHED;
		break;
	default:
		v4l2_info(&state->sensor_sd, "Unknown AF status %#x\n", reg);
		fallthrough;
	case REG_CAF_STATUS_UNFOCUSED:
	case REG_AF_STATUS_UNFOCUSED:
	case REG_AF_STATUS_INVALID:
		ctrl->val = V4L2_AUTO_FOCUS_STATUS_FAILED;
		break;
	}

	return ret;
}

static int s5c73m3_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sensor_sd(ctrl);
	struct s5c73m3 *state = sensor_sd_to_s5c73m3(sd);
	int ret;

	if (state->power == 0)
		return -EBUSY;

	switch (ctrl->id) {
	case V4L2_CID_FOCUS_AUTO:
		ret = s5c73m3_get_af_status(state, state->ctrls.af_status);
		if (ret)
			return ret;
		break;
	}

	return 0;
}

static int s5c73m3_set_colorfx(struct s5c73m3 *state, int val)
{
	static const unsigned short colorfx[][2] = {
		{ V4L2_COLORFX_NONE,	 COMM_IMAGE_EFFECT_NONE },
		{ V4L2_COLORFX_BW,	 COMM_IMAGE_EFFECT_MONO },
		{ V4L2_COLORFX_SEPIA,	 COMM_IMAGE_EFFECT_SEPIA },
		{ V4L2_COLORFX_NEGATIVE, COMM_IMAGE_EFFECT_NEGATIVE },
		{ V4L2_COLORFX_AQUA,	 COMM_IMAGE_EFFECT_AQUA },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(colorfx); i++) {
		if (colorfx[i][0] != val)
			continue;

		v4l2_dbg(1, s5c73m3_dbg, &state->sensor_sd,
			 "Setting %s color effect\n",
			 v4l2_ctrl_get_menu(state->ctrls.colorfx->id)[i]);

		return s5c73m3_isp_command(state, COMM_IMAGE_EFFECT,
					 colorfx[i][1]);
	}
	return -EINVAL;
}

/* Set exposure metering/exposure bias */
static int s5c73m3_set_exposure(struct s5c73m3 *state, int auto_exp)
{
	struct v4l2_subdev *sd = &state->sensor_sd;
	struct s5c73m3_ctrls *ctrls = &state->ctrls;
	int ret = 0;

	if (ctrls->exposure_metering->is_new) {
		u16 metering;

		switch (ctrls->exposure_metering->val) {
		case V4L2_EXPOSURE_METERING_CENTER_WEIGHTED:
			metering = COMM_METERING_CENTER;
			break;
		case V4L2_EXPOSURE_METERING_SPOT:
			metering = COMM_METERING_SPOT;
			break;
		default:
			metering = COMM_METERING_AVERAGE;
			break;
		}

		ret = s5c73m3_isp_command(state, COMM_METERING, metering);
	}

	if (!ret && ctrls->exposure_bias->is_new) {
		u16 exp_bias = ctrls->exposure_bias->val;
		ret = s5c73m3_isp_command(state, COMM_EV, exp_bias);
	}

	v4l2_dbg(1, s5c73m3_dbg, sd,
		 "%s: exposure bias: %#x, metering: %#x (%d)\n",  __func__,
		 ctrls->exposure_bias->val, ctrls->exposure_metering->val, ret);

	return ret;
}

static int s5c73m3_set_white_balance(struct s5c73m3 *state, int val)
{
	static const unsigned short wb[][2] = {
		{ V4L2_WHITE_BALANCE_INCANDESCENT,  COMM_AWB_MODE_INCANDESCENT},
		{ V4L2_WHITE_BALANCE_FLUORESCENT,   COMM_AWB_MODE_FLUORESCENT1},
		{ V4L2_WHITE_BALANCE_FLUORESCENT_H, COMM_AWB_MODE_FLUORESCENT2},
		{ V4L2_WHITE_BALANCE_CLOUDY,        COMM_AWB_MODE_CLOUDY},
		{ V4L2_WHITE_BALANCE_DAYLIGHT,      COMM_AWB_MODE_DAYLIGHT},
		{ V4L2_WHITE_BALANCE_AUTO,          COMM_AWB_MODE_AUTO},
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(wb); i++) {
		if (wb[i][0] != val)
			continue;

		v4l2_dbg(1, s5c73m3_dbg, &state->sensor_sd,
			 "Setting white balance to: %s\n",
			 v4l2_ctrl_get_menu(state->ctrls.auto_wb->id)[i]);

		return s5c73m3_isp_command(state, COMM_AWB_MODE, wb[i][1]);
	}

	return -EINVAL;
}

static int s5c73m3_af_run(struct s5c73m3 *state, bool on)
{
	struct s5c73m3_ctrls *c = &state->ctrls;

	if (!on)
		return s5c73m3_isp_command(state, COMM_AF_CON,
							COMM_AF_CON_STOP);

	if (c->focus_auto->val)
		return s5c73m3_isp_command(state, COMM_AF_MODE,
					   COMM_AF_MODE_PREVIEW_CAF_START);

	return s5c73m3_isp_command(state, COMM_AF_CON, COMM_AF_CON_START);
}

static int s5c73m3_3a_lock(struct s5c73m3 *state, struct v4l2_ctrl *ctrl)
{
	bool awb_lock = ctrl->val & V4L2_LOCK_WHITE_BALANCE;
	bool ae_lock = ctrl->val & V4L2_LOCK_EXPOSURE;
	bool af_lock = ctrl->val & V4L2_LOCK_FOCUS;
	int ret = 0;

	if ((ctrl->val ^ ctrl->cur.val) & V4L2_LOCK_EXPOSURE) {
		ret = s5c73m3_isp_command(state, COMM_AE_CON,
				ae_lock ? COMM_AE_STOP : COMM_AE_START);
		if (ret)
			return ret;
	}

	if (((ctrl->val ^ ctrl->cur.val) & V4L2_LOCK_WHITE_BALANCE)
	    && state->ctrls.auto_wb->val) {
		ret = s5c73m3_isp_command(state, COMM_AWB_CON,
			awb_lock ? COMM_AWB_STOP : COMM_AWB_START);
		if (ret)
			return ret;
	}

	if ((ctrl->val ^ ctrl->cur.val) & V4L2_LOCK_FOCUS)
		ret = s5c73m3_af_run(state, !af_lock);

	return ret;
}

static int s5c73m3_set_auto_focus(struct s5c73m3 *state, int caf)
{
	struct s5c73m3_ctrls *c = &state->ctrls;
	int ret = 1;

	if (c->af_distance->is_new) {
		u16 mode = (c->af_distance->val == V4L2_AUTO_FOCUS_RANGE_MACRO)
				? COMM_AF_MODE_MACRO : COMM_AF_MODE_NORMAL;
		ret = s5c73m3_isp_command(state, COMM_AF_MODE, mode);
		if (ret != 0)
			return ret;
	}

	if (!ret || (c->focus_auto->is_new && c->focus_auto->val) ||
							c->af_start->is_new)
		ret = s5c73m3_af_run(state, 1);
	else if ((c->focus_auto->is_new && !c->focus_auto->val) ||
							c->af_stop->is_new)
		ret = s5c73m3_af_run(state, 0);
	else
		ret = 0;

	return ret;
}

static int s5c73m3_set_contrast(struct s5c73m3 *state, int val)
{
	u16 reg = (val < 0) ? -val + 2 : val;
	return s5c73m3_isp_command(state, COMM_CONTRAST, reg);
}

static int s5c73m3_set_saturation(struct s5c73m3 *state, int val)
{
	u16 reg = (val < 0) ? -val + 2 : val;
	return s5c73m3_isp_command(state, COMM_SATURATION, reg);
}

static int s5c73m3_set_sharpness(struct s5c73m3 *state, int val)
{
	u16 reg = (val < 0) ? -val + 2 : val;
	return s5c73m3_isp_command(state, COMM_SHARPNESS, reg);
}

static int s5c73m3_set_iso(struct s5c73m3 *state, int val)
{
	u32 iso;

	if (val == V4L2_ISO_SENSITIVITY_MANUAL)
		iso = state->ctrls.iso->val + 1;
	else
		iso = 0;

	return s5c73m3_isp_command(state, COMM_ISO, iso);
}

static int s5c73m3_set_stabilization(struct s5c73m3 *state, int val)
{
	struct v4l2_subdev *sd = &state->sensor_sd;

	v4l2_dbg(1, s5c73m3_dbg, sd, "Image stabilization: %d\n", val);

	return s5c73m3_isp_command(state, COMM_FRAME_RATE, val ?
			COMM_FRAME_RATE_ANTI_SHAKE : COMM_FRAME_RATE_AUTO_SET);
}

static int s5c73m3_set_jpeg_quality(struct s5c73m3 *state, int quality)
{
	int reg;

	if (quality <= 65)
		reg = COMM_IMAGE_QUALITY_NORMAL;
	else if (quality <= 75)
		reg = COMM_IMAGE_QUALITY_FINE;
	else
		reg = COMM_IMAGE_QUALITY_SUPERFINE;

	return s5c73m3_isp_command(state, COMM_IMAGE_QUALITY, reg);
}

static int s5c73m3_set_scene_program(struct s5c73m3 *state, int val)
{
	static const unsigned short scene_lookup[] = {
		COMM_SCENE_MODE_NONE,	     /* V4L2_SCENE_MODE_NONE */
		COMM_SCENE_MODE_AGAINST_LIGHT,/* V4L2_SCENE_MODE_BACKLIGHT */
		COMM_SCENE_MODE_BEACH,	     /* V4L2_SCENE_MODE_BEACH_SNOW */
		COMM_SCENE_MODE_CANDLE,	     /* V4L2_SCENE_MODE_CANDLE_LIGHT */
		COMM_SCENE_MODE_DAWN,	     /* V4L2_SCENE_MODE_DAWN_DUSK */
		COMM_SCENE_MODE_FALL,	     /* V4L2_SCENE_MODE_FALL_COLORS */
		COMM_SCENE_MODE_FIRE,	     /* V4L2_SCENE_MODE_FIREWORKS */
		COMM_SCENE_MODE_LANDSCAPE,    /* V4L2_SCENE_MODE_LANDSCAPE */
		COMM_SCENE_MODE_NIGHT,	     /* V4L2_SCENE_MODE_NIGHT */
		COMM_SCENE_MODE_INDOOR,	     /* V4L2_SCENE_MODE_PARTY_INDOOR */
		COMM_SCENE_MODE_PORTRAIT,     /* V4L2_SCENE_MODE_PORTRAIT */
		COMM_SCENE_MODE_SPORTS,	     /* V4L2_SCENE_MODE_SPORTS */
		COMM_SCENE_MODE_SUNSET,	     /* V4L2_SCENE_MODE_SUNSET */
		COMM_SCENE_MODE_TEXT,	     /* V4L2_SCENE_MODE_TEXT */
	};

	v4l2_dbg(1, s5c73m3_dbg, &state->sensor_sd, "Setting %s scene mode\n",
		 v4l2_ctrl_get_menu(state->ctrls.scene_mode->id)[val]);

	return s5c73m3_isp_command(state, COMM_SCENE_MODE, scene_lookup[val]);
}

static int s5c73m3_set_power_line_freq(struct s5c73m3 *state, int val)
{
	unsigned int pwr_line_freq = COMM_FLICKER_NONE;

	switch (val) {
	case V4L2_CID_POWER_LINE_FREQUENCY_DISABLED:
		pwr_line_freq = COMM_FLICKER_NONE;
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY_50HZ:
		pwr_line_freq = COMM_FLICKER_AUTO_50HZ;
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY_60HZ:
		pwr_line_freq = COMM_FLICKER_AUTO_60HZ;
		break;
	default:
	case V4L2_CID_POWER_LINE_FREQUENCY_AUTO:
		pwr_line_freq = COMM_FLICKER_NONE;
	}

	return s5c73m3_isp_command(state, COMM_FLICKER_MODE, pwr_line_freq);
}

static int s5c73m3_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sensor_sd(ctrl);
	struct s5c73m3 *state = sensor_sd_to_s5c73m3(sd);
	int ret = 0;

	v4l2_dbg(1, s5c73m3_dbg, sd, "set_ctrl: %s, value: %d\n",
		 ctrl->name, ctrl->val);

	mutex_lock(&state->lock);
	/*
	 * If the device is not powered up by the host driver do
	 * not apply any controls to H/W at this time. Instead
	 * the controls will be restored right after power-up.
	 */
	if (state->power == 0)
		goto unlock;

	if (ctrl->flags & V4L2_CTRL_FLAG_INACTIVE) {
		ret = -EINVAL;
		goto unlock;
	}

	switch (ctrl->id) {
	case V4L2_CID_3A_LOCK:
		ret = s5c73m3_3a_lock(state, ctrl);
		break;

	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
		ret = s5c73m3_set_white_balance(state, ctrl->val);
		break;

	case V4L2_CID_CONTRAST:
		ret = s5c73m3_set_contrast(state, ctrl->val);
		break;

	case V4L2_CID_COLORFX:
		ret = s5c73m3_set_colorfx(state, ctrl->val);
		break;

	case V4L2_CID_EXPOSURE_AUTO:
		ret = s5c73m3_set_exposure(state, ctrl->val);
		break;

	case V4L2_CID_FOCUS_AUTO:
		ret = s5c73m3_set_auto_focus(state, ctrl->val);
		break;

	case V4L2_CID_IMAGE_STABILIZATION:
		ret = s5c73m3_set_stabilization(state, ctrl->val);
		break;

	case V4L2_CID_ISO_SENSITIVITY:
		ret = s5c73m3_set_iso(state, ctrl->val);
		break;

	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		ret = s5c73m3_set_jpeg_quality(state, ctrl->val);
		break;

	case V4L2_CID_POWER_LINE_FREQUENCY:
		ret = s5c73m3_set_power_line_freq(state, ctrl->val);
		break;

	case V4L2_CID_SATURATION:
		ret = s5c73m3_set_saturation(state, ctrl->val);
		break;

	case V4L2_CID_SCENE_MODE:
		ret = s5c73m3_set_scene_program(state, ctrl->val);
		break;

	case V4L2_CID_SHARPNESS:
		ret = s5c73m3_set_sharpness(state, ctrl->val);
		break;

	case V4L2_CID_WIDE_DYNAMIC_RANGE:
		ret = s5c73m3_isp_command(state, COMM_WDR, !!ctrl->val);
		break;

	case V4L2_CID_ZOOM_ABSOLUTE:
		ret = s5c73m3_isp_command(state, COMM_ZOOM_STEP, ctrl->val);
		break;
	}
unlock:
	mutex_unlock(&state->lock);
	return ret;
}

static const struct v4l2_ctrl_ops s5c73m3_ctrl_ops = {
	.g_volatile_ctrl	= s5c73m3_g_volatile_ctrl,
	.s_ctrl			= s5c73m3_s_ctrl,
};

/* Supported manual ISO values */
static const s64 iso_qmenu[] = {
	/* COMM_ISO: 0x0001...0x0004 */
	100, 200, 400, 800,
};

/* Supported exposure bias values (-2.0EV...+2.0EV) */
static const s64 ev_bias_qmenu[] = {
	/* COMM_EV: 0x0000...0x0008 */
	-2000, -1500, -1000, -500, 0, 500, 1000, 1500, 2000
};

int s5c73m3_init_controls(struct s5c73m3 *state)
{
	const struct v4l2_ctrl_ops *ops = &s5c73m3_ctrl_ops;
	struct s5c73m3_ctrls *ctrls = &state->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;

	int ret = v4l2_ctrl_handler_init(hdl, 22);
	if (ret)
		return ret;

	/* White balance */
	ctrls->auto_wb = v4l2_ctrl_new_std_menu(hdl, ops,
			V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE,
			9, ~0x15e, V4L2_WHITE_BALANCE_AUTO);

	/* Exposure (only automatic exposure) */
	ctrls->auto_exposure = v4l2_ctrl_new_std_menu(hdl, ops,
			V4L2_CID_EXPOSURE_AUTO, 0, ~0x01, V4L2_EXPOSURE_AUTO);

	ctrls->exposure_bias = v4l2_ctrl_new_int_menu(hdl, ops,
			V4L2_CID_AUTO_EXPOSURE_BIAS,
			ARRAY_SIZE(ev_bias_qmenu) - 1,
			ARRAY_SIZE(ev_bias_qmenu)/2 - 1,
			ev_bias_qmenu);

	ctrls->exposure_metering = v4l2_ctrl_new_std_menu(hdl, ops,
			V4L2_CID_EXPOSURE_METERING,
			2, ~0x7, V4L2_EXPOSURE_METERING_AVERAGE);

	/* Auto focus */
	ctrls->focus_auto = v4l2_ctrl_new_std(hdl, ops,
			V4L2_CID_FOCUS_AUTO, 0, 1, 1, 0);

	ctrls->af_start = v4l2_ctrl_new_std(hdl, ops,
			V4L2_CID_AUTO_FOCUS_START, 0, 1, 1, 0);

	ctrls->af_stop = v4l2_ctrl_new_std(hdl, ops,
			V4L2_CID_AUTO_FOCUS_STOP, 0, 1, 1, 0);

	ctrls->af_status = v4l2_ctrl_new_std(hdl, ops,
			V4L2_CID_AUTO_FOCUS_STATUS, 0,
			(V4L2_AUTO_FOCUS_STATUS_BUSY |
			 V4L2_AUTO_FOCUS_STATUS_REACHED |
			 V4L2_AUTO_FOCUS_STATUS_FAILED),
			0, V4L2_AUTO_FOCUS_STATUS_IDLE);

	ctrls->af_distance = v4l2_ctrl_new_std_menu(hdl, ops,
			V4L2_CID_AUTO_FOCUS_RANGE,
			V4L2_AUTO_FOCUS_RANGE_MACRO,
			~(1 << V4L2_AUTO_FOCUS_RANGE_NORMAL |
			  1 << V4L2_AUTO_FOCUS_RANGE_MACRO),
			V4L2_AUTO_FOCUS_RANGE_NORMAL);
	/* ISO sensitivity */
	ctrls->auto_iso = v4l2_ctrl_new_std_menu(hdl, ops,
			V4L2_CID_ISO_SENSITIVITY_AUTO, 1, 0,
			V4L2_ISO_SENSITIVITY_AUTO);

	ctrls->iso = v4l2_ctrl_new_int_menu(hdl, ops,
			V4L2_CID_ISO_SENSITIVITY, ARRAY_SIZE(iso_qmenu) - 1,
			ARRAY_SIZE(iso_qmenu)/2 - 1, iso_qmenu);

	ctrls->contrast = v4l2_ctrl_new_std(hdl, ops,
			V4L2_CID_CONTRAST, -2, 2, 1, 0);

	ctrls->saturation = v4l2_ctrl_new_std(hdl, ops,
			V4L2_CID_SATURATION, -2, 2, 1, 0);

	ctrls->sharpness = v4l2_ctrl_new_std(hdl, ops,
			V4L2_CID_SHARPNESS, -2, 2, 1, 0);

	ctrls->zoom = v4l2_ctrl_new_std(hdl, ops,
			V4L2_CID_ZOOM_ABSOLUTE, 0, 30, 1, 0);

	ctrls->colorfx = v4l2_ctrl_new_std_menu(hdl, ops, V4L2_CID_COLORFX,
			V4L2_COLORFX_AQUA, ~0x40f, V4L2_COLORFX_NONE);

	ctrls->wdr = v4l2_ctrl_new_std(hdl, ops,
			V4L2_CID_WIDE_DYNAMIC_RANGE, 0, 1, 1, 0);

	ctrls->stabilization = v4l2_ctrl_new_std(hdl, ops,
			V4L2_CID_IMAGE_STABILIZATION, 0, 1, 1, 0);

	v4l2_ctrl_new_std_menu(hdl, ops, V4L2_CID_POWER_LINE_FREQUENCY,
			       V4L2_CID_POWER_LINE_FREQUENCY_AUTO, 0,
			       V4L2_CID_POWER_LINE_FREQUENCY_AUTO);

	ctrls->jpeg_quality = v4l2_ctrl_new_std(hdl, ops,
			V4L2_CID_JPEG_COMPRESSION_QUALITY, 1, 100, 1, 80);

	ctrls->scene_mode = v4l2_ctrl_new_std_menu(hdl, ops,
			V4L2_CID_SCENE_MODE, V4L2_SCENE_MODE_TEXT, ~0x3fff,
			V4L2_SCENE_MODE_NONE);

	ctrls->aaa_lock = v4l2_ctrl_new_std(hdl, ops,
			V4L2_CID_3A_LOCK, 0, 0x7, 0, 0);

	if (hdl->error) {
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		return ret;
	}

	v4l2_ctrl_auto_cluster(3, &ctrls->auto_exposure, 0, false);
	ctrls->auto_iso->flags |= V4L2_CTRL_FLAG_VOLATILE |
				V4L2_CTRL_FLAG_UPDATE;
	v4l2_ctrl_auto_cluster(2, &ctrls->auto_iso, 0, false);
	ctrls->af_status->flags |= V4L2_CTRL_FLAG_VOLATILE;
	v4l2_ctrl_cluster(5, &ctrls->focus_auto);

	state->sensor_sd.ctrl_handler = hdl;

	return 0;
}

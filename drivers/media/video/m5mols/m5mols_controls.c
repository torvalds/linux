/*
 * Controls for M-5MOLS 8M Pixel camera sensor with ISP
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * Author: HeungJun Kim <riverful.kim@samsung.com>
 *
 * Copyright (C) 2009 Samsung Electronics Co., Ltd.
 * Author: Dongsoo Nathaniel Kim <dongsoo45.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>

#include "m5mols.h"
#include "m5mols_reg.h"

static struct m5mols_scenemode m5mols_default_scenemode[] = {
	[REG_SCENE_NORMAL] = {
		REG_AE_CENTER, REG_AE_INDEX_00, REG_AWB_AUTO, 0,
		REG_CHROMA_ON, 3, REG_EDGE_ON, 5,
		REG_AF_NORMAL, REG_FD_OFF,
		REG_MCC_NORMAL, REG_LIGHT_OFF, REG_FLASH_OFF,
		5, REG_ISO_AUTO, REG_CAP_NONE, REG_WDR_OFF,
	},
	[REG_SCENE_PORTRAIT] = {
		REG_AE_CENTER, REG_AE_INDEX_00, REG_AWB_AUTO, 0,
		REG_CHROMA_ON, 3, REG_EDGE_ON, 4,
		REG_AF_NORMAL, BIT_FD_EN | BIT_FD_DRAW_FACE_FRAME,
		REG_MCC_OFF, REG_LIGHT_OFF, REG_FLASH_OFF,
		6, REG_ISO_AUTO, REG_CAP_NONE, REG_WDR_OFF,
	},
	[REG_SCENE_LANDSCAPE] = {
		REG_AE_ALL, REG_AE_INDEX_00, REG_AWB_AUTO, 0,
		REG_CHROMA_ON, 4, REG_EDGE_ON, 6,
		REG_AF_NORMAL, REG_FD_OFF,
		REG_MCC_OFF, REG_LIGHT_OFF, REG_FLASH_OFF,
		6, REG_ISO_AUTO, REG_CAP_NONE, REG_WDR_OFF,
	},
	[REG_SCENE_SPORTS] = {
		REG_AE_CENTER, REG_AE_INDEX_00, REG_AWB_AUTO, 0,
		REG_CHROMA_ON, 3, REG_EDGE_ON, 5,
		REG_AF_NORMAL, REG_FD_OFF,
		REG_MCC_OFF, REG_LIGHT_OFF, REG_FLASH_OFF,
		6, REG_ISO_AUTO, REG_CAP_NONE, REG_WDR_OFF,
	},
	[REG_SCENE_PARTY_INDOOR] = {
		REG_AE_CENTER, REG_AE_INDEX_00, REG_AWB_AUTO, 0,
		REG_CHROMA_ON, 4, REG_EDGE_ON, 5,
		REG_AF_NORMAL, REG_FD_OFF,
		REG_MCC_OFF, REG_LIGHT_OFF, REG_FLASH_OFF,
		6, REG_ISO_200, REG_CAP_NONE, REG_WDR_OFF,
	},
	[REG_SCENE_BEACH_SNOW] = {
		REG_AE_CENTER, REG_AE_INDEX_10_POS, REG_AWB_AUTO, 0,
		REG_CHROMA_ON, 4, REG_EDGE_ON, 5,
		REG_AF_NORMAL, REG_FD_OFF,
		REG_MCC_OFF, REG_LIGHT_OFF, REG_FLASH_OFF,
		6, REG_ISO_50, REG_CAP_NONE, REG_WDR_OFF,
	},
	[REG_SCENE_SUNSET] = {
		REG_AE_CENTER, REG_AE_INDEX_00, REG_AWB_PRESET,
		REG_AWB_DAYLIGHT,
		REG_CHROMA_ON, 3, REG_EDGE_ON, 5,
		REG_AF_NORMAL, REG_FD_OFF,
		REG_MCC_OFF, REG_LIGHT_OFF, REG_FLASH_OFF,
		6, REG_ISO_AUTO, REG_CAP_NONE, REG_WDR_OFF,
	},
	[REG_SCENE_DAWN_DUSK] = {
		REG_AE_CENTER, REG_AE_INDEX_00, REG_AWB_PRESET,
		REG_AWB_FLUORESCENT_1,
		REG_CHROMA_ON, 3, REG_EDGE_ON, 5,
		REG_AF_NORMAL, REG_FD_OFF,
		REG_MCC_OFF, REG_LIGHT_OFF, REG_FLASH_OFF,
		6, REG_ISO_AUTO, REG_CAP_NONE, REG_WDR_OFF,
	},
	[REG_SCENE_FALL] = {
		REG_AE_CENTER, REG_AE_INDEX_00, REG_AWB_AUTO, 0,
		REG_CHROMA_ON, 5, REG_EDGE_ON, 5,
		REG_AF_NORMAL, REG_FD_OFF,
		REG_MCC_OFF, REG_LIGHT_OFF, REG_FLASH_OFF,
		6, REG_ISO_AUTO, REG_CAP_NONE, REG_WDR_OFF,
	},
	[REG_SCENE_NIGHT] = {
		REG_AE_CENTER, REG_AE_INDEX_00, REG_AWB_AUTO, 0,
		REG_CHROMA_ON, 3, REG_EDGE_ON, 5,
		REG_AF_NORMAL, REG_FD_OFF,
		REG_MCC_OFF, REG_LIGHT_OFF, REG_FLASH_OFF,
		6, REG_ISO_AUTO, REG_CAP_NONE, REG_WDR_OFF,
	},
	[REG_SCENE_AGAINST_LIGHT] = {
		REG_AE_CENTER, REG_AE_INDEX_00, REG_AWB_AUTO, 0,
		REG_CHROMA_ON, 3, REG_EDGE_ON, 5,
		REG_AF_NORMAL, REG_FD_OFF,
		REG_MCC_OFF, REG_LIGHT_OFF, REG_FLASH_OFF,
		6, REG_ISO_AUTO, REG_CAP_NONE, REG_WDR_OFF,
	},
	[REG_SCENE_FIRE] = {
		REG_AE_CENTER, REG_AE_INDEX_00, REG_AWB_AUTO, 0,
		REG_CHROMA_ON, 3, REG_EDGE_ON, 5,
		REG_AF_NORMAL, REG_FD_OFF,
		REG_MCC_OFF, REG_LIGHT_OFF, REG_FLASH_OFF,
		6, REG_ISO_50, REG_CAP_NONE, REG_WDR_OFF,
	},
	[REG_SCENE_TEXT] = {
		REG_AE_CENTER, REG_AE_INDEX_00, REG_AWB_AUTO, 0,
		REG_CHROMA_ON, 3, REG_EDGE_ON, 7,
		REG_AF_MACRO, REG_FD_OFF,
		REG_MCC_OFF, REG_LIGHT_OFF, REG_FLASH_OFF,
		6, REG_ISO_AUTO, REG_CAP_ANTI_SHAKE, REG_WDR_ON,
	},
	[REG_SCENE_CANDLE] = {
		REG_AE_CENTER, REG_AE_INDEX_00, REG_AWB_AUTO, 0,
		REG_CHROMA_ON, 3, REG_EDGE_ON, 5,
		REG_AF_NORMAL, REG_FD_OFF,
		REG_MCC_OFF, REG_LIGHT_OFF, REG_FLASH_OFF,
		6, REG_ISO_AUTO, REG_CAP_NONE, REG_WDR_OFF,
	},
};

/**
 * m5mols_do_scenemode() - Change current scenemode
 * @mode:	Desired mode of the scenemode
 *
 * WARNING: The execution order is important. Do not change the order.
 */
int m5mols_do_scenemode(struct m5mols_info *info, u8 mode)
{
	struct v4l2_subdev *sd = &info->sd;
	struct m5mols_scenemode scenemode = m5mols_default_scenemode[mode];
	int ret;

	if (mode > REG_SCENE_CANDLE)
		return -EINVAL;

	ret = m5mols_lock_3a(info, false);
	if (!ret)
		ret = m5mols_write(sd, AE_EV_PRESET_MONITOR, mode);
	if (!ret)
		ret = m5mols_write(sd, AE_EV_PRESET_CAPTURE, mode);
	if (!ret)
		ret = m5mols_write(sd, AE_MODE, scenemode.metering);
	if (!ret)
		ret = m5mols_write(sd, AE_INDEX, scenemode.ev_bias);
	if (!ret)
		ret = m5mols_write(sd, AWB_MODE, scenemode.wb_mode);
	if (!ret)
		ret = m5mols_write(sd, AWB_MANUAL, scenemode.wb_preset);
	if (!ret)
		ret = m5mols_write(sd, MON_CHROMA_EN, scenemode.chroma_en);
	if (!ret)
		ret = m5mols_write(sd, MON_CHROMA_LVL, scenemode.chroma_lvl);
	if (!ret)
		ret = m5mols_write(sd, MON_EDGE_EN, scenemode.edge_en);
	if (!ret)
		ret = m5mols_write(sd, MON_EDGE_LVL, scenemode.edge_lvl);
	if (!ret && is_available_af(info))
		ret = m5mols_write(sd, AF_MODE, scenemode.af_range);
	if (!ret && is_available_af(info))
		ret = m5mols_write(sd, FD_CTL, scenemode.fd_mode);
	if (!ret)
		ret = m5mols_write(sd, MON_TONE_CTL, scenemode.tone);
	if (!ret)
		ret = m5mols_write(sd, AE_ISO, scenemode.iso);
	if (!ret)
		ret = m5mols_set_mode(info, REG_CAPTURE);
	if (!ret)
		ret = m5mols_write(sd, CAPP_WDR_EN, scenemode.wdr);
	if (!ret)
		ret = m5mols_write(sd, CAPP_MCC_MODE, scenemode.mcc);
	if (!ret)
		ret = m5mols_write(sd, CAPP_LIGHT_CTRL, scenemode.light);
	if (!ret)
		ret = m5mols_write(sd, CAPP_FLASH_CTRL, scenemode.flash);
	if (!ret)
		ret = m5mols_write(sd, CAPC_MODE, scenemode.capt_mode);
	if (!ret)
		ret = m5mols_set_mode(info, REG_MONITOR);

	return ret;
}

static int m5mols_lock_ae(struct m5mols_info *info, bool lock)
{
	int ret = 0;

	if (info->lock_ae != lock)
		ret = m5mols_write(&info->sd, AE_LOCK,
				lock ? REG_AE_LOCK : REG_AE_UNLOCK);
	if (!ret)
		info->lock_ae = lock;

	return ret;
}

static int m5mols_lock_awb(struct m5mols_info *info, bool lock)
{
	int ret = 0;

	if (info->lock_awb != lock)
		ret = m5mols_write(&info->sd, AWB_LOCK,
				lock ? REG_AWB_LOCK : REG_AWB_UNLOCK);
	if (!ret)
		info->lock_awb = lock;

	return ret;
}

/* m5mols_lock_3a() - Lock 3A(Auto Exposure, Auto Whitebalance, Auto Focus) */
int m5mols_lock_3a(struct m5mols_info *info, bool lock)
{
	int ret;

	ret = m5mols_lock_ae(info, lock);
	if (!ret)
		ret = m5mols_lock_awb(info, lock);
	/* Don't need to handle unlocking AF */
	if (!ret && is_available_af(info) && lock)
		ret = m5mols_write(&info->sd, AF_EXECUTE, REG_AF_STOP);

	return ret;
}

static int m5mols_set_metering_mode(struct m5mols_info *info, int mode)
{
	unsigned int metering;

	switch (mode) {
	case V4L2_EXPOSURE_METERING_CENTER_WEIGHTED:
		metering = REG_AE_CENTER;
		break;
	case V4L2_EXPOSURE_METERING_SPOT:
		metering = REG_AE_SPOT;
		break;
	default:
		metering = REG_AE_ALL;
		break;
	}

	return m5mols_write(&info->sd, AE_MODE, metering);
}

static int m5mols_set_exposure(struct m5mols_info *info, int exposure)
{
	struct v4l2_subdev *sd = &info->sd;
	int ret;

	ret = m5mols_lock_ae(info, exposure != V4L2_EXPOSURE_AUTO);
	if (ret < 0)
		return ret;

	if (exposure == V4L2_EXPOSURE_AUTO) {
		ret = m5mols_set_metering_mode(info, info->metering->val);
		if (ret < 0)
			return ret;

		v4l2_dbg(1, m5mols_debug, sd,
			 "%s: exposure bias: %#x, metering: %#x\n",
			 __func__, info->exposure_bias->val,
			 info->metering->val);

		return m5mols_write(sd, AE_INDEX, info->exposure_bias->val);
	}

	if (exposure == V4L2_EXPOSURE_MANUAL) {
		ret = m5mols_write(sd, AE_MODE, REG_AE_OFF);
		if (ret == 0)
			ret = m5mols_write(sd, AE_MAN_GAIN_MON,
					   info->exposure->val);
		if (ret == 0)
			ret = m5mols_write(sd, AE_MAN_GAIN_CAP,
					   info->exposure->val);

		v4l2_dbg(1, m5mols_debug, sd, "%s: exposure: %#x\n",
			 __func__, info->exposure->val);
	}

	return ret;
}

static int m5mols_set_white_balance(struct m5mols_info *info, int val)
{
	static const unsigned short wb[][2] = {
		{ V4L2_WHITE_BALANCE_INCANDESCENT,  REG_AWB_INCANDESCENT },
		{ V4L2_WHITE_BALANCE_FLUORESCENT,   REG_AWB_FLUORESCENT_1 },
		{ V4L2_WHITE_BALANCE_FLUORESCENT_H, REG_AWB_FLUORESCENT_2 },
		{ V4L2_WHITE_BALANCE_HORIZON,       REG_AWB_HORIZON },
		{ V4L2_WHITE_BALANCE_DAYLIGHT,      REG_AWB_DAYLIGHT },
		{ V4L2_WHITE_BALANCE_FLASH,         REG_AWB_LEDLIGHT },
		{ V4L2_WHITE_BALANCE_CLOUDY,        REG_AWB_CLOUDY },
		{ V4L2_WHITE_BALANCE_SHADE,         REG_AWB_SHADE },
		{ V4L2_WHITE_BALANCE_AUTO,          REG_AWB_AUTO },
	};
	int i;
	struct v4l2_subdev *sd = &info->sd;
	int ret = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(wb); i++) {
		int awb;
		if (wb[i][0] != val)
			continue;

		v4l2_dbg(1, m5mols_debug, sd,
			 "Setting white balance to: %#x\n", wb[i][0]);

		awb = wb[i][0] == V4L2_WHITE_BALANCE_AUTO;
		ret = m5mols_write(sd, AWB_MODE, awb ? REG_AWB_AUTO :
						 REG_AWB_PRESET);
		if (ret < 0)
			return ret;

		if (!awb)
			ret = m5mols_write(sd, AWB_MANUAL, wb[i][1]);
	}

	return ret;
}

static int m5mols_set_saturation(struct m5mols_info *info, int val)
{
	int ret = m5mols_write(&info->sd, MON_CHROMA_LVL, val);
	if (ret < 0)
		return ret;

	return m5mols_write(&info->sd, MON_CHROMA_EN, REG_CHROMA_ON);
}

static int m5mols_set_color_effect(struct m5mols_info *info, int val)
{
	unsigned int m_effect = REG_COLOR_EFFECT_OFF;
	unsigned int p_effect = REG_EFFECT_OFF;
	unsigned int cfix_r = 0, cfix_b = 0;
	struct v4l2_subdev *sd = &info->sd;
	int ret = 0;

	switch (val) {
	case V4L2_COLORFX_BW:
		m_effect = REG_COLOR_EFFECT_ON;
		break;
	case V4L2_COLORFX_NEGATIVE:
		p_effect = REG_EFFECT_NEGA;
		break;
	case V4L2_COLORFX_EMBOSS:
		p_effect = REG_EFFECT_EMBOSS;
		break;
	case V4L2_COLORFX_SEPIA:
		m_effect = REG_COLOR_EFFECT_ON;
		cfix_r = REG_CFIXR_SEPIA;
		cfix_b = REG_CFIXB_SEPIA;
		break;
	}

	ret = m5mols_write(sd, PARM_EFFECT, p_effect);
	if (!ret)
		ret = m5mols_write(sd, MON_EFFECT, m_effect);

	if (ret == 0 && m_effect == REG_COLOR_EFFECT_ON) {
		ret = m5mols_write(sd, MON_CFIXR, cfix_r);
		if (!ret)
			ret = m5mols_write(sd, MON_CFIXB, cfix_b);
	}

	v4l2_dbg(1, m5mols_debug, sd,
		 "p_effect: %#x, m_effect: %#x, r: %#x, b: %#x (%d)\n",
		 p_effect, m_effect, cfix_r, cfix_b, ret);

	return ret;
}

static int m5mols_set_iso(struct m5mols_info *info, int auto_iso)
{
	u32 iso = auto_iso ? 0 : info->iso->val + 1;

	return m5mols_write(&info->sd, AE_ISO, iso);
}

static int m5mols_set_wdr(struct m5mols_info *info, int wdr)
{
	int ret;

	ret = m5mols_write(&info->sd, MON_TONE_CTL, wdr ? 9 : 5);
	if (ret < 0)
		return ret;

	ret = m5mols_set_mode(info, REG_CAPTURE);
	if (ret < 0)
		return ret;

	return m5mols_write(&info->sd, CAPP_WDR_EN, wdr);
}

static int m5mols_set_stabilization(struct m5mols_info *info, int val)
{
	struct v4l2_subdev *sd = &info->sd;
	unsigned int evp = val ? 0xe : 0x0;
	int ret;

	ret = m5mols_write(sd, AE_EV_PRESET_MONITOR, evp);
	if (ret < 0)
		return ret;

	return m5mols_write(sd, AE_EV_PRESET_CAPTURE, evp);
}

static int m5mols_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct m5mols_info *info = to_m5mols(sd);
	int ret = 0;
	u8 status;

	v4l2_dbg(1, m5mols_debug, sd, "%s: ctrl: %s (%d)\n",
		 __func__, ctrl->name, info->isp_ready);

	if (!info->isp_ready)
		return -EBUSY;

	switch (ctrl->id) {
	case V4L2_CID_ISO_SENSITIVITY_AUTO:
		ret = m5mols_read_u8(sd, AE_ISO, &status);
		if (ret == 0)
			ctrl->val = !status;
		if (status != REG_ISO_AUTO)
			info->iso->val = status - 1;
		break;
	}

	return ret;
}

static int m5mols_s_ctrl(struct v4l2_ctrl *ctrl)
{
	unsigned int ctrl_mode = m5mols_get_ctrl_mode(ctrl);
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct m5mols_info *info = to_m5mols(sd);
	int last_mode = info->mode;
	int ret = 0;

	/*
	 * If needed, defer restoring the controls until
	 * the device is fully initialized.
	 */
	if (!info->isp_ready) {
		info->ctrl_sync = 0;
		return 0;
	}

	v4l2_dbg(1, m5mols_debug, sd, "%s: %s, val: %d, priv: %#x\n",
		 __func__, ctrl->name, ctrl->val, (int)ctrl->priv);

	if (ctrl_mode && ctrl_mode != info->mode) {
		ret = m5mols_set_mode(info, ctrl_mode);
		if (ret < 0)
			return ret;
	}

	switch (ctrl->id) {
	case V4L2_CID_ZOOM_ABSOLUTE:
		ret = m5mols_write(sd, MON_ZOOM, ctrl->val);
		break;

	case V4L2_CID_EXPOSURE_AUTO:
		ret = m5mols_set_exposure(info, ctrl->val);
		break;

	case V4L2_CID_ISO_SENSITIVITY:
		ret = m5mols_set_iso(info, ctrl->val);
		break;

	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
		ret = m5mols_set_white_balance(info, ctrl->val);
		break;

	case V4L2_CID_SATURATION:
		ret = m5mols_set_saturation(info, ctrl->val);
		break;

	case V4L2_CID_COLORFX:
		ret = m5mols_set_color_effect(info, ctrl->val);
		break;

	case V4L2_CID_WIDE_DYNAMIC_RANGE:
		ret = m5mols_set_wdr(info, ctrl->val);
		break;

	case V4L2_CID_IMAGE_STABILIZATION:
		ret = m5mols_set_stabilization(info, ctrl->val);
		break;

	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		ret = m5mols_write(sd, CAPP_JPEG_RATIO, ctrl->val);
		break;
	}

	if (ret == 0 && info->mode != last_mode)
		ret = m5mols_set_mode(info, last_mode);

	return ret;
}

static const struct v4l2_ctrl_ops m5mols_ctrl_ops = {
	.g_volatile_ctrl	= m5mols_g_volatile_ctrl,
	.s_ctrl			= m5mols_s_ctrl,
};

/* Supported manual ISO values */
static const s64 iso_qmenu[] = {
	/* AE_ISO: 0x01...0x07 */
	50, 100, 200, 400, 800, 1600, 3200
};

/* Supported Exposure Bias values, -2.0EV...+2.0EV */
static const s64 ev_bias_qmenu[] = {
	/* AE_INDEX: 0x00...0x08 */
	-2000, -1500, -1000, -500, 0, 500, 1000, 1500, 2000
};

int m5mols_init_controls(struct v4l2_subdev *sd)
{
	struct m5mols_info *info = to_m5mols(sd);
	u16 exposure_max;
	u16 zoom_step;
	int ret;

	/* Determine the firmware dependant control range and step values */
	ret = m5mols_read_u16(sd, AE_MAX_GAIN_MON, &exposure_max);
	if (ret < 0)
		return ret;

	zoom_step = is_manufacturer(info, REG_SAMSUNG_OPTICS) ? 31 : 1;
	v4l2_ctrl_handler_init(&info->handle, 20);

	info->auto_wb = v4l2_ctrl_new_std_menu(&info->handle,
			&m5mols_ctrl_ops, V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE,
			9, ~0x3fe, V4L2_WHITE_BALANCE_AUTO);

	/* Exposure control cluster */
	info->auto_exposure = v4l2_ctrl_new_std_menu(&info->handle,
			&m5mols_ctrl_ops, V4L2_CID_EXPOSURE_AUTO,
			1, ~0x03, V4L2_EXPOSURE_AUTO);

	info->exposure = v4l2_ctrl_new_std(&info->handle,
			&m5mols_ctrl_ops, V4L2_CID_EXPOSURE,
			0, exposure_max, 1, exposure_max / 2);

	info->exposure_bias = v4l2_ctrl_new_int_menu(&info->handle,
			&m5mols_ctrl_ops, V4L2_CID_AUTO_EXPOSURE_BIAS,
			ARRAY_SIZE(ev_bias_qmenu) - 1,
			ARRAY_SIZE(ev_bias_qmenu)/2 - 1,
			ev_bias_qmenu);

	info->metering = v4l2_ctrl_new_std_menu(&info->handle,
			&m5mols_ctrl_ops, V4L2_CID_EXPOSURE_METERING,
			2, ~0x7, V4L2_EXPOSURE_METERING_AVERAGE);

	/* ISO control cluster */
	info->auto_iso = v4l2_ctrl_new_std_menu(&info->handle, &m5mols_ctrl_ops,
			V4L2_CID_ISO_SENSITIVITY_AUTO, 1, ~0x03, 1);

	info->iso = v4l2_ctrl_new_int_menu(&info->handle, &m5mols_ctrl_ops,
			V4L2_CID_ISO_SENSITIVITY, ARRAY_SIZE(iso_qmenu) - 1,
			ARRAY_SIZE(iso_qmenu)/2 - 1, iso_qmenu);

	info->saturation = v4l2_ctrl_new_std(&info->handle, &m5mols_ctrl_ops,
			V4L2_CID_SATURATION, 1, 5, 1, 3);

	info->zoom = v4l2_ctrl_new_std(&info->handle, &m5mols_ctrl_ops,
			V4L2_CID_ZOOM_ABSOLUTE, 1, 70, zoom_step, 1);

	info->colorfx = v4l2_ctrl_new_std_menu(&info->handle, &m5mols_ctrl_ops,
			V4L2_CID_COLORFX, 4, 0, V4L2_COLORFX_NONE);

	info->wdr = v4l2_ctrl_new_std(&info->handle, &m5mols_ctrl_ops,
			V4L2_CID_WIDE_DYNAMIC_RANGE, 0, 1, 1, 0);

	info->stabilization = v4l2_ctrl_new_std(&info->handle, &m5mols_ctrl_ops,
			V4L2_CID_IMAGE_STABILIZATION, 0, 1, 1, 0);

	info->jpeg_quality = v4l2_ctrl_new_std(&info->handle, &m5mols_ctrl_ops,
			V4L2_CID_JPEG_COMPRESSION_QUALITY, 1, 100, 1, 80);

	if (info->handle.error) {
		int ret = info->handle.error;
		v4l2_err(sd, "Failed to initialize controls: %d\n", ret);
		v4l2_ctrl_handler_free(&info->handle);
		return ret;
	}

	v4l2_ctrl_auto_cluster(4, &info->auto_exposure, 1, false);
	info->auto_iso->flags |= V4L2_CTRL_FLAG_VOLATILE |
				V4L2_CTRL_FLAG_UPDATE;
	v4l2_ctrl_auto_cluster(2, &info->auto_iso, 0, false);

	m5mols_set_ctrl_mode(info->auto_exposure, REG_PARAMETER);
	m5mols_set_ctrl_mode(info->auto_wb, REG_PARAMETER);
	m5mols_set_ctrl_mode(info->colorfx, REG_MONITOR);

	sd->ctrl_handler = &info->handle;

	return 0;
}

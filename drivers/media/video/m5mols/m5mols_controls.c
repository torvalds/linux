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
		ret = m5mols_mode(info, REG_CAPTURE);
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
		ret = m5mols_mode(info, REG_MONITOR);

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

/* m5mols_set_ctrl() - The main s_ctrl function called by m5mols_set_ctrl() */
int m5mols_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct m5mols_info *info = to_m5mols(sd);
	int ret;

	switch (ctrl->id) {
	case V4L2_CID_ZOOM_ABSOLUTE:
		return m5mols_write(sd, MON_ZOOM, ctrl->val);

	case V4L2_CID_EXPOSURE_AUTO:
		ret = m5mols_lock_ae(info,
			ctrl->val == V4L2_EXPOSURE_AUTO ? false : true);
		if (!ret && ctrl->val == V4L2_EXPOSURE_AUTO)
			ret = m5mols_write(sd, AE_MODE, REG_AE_ALL);
		if (!ret && ctrl->val == V4L2_EXPOSURE_MANUAL) {
			int val = info->exposure->val;
			ret = m5mols_write(sd, AE_MODE, REG_AE_OFF);
			if (!ret)
				ret = m5mols_write(sd, AE_MAN_GAIN_MON, val);
			if (!ret)
				ret = m5mols_write(sd, AE_MAN_GAIN_CAP, val);
		}
		return ret;

	case V4L2_CID_AUTO_WHITE_BALANCE:
		ret = m5mols_lock_awb(info, ctrl->val ? false : true);
		if (!ret)
			ret = m5mols_write(sd, AWB_MODE, ctrl->val ?
				REG_AWB_AUTO : REG_AWB_PRESET);
		return ret;

	case V4L2_CID_SATURATION:
		ret = m5mols_write(sd, MON_CHROMA_LVL, ctrl->val);
		if (!ret)
			ret = m5mols_write(sd, MON_CHROMA_EN, REG_CHROMA_ON);
		return ret;

	case V4L2_CID_COLORFX:
		/*
		 * This control uses two kinds of registers: normal & color.
		 * The normal effect belongs to category 1, while the color
		 * one belongs to category 2.
		 *
		 * The normal effect uses one register: CAT1_EFFECT.
		 * The color effect uses three registers:
		 * CAT2_COLOR_EFFECT, CAT2_CFIXR, CAT2_CFIXB.
		 */
		ret = m5mols_write(sd, PARM_EFFECT,
			ctrl->val == V4L2_COLORFX_NEGATIVE ? REG_EFFECT_NEGA :
			ctrl->val == V4L2_COLORFX_EMBOSS ? REG_EFFECT_EMBOSS :
			REG_EFFECT_OFF);
		if (!ret)
			ret = m5mols_write(sd, MON_EFFECT,
				ctrl->val == V4L2_COLORFX_SEPIA ?
				REG_COLOR_EFFECT_ON : REG_COLOR_EFFECT_OFF);
		if (!ret)
			ret = m5mols_write(sd, MON_CFIXR,
				ctrl->val == V4L2_COLORFX_SEPIA ?
				REG_CFIXR_SEPIA : 0);
		if (!ret)
			ret = m5mols_write(sd, MON_CFIXB,
				ctrl->val == V4L2_COLORFX_SEPIA ?
				REG_CFIXB_SEPIA : 0);
		return ret;
	}

	return -EINVAL;
}

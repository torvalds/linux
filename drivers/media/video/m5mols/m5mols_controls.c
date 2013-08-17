/*
 * Controls for M5MOLS 8M Pixel camera sensor with ISP
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
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>

#include "m5mols.h"
#include "m5mols_reg.h"

static int m5mols_wb_mode(struct m5mols_info *info, struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = &info->sd;
	int ret;

	if (info->is_awb_lock) {
		ret = m5mols_set_awb_lock(info, false);
		if (!ret)
			return ret;
	}

	/* 0x01 : Auto Whitebalance, 0x02 : Manual Whitebalance. */
	return i2c_w8_wb(sd, CAT6_AWB_MODE, (ctrl->val) ? 0x1 : 0x2);

}

static int m5mols_exposure_mode(struct m5mols_info *info,
		struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = &info->sd;
	int ret;

	if (info->is_ae_lock) {
		ret = m5mols_set_ae_lock(info, false);
		if (ret)
			return ret;
	}

	/* 0x01 : Auto Exposure, 0x0 : Manual Exposure. */
	return i2c_w8_ae(sd, CAT3_AE_MODE,
			(ctrl->val == V4L2_EXPOSURE_AUTO) ? 0x1 : 0x0);
}

static int m5mols_exposure(struct m5mols_info *info)
{
	struct v4l2_subdev *sd = &info->sd;

	return i2c_w16_ae(sd, CAT3_MANUAL_GAIN_MON, info->exposure->val);
}

static int m5mols_zoom(struct m5mols_info *info, struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = &info->sd;

	return i2c_w8_mon(sd, CAT2_ZOOM, ctrl->val);
}

static int m5mols_focus_mode(struct m5mols_info *info,
		struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = &info->sd;
	u32 reg;
	int ret;

	ret = m5mols_set_mode(sd, MODE_MONITOR);
	if (!ret)
		ret = i2c_r8_system(sd, CAT0_INT_FACTOR, &reg);
	if (!ret)
		ret = i2c_w8_system(sd, CAT0_INT_ENABLE, (1 << INT_BIT_AF));
	if (!ret)
		/* must be excuted in the monitor mode */
		ret = i2c_w8_lens(sd, CATA_INIT_AF_FUNC, 0x1);

	/* 0x0 : Normal AF mode
	 * 0x1 : Macro AF mode
	 * 0x2 : Continuous AF mode (Not working) */
	return i2c_w8_lens(sd, CATA_AF_MODE, 0x0);
}

static int m5mols_focus(struct m5mols_info *info, struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = &info->sd;

	/* 0x0: Stop,
	   0x1: Excute AF,
	   0x02: Excutre MF rel(Not tested),
	   0x03: Excute MF absol(Not tested) */
	return i2c_w8_lens(sd, CATA_AF_EXCUTE, ctrl->val);
}

static int m5mols_set_saturation(struct m5mols_info *info,
		struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = &info->sd;
	static u8 m5mols_chroma_lvl[] = {
		0x1c, 0x3e, 0x5f, 0x80, 0xa1, 0xc2, 0xe4,
	};
	int ret;

	ret = i2c_w8_mon(sd, CAT2_CHROMA_LVL, m5mols_chroma_lvl[ctrl->val]);
	if (!ret)
		ret = i2c_w8_mon(sd, CAT2_CHROMA_EN, true);

	return ret;
}

static int m5mols_set_colorfx(struct m5mols_info *info, struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = &info->sd;
	static u8 m5mols_effects_gamma[] = {	/* cat 1: Effects */
		[V4L2_COLORFX_NEGATIVE]		= 0x01,
		[V4L2_COLORFX_EMBOSS]		= 0x06,
		[V4L2_COLORFX_SKETCH]		= 0x07,
	};
	static u8 m5mols_cfixb_chroma[] = {	/* cat 2: Cr for effect */
		[V4L2_COLORFX_BW]		= 0x0,
		[V4L2_COLORFX_SEPIA]		= 0xd8,
		[V4L2_COLORFX_SKY_BLUE]		= 0x40,
		[V4L2_COLORFX_GRASS_GREEN]	= 0xe0,
	};
	static u8 m5mols_cfixr_chroma[] = {	/* cat 2: Cb for effect */
		[V4L2_COLORFX_BW]		= 0x0,
		[V4L2_COLORFX_SEPIA]		= 0x18,
		[V4L2_COLORFX_SKY_BLUE]		= 0x00,
		[V4L2_COLORFX_GRASS_GREEN]	= 0xe0,
	};
	int ret = -EINVAL;

	switch (ctrl->val) {
	case V4L2_COLORFX_NONE:
		return i2c_w8_mon(sd, CAT2_COLOR_EFFECT, false);
	case V4L2_COLORFX_BW:		/* chroma: Gray */
	case V4L2_COLORFX_SEPIA:	/* chroma: Sepia */
	case V4L2_COLORFX_SKY_BLUE:	/* chroma: Blue */
	case V4L2_COLORFX_GRASS_GREEN:	/* chroma: Green */
		ret = i2c_w8_mon(sd, CAT2_CFIXB,
				m5mols_cfixb_chroma[ctrl->val]);
		if (!ret)
			ret = i2c_w8_mon(sd, CAT2_CFIXR,
					m5mols_cfixr_chroma[ctrl->val]);
		if (!ret)
			ret = i2c_w8_mon(sd, CAT2_COLOR_EFFECT, true);
		return ret;
	case V4L2_COLORFX_NEGATIVE:	/* gamma: Negative */
	case V4L2_COLORFX_EMBOSS:	/* gamma: Emboss */
	case V4L2_COLORFX_SKETCH:	/* gamma: Outline */
		ret = i2c_w8_param(sd, CAT1_EFFECT,
				m5mols_effects_gamma[ctrl->val]);
		if (!ret)
			ret = i2c_w8_mon(sd, CAT2_COLOR_EFFECT, true);
		return ret;
	}

	return ret;
}

int m5mols_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct m5mols_info *info = to_m5mols(sd);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_ZOOM_ABSOLUTE:
		return m5mols_zoom(info, ctrl);
	case V4L2_CID_FOCUS_AUTO:
		if (ctrl->val != 0)
			ret = m5mols_focus_mode(info, ctrl);
		if (!ret)
			ret = m5mols_focus(info, ctrl);
		return ret;
	case V4L2_CID_EXPOSURE_AUTO:
		if (!ctrl->is_new)
			ctrl->val = V4L2_EXPOSURE_MANUAL;
		ret = m5mols_exposure_mode(info, ctrl);
		if (!ret && ctrl->val == V4L2_EXPOSURE_MANUAL)
			ret = m5mols_exposure(info);
		return ret;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		return m5mols_wb_mode(info, ctrl);
	case V4L2_CID_SATURATION:
		return m5mols_set_saturation(info, ctrl);
	case V4L2_CID_COLORFX:
		return m5mols_set_colorfx(info, ctrl);
	}

	return -EINVAL;
}

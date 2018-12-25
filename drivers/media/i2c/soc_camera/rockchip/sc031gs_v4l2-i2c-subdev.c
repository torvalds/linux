// SPDX-License-Identifier: GPL-2.0
/*
 * sc031gs sensor driver
 *
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * Copyright (C) 2012-2014 Intel Mobile Communications GmbH
 *
 * Copyright (C) 2008 Texas Instruments.
 *
 * Note:
 *
 *v0.1.0:
 *1. Initialize version;
 *
 */

#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf-core.h>
#include <linux/slab.h>
#include <media/v4l2-controls_rockchip.h>
#include "sc_camera_module.h"

#define SC031GS_DRIVER_NAME "sc031gs"

#define SC031GS_AEC_PK_LONG_GAIN_HIGH_REG 0x3e08
#define SC031GS_AEC_PK_LONG_GAIN_LOW_REG 0x3e09

#define SC031GS_AEC_PK_LONG_EXPO_HIGH_REG 0x3e01
#define SC031GS_AEC_PK_LONG_EXPO_LOW_REG 0x3e02

#define SC031GS_AEC_GROUP_UPDATE_ADDRESS 0x3812
#define SC031GS_AEC_GROUP_UPDATE_START_DATA 0x00
#define SC031GS_AEC_GROUP_UPDATE_END_DATA 0x30

#define SC031GS_PIDH_ADDR 0x3107
#define SC031GS_PIDL_ADDR 0x3108

/* High byte of product ID */
#define SC031GS_PIDH_MAGIC 0x00
/* Low byte of product ID  */
#define SC031GS_PIDL_MAGIC 0x31

#define SC031GS_EXT_CLK 24000000
#define SC031GS_TIMING_VTS_HIGH_REG 0x320e
#define SC031GS_TIMING_VTS_LOW_REG 0x320f
#define SC031GS_TIMING_HTS_HIGH_REG 0x320c
#define SC031GS_TIMING_HTS_LOW_REG 0x320d
#define SC031GS_FINE_INTG_TIME_MIN 0
#define SC031GS_FINE_INTG_TIME_MAX_MARGIN 0
#define SC031GS_COARSE_INTG_TIME_MIN 1
#define SC031GS_COARSE_INTG_TIME_MAX_MARGIN 4
#define SC031GS_HORIZONTAL_OUTPUT_SIZE_HIGH_REG 0x3208
#define SC031GS_HORIZONTAL_OUTPUT_SIZE_LOW_REG 0x3209
#define SC031GS_VERTICAL_OUTPUT_SIZE_HIGH_REG 0x320a
#define SC031GS_VERTICAL_OUTPUT_SIZE_LOW_REG 0x320b

#define SC031GS_MIRROR_FILP 0x3221

static struct sc_camera_module sc031gs;

/* ======================================================================== */
/* Base sensor configs */
/* ======================================================================== */
/* MCLK:24MHz  640x480  50fps   mipi 1lane  10bit  720Mbps/lane */
static struct sc_camera_module_reg sc031gs_init_tab_640_480_50fps[] = {
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x0100, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3000, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3001, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x300f, 0x0f},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3018, 0x13},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3019, 0xfe},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x301c, 0x78},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3031, 0x0a},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3037, 0x20},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x303f, 0x01},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3208, 0x02},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3209, 0x80},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x320a, 0x01},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x320b, 0xe0},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x320c, 0x03},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x320d, 0x6e},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x320e, 0x06},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x320f, 0x67},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3250, 0xc0},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3251, 0x02},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3252, 0x02},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3253, 0xa6},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3254, 0x02},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3255, 0x07},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3304, 0x48},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3306, 0x38},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3309, 0x68},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x330b, 0xe0},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x330c, 0x18},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x330f, 0x20},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3310, 0x10},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3314, 0x3a},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3315, 0x38},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3316, 0x48},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3317, 0x20},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3329, 0x3c},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x332d, 0x3c},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x332f, 0x40},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3335, 0x44},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3344, 0x44},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x335b, 0x80},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x335f, 0x80},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3366, 0x06},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3385, 0x31},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3387, 0x51},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3389, 0x01},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x33b1, 0x03},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x33b2, 0x06},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3621, 0xa4},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3622, 0x05},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3630, 0x46},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3631, 0x48},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3633, 0x52},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3636, 0x25},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3637, 0x89},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3638, 0x0f},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3639, 0x08},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x363a, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x363b, 0x48},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x363c, 0x06},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x363d, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x363e, 0xf8},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3640, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3641, 0x01},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x36e9, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x36ea, 0x3b},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x36eb, 0x0e},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x36ec, 0x0e},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x36ed, 0x33},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x36f9, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x36fa, 0x3a},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x36fc, 0x01},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3908, 0x91},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3d08, 0x01},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3e01, 0x2a},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3e02, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3e03, 0x0b},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3e06, 0x0c},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x4500, 0x59},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x4501, 0xc4},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x4603, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x5011, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x4418, 0x08},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x4419, 0x8a},
};

static struct sc_camera_module_config sc031gs_configs[] = {
	{
		.name = "640x480_50fps",
		.frm_fmt = {
			.width = 640,
			.height = 480,
			.code = MEDIA_BUS_FMT_Y10_1X10
		},
		.frm_intrvl = {
			.interval = {
				.numerator = 1,
				.denominator = 50
			}
		},
		.auto_exp_enabled = false,
		.auto_gain_enabled = false,
		.auto_wb_enabled = false,
		.reg_table = (void *)sc031gs_init_tab_640_480_50fps,
		.reg_table_num_entries =
			ARRAY_SIZE(sc031gs_init_tab_640_480_50fps),
		.v_blanking_time_us = 3078,
		.max_exp_gain_h = 16,
		.max_exp_gain_l = 0,
		PLTFRM_CAM_ITF_MIPI_CFG(0, 1, 720, SC031GS_EXT_CLK)
	}
};

static int sc031gs_set_flip(struct sc_camera_module *cam_mod,
	struct pltfrm_camera_module_reg reglist[],
	int len)
{
	int i, mode = 0;
	u16 orientation = 0;

	mode = sc_camera_module_get_flip_mirror(cam_mod);

	if (mode == -1) {
		sc_camera_module_pr_debug(cam_mod,
			"dts don't set flip, return!\n");
		return 0;
	}

	if (!IS_ERR_OR_NULL(cam_mod->active_config)) {
		if (mode == SC_MIRROR_BIT_MASK)
			orientation |= 0x06;
		if (mode == SC_FLIP_BIT_MASK)
			orientation |= 0x60;
		for (i = 0; i < len; i++) {
			if (reglist[i].reg == SC031GS_MIRROR_FILP)
				reglist[i].val = orientation;
		}
	}
	return 0;
}

static int sc031gs_g_vts(struct sc_camera_module *cam_mod, u32 *vts)
{
	u32 msb, lsb;
	int ret;

	ret = sc_camera_module_read_reg_table(cam_mod,
		SC031GS_TIMING_VTS_HIGH_REG,
		&msb);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = sc_camera_module_read_reg_table(cam_mod,
		SC031GS_TIMING_VTS_LOW_REG,
		&lsb);
	if (IS_ERR_VALUE(ret))
		goto err;

	*vts = (msb << 8) | lsb;

	return 0;
err:
	sc_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int sc031gs_auto_adjust_fps(struct sc_camera_module *cam_mod,
	u32 exp_time)
{
	int ret;
	u32 vts;

	if ((exp_time + SC031GS_COARSE_INTG_TIME_MAX_MARGIN) >
		cam_mod->vts_min)
		vts = (exp_time + SC031GS_COARSE_INTG_TIME_MAX_MARGIN);
	else
		vts = cam_mod->vts_min;

	ret = sc_camera_module_write_reg(cam_mod,
		SC031GS_TIMING_VTS_LOW_REG,
		vts & 0xFF);
	ret |= sc_camera_module_write_reg(cam_mod,
		SC031GS_TIMING_VTS_HIGH_REG,
		(vts >> 8) & 0xFF);

	if (IS_ERR_VALUE(ret)) {
		sc_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	} else {
		sc_camera_module_pr_debug(cam_mod,
			"updated vts = 0x%x,vts_min=0x%x\n",
			vts, cam_mod->vts_min);
		cam_mod->vts_cur = vts;
	}

	return ret;
}

static int sc031gs_set_vts(struct sc_camera_module *cam_mod,
	u32 vts)
{
	int ret = 0;

	if (vts <= cam_mod->vts_min)
		return ret;

	ret = sc_camera_module_write_reg(cam_mod,
		SC031GS_TIMING_VTS_LOW_REG,
		vts & 0xFF);
	ret |= sc_camera_module_write_reg(cam_mod,
		SC031GS_TIMING_VTS_HIGH_REG,
		(vts >> 8) & 0xFF);

	if (IS_ERR_VALUE(ret)) {
		sc_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	} else {
		sc_camera_module_pr_debug(cam_mod,
			"updated vts = 0x%x,vts_min=0x%x\n",
			vts, cam_mod->vts_min);
		cam_mod->vts_cur = vts;
	}

	return ret;
}

static int sc031gs_write_aec(struct sc_camera_module *cam_mod)
{
	int ret = 0;

	sc_camera_module_pr_debug(cam_mod,
		"exp_time = %d lines, gain = %d, flash_mode = %d\n",
		cam_mod->exp_config.exp_time,
		cam_mod->exp_config.gain,
		cam_mod->exp_config.flash_mode);

	/*
	 * if the sensor is already streaming, write to shadow registers,
	 * if the sensor is in SW standby, write to active registers,
	 * if the sensor is off/registers are not writeable, do nothing
	 */
	if (cam_mod->state == SC_CAMERA_MODULE_SW_STANDBY ||
		cam_mod->state == SC_CAMERA_MODULE_STREAMING) {
		u32 a_gain = cam_mod->exp_config.gain;
		u32 exp_time = cam_mod->exp_config.exp_time;
		u32 vts = cam_mod->vts_min;
		u32 coarse_again, fine_again, fine_again_reg, coarse_again_reg;

		a_gain = a_gain * cam_mod->exp_config.gain_percent / 100;

		/* hold reg start */
		mutex_lock(&cam_mod->lock);
		ret = sc_camera_module_write_reg(cam_mod,
			SC031GS_AEC_GROUP_UPDATE_ADDRESS,
			SC031GS_AEC_GROUP_UPDATE_START_DATA);

		if (!IS_ERR_VALUE(ret) && cam_mod->auto_adjust_fps)
			ret |= sc031gs_auto_adjust_fps(cam_mod,
				cam_mod->exp_config.exp_time);
		if (exp_time > vts - 6)
			exp_time = vts - 6;

		if (a_gain < 0x20) { /*1x ~ 2x*/
			fine_again = a_gain - 16;
			coarse_again = 0x03;
			fine_again_reg = ((0x01 << 4) & 0x10) |
				(fine_again & 0x0f);
			coarse_again_reg = coarse_again & 0x1F;
		} else if (a_gain < 0x40) { /*2x ~ 4x*/
			fine_again = (a_gain >> 1) - 16;
			coarse_again = 0x7;
			fine_again_reg = ((0x01 << 4) & 0x10) |
				(fine_again & 0x0f);
			coarse_again_reg = coarse_again & 0x1F;
		} else if (a_gain < 0x80) { /*4x ~ 8x*/
			fine_again = (a_gain >> 2) - 16;
			coarse_again = 0xf;
			fine_again_reg = ((0x01 << 4) & 0x10) |
				(fine_again & 0x0f);
			coarse_again_reg = coarse_again & 0x1F;
		} else { /*8x ~ 16x*/
			fine_again = (a_gain >> 3) - 16;
			coarse_again = 0x1f;
			fine_again_reg = ((0x01 << 4) & 0x10) |
				(fine_again & 0x0f);
			coarse_again_reg = coarse_again & 0x1F;
		}

		if (a_gain < 0x20) {
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3314, 0x3a);
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3317, 0x20);
		} else {
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3314, 0x44);
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3317, 0x0f);
		}

		ret |= sc_camera_module_write_reg(cam_mod,
			SC031GS_AEC_PK_LONG_GAIN_HIGH_REG,
			coarse_again_reg);
		ret |= sc_camera_module_write_reg(cam_mod,
			SC031GS_AEC_PK_LONG_GAIN_LOW_REG,
			fine_again_reg);

		ret |= sc_camera_module_write_reg(cam_mod,
			SC031GS_AEC_PK_LONG_EXPO_HIGH_REG,
			(exp_time >> 4) & 0xff);
		ret |= sc_camera_module_write_reg(cam_mod,
			SC031GS_AEC_PK_LONG_EXPO_LOW_REG,
			(exp_time & 0xff) << 4);

		if (!cam_mod->auto_adjust_fps)
			ret |= sc031gs_set_vts(cam_mod,
				cam_mod->exp_config.vts_value);

		/* hold reg end */
		ret |= sc_camera_module_write_reg(cam_mod,
			SC031GS_AEC_GROUP_UPDATE_ADDRESS,
			SC031GS_AEC_GROUP_UPDATE_END_DATA);
		mutex_unlock(&cam_mod->lock);
	}

	if (IS_ERR_VALUE(ret))
		sc_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int sc031gs_g_ctrl(struct sc_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	sc_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
	case V4L2_CID_FLASH_LED_MODE:
		/* nothing to be done here */
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (IS_ERR_VALUE(ret))
		sc_camera_module_pr_debug(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int sc031gs_filltimings(struct sc_camera_module_custom_config *custom)
{
	u32 i, j;
	u32 win_h_off = 0, win_v_off = 0;
	struct sc_camera_module_config *config;
	struct sc_camera_module_timings *timings;
	struct sc_camera_module_reg *reg_table;
	u32 reg_table_num_entries;

	for (i = 0; i < custom->num_configs; i++) {
		config = &custom->configs[i];
		reg_table = config->reg_table;
		reg_table_num_entries = config->reg_table_num_entries;
		timings = &config->timings;

		memset(timings, 0x00, sizeof(*timings));
		for (j = 0; j < reg_table_num_entries; j++) {
			switch (reg_table[j].reg) {
			case SC031GS_TIMING_VTS_HIGH_REG:
				if (timings->frame_length_lines & 0xff00)
					timings->frame_length_lines = 0;
				timings->frame_length_lines =
					((reg_table[j].val << 8) |
					(timings->frame_length_lines & 0xff));
				break;
			case SC031GS_TIMING_VTS_LOW_REG:
				timings->frame_length_lines =
					(reg_table[j].val |
					(timings->frame_length_lines & 0xff00));
				break;
			case SC031GS_TIMING_HTS_HIGH_REG:
				if (timings->line_length_pck & 0xff00)
					timings->line_length_pck = 0;
				timings->line_length_pck =
					((reg_table[j].val << 8) |
					timings->line_length_pck);
				break;
			case SC031GS_TIMING_HTS_LOW_REG:
				timings->line_length_pck =
					(reg_table[j].val |
					(timings->line_length_pck & 0xff00));
				break;
			case SC031GS_HORIZONTAL_OUTPUT_SIZE_HIGH_REG:
				timings->sensor_output_width =
					((reg_table[j].val << 8) |
					(timings->sensor_output_width & 0xff));
				break;
			case SC031GS_HORIZONTAL_OUTPUT_SIZE_LOW_REG:
				timings->sensor_output_width =
					(reg_table[j].val |
					(timings->sensor_output_width & 0xff00));
				break;
			case SC031GS_VERTICAL_OUTPUT_SIZE_HIGH_REG:
				timings->sensor_output_height =
					((reg_table[j].val << 8) |
					(timings->sensor_output_height & 0xff));
				break;
			case SC031GS_VERTICAL_OUTPUT_SIZE_LOW_REG:
				timings->sensor_output_height =
					(reg_table[j].val |
					(timings->sensor_output_height & 0xff00));
				break;
			}
		}

		timings->crop_horizontal_start += win_h_off;
		timings->crop_horizontal_end -= win_h_off;
		timings->crop_vertical_start += win_v_off;
		timings->crop_vertical_end -= win_v_off;
		timings->exp_time = timings->exp_time >> 4;

		timings->vt_pix_clk_freq_hz =
			config->frm_intrvl.interval.denominator
			* timings->frame_length_lines
			* timings->line_length_pck;

		timings->coarse_integration_time_min =
			SC031GS_COARSE_INTG_TIME_MIN;
		timings->coarse_integration_time_max_margin =
			SC031GS_COARSE_INTG_TIME_MAX_MARGIN;

		/* OV Sensor do not use fine integration time. */
		timings->fine_integration_time_min =
			SC031GS_FINE_INTG_TIME_MIN;
		timings->fine_integration_time_max_margin =
			SC031GS_FINE_INTG_TIME_MAX_MARGIN;
	}

	return 0;
}

static int sc031gs_g_timings(struct sc_camera_module *cam_mod,
	struct sc_camera_module_timings *timings)
{
	int ret = 0;
	unsigned int vts;

	if (IS_ERR_OR_NULL(cam_mod->active_config))
		goto err;

	*timings = cam_mod->active_config->timings;

	vts = (!cam_mod->vts_cur) ?
		timings->frame_length_lines :
		cam_mod->vts_cur;

	if (cam_mod->frm_intrvl_valid)
		timings->vt_pix_clk_freq_hz =
			cam_mod->frm_intrvl.interval.denominator
			* vts
			* timings->line_length_pck;
	else
		timings->vt_pix_clk_freq_hz =
			cam_mod->active_config->frm_intrvl.interval.denominator
			* vts
			* timings->line_length_pck;

	timings->frame_length_lines = vts;

	return ret;
err:
	sc_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int sc031gs_s_ctrl(struct sc_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	sc_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
		ret = sc031gs_write_aec(cam_mod);
		break;
	case V4L2_CID_FLASH_LED_MODE:
		/* nothing to be done here */
		break;
	case V4L2_CID_FOCUS_ABSOLUTE:
		/* todo*/
		break;
	/*
	 *case RK_V4L2_CID_AUTO_FPS:
	 *  if (cam_mod->auto_adjust_fps)
	 * ret = sc031gs_auto_adjust_fps(
	 *cam_mod,
	 *cam_mod->exp_config.exp_time);
	 *break;
	 */
	default:
		ret = -EINVAL;
		break;
	}

	if (IS_ERR_VALUE(ret))
		sc_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int sc031gs_s_ext_ctrls(struct sc_camera_module *cam_mod,
	struct sc_camera_module_ext_ctrls *ctrls)
{
	int ret = 0;

	if ((ctrls->ctrls[0].id == V4L2_CID_GAIN ||
		ctrls->ctrls[0].id == V4L2_CID_EXPOSURE))
		ret = sc031gs_write_aec(cam_mod);
	else
		ret = -EINVAL;

	if (IS_ERR_VALUE(ret))
		sc_camera_module_pr_debug(cam_mod,
			"failed with error (%d)\n", ret);

	return ret;
}

static int sc031gs_start_streaming(struct sc_camera_module *cam_mod)
{
	int ret = 0;

	sc_camera_module_pr_info(cam_mod, "active config=%s\n",
			cam_mod->active_config->name);

	ret = sc031gs_g_vts(cam_mod, &cam_mod->vts_min);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = sc_camera_module_write_reg(cam_mod, 0x0100, 0x01);
	if (IS_ERR_VALUE(ret))
		goto err;

	return 0;
err:
	sc_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

static int sc031gs_stop_streaming(struct sc_camera_module *cam_mod)
{
	int ret = 0;

	sc_camera_module_pr_info(cam_mod, "\n");

	ret = sc_camera_module_write_reg(cam_mod, 0x0100, 0x00);

	if (IS_ERR_VALUE(ret))
		goto err;

	return 0;
err:
	sc_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

static int sc031gs_check_camera_id(struct sc_camera_module *cam_mod)
{
	u32 pidh, pidl;
	int ret = 0;

	sc_camera_module_pr_debug(cam_mod, "\n");

	ret |= sc_camera_module_read_reg(cam_mod, 1, SC031GS_PIDH_ADDR, &pidh);
	ret |= sc_camera_module_read_reg(cam_mod, 1, SC031GS_PIDL_ADDR, &pidl);

	if (IS_ERR_VALUE(ret)) {
		sc_camera_module_pr_err(cam_mod,
			"register read failed, camera module powered off?\n");
		goto err;
	}

	if (pidh == SC031GS_PIDH_MAGIC && pidl == SC031GS_PIDL_MAGIC) {
		sc_camera_module_pr_info(cam_mod,
			"successfully detected camera ID 0x%02x%02x\n",
			pidh, pidl);
	} else {
		sc_camera_module_pr_err(cam_mod,
			"wrong camera ID, expected 0x%02x%02x, detected 0x%02x%02x\n",
			SC031GS_PIDH_MAGIC, SC031GS_PIDL_MAGIC, pidh, pidl);
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	sc_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/* ======================================================================== */
/* This part is platform dependent */
/* ======================================================================== */

static struct v4l2_subdev_core_ops sc031gs_camera_module_core_ops = {
	.g_ctrl = sc_camera_module_g_ctrl,
	.s_ctrl = sc_camera_module_s_ctrl,
	.s_ext_ctrls = sc_camera_module_s_ext_ctrls,
	.s_power = sc_camera_module_s_power,
	.ioctl = sc_camera_module_ioctl
};

static struct v4l2_subdev_video_ops sc031gs_camera_module_video_ops = {
	.s_frame_interval = sc_camera_module_s_frame_interval,
	.g_frame_interval = sc_camera_module_g_frame_interval,
	.s_stream = sc_camera_module_s_stream
};

static struct v4l2_subdev_pad_ops sc031gs_camera_module_pad_ops = {
	.enum_frame_interval = sc_camera_module_enum_frameintervals,
	.get_fmt = sc_camera_module_g_fmt,
	.set_fmt = sc_camera_module_s_fmt,
};

static struct v4l2_subdev_ops sc031gs_camera_module_ops = {
	.core = &sc031gs_camera_module_core_ops,
	.video = &sc031gs_camera_module_video_ops,
	.pad = &sc031gs_camera_module_pad_ops,
};

static struct sc_camera_module_custom_config sc031gs_custom_config = {
	.start_streaming = sc031gs_start_streaming,
	.stop_streaming = sc031gs_stop_streaming,
	.s_ctrl = sc031gs_s_ctrl,
	.g_ctrl = sc031gs_g_ctrl,
	.s_ext_ctrls = sc031gs_s_ext_ctrls,
	.g_timings = sc031gs_g_timings,
	.set_flip = sc031gs_set_flip,
	.s_vts = sc031gs_auto_adjust_fps,
	.check_camera_id = sc031gs_check_camera_id,
	.configs = sc031gs_configs,
	.num_configs = ARRAY_SIZE(sc031gs_configs),
	.power_up_delays_ms = {5, 30, 30},
	/*
	 *0: Exposure time valid fields;
	 *1: Exposure gain valid fields;
	 *(2 fields == 1 frames)
	 */
	.exposure_valid_frame = {4, 4}
};

static int sc031gs_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	dev_info(&client->dev, "probing...\n");
	sc031gs_filltimings(&sc031gs_custom_config);

	v4l2_i2c_subdev_init(&sc031gs.sd, client,
		&sc031gs_camera_module_ops);
	sc031gs.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sc031gs.custom = sc031gs_custom_config;
	mutex_init(&sc031gs.lock);
	dev_info(&client->dev, "probing successful\n");

	return 0;
}

static int sc031gs_remove(struct i2c_client *client)
{
	struct sc_camera_module *cam_mod = i2c_get_clientdata(client);

	dev_info(&client->dev, "removing device...\n");

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */
	mutex_destroy(&cam_mod->lock);
	sc_camera_module_release(cam_mod);

	dev_info(&client->dev, "removed\n");
	return 0;
}

static const struct i2c_device_id sc031gs_id[] = {
	{ SC031GS_DRIVER_NAME, 0 },
	{ }
};

static const struct of_device_id sc031gs_of_match[] = {
	{.compatible = "smartsens,sc031gs-v4l2-i2c-subdev"},
	{},
};

MODULE_DEVICE_TABLE(i2c, sc031gs_id);

static struct i2c_driver sc031gs_i2c_driver = {
	.driver = {
		.name = SC031GS_DRIVER_NAME,
		.of_match_table = sc031gs_of_match
	},
	.probe = sc031gs_probe,
	.remove = sc031gs_remove,
	.id_table = sc031gs_id,
};

module_i2c_driver(sc031gs_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for sc031gs");
MODULE_AUTHOR("zack.zeng");
MODULE_LICENSE("GPL");

